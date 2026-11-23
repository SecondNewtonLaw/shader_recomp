#include "SR/Frontend/Container/ElfContainer.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

#include "SR/Common/ByteReader.hpp"

// ELF gABI layout; section tables only. NVIDIA cubin and AMDGPU code objects
// both use this container.
namespace SR::Frontend {
namespace {

constexpr std::uint8_t kElfMagic[4] = {0x7f, 'E', 'L', 'F'};
constexpr std::uint32_t kSectionNoBits = 8;
constexpr std::uint32_t kMaxSections = 8192;

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

} // namespace

bool ElfContainer::Probe(std::span<const std::uint8_t> bytes) noexcept
{
    return bytes.size() >= 16 && std::memcmp(bytes.data(), kElfMagic, 4) == 0;
}

Result<ElfContainer> ElfContainer::Parse(std::span<const std::uint8_t> bytes)
{
    if (!Probe(bytes))
        return std::unexpected(MakeError("SR-ELF-001", "missing ELF magic", 0));

    const std::uint8_t elfClass = bytes[4];
    const std::uint8_t elfData = bytes[5];
    if (elfData != 1)
        return std::unexpected(MakeError("SR-ELF-002", "only little-endian ELF is supported", 5));

    if (elfClass != 1 && elfClass != 2)
        return std::unexpected(MakeError("SR-ELF-003", "invalid ELF class", 4));

    ByteReader reader(bytes);
    if (!reader.readBytes(16))
        return std::unexpected(MakeError("SR-ELF-001", "truncated ELF ident", 0));

    const auto type = reader.readU16();
    const auto machine = reader.readU16();
    const auto version = reader.readU32();
    if (!type || !machine || !version)
        return std::unexpected(MakeError("SR-ELF-001", "truncated ELF header", reader.offset()));

    std::uint64_t sectionOffset = 0;
    std::uint16_t sectionEntrySize = 0;
    std::uint16_t sectionCount = 0;
    std::uint16_t stringSection = 0;

    if (elfClass == 2)
    {
        const auto entry = reader.readU64();
        const auto programOffset = reader.readU64();
        const auto sections = reader.readU64();
        const auto flags = reader.readU32();
        const auto headerSize = reader.readU16();
        const auto programEntrySize = reader.readU16();
        const auto programCount = reader.readU16();
        const auto sectionEntry = reader.readU16();
        const auto sectionNum = reader.readU16();
        const auto stringIndex = reader.readU16();
        (void)entry;
        (void)programOffset;
        (void)flags;
        (void)headerSize;
        (void)programEntrySize;
        (void)programCount;

        if (!sections || !sectionEntry || !sectionNum || !stringIndex)
            return std::unexpected(MakeError("SR-ELF-001", "truncated ELF64 header", reader.offset()));

        sectionOffset = *sections;
        sectionEntrySize = *sectionEntry;
        sectionCount = *sectionNum;
        stringSection = *stringIndex;

        if (sectionEntrySize < 64)
            return std::unexpected(MakeError("SR-ELF-004", "ELF64 section header is too small", reader.offset() - 4));
    }
    else
    {
        const auto entry = reader.readU32();
        const auto programOffset = reader.readU32();
        const auto sections = reader.readU32();
        const auto flags = reader.readU32();
        const auto headerSize = reader.readU16();
        const auto programEntrySize = reader.readU16();
        const auto programCount = reader.readU16();
        const auto sectionEntry = reader.readU16();
        const auto sectionNum = reader.readU16();
        const auto stringIndex = reader.readU16();
        (void)entry;
        (void)programOffset;
        (void)flags;
        (void)headerSize;
        (void)programEntrySize;
        (void)programCount;

        if (!sections || !sectionEntry || !sectionNum || !stringIndex)
            return std::unexpected(MakeError("SR-ELF-001", "truncated ELF32 header", reader.offset()));

        sectionOffset = *sections;
        sectionEntrySize = *sectionEntry;
        sectionCount = *sectionNum;
        stringSection = *stringIndex;

        if (sectionEntrySize < 40)
            return std::unexpected(MakeError("SR-ELF-004", "ELF32 section header is too small", reader.offset() - 4));
    }

    if (sectionCount == 0 || sectionCount > kMaxSections)
        return std::unexpected(MakeError("SR-ELF-004", "invalid section count", 0));

    const std::uint64_t tableSize = static_cast<std::uint64_t>(sectionCount) * sectionEntrySize;
    if (sectionOffset + tableSize > bytes.size())
        return std::unexpected(MakeError("SR-ELF-004", "section table exceeds the file", 0));

    if (stringSection >= sectionCount)
        return std::unexpected(MakeError("SR-ELF-004", "invalid section name table index", 0));

    ElfContainer container;
    container.m_bytes = bytes;
    container.m_is64 = elfClass == 2;
    container.m_machine = *machine;
    container.m_sections.reserve(sectionCount);

    for (std::uint16_t i = 0; i < sectionCount; ++i)
    {
        ByteReader sectionReader(bytes.subspan(sectionOffset + static_cast<std::uint64_t>(i) * sectionEntrySize));
        const auto nameOffset = sectionReader.readU32();
        const auto sectionType = sectionReader.readU32();
        if (!nameOffset || !sectionType)
            return std::unexpected(MakeError("SR-ELF-004", "truncated section header", 0));

        std::uint64_t flags = 0;
        std::uint64_t offset = 0;
        std::uint64_t size = 0;

        if (elfClass == 2)
        {
            const auto sectionFlags = sectionReader.readU64();
            const auto address = sectionReader.readU64();
            const auto sectionOffsetValue = sectionReader.readU64();
            const auto sectionSize = sectionReader.readU64();
            const auto link = sectionReader.readU32();
            const auto info = sectionReader.readU32();
            const auto alignment = sectionReader.readU64();
            const auto entrySize = sectionReader.readU64();
            (void)address;
            (void)link;
            (void)info;
            (void)alignment;
            (void)entrySize;

            if (!sectionFlags || !sectionOffsetValue || !sectionSize)
                return std::unexpected(MakeError("SR-ELF-004", "truncated section header", 0));

            flags = *sectionFlags;
            offset = *sectionOffsetValue;
            size = *sectionSize;
        }
        else
        {
            const auto sectionFlags = sectionReader.readU32();
            const auto address = sectionReader.readU32();
            const auto sectionOffsetValue = sectionReader.readU32();
            const auto sectionSize = sectionReader.readU32();
            const auto link = sectionReader.readU32();
            const auto info = sectionReader.readU32();
            const auto alignment = sectionReader.readU32();
            const auto entrySize = sectionReader.readU32();
            (void)address;
            (void)link;
            (void)info;
            (void)alignment;
            (void)entrySize;

            if (!sectionFlags || !sectionOffsetValue || !sectionSize)
                return std::unexpected(MakeError("SR-ELF-004", "truncated section header", 0));

            flags = *sectionFlags;
            offset = *sectionOffsetValue;
            size = *sectionSize;
        }

        if (*sectionType != kSectionNoBits && offset + size > bytes.size())
            return std::unexpected(MakeError("SR-ELF-005", "section data exceeds the file", 0));

        ElfSection section;
        section.type = *sectionType;
        section.flags = flags;
        section.offset = offset;
        section.size = size;
        if (*sectionType != kSectionNoBits)
            section.data = bytes.subspan(offset, size);
        container.m_sections.push_back(std::move(section));
    }

    const auto& stringData = container.m_sections[stringSection].data;

    // Section names are read after the table because the string table is one of
    // the sections. Re-read the stored name offsets from the table.
    for (std::uint16_t i = 0; i < sectionCount; ++i)
    {
        ByteReader sectionReader(bytes.subspan(sectionOffset + static_cast<std::uint64_t>(i) * sectionEntrySize));
        const auto nameOffset = sectionReader.readU32();
        if (!nameOffset)
            continue;

        if (*nameOffset >= stringData.size())
        {
            container.m_sections[i].name.clear();
            continue;
        }

        const auto* start = stringData.data() + *nameOffset;
        const auto* end = std::find(start, stringData.data() + stringData.size(), 0);
        container.m_sections[i].name.assign(start, end);
    }

    return container;
}

bool ElfContainer::is64() const noexcept
{
    return m_is64;
}

std::uint16_t ElfContainer::machine() const noexcept
{
    return m_machine;
}

std::span<const std::uint8_t> ElfContainer::bytes() const noexcept
{
    return m_bytes;
}

const std::vector<ElfSection>& ElfContainer::sections() const noexcept
{
    return m_sections;
}

const ElfSection* ElfContainer::Find(std::string_view name) const noexcept
{
    for (const auto& section : m_sections)
    {
        if (section.name == name)
            return &section;
    }

    return nullptr;
}

const ElfSection* ElfContainer::FindPrefix(std::string_view prefix) const noexcept
{
    for (const auto& section : m_sections)
    {
        if (section.name.size() >= prefix.size() &&
            section.name.compare(0, prefix.size(), prefix) == 0)
        {
            return &section;
        }
    }

    return nullptr;
}

} // namespace SR::Frontend
