#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace SR::Frontend::Gfx {

// How a buffer format's components are interpreted.
enum class BufferFormatClass : std::uint8_t
{
    UNorm,
    SNorm,
    UScaled,
    SScaled,
    UInt,
    SInt,
    Float,
};

// The layout of one MTBUF/V# surface format: component count, element stride,
// per-component width and bit offset.
struct BufferFormatInfo
{
    BufferFormatClass numeric = BufferFormatClass::UInt;
    std::uint8_t components = 0;
    std::uint8_t elementBytes = 0;
    std::array<std::uint8_t, 4> bits{};
    std::array<std::uint8_t, 4> bitOffsets{};
};

// The format code the instruction carries: (number << 4) | data.
[[nodiscard]] constexpr std::uint32_t UnifiedBufferFormat(std::uint32_t dataFormat,
                                                          std::uint32_t numberFormat) noexcept
{
    return (numberFormat << 4u) | (dataFormat & 0xFu);
}

// The layout of the (data format, number format) pair, or nothing when the
// pair names no format this backend models. The table is measured one pair at a
// time with llvm-mc/llvm-objdump for gfx1030: the disassembler names each pair
// with a BUF_FMT_* layout, and pairs it does not name are absent rather than
// guessed.
[[nodiscard]] std::optional<BufferFormatInfo> BufferFormatOf(std::uint32_t dataFormat,
                                                             std::uint32_t numberFormat) noexcept;

} // namespace SR::Frontend::Gfx
