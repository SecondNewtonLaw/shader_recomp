#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "SR/Common/Diagnostic.hpp"

namespace SR::Frontend {

struct DxbcChunk
{
    std::string fourcc;
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    std::span<const std::uint8_t> data;
};

class DxbcContainer
{
public:
    [[nodiscard]] static bool Probe(std::span<const std::uint8_t> bytes) noexcept;
    [[nodiscard]] static Result<DxbcContainer> Parse(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
    [[nodiscard]] const std::vector<DxbcChunk>& chunks() const noexcept;
    [[nodiscard]] std::uint16_t versionMajor() const noexcept;
    [[nodiscard]] std::uint16_t versionMinor() const noexcept;

    [[nodiscard]] const DxbcChunk* Find(std::string_view fourcc) const noexcept;
    [[nodiscard]] bool HasChunk(std::string_view fourcc) const noexcept;

    [[nodiscard]] bool IsDxil() const noexcept;
    [[nodiscard]] bool IsTokenized() const noexcept;
    [[nodiscard]] const DxbcChunk* CodeChunk() const noexcept;

private:
    std::span<const std::uint8_t> m_bytes;
    std::vector<DxbcChunk> m_chunks;
    std::uint16_t m_versionMajor = 0;
    std::uint16_t m_versionMinor = 0;
};

} // namespace SR::Frontend
