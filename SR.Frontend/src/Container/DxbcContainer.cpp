#include "SR/Frontend/Container/DxbcContainer.hpp"

#include <cstring>
#include <utility>

#include "SR/Common/ByteReader.hpp"

// Layout follows the DirectX Container specification as documented by LLVM
// (llvm/docs/DirectX/DXContainer.md) and DXC's DxilContainer.h.
namespace SR::Frontend {
namespace {

constexpr std::uint32_t kHeaderSize = 32;
constexpr std::uint32_t kMaxChunks = 64;

Diagnostic MakeError(std::string code, std::string message, std::uint32_t offset)
{
    return Diagnostic::Error(std::move(code), std::move(message), offset);
}

} // namespace

bool DxbcContainer::Probe(std::span<const std::uint8_t> bytes) noexcept
{
    return bytes.size() >= 4 && std::memcmp(bytes.data(), "DXBC", 4) == 0;
}

Result<DxbcContainer> DxbcContainer::Parse(std::span<const std::uint8_t> bytes)
{
    if (!Probe(bytes))
        return std::unexpected(MakeError("SR-DXBC-001", "missing DXBC magic", 0));

    ByteReader reader(bytes);
    if (!reader.readBytes(4))
        return std::unexpected(MakeError("SR-DXBC-002", "truncated header", 0));

    if (!reader.readBytes(16))
        return std::unexpected(MakeError("SR-DXBC-002", "truncated checksum", 4));

    const auto versionMajor = reader.readU16();
    const auto versionMinor = reader.readU16();
    const auto totalSize = reader.readU32();
    const auto chunkCount = reader.readU32();
    if (!versionMajor || !versionMinor || !totalSize || !chunkCount)
        return std::unexpected(MakeError("SR-DXBC-002", "truncated header", reader.offset()));

    if (*totalSize < kHeaderSize || *totalSize > bytes.size())
        return std::unexpected(MakeError("SR-DXBC-003", "invalid container size", 24));

    if (*chunkCount == 0 || *chunkCount > kMaxChunks)
        return std::unexpected(MakeError("SR-DXBC-004", "invalid chunk count", 28));

    const std::size_t tableEnd = kHeaderSize + 4ull * *chunkCount;
    if (tableEnd > *totalSize)
        return std::unexpected(MakeError("SR-DXBC-004", "chunk table exceeds container", 28));

    DxbcContainer container;
    container.m_bytes = bytes.first(*totalSize);
    container.m_chunks.reserve(*chunkCount);
    container.m_versionMajor = *versionMajor;
    container.m_versionMinor = *versionMinor;

    for (std::uint32_t i = 0; i < *chunkCount; ++i)
    {
        const auto chunkOffset = reader.readU32();
        if (!chunkOffset)
            return std::unexpected(MakeError("SR-DXBC-004", "truncated chunk table", reader.offset() - 4));

        if (*chunkOffset < tableEnd || *chunkOffset + 8ull > *totalSize)
            return std::unexpected(MakeError("SR-DXBC-005", "chunk header outside container", *chunkOffset));

        ByteReader chunkReader(container.m_bytes.subspan(*chunkOffset));
        const auto fourcc = chunkReader.readBytes(4);
        const auto chunkSize = chunkReader.readU32();
        if (!fourcc || !chunkSize)
            return std::unexpected(MakeError("SR-DXBC-005", "truncated chunk header", *chunkOffset));

        if (static_cast<std::uint64_t>(*chunkOffset) + 8 + *chunkSize > *totalSize)
            return std::unexpected(MakeError("SR-DXBC-006", "chunk data outside container", *chunkOffset));

        DxbcChunk chunk;
        chunk.fourcc.assign(reinterpret_cast<const char*>(fourcc->data()), fourcc->size());
        chunk.offset = *chunkOffset;
        chunk.size = *chunkSize;
        chunk.data = container.m_bytes.subspan(*chunkOffset + 8, *chunkSize);
        container.m_chunks.push_back(std::move(chunk));
    }

    return container;
}

std::span<const std::uint8_t> DxbcContainer::bytes() const noexcept
{
    return m_bytes;
}

const std::vector<DxbcChunk>& DxbcContainer::chunks() const noexcept
{
    return m_chunks;
}

std::uint16_t DxbcContainer::versionMajor() const noexcept
{
    return m_versionMajor;
}

std::uint16_t DxbcContainer::versionMinor() const noexcept
{
    return m_versionMinor;
}

const DxbcChunk* DxbcContainer::Find(std::string_view fourcc) const noexcept
{
    for (const auto& chunk : m_chunks)
    {
        if (chunk.fourcc == fourcc)
            return &chunk;
    }

    return nullptr;
}

bool DxbcContainer::HasChunk(std::string_view fourcc) const noexcept
{
    return Find(fourcc) != nullptr;
}

bool DxbcContainer::IsDxil() const noexcept
{
    return HasChunk("DXIL");
}

bool DxbcContainer::IsTokenized() const noexcept
{
    return HasChunk("SHDR") || HasChunk("SHEX") || HasChunk("SHAB");
}

const DxbcChunk* DxbcContainer::CodeChunk() const noexcept
{
    if (const auto* chunk = Find("SHEX"))
        return chunk;
    if (const auto* chunk = Find("SHDR"))
        return chunk;
    return Find("SHAB");
}

} // namespace SR::Frontend
