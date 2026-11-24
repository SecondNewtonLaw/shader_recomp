#include "SR/Frontend/Container/XboxShaderContainer.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include <zlib.h>

#include "SR/Frontend/Container/DxbcContainer.hpp"

// Layouts follow two independent readings of retail shader packs:
//
//   VIctory  crates/victory-shader/src/native.rs  - record table, root layout
//            and used-resource tables, per-stage IO
//   XCL      crates/xcl/src/gpu/xbox.rs           - direct code pointer,
//            hardware-stage descriptors, wave control
//
// The two agree on the nesting (DXBC -> DXIL part -> XDAT -> XBCH -> FSSM) and
// on the record fields, and disagree on where the code pointer lives. Both are
// validated here; notes() records which one matched.
namespace SR::Frontend {
namespace {

constexpr std::uint32_t kDxilProgramHeaderSize = 24; // DxilContainer.h
constexpr std::uint32_t kXdatSearchLimit = 0x100;
constexpr std::uint32_t kMaxXdatEntries = 64;
constexpr std::uint64_t kAbsentRecord = 0xDEADBEEFDEADBEEFull;

// The largest FSSM seen in a retail pack is well under a megabyte; the cap keeps
// a corrupt size field from turning into an allocation.
constexpr std::size_t kMaxInflatedBytes = 16u << 20;

// s_endpgm and s_code_end, the two words a program may end with.
constexpr std::uint32_t kSEndpgm = 0xBF810000u;
constexpr std::uint32_t kSCodeEnd = 0xBF9F0000u;

constexpr std::uint32_t kRecordSize = 0x54;
constexpr std::uint32_t kRecordFlags = 0x14;
constexpr std::uint32_t kRecordLds = 0x4C;
constexpr std::uint32_t kRecordRegisters = 0x50;
constexpr std::uint32_t kFlagWave32 = 1u << 5;

// Compute stage data starts at record + kRecordSize; COMPUTE_PGM_RSRC2 and the
// thread-group size sit 0x24 and 0x28 bytes into it.
constexpr std::uint32_t kComputeRsrc2 = 0x24;
constexpr std::uint32_t kComputeThreads = 0x28;

// Record offsets inside the FSSM payload.
struct Slot
{
    XboxStage stage;
    std::uint32_t offset;
};

constexpr Slot kSlots[] = {
    {XboxStage::Ls, 0x08},
    {XboxStage::Hs, 0x10},
    {XboxStage::Es, 0x18},
    {XboxStage::Gs, 0x20},
    {XboxStage::Vs, 0x28},
    {XboxStage::Ps, 0x30},
    {XboxStage::Mesh, 0x48},
    {XboxStage::Cs, 0x60},
};

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

// Diagnostics quote container fields, so they read as hex like the documents.
std::string Hex(std::uint32_t value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string text = "0x";

    bool started = false;
    for (int shift = 28; shift >= 0; shift -= 4)
    {
        const std::uint32_t nibble = (value >> shift) & 0xFu;
        if (nibble != 0 || started || shift == 0)
        {
            text.push_back(digits[nibble]);
            started = true;
        }
    }

    return text;
}

std::optional<std::uint32_t> ReadU32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    if (offset > bytes.size() || bytes.size() - offset < 4)
        return std::nullopt;

    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8)
         | (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) | (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

std::optional<std::uint64_t> ReadU64(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    const auto low = ReadU32(bytes, offset);
    const auto high = ReadU32(bytes, offset + 4);
    if (!low || !high)
        return std::nullopt;

    return static_cast<std::uint64_t>(*low) | (static_cast<std::uint64_t>(*high) << 32);
}

bool StartsWith(std::span<const std::uint8_t> bytes, std::size_t offset, const char (&tag)[5]) noexcept
{
    return offset <= bytes.size() && bytes.size() - offset >= 4 && std::memcmp(bytes.data() + offset, tag, 4) == 0;
}

IL::Stage StageFromDxilKind(std::uint32_t kind) noexcept
{
    switch (kind)
    {
    case 0:
        return IL::Stage::Fragment;
    case 1:
        return IL::Stage::Vertex;
    case 2:
        return IL::Stage::Geometry;
    case 3:
        return IL::Stage::TessControl;
    case 4:
        return IL::Stage::TessEval;
    case 5:
        return IL::Stage::Compute;
    case 13:
        return IL::Stage::Mesh;
    case 14:
        return IL::Stage::Task;
    default:
        return IL::Stage::Unknown;
    }
}

// The zlib stream starts at +0x18 in every container seen, but the header is
// described differently by different authors; the stream is found by its
// checksum pair rather than by trusting a fixed offset.
bool LooksLikeZlibHeader(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept
{
    if (offset + 2 > bytes.size())
        return false;

    const auto cmf = bytes[offset];
    const auto flg = bytes[offset + 1];
    return (cmf & 0x0Fu) == 8 && (cmf >> 4) <= 7 && ((static_cast<std::uint16_t>(cmf) << 8) | flg) % 31 == 0
        && (flg & 0x20u) == 0;
}

Result<std::vector<std::uint8_t>> Inflate(std::span<const std::uint8_t> stream)
{
    z_stream state{};
    if (inflateInit(&state) != Z_OK)
        return std::unexpected(MakeError("SR-XBOX-010", "zlib initialisation failed", 0));

    state.next_in = const_cast<Bytef*>(stream.data());
    state.avail_in = static_cast<uInt>(std::min<std::size_t>(stream.size(), 0xFFFFFFFFu));

    std::vector<std::uint8_t> out;
    std::array<std::uint8_t, 16384> chunk{};

    int status = Z_OK;
    while (status != Z_STREAM_END)
    {
        state.next_out = chunk.data();
        state.avail_out = static_cast<uInt>(chunk.size());

        status = inflate(&state, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END)
        {
            inflateEnd(&state);
            return std::unexpected(MakeError("SR-XBOX-011",
                                             "XBCH stream does not inflate (zlib error " + std::to_string(status) + ")",
                                             0));
        }

        const std::size_t produced = chunk.size() - state.avail_out;
        if (out.size() + produced > kMaxInflatedBytes)
        {
            inflateEnd(&state);
            return std::unexpected(MakeError("SR-XBOX-012", "FSSM exceeds the decompressed size limit", 0));
        }

        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(produced));
    }

    inflateEnd(&state);
    return out;
}

struct XdatEntry
{
    std::uint32_t id = 0;
    std::uint32_t offset = 0; // in the DXIL part
    std::uint32_t size = 0;
};

std::optional<std::size_t> FindXdat(std::span<const std::uint8_t> part) noexcept
{
    for (std::size_t offset = kDxilProgramHeaderSize; offset + 4 <= part.size() && offset <= kXdatSearchLimit;
         offset += 4)
    {
        if (StartsWith(part, offset, "XDAT"))
            return offset;
    }

    return std::nullopt;
}

// The table position and the base entry offsets are relative either to the part
// or to XDAT itself. Every combination is checked against the part bounds, and
// the first one that holds for every entry wins.
Result<std::vector<XdatEntry>> ReadXdat(std::span<const std::uint8_t> part, std::size_t xdat)
{
    const auto count = ReadU32(part, xdat + 4);
    const auto tableOffset = ReadU32(part, xdat + 8);
    if (!count || !tableOffset)
        return std::unexpected(MakeError("SR-XBOX-013", "truncated XDAT header", static_cast<std::uint32_t>(xdat)));

    if (*count == 0 || *count > kMaxXdatEntries)
        return std::unexpected(MakeError("SR-XBOX-014",
                                         "XDAT entry count " + std::to_string(*count) + " is out of range",
                                         static_cast<std::uint32_t>(xdat)));

    const std::uint32_t tables[] = {*tableOffset, static_cast<std::uint32_t>(xdat + 12)};
    const std::uint32_t bases[] = {0, static_cast<std::uint32_t>(xdat)};

    for (const std::uint32_t table : tables)
    {
        const std::uint64_t tableEnd = static_cast<std::uint64_t>(table) + static_cast<std::uint64_t>(*count) * 12;
        if (table < xdat + 12 || tableEnd > part.size())
            continue;

        for (const std::uint32_t base : bases)
        {
            std::vector<XdatEntry> entries;
            entries.reserve(*count);
            bool valid = true;

            for (std::uint32_t i = 0; i < *count && valid; ++i)
            {
                const std::size_t at = static_cast<std::size_t>(table) + static_cast<std::size_t>(i) * 12;
                XdatEntry entry;
                entry.id = ReadU32(part, at).value_or(0);
                entry.offset = ReadU32(part, at + 4).value_or(0);
                entry.size = ReadU32(part, at + 8).value_or(0);

                const std::uint64_t start = static_cast<std::uint64_t>(base) + entry.offset;
                valid = entry.size != 0 && start >= tableEnd && start + entry.size <= part.size();
                if (valid)
                {
                    entry.offset = static_cast<std::uint32_t>(start);
                    entries.push_back(entry);
                }
            }

            if (valid)
                return entries;
        }
    }

    return std::unexpected(
        MakeError("SR-XBOX-015", "XDAT table does not validate against the DXIL part", static_cast<std::uint32_t>(xdat)));
}

bool EndsWithTerminator(std::span<const std::uint8_t> code) noexcept
{
    // Programs are padded with s_code_end-style words, so the terminator sits in
    // the last few words rather than exactly at the end.
    const std::size_t words = std::min<std::size_t>(code.size() / 4, 8);
    for (std::size_t i = 0; i < words; ++i)
    {
        const std::size_t at = code.size() - (i + 1) * 4;
        const std::uint32_t word = static_cast<std::uint32_t>(code[at]) | (static_cast<std::uint32_t>(code[at + 1]) << 8)
                                 | (static_cast<std::uint32_t>(code[at + 2]) << 16)
                                 | (static_cast<std::uint32_t>(code[at + 3]) << 24);
        if (word == kSEndpgm || word == kSCodeEnd)
            return true;
    }

    return false;
}

// One FSSM record: code plus the state the emitter needs. `payload` is the FSSM
// stream after its 16-byte header, so record and code offsets index it directly.
std::optional<XboxProgram> ReadRecord(std::span<const std::uint8_t> payload,
                                      std::uint32_t stream,
                                      std::uint64_t recordOffset,
                                      XboxStage stage)
{
    if (recordOffset > payload.size() || payload.size() - recordOffset < kRecordSize)
        return std::nullopt;

    const std::size_t record = static_cast<std::size_t>(recordOffset);
    const auto codeOffset = ReadU64(payload, record);
    const auto codeSize = ReadU64(payload, record + 8);
    if (!codeOffset || !codeSize || *codeSize == 0 || *codeSize % 4 != 0)
        return std::nullopt;

    if (*codeOffset > payload.size() || *codeSize > payload.size() - *codeOffset)
        return std::nullopt;

    const auto code = payload.subspan(static_cast<std::size_t>(*codeOffset), static_cast<std::size_t>(*codeSize));
    if (!EndsWithTerminator(code))
        return std::nullopt;

    XboxProgram program;
    program.stage = stage;
    program.stream = stream;
    program.codeOffset = static_cast<std::uint32_t>(*codeOffset);
    program.codeSize = static_cast<std::uint32_t>(*codeSize);

    if (const auto flags = ReadU32(payload, record + kRecordFlags))
        program.wave32 = (*flags & kFlagWave32) != 0u;

    if (const auto lds = ReadU32(payload, record + kRecordLds))
        program.ldsBytes = *lds;

    if (const auto registers = ReadU32(payload, record + kRecordRegisters))
    {
        program.vgprCount = *registers & 0x3FFu;
        program.sgprCount = (*registers >> 20) & 0xFFu;
    }

    if (stage == XboxStage::Cs)
    {
        const std::size_t stageData = record + kRecordSize;
        if (stageData + kComputeThreads + 12 <= payload.size())
        {
            if (const auto rsrc2 = ReadU32(payload, stageData + kComputeRsrc2))
            {
                program.computeRsrc2 = *rsrc2;
                program.hasComputeState = true;
            }

            program.threadGroupSize = {ReadU32(payload, stageData + kComputeThreads).value_or(1),
                                       ReadU32(payload, stageData + kComputeThreads + 4).value_or(1),
                                       ReadU32(payload, stageData + kComputeThreads + 8).value_or(1)};
        }
    }

    return program;
}

} // namespace

std::string_view ToString(XboxStage stage) noexcept
{
    switch (stage)
    {
    case XboxStage::Ls:
        return "ls";
    case XboxStage::Hs:
        return "hs";
    case XboxStage::Es:
        return "es";
    case XboxStage::Gs:
        return "gs";
    case XboxStage::Vs:
        return "vs";
    case XboxStage::Ps:
        return "ps";
    case XboxStage::Cs:
        return "cs";
    case XboxStage::Mesh:
        return "ngg";
    case XboxStage::Unknown:
        break;
    }

    return "unknown";
}

bool XboxShaderContainer::Probe(std::span<const std::uint8_t> bytes) noexcept
{
    if (!DxbcContainer::Probe(bytes))
        return false;

    const auto container = DxbcContainer::Parse(bytes);
    if (!container)
        return false;

    const DxbcChunk* dxil = container->Find("DXIL");
    return dxil != nullptr && FindXdat(dxil->data).has_value();
}

Result<XboxShaderContainer> XboxShaderContainer::Parse(std::span<const std::uint8_t> bytes)
{
    const auto dxbc = DxbcContainer::Parse(bytes);
    if (!dxbc)
        return std::unexpected(dxbc.error());

    const DxbcChunk* dxil = dxbc->Find("DXIL");
    if (dxil == nullptr)
        return std::unexpected(MakeError("SR-XBOX-001", "container has no DXIL part", 0));

    const std::size_t xdat = FindXdat(dxil->data).value_or(0);
    if (xdat == 0)
        return std::unexpected(MakeError("SR-XBOX-002", "DXIL part carries no XDAT index", dxil->offset));

    const auto entries = ReadXdat(dxil->data, xdat);
    if (!entries)
        return std::unexpected(entries.error());

    XboxShaderContainer container;
    container.m_blob = dxbc->bytes();

    const std::uint32_t programVersion = ReadU32(dxil->data, 0).value_or(0);
    container.m_stage = StageFromDxilKind(programVersion >> 16);
    container.m_shaderModelMajor = static_cast<std::uint16_t>((programVersion >> 4) & 0xFu);
    container.m_shaderModelMinor = static_cast<std::uint16_t>(programVersion & 0xFu);

    if (xdat != kDxilProgramHeaderSize)
        container.m_notes.push_back("XDAT index at +" + Hex(static_cast<std::uint32_t>(xdat)) + " in the DXIL part");

    bool sawProgram = false;

    for (const XdatEntry& entry : *entries)
    {
        // 0x12 is the 16-byte shader hash; 0x0d is one XBCH per hardware-stage
        // variant.
        if (entry.id == 0x12 && entry.size == 16)
        {
            container.m_hashOffset = dxil->offset + 8 + entry.offset;
            container.m_hashSize = entry.size;
            continue;
        }

        if (entry.id != 0x0D)
            continue;

        const auto blob = dxil->data.subspan(entry.offset, entry.size);
        if (!StartsWith(blob, 0, "XBCH"))
        {
            container.m_notes.push_back("XDAT 0x0d entry carries no XBCH header");
            continue;
        }

        const auto version = ReadU32(blob, 4);
        if (!version || *version != 3)
        {
            container.m_notes.push_back("XBCH version " + std::to_string(version.value_or(0)) + " is not 3");
            continue;
        }

        std::size_t stream = 0;
        if (LooksLikeZlibHeader(blob, 0x18))
            stream = 0x18;
        else
        {
            const std::size_t limit = std::min<std::size_t>(blob.size(), 64);
            for (std::size_t offset = 8; offset + 2 <= limit && stream == 0; ++offset)
            {
                if (LooksLikeZlibHeader(blob, offset))
                    stream = offset;
            }
        }

        if (stream == 0)
        {
            container.m_notes.push_back("XBCH carries no zlib stream");
            continue;
        }

        const auto inflated = Inflate(blob.subspan(stream));
        if (!inflated)
        {
            container.m_notes.push_back(inflated.error().message);
            continue;
        }

        // The header is described as u32 and as u64 sizes in the two readings;
        // the decompressed length is the authority, and a disagreement is
        // reported rather than applied.
        const std::uint32_t declaredPacked = ReadU32(blob, 8).value_or(0);
        const std::uint32_t declaredRaw = ReadU32(blob, 0x10).value_or(0);
        if (declaredRaw != 0 && declaredRaw != inflated->size())
        {
            container.m_notes.push_back("XBCH raw size " + std::to_string(declaredRaw) + " disagrees with the "
                                        + std::to_string(inflated->size()) + " bytes decompressed");
        }

        if (declaredPacked != 0 && declaredPacked != blob.size() - stream && declaredPacked != blob.size())
            container.m_notes.push_back("XBCH packed size " + std::to_string(declaredPacked)
                                        + " matches neither the stream nor the header");

        const auto fssm = std::move(inflated.value());
        if (fssm.size() < 0x10 || std::memcmp(fssm.data(), "FSSM", 4) != 0)
        {
            container.m_notes.push_back("XBCH payload is not an FSSM program");
            continue;
        }

        container.m_fssmVersion = ReadU32(fssm, 4).value_or(0);
        if (container.m_fssmVersion != 0x101)
            container.m_notes.push_back("FSSM version " + Hex(container.m_fssmVersion) + " is not 0x101");

        const std::uint32_t streamIndex = static_cast<std::uint32_t>(container.m_streams.size());
        container.m_streams.push_back(std::move(fssm));

        const auto payload = std::span<const std::uint8_t>(container.m_streams.back()).subspan(0x10);

        std::vector<XboxProgram> records;
        if (payload.size() >= 0x68)
        {
            if (const auto slots = ReadU64(payload, 0))
            {
                if (*slots != 6)
                    container.m_notes.push_back("FSSM graphics slot count is " + std::to_string(*slots) + ", not 6");
            }

            for (const Slot& slot : kSlots)
            {
                const auto offset = ReadU64(payload, slot.offset);
                if (!offset || *offset == kAbsentRecord)
                    continue;

                if (auto program = ReadRecord(payload, streamIndex, *offset, slot.stage))
                    records.push_back(std::move(*program));
                else
                    container.m_notes.push_back("FSSM " + std::string(ToString(slot.stage)) + " record at "
                                                + Hex(static_cast<std::uint32_t>(*offset)) + " does not validate");
            }
        }

        if (records.empty() && payload.size() >= 0xD0)
        {
            // XCL and DedSec read the code pointer directly: FSSM + 0xC0 holds
            // the code offset and +0xC8 its size, both relative to +0x10.
            const auto offset = ReadU64(payload, 0xC0);
            const auto size = ReadU64(payload, 0xC8);
            if (offset && size && *size != 0 && *size % 4 == 0 && *offset <= payload.size()
                && *size <= payload.size() - *offset
                && EndsWithTerminator(
                    payload.subspan(static_cast<std::size_t>(*offset), static_cast<std::size_t>(*size))))
            {
                XboxProgram program;
                program.stage = container.m_stage == IL::Stage::Compute ? XboxStage::Cs : XboxStage::Unknown;
                program.stream = streamIndex;
                program.codeOffset = static_cast<std::uint32_t>(*offset);
                program.codeSize = static_cast<std::uint32_t>(*size);
                records.push_back(program);
                container.m_notes.push_back("FSSM resolved through the direct code pointer");
            }
        }

        if (records.empty())
        {
            container.m_notes.push_back("FSSM carries no program that validates");
            continue;
        }

        sawProgram = true;
        container.m_programs.insert(container.m_programs.end(), records.begin(), records.end());
    }

    if (!sawProgram)
        return std::unexpected(MakeError("SR-XBOX-003", "container carries no native program", dxil->offset));

    return container;
}

std::span<const std::uint8_t> XboxShaderContainer::bytes() const noexcept
{
    return m_blob;
}

IL::Stage XboxShaderContainer::stage() const noexcept
{
    return m_stage;
}

std::uint16_t XboxShaderContainer::shaderModelMajor() const noexcept
{
    return m_shaderModelMajor;
}

std::uint16_t XboxShaderContainer::shaderModelMinor() const noexcept
{
    return m_shaderModelMinor;
}

std::uint32_t XboxShaderContainer::fssmVersion() const noexcept
{
    return m_fssmVersion;
}

const std::vector<XboxProgram>& XboxShaderContainer::programs() const noexcept
{
    return m_programs;
}

std::span<const std::uint8_t> XboxShaderContainer::code(const XboxProgram& program) const noexcept
{
    if (program.stream >= m_streams.size())
        return {};

    const auto& stream = m_streams[program.stream];
    if (stream.size() < 0x10)
        return {};

    // Code offsets are payload-relative.
    const auto payload = std::span<const std::uint8_t>(stream).subspan(0x10);
    if (program.codeOffset > payload.size() || program.codeSize > payload.size() - program.codeOffset)
        return {};

    return payload.subspan(program.codeOffset, program.codeSize);
}

std::span<const std::uint8_t> XboxShaderContainer::hash() const noexcept
{
    if (m_hashSize == 0 || m_hashOffset > m_blob.size() || m_hashSize > m_blob.size() - m_hashOffset)
        return {};

    return m_blob.subspan(m_hashOffset, m_hashSize);
}

const std::vector<std::string>& XboxShaderContainer::notes() const noexcept
{
    return m_notes;
}

} // namespace SR::Frontend
