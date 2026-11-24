#include "SR/Frontend/Arch/Gfx/GfxBufferFormat.hpp"

namespace SR::Frontend::Gfx {
namespace {

struct Layout
{
    std::uint8_t components;
    std::uint8_t elementBytes;
    std::array<std::uint8_t, 4> bits;
    std::array<std::uint8_t, 4> bitOffsets;
    BufferFormatClass numeric;
};

// Measured one (data, number) pair at a time with llvm-mc and llvm-objdump for
// gfx1030. Components are packed from bit zero with their widths in order.
constexpr std::array<std::array<std::optional<Layout>, 8>, 16> kLayoutByFormat = {{
    {{std::nullopt, Layout{2u, 2u, {8, 8, 0, 0}, {0, 8, 0, 0}, BufferFormatClass::UScaled}, Layout{3u, 4u, {10, 11, 11, 0}, {0, 10, 21, 0}, BufferFormatClass::UScaled}, Layout{4u, 4u, {10, 10, 10, 2}, {0, 10, 20, 30}, BufferFormatClass::UInt}, Layout{2u, 8u, {32, 32, 0, 0}, {0, 32, 0, 0}, BufferFormatClass::Float}, std::nullopt, std::nullopt, std::nullopt}},
    {{std::nullopt, Layout{2u, 2u, {8, 8, 0, 0}, {0, 8, 0, 0}, BufferFormatClass::SScaled}, Layout{3u, 4u, {10, 11, 11, 0}, {0, 10, 21, 0}, BufferFormatClass::SScaled}, Layout{4u, 4u, {10, 10, 10, 2}, {0, 10, 20, 30}, BufferFormatClass::SInt}, Layout{4u, 8u, {16, 16, 16, 16}, {0, 16, 32, 48}, BufferFormatClass::UNorm}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 1u, {8, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::SNorm}, Layout{2u, 2u, {8, 8, 0, 0}, {0, 8, 0, 0}, BufferFormatClass::UInt}, Layout{3u, 4u, {10, 11, 11, 0}, {0, 10, 21, 0}, BufferFormatClass::UInt}, Layout{4u, 4u, {2, 10, 10, 10}, {0, 2, 12, 22}, BufferFormatClass::UNorm}, Layout{4u, 8u, {16, 16, 16, 16}, {0, 16, 32, 48}, BufferFormatClass::SNorm}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 1u, {8, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::UScaled}, Layout{2u, 2u, {8, 8, 0, 0}, {0, 8, 0, 0}, BufferFormatClass::SInt}, Layout{3u, 4u, {10, 11, 11, 0}, {0, 10, 21, 0}, BufferFormatClass::SInt}, Layout{4u, 4u, {2, 10, 10, 10}, {0, 2, 12, 22}, BufferFormatClass::SNorm}, Layout{4u, 8u, {16, 16, 16, 16}, {0, 16, 32, 48}, BufferFormatClass::UScaled}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 1u, {8, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::SScaled}, Layout{1u, 4u, {32, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::UInt}, Layout{3u, 4u, {10, 11, 11, 0}, {0, 10, 21, 0}, BufferFormatClass::Float}, Layout{4u, 4u, {2, 10, 10, 10}, {0, 2, 12, 22}, BufferFormatClass::UScaled}, Layout{4u, 8u, {16, 16, 16, 16}, {0, 16, 32, 48}, BufferFormatClass::SScaled}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 1u, {8, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::UInt}, Layout{1u, 4u, {32, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::SInt}, Layout{3u, 4u, {11, 11, 10, 0}, {0, 11, 22, 0}, BufferFormatClass::UNorm}, Layout{4u, 4u, {2, 10, 10, 10}, {0, 2, 12, 22}, BufferFormatClass::SScaled}, Layout{4u, 8u, {16, 16, 16, 16}, {0, 16, 32, 48}, BufferFormatClass::UInt}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 1u, {8, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::SInt}, Layout{1u, 4u, {32, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::Float}, Layout{3u, 4u, {11, 11, 10, 0}, {0, 11, 22, 0}, BufferFormatClass::SNorm}, Layout{4u, 4u, {2, 10, 10, 10}, {0, 2, 12, 22}, BufferFormatClass::UInt}, Layout{4u, 8u, {16, 16, 16, 16}, {0, 16, 32, 48}, BufferFormatClass::SInt}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 2u, {16, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::UNorm}, Layout{2u, 4u, {16, 16, 0, 0}, {0, 16, 0, 0}, BufferFormatClass::UNorm}, Layout{3u, 4u, {11, 11, 10, 0}, {0, 11, 22, 0}, BufferFormatClass::UScaled}, Layout{4u, 4u, {2, 10, 10, 10}, {0, 2, 12, 22}, BufferFormatClass::SInt}, Layout{4u, 8u, {16, 16, 16, 16}, {0, 16, 32, 48}, BufferFormatClass::Float}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 2u, {16, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::SNorm}, Layout{2u, 4u, {16, 16, 0, 0}, {0, 16, 0, 0}, BufferFormatClass::SNorm}, Layout{3u, 4u, {11, 11, 10, 0}, {0, 11, 22, 0}, BufferFormatClass::SScaled}, Layout{4u, 4u, {8, 8, 8, 8}, {0, 8, 16, 24}, BufferFormatClass::UNorm}, Layout{3u, 12u, {32, 32, 32, 0}, {0, 32, 64, 0}, BufferFormatClass::UInt}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 2u, {16, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::UScaled}, Layout{2u, 4u, {16, 16, 0, 0}, {0, 16, 0, 0}, BufferFormatClass::UScaled}, Layout{3u, 4u, {11, 11, 10, 0}, {0, 11, 22, 0}, BufferFormatClass::UInt}, Layout{4u, 4u, {8, 8, 8, 8}, {0, 8, 16, 24}, BufferFormatClass::SNorm}, Layout{3u, 12u, {32, 32, 32, 0}, {0, 32, 64, 0}, BufferFormatClass::SInt}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 2u, {16, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::SScaled}, Layout{2u, 4u, {16, 16, 0, 0}, {0, 16, 0, 0}, BufferFormatClass::SScaled}, Layout{3u, 4u, {11, 11, 10, 0}, {0, 11, 22, 0}, BufferFormatClass::SInt}, Layout{4u, 4u, {8, 8, 8, 8}, {0, 8, 16, 24}, BufferFormatClass::UScaled}, Layout{3u, 12u, {32, 32, 32, 0}, {0, 32, 64, 0}, BufferFormatClass::Float}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 2u, {16, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::UInt}, Layout{2u, 4u, {16, 16, 0, 0}, {0, 16, 0, 0}, BufferFormatClass::UInt}, Layout{3u, 4u, {11, 11, 10, 0}, {0, 11, 22, 0}, BufferFormatClass::Float}, Layout{4u, 4u, {8, 8, 8, 8}, {0, 8, 16, 24}, BufferFormatClass::SScaled}, Layout{4u, 16u, {32, 32, 32, 32}, {0, 32, 64, 96}, BufferFormatClass::UInt}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 2u, {16, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::SInt}, Layout{2u, 4u, {16, 16, 0, 0}, {0, 16, 0, 0}, BufferFormatClass::SInt}, Layout{4u, 4u, {10, 10, 10, 2}, {0, 10, 20, 30}, BufferFormatClass::UNorm}, Layout{4u, 4u, {8, 8, 8, 8}, {0, 8, 16, 24}, BufferFormatClass::UInt}, Layout{4u, 16u, {32, 32, 32, 32}, {0, 32, 64, 96}, BufferFormatClass::SInt}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{1u, 2u, {16, 0, 0, 0}, {0, 0, 0, 0}, BufferFormatClass::Float}, Layout{2u, 4u, {16, 16, 0, 0}, {0, 16, 0, 0}, BufferFormatClass::Float}, Layout{4u, 4u, {10, 10, 10, 2}, {0, 10, 20, 30}, BufferFormatClass::SNorm}, Layout{4u, 4u, {8, 8, 8, 8}, {0, 8, 16, 24}, BufferFormatClass::SInt}, Layout{4u, 16u, {32, 32, 32, 32}, {0, 32, 64, 96}, BufferFormatClass::Float}, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{2u, 2u, {8, 8, 0, 0}, {0, 8, 0, 0}, BufferFormatClass::UNorm}, Layout{3u, 4u, {10, 11, 11, 0}, {0, 10, 21, 0}, BufferFormatClass::UNorm}, Layout{4u, 4u, {10, 10, 10, 2}, {0, 10, 20, 30}, BufferFormatClass::UScaled}, Layout{2u, 8u, {32, 32, 0, 0}, {0, 32, 0, 0}, BufferFormatClass::UInt}, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
    {{Layout{2u, 2u, {8, 8, 0, 0}, {0, 8, 0, 0}, BufferFormatClass::SNorm}, Layout{3u, 4u, {10, 11, 11, 0}, {0, 10, 21, 0}, BufferFormatClass::SNorm}, Layout{4u, 4u, {10, 10, 10, 2}, {0, 10, 20, 30}, BufferFormatClass::SScaled}, Layout{2u, 8u, {32, 32, 0, 0}, {0, 32, 0, 0}, BufferFormatClass::SInt}, std::nullopt, std::nullopt, std::nullopt, std::nullopt}},
}};

} // namespace

std::optional<BufferFormatInfo> BufferFormatOf(std::uint32_t dataFormat,
                                               std::uint32_t numberFormat) noexcept
{
    if (dataFormat > 0xFu || numberFormat > 0x7u)
        return std::nullopt;
    const auto& layout = kLayoutByFormat[dataFormat][numberFormat];
    if (!layout)
        return std::nullopt;

    BufferFormatInfo info;
    info.numeric = layout->numeric;
    info.components = layout->components;
    info.elementBytes = layout->elementBytes;
    info.bits = layout->bits;
    info.bitOffsets = layout->bitOffsets;
    return info;
}

} // namespace SR::Frontend::Gfx
