#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace SR::IL {

using TypeId = std::uint32_t;
inline constexpr TypeId InvalidType = 0xFFFFFFFFu;

enum class TypeKind : std::uint8_t
{
    Void,
    Bool,
    Int,
    Float,
    Vector,
    Packed,
    Matrix,
    Array,
    Struct,
    Pointer,
    Mask,
    Resource,
    Opaque,
};

enum class FloatFormat : std::uint8_t
{
    F16,
    BF16,
    F32,
    F64,
    TF32,
    FP8_E4M3,
    FP8_E5M2,
    FP6,
    FP4,
};

enum class Space : std::uint8_t
{
    Private,
    Workgroup,
    DeviceShared,
    Global,
    Constant,
    PushConstant,
    Input,
    Output,
    Image,
    Tensor,
    Opaque,
};

enum class ResourceKind : std::uint8_t
{
    Buffer,
    Image,
    Sampler,
    Tensor,
    PushConstant,
    Opaque,
};

// A structural type. Types are interned by TypeTable, so equality is identity
// after interning; SameType exists for verifier checks on hand-built types.
struct Type
{
    TypeKind kind = TypeKind::Void;

    // Int / Mask
    std::uint8_t width = 0;
    bool isSigned = false;

    // Float
    FloatFormat floatFormat = FloatFormat::F32;

    // Vector / Packed / Array / Pointer / Matrix
    TypeId element = InvalidType;
    std::uint32_t count = 0;
    std::uint32_t columns = 0;
    std::uint32_t rows = 0;

    // Struct
    std::vector<TypeId> members;
    bool packed = false;

    // Pointer
    Space space = Space::Private;

    // Resource
    ResourceKind resourceKind = ResourceKind::Buffer;
    std::uint32_t format = 0;
    std::uint32_t stride = 0;

    // Opaque
    std::string name;
};

[[nodiscard]] bool SameType(const Type& lhs, const Type& rhs) noexcept;
[[nodiscard]] std::string DescribeType(const Type& type);
[[nodiscard]] std::string_view ToString(TypeKind kind) noexcept;
[[nodiscard]] std::string_view ToString(Space space) noexcept;
[[nodiscard]] std::string_view ToString(ResourceKind kind) noexcept;
[[nodiscard]] std::string_view ToString(FloatFormat format) noexcept;

// Deterministic interning. Types are the only table with O(1) identity; the
// pool is small (tens of entries) and insertion order is walk order.
class TypeTable
{
public:
    [[nodiscard]] TypeId Add(Type type);
    [[nodiscard]] const Type& Get(TypeId id) const;
    [[nodiscard]] std::size_t size() const noexcept { return m_types.size(); }
    [[nodiscard]] bool contains(TypeId id) const noexcept { return id < m_types.size(); }

    [[nodiscard]] TypeId Void();
    [[nodiscard]] TypeId Bool();
    [[nodiscard]] TypeId Int(std::uint8_t width, bool isSigned);
    [[nodiscard]] TypeId Float(FloatFormat format = FloatFormat::F32);
    [[nodiscard]] TypeId Vector(TypeId element, std::uint32_t count);
    [[nodiscard]] TypeId Packed(TypeId element, std::uint32_t count);
    [[nodiscard]] TypeId Matrix(TypeId element, std::uint32_t columns, std::uint32_t rows);
    [[nodiscard]] TypeId Array(TypeId element, std::uint32_t count);
    [[nodiscard]] TypeId Pointer(Space space, TypeId pointee);
    [[nodiscard]] TypeId Mask(std::uint8_t width);
    [[nodiscard]] TypeId Resource(ResourceKind kind, std::uint32_t format = 0, std::uint32_t stride = 0);
    [[nodiscard]] TypeId Opaque(std::string name);

    [[nodiscard]] TypeId U32() { return Int(32, false); }
    [[nodiscard]] TypeId I32() { return Int(32, true); }
    [[nodiscard]] TypeId U64() { return Int(64, false); }
    [[nodiscard]] TypeId I64() { return Int(64, true); }
    [[nodiscard]] TypeId F32() { return Float(FloatFormat::F32); }

    // Lookups on an immutable, already-interned table.
    [[nodiscard]] TypeId FindBool() const;
    [[nodiscard]] TypeId FindInt(std::uint8_t width, bool isSigned) const;
    [[nodiscard]] TypeId FindFloat(FloatFormat format) const;
    [[nodiscard]] TypeId FindPointer(Space space, TypeId pointee) const;

private:
    std::vector<Type> m_types;
};

} // namespace SR::IL
