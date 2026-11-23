#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "SR/Common/Diagnostic.hpp"

namespace SR {

class ByteReader
{
public:
    explicit ByteReader(std::span<const std::uint8_t> data);

    [[nodiscard]] std::size_t offset() const noexcept;
    [[nodiscard]] std::size_t remaining() const noexcept;
    [[nodiscard]] bool empty() const noexcept;

    void seek(std::size_t offset);

    [[nodiscard]] Result<std::uint8_t> readU8();
    [[nodiscard]] Result<std::uint16_t> readU16();
    [[nodiscard]] Result<std::uint32_t> readU32();
    [[nodiscard]] Result<std::uint64_t> readU64();

    [[nodiscard]] Result<std::span<const std::uint8_t>> readBytes(std::size_t count);

private:
    [[nodiscard]] Diagnostic OutOfBounds(std::size_t requested) const;

    std::span<const std::uint8_t> m_data;
    std::size_t m_offset = 0;
};

} // namespace SR
