#pragma once

#include <cstdint>
#include <string_view>

namespace SR::Frontend {

enum class BinaryFormat : std::uint8_t
{
    Unknown,
    Dxbc,
    Dxil,
    Spirv,
    Cubin,
    Hsaco,
    XboxShader,
};

constexpr std::string_view ToString(BinaryFormat format)
{
    switch (format)
    {
    case BinaryFormat::Dxbc:
        return "dxbc";
    case BinaryFormat::Dxil:
        return "dxil";
    case BinaryFormat::Spirv:
        return "spirv";
    case BinaryFormat::Cubin:
        return "cubin";
    case BinaryFormat::Hsaco:
        return "hsaco";
    case BinaryFormat::XboxShader:
        return "xbox";
    case BinaryFormat::Unknown:
        break;
    }

    return "unknown";
}

} // namespace SR::Frontend
