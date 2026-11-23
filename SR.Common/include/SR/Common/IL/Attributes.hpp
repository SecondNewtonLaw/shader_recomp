#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace SR::IL {

enum class Rounding : std::uint8_t
{
    Default,
    RTNE,
    RTZ,
    RU,
    RD,
    Stochastic,
};

enum class Denorm : std::uint8_t
{
    Default,
    Keep,
    FlushIn,
    FlushOut,
    FlushBoth,
};

enum class MemoryOrder : std::uint8_t
{
    None,
    Relaxed,
    Acquire,
    Release,
    AcqRel,
};

enum class MemoryScope : std::uint8_t
{
    None,
    Wave,
    Workgroup,
    Device,
    System,
};

enum class CachePolicy : std::uint8_t
{
    Default,
    Streaming,
    Persistent,
};

enum class Effect : std::uint16_t
{
    None = 0,
    ReadMemory = 1u << 0,
    WriteMemory = 1u << 1,
    Atomic = 1u << 2,
    Barrier = 1u << 3,
    Wave = 1u << 4,
    Call = 1u << 5,
    Trap = 1u << 6,
};

[[nodiscard]] constexpr Effect operator|(Effect lhs, Effect rhs) noexcept
{
    return static_cast<Effect>(static_cast<std::uint16_t>(lhs) | static_cast<std::uint16_t>(rhs));
}

[[nodiscard]] constexpr Effect operator&(Effect lhs, Effect rhs) noexcept
{
    return static_cast<Effect>(static_cast<std::uint16_t>(lhs) & static_cast<std::uint16_t>(rhs));
}

[[nodiscard]] constexpr bool HasEffect(Effect value, Effect bit) noexcept
{
    return (value & bit) != Effect::None;
}

// Semantic attributes. Vendor modifiers never become separate opcodes.
struct Attrs
{
    Rounding rounding = Rounding::Default;
    Denorm denorm = Denorm::Default;
    bool saturate = false;
    bool approx = false;
    MemoryOrder order = MemoryOrder::None;
    MemoryScope scope = MemoryScope::None;
    bool isVolatile = false;
    CachePolicy cache = CachePolicy::Default;
};

[[nodiscard]] std::string_view ToString(Rounding rounding) noexcept;
[[nodiscard]] std::string_view ToString(Denorm denorm) noexcept;
[[nodiscard]] std::string_view ToString(MemoryOrder order) noexcept;
[[nodiscard]] std::string_view ToString(MemoryScope scope) noexcept;
[[nodiscard]] std::string DescribeAttrs(const Attrs& attrs);

} // namespace SR::IL
