#include "SR/Common/IL/Attributes.hpp"

#include <string>

namespace SR::IL {

std::string_view ToString(Rounding rounding) noexcept
{
    switch (rounding)
    {
    case Rounding::Default:
        return "default";
    case Rounding::RTNE:
        return "rtne";
    case Rounding::RTZ:
        return "rtz";
    case Rounding::RU:
        return "ru";
    case Rounding::RD:
        return "rd";
    case Rounding::Stochastic:
        return "stochastic";
    }

    return "unknown";
}

std::string_view ToString(Denorm denorm) noexcept
{
    switch (denorm)
    {
    case Denorm::Default:
        return "default";
    case Denorm::Keep:
        return "keep";
    case Denorm::FlushIn:
        return "flush-in";
    case Denorm::FlushOut:
        return "flush-out";
    case Denorm::FlushBoth:
        return "flush-both";
    }

    return "unknown";
}

std::string_view ToString(MemoryOrder order) noexcept
{
    switch (order)
    {
    case MemoryOrder::None:
        return "none";
    case MemoryOrder::Relaxed:
        return "relaxed";
    case MemoryOrder::Acquire:
        return "acquire";
    case MemoryOrder::Release:
        return "release";
    case MemoryOrder::AcqRel:
        return "acq-rel";
    }

    return "unknown";
}

std::string_view ToString(MemoryScope scope) noexcept
{
    switch (scope)
    {
    case MemoryScope::None:
        return "none";
    case MemoryScope::Wave:
        return "wave";
    case MemoryScope::Workgroup:
        return "workgroup";
    case MemoryScope::Device:
        return "device";
    case MemoryScope::System:
        return "system";
    }

    return "unknown";
}

std::string DescribeAttrs(const Attrs& attrs)
{
    std::string text;
    const auto append = [&text](std::string_view value) {
        if (!text.empty())
            text += ',';
        text += value;
    };

    if (attrs.rounding != Rounding::Default)
        append(ToString(attrs.rounding));
    if (attrs.denorm != Denorm::Default)
        append(ToString(attrs.denorm));
    if (attrs.saturate)
        append("sat");
    if (attrs.approx)
        append("approx");
    if (attrs.order != MemoryOrder::None)
        append(ToString(attrs.order));
    if (attrs.scope != MemoryScope::None)
        append(ToString(attrs.scope));
    if (attrs.isVolatile)
        append("volatile");

    return text;
}

} // namespace SR::IL
