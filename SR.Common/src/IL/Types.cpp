#include "SR/Common/IL/Types.hpp"

#include <string>
#include <utility>

namespace SR::IL {
namespace {

std::string_view FloatSuffix(FloatFormat format) noexcept
{
    switch (format)
    {
    case FloatFormat::F16:
        return "f16";
    case FloatFormat::BF16:
        return "bf16";
    case FloatFormat::F32:
        return "f32";
    case FloatFormat::F64:
        return "f64";
    case FloatFormat::TF32:
        return "tf32";
    case FloatFormat::FP8_E4M3:
        return "fp8e4m3";
    case FloatFormat::FP8_E5M2:
        return "fp8e5m2";
    case FloatFormat::FP6:
        return "fp6";
    case FloatFormat::FP4:
        return "fp4";
    }

    return "f?";
}

} // namespace

std::string_view ToString(TypeKind kind) noexcept
{
    switch (kind)
    {
    case TypeKind::Void:
        return "void";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::Int:
        return "int";
    case TypeKind::Float:
        return "float";
    case TypeKind::Vector:
        return "vector";
    case TypeKind::Packed:
        return "packed";
    case TypeKind::Matrix:
        return "matrix";
    case TypeKind::Array:
        return "array";
    case TypeKind::Struct:
        return "struct";
    case TypeKind::Pointer:
        return "pointer";
    case TypeKind::Mask:
        return "mask";
    case TypeKind::Resource:
        return "resource";
    case TypeKind::Opaque:
        return "opaque";
    }

    return "unknown";
}

std::string_view ToString(Space space) noexcept
{
    switch (space)
    {
    case Space::Private:
        return "private";
    case Space::Workgroup:
        return "workgroup";
    case Space::DeviceShared:
        return "device-shared";
    case Space::Global:
        return "global";
    case Space::Constant:
        return "constant";
    case Space::PushConstant:
        return "push-constant";
    case Space::Input:
        return "input";
    case Space::Output:
        return "output";
    case Space::Image:
        return "image";
    case Space::Tensor:
        return "tensor";
    case Space::Opaque:
        return "opaque";
    }

    return "unknown";
}

std::string_view ToString(ResourceKind kind) noexcept
{
    switch (kind)
    {
    case ResourceKind::Buffer:
        return "buffer";
    case ResourceKind::Image:
        return "image";
    case ResourceKind::Sampler:
        return "sampler";
    case ResourceKind::Tensor:
        return "tensor";
    case ResourceKind::PushConstant:
        return "push-constant";
    case ResourceKind::Opaque:
        return "opaque";
    }

    return "unknown";
}

std::string_view ToString(FloatFormat format) noexcept
{
    switch (format)
    {
    case FloatFormat::F16:
        return "f16";
    case FloatFormat::BF16:
        return "bf16";
    case FloatFormat::F32:
        return "f32";
    case FloatFormat::F64:
        return "f64";
    case FloatFormat::TF32:
        return "tf32";
    case FloatFormat::FP8_E4M3:
        return "fp8_e4m3";
    case FloatFormat::FP8_E5M2:
        return "fp8_e5m2";
    case FloatFormat::FP6:
        return "fp6";
    case FloatFormat::FP4:
        return "fp4";
    }

    return "unknown";
}

bool SameType(const Type& lhs, const Type& rhs) noexcept
{
    if (lhs.kind != rhs.kind)
        return false;

    switch (lhs.kind)
    {
    case TypeKind::Void:
    case TypeKind::Bool:
        return true;
    case TypeKind::Int:
        return lhs.width == rhs.width && lhs.isSigned == rhs.isSigned;
    case TypeKind::Float:
        return lhs.floatFormat == rhs.floatFormat;
    case TypeKind::Vector:
    case TypeKind::Packed:
    case TypeKind::Array:
        return lhs.element == rhs.element && lhs.count == rhs.count;
    case TypeKind::Matrix:
        return lhs.element == rhs.element && lhs.columns == rhs.columns && lhs.rows == rhs.rows;
    case TypeKind::Struct:
        return lhs.members == rhs.members && lhs.packed == rhs.packed;
    case TypeKind::Pointer:
        return lhs.space == rhs.space && lhs.element == rhs.element;
    case TypeKind::Mask:
        return lhs.width == rhs.width;
    case TypeKind::Resource:
        return lhs.resourceKind == rhs.resourceKind && lhs.format == rhs.format && lhs.stride == rhs.stride;
    case TypeKind::Opaque:
        return lhs.name == rhs.name;
    }

    return false;
}

std::string DescribeType(const Type& type)
{
    switch (type.kind)
    {
    case TypeKind::Void:
        return "void";
    case TypeKind::Bool:
        return "bool";
    case TypeKind::Int:
        return (type.isSigned ? "i" : "u") + std::to_string(type.width);
    case TypeKind::Float:
        return std::string(FloatSuffix(type.floatFormat));
    case TypeKind::Vector:
        return "v" + std::to_string(type.count) + "<" + std::to_string(type.element) + ">";
    case TypeKind::Packed:
        return "packed" + std::to_string(type.count) + "<" + std::to_string(type.element) + ">";
    case TypeKind::Matrix:
        return "matrix(" + std::to_string(type.columns) + "x" + std::to_string(type.rows) + ")<" +
               std::to_string(type.element) + ">";
    case TypeKind::Array:
        return "array" + std::to_string(type.count) + "<" + std::to_string(type.element) + ">";
    case TypeKind::Struct:
    {
        std::string text = "struct{";
        for (std::size_t i = 0; i < type.members.size(); ++i)
        {
            if (i != 0)
                text += ", ";
            text += std::to_string(type.members[i]);
        }
        text += "}";
        return text;
    }
    case TypeKind::Pointer:
        return "ptr<" + std::string(ToString(type.space)) + ", " + std::to_string(type.element) + ">";
    case TypeKind::Mask:
        return "mask" + std::to_string(type.width);
    case TypeKind::Resource:
    {
        std::string text = "res<";
        text += ToString(type.resourceKind);
        text += ">";
        return text;
    }
    case TypeKind::Opaque:
        return "opaque{" + type.name + "}";
    }

    return "?";
}

TypeId TypeTable::Add(Type type)
{
    for (std::size_t i = 0; i < m_types.size(); ++i)
    {
        if (SameType(m_types[i], type))
            return static_cast<TypeId>(i);
    }

    m_types.push_back(std::move(type));
    return static_cast<TypeId>(m_types.size() - 1);
}

TypeId TypeTable::FindBool() const
{
    for (std::size_t i = 0; i < m_types.size(); ++i)
    {
        if (m_types[i].kind == TypeKind::Bool)
            return static_cast<TypeId>(i);
    }
    return InvalidType;
}

TypeId TypeTable::FindInt(std::uint8_t width, bool isSigned) const
{
    for (std::size_t i = 0; i < m_types.size(); ++i)
    {
        const Type& type = m_types[i];
        if (type.kind == TypeKind::Int && type.width == width && type.isSigned == isSigned)
            return static_cast<TypeId>(i);
    }
    return InvalidType;
}

TypeId TypeTable::FindFloat(FloatFormat format) const
{
    for (std::size_t i = 0; i < m_types.size(); ++i)
    {
        const Type& type = m_types[i];
        if (type.kind == TypeKind::Float && type.floatFormat == format)
            return static_cast<TypeId>(i);
    }
    return InvalidType;
}

TypeId TypeTable::FindPointer(Space space, TypeId pointee) const
{
    for (std::size_t i = 0; i < m_types.size(); ++i)
    {
        const Type& type = m_types[i];
        if (type.kind == TypeKind::Pointer && type.space == space && type.element == pointee)
            return static_cast<TypeId>(i);
    }
    return InvalidType;
}

const Type& TypeTable::Get(TypeId id) const
{
    return m_types[id];
}

TypeId TypeTable::Void()
{
    Type type;
    type.kind = TypeKind::Void;
    return Add(std::move(type));
}

TypeId TypeTable::Bool()
{
    Type type;
    type.kind = TypeKind::Bool;
    return Add(std::move(type));
}

TypeId TypeTable::Int(std::uint8_t width, bool isSigned)
{
    Type type;
    type.kind = TypeKind::Int;
    type.width = width;
    type.isSigned = isSigned;
    return Add(std::move(type));
}

TypeId TypeTable::Float(FloatFormat format)
{
    Type type;
    type.kind = TypeKind::Float;
    type.floatFormat = format;
    return Add(std::move(type));
}

TypeId TypeTable::Vector(TypeId element, std::uint32_t count)
{
    Type type;
    type.kind = TypeKind::Vector;
    type.element = element;
    type.count = count;
    return Add(std::move(type));
}

TypeId TypeTable::Packed(TypeId element, std::uint32_t count)
{
    Type type;
    type.kind = TypeKind::Packed;
    type.element = element;
    type.count = count;
    return Add(std::move(type));
}

TypeId TypeTable::Matrix(TypeId element, std::uint32_t columns, std::uint32_t rows)
{
    Type type;
    type.kind = TypeKind::Matrix;
    type.element = element;
    type.columns = columns;
    type.rows = rows;
    return Add(std::move(type));
}

TypeId TypeTable::Array(TypeId element, std::uint32_t count)
{
    Type type;
    type.kind = TypeKind::Array;
    type.element = element;
    type.count = count;
    return Add(std::move(type));
}

TypeId TypeTable::Pointer(Space space, TypeId pointee)
{
    Type type;
    type.kind = TypeKind::Pointer;
    type.space = space;
    type.element = pointee;
    return Add(std::move(type));
}

TypeId TypeTable::Mask(std::uint8_t width)
{
    Type type;
    type.kind = TypeKind::Mask;
    type.width = width;
    return Add(std::move(type));
}

TypeId TypeTable::Resource(ResourceKind kind, std::uint32_t format, std::uint32_t stride)
{
    Type type;
    type.kind = TypeKind::Resource;
    type.resourceKind = kind;
    type.format = format;
    type.stride = stride;
    return Add(std::move(type));
}

TypeId TypeTable::Opaque(std::string name)
{
    Type type;
    type.kind = TypeKind::Opaque;
    type.name = std::move(name);
    return Add(std::move(type));
}

} // namespace SR::IL
