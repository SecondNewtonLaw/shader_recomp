#include "SR/Frontend/Readers/DxilReader.hpp"

#include <cstring>
#include <string>
#include <utility>

#include "SR/Common/ByteReader.hpp"
#include "SR/Frontend/Container/DxbcContainer.hpp"

// ProgramHeader/BitcodeHeader layout follows DXC's DxilContainer.h.
namespace SR::Frontend {
namespace {

constexpr std::uint32_t kDxilMagic = 0x4c495844u; // "DXIL"
constexpr std::uint32_t kProgramHeaderSize = 24;

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

IL::Stage StageFromShaderKind(std::uint32_t kind) noexcept
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
    case 7:
        return IL::Stage::Mesh;
    case 8:
        return IL::Stage::Task;
    default:
        return IL::Stage::Unknown;
    }
}

} // namespace

BinaryFormat DxilReader::Format() const
{
    return BinaryFormat::Dxil;
}

std::string_view DxilReader::Name() const
{
    return "dxil";
}

bool DxilReader::Probe(std::span<const std::uint8_t> bytes) const
{
    const auto container = DxbcContainer::Parse(bytes);
    return container.has_value() && container->IsDxil();
}

Result<DecodedShader> DxilReader::Read(std::span<const std::uint8_t> bytes) const
{
    const auto container = DxbcContainer::Parse(bytes);
    if (!container)
        return std::unexpected(container.error());

    const DxbcChunk* part = container->Find("DXIL");
    if (!part)
        return std::unexpected(MakeError("SR-DXIL-001", "missing DXIL part", 0));

    if (part->data.size() < kProgramHeaderSize)
        return std::unexpected(MakeError("SR-DXIL-002", "truncated DXIL program header", part->offset));

    ByteReader reader(part->data);
    const auto programVersion = reader.readU32();
    const auto sizeInUint32 = reader.readU32();
    const auto bitcodeMagic = reader.readU32();
    const auto dxilVersion = reader.readU32();
    const auto bitcodeOffset = reader.readU32();
    const auto bitcodeSize = reader.readU32();

    if (!programVersion || !sizeInUint32 || !bitcodeMagic || !dxilVersion || !bitcodeOffset || !bitcodeSize)
        return std::unexpected(MakeError("SR-DXIL-002", "truncated DXIL program header", part->offset));

    if (*bitcodeMagic != kDxilMagic)
        return std::unexpected(MakeError("SR-DXIL-003", "invalid DXIL bitcode magic", part->offset + 8));

    if (*sizeInUint32 * 4ull > part->data.size())
        return std::unexpected(MakeError("SR-DXIL-004", "DXIL program size exceeds the part", part->offset + 4));

    if (*bitcodeOffset < 16)
        return std::unexpected(MakeError("SR-DXIL-005", "invalid DXIL bitcode offset", part->offset + 16));

    const std::uint64_t bitcodeStart = 8ull + *bitcodeOffset;
    const std::uint64_t bitcodeEnd = bitcodeStart + *bitcodeSize;
    if (bitcodeEnd > part->data.size())
        return std::unexpected(MakeError("SR-DXIL-005", "DXIL bitcode exceeds the part", part->offset + 16));

    DecodedShader shader;
    shader.format = BinaryFormat::Dxil;
    shader.architecture = "dxil";
    shader.programVersion = *programVersion;
    shader.shaderModel = *programVersion & 0xffffu;
    shader.stage = StageFromShaderKind((*programVersion >> 16) & 0xffffu);

    const auto* begin = part->data.data() + bitcodeStart;
    const auto* end = part->data.data() + bitcodeEnd;
    shader.rawBytecode.assign(begin, end);

    return shader;
}

} // namespace SR::Frontend
