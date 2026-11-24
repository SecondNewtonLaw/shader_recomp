#include "SR/Frontend/Arch/Gfx/GfxInterpreter.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

#include "SR/Common/IL/Instruction.hpp"

namespace SR::Frontend::Gfx {
namespace {

Diagnostic Error(std::string code, std::string message, std::uint32_t pc)
{
    return Diagnostic::Error(std::move(code), std::move(message), pc);
}

std::int32_t AsSigned(std::uint32_t value) noexcept
{
    return static_cast<std::int32_t>(value);
}

float AsFloat(std::uint32_t value) noexcept
{
    return std::bit_cast<float>(value);
}

float HalfToFloat(std::uint16_t half) noexcept
{
    const std::uint32_t sign = (half & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1Fu;
    const std::uint32_t mantissa = half & 0x3FFu;
    std::uint32_t bits = 0;
    if (exponent == 0)
    {
        if (mantissa == 0)
        {
            bits = sign;
        }
        else
        {
            std::uint32_t value = mantissa;
            std::int32_t shift = 0;
            while ((value & 0x400u) == 0)
            {
                value <<= 1;
                ++shift;
            }
            value &= 0x3FFu;
            bits = sign | (static_cast<std::uint32_t>(127 - 15 - shift + 1) << 23) | (value << 13);
        }
    }
    else if (exponent == 0x1F)
    {
        bits = sign | 0x7F800000u | (mantissa << 13);
    }
    else
    {
        bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    return std::bit_cast<float>(bits);
}

std::uint16_t FloatToHalf(float value) noexcept
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127 + 15;
    const std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent <= 0)
    {
        if (exponent < -10)
            return static_cast<std::uint16_t>(sign);
        const std::uint32_t shifted = (mantissa | 0x800000u) >> (1 - exponent);
        return static_cast<std::uint16_t>(sign | (shifted >> 13));
    }
    if (exponent >= 31)
        return static_cast<std::uint16_t>(sign | 0x7C00u | (mantissa != 0 ? 0x200u : 0u));
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13));
}

std::uint32_t AsBits(float value) noexcept
{
    return std::bit_cast<std::uint32_t>(value);
}

std::uint32_t SignExtend16(std::uint32_t value) noexcept
{
    return static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(value & 0xFFFFu)));
}

std::uint32_t BitReverse(std::uint32_t value) noexcept
{
    std::uint32_t result = 0;
    for (unsigned bit = 0; bit < 32; ++bit)
        result = (result << 1) | ((value >> bit) & 1u);
    return result;
}

std::uint64_t BitReverse64(std::uint64_t value) noexcept
{
    std::uint64_t result = 0;
    for (unsigned bit = 0; bit < 64; ++bit)
        result = (result << 1) | ((value >> bit) & 1u);
    return result;
}

std::uint32_t BitMask(std::uint32_t width) noexcept
{
    if (width >= 32)
        return 0xFFFFFFFFu;
    return (1u << width) - 1u;
}

bool Ordered(float left, float right) noexcept
{
    return !std::isnan(left) && !std::isnan(right);
}

float FloatSource(const DecodedInstruction& instruction, std::uint8_t index, std::uint32_t bits) noexcept
{
    float value = AsFloat(bits);
    const std::uint8_t bit = static_cast<std::uint8_t>(index - 1);
    if ((instruction.abs & (1u << bit)) != 0)
        value = std::fabs(value);
    if ((instruction.neg & (1u << bit)) != 0)
        value = -value;
    return value;
}

std::uint32_t FloatResult(const DecodedInstruction& instruction, float value) noexcept
{
    if (instruction.omod == 1)
        value *= 2.0f;
    else if (instruction.omod == 2)
        value *= 4.0f;
    else if (instruction.omod == 3)
        value *= 0.5f;
    if (instruction.clamp)
        value = std::fmin(std::fmax(value, 0.0f), 1.0f);
    return AsBits(value);
}

std::optional<std::uint32_t> ReadOperand(const IsaState& state, const Operand& operand)
{
    switch (operand.kind)
    {
    case OperandKind::Sgpr:
        return state.sgpr[operand.value];
    case OperandKind::Sdst:
        return state.sgpr[operand.value];
    case OperandKind::Vgpr:
    case OperandKind::Vsrc:
        return state.vgpr[operand.value];
    case OperandKind::Literal:
    case OperandKind::Imm32:
        return operand.value;
    case OperandKind::Ssrc:
    case OperandKind::Src:
        if (operand.isInline)
        {
            if (const auto bits = InlineFloatBits(operand.value))
                return *bits;
            if ((operand.value >= 128 && operand.value <= 208))
                return InlineInteger(operand.value);
            return std::nullopt;
        }
        if (operand.value < 128)
            return state.sgpr[operand.value];
        return state.vgpr[operand.value];
    default:
        return std::nullopt;
    }
}

bool WriteOperand(IsaState& state, const Operand& operand, std::uint32_t value)
{
    switch (operand.kind)
    {
    case OperandKind::Sdst:
        if (operand.value >= 256)
            return false;
        state.sgpr[operand.value] = value;
        return true;
    case OperandKind::Vgpr:
        if (operand.value >= 256)
            return false;
        state.vgpr[operand.value] = value;
        return true;
    default:
        return false;
    }
}

std::optional<std::uint64_t> ReadPair(const IsaState& state, const Operand& operand)
{
    if (operand.kind == OperandKind::Sgpr || (operand.kind == OperandKind::Ssrc && !operand.isInline) ||
        (operand.kind == OperandKind::Src && !operand.isInline))
    {
        const std::uint32_t index = operand.value;
        if (index + 1 >= 256)
            return std::nullopt;
        return static_cast<std::uint64_t>(state.sgpr[index]) |
               (static_cast<std::uint64_t>(state.sgpr[index + 1]) << 32);
    }
    if ((operand.kind == OperandKind::Ssrc || operand.kind == OperandKind::Src) && operand.isInline)
    {
        const std::int64_t extended = static_cast<std::int32_t>(InlineInteger(operand.value));
        return static_cast<std::uint64_t>(extended);
    }
    if (operand.kind == OperandKind::Literal)
        return static_cast<std::uint64_t>(operand.value);
    return std::nullopt;
}

bool WritePair(IsaState& state, const Operand& operand, std::uint64_t value)
{
    if (operand.kind != OperandKind::Sdst || operand.value + 1 >= 256)
        return false;
    state.sgpr[operand.value] = static_cast<std::uint32_t>(value);
    state.sgpr[operand.value + 1] = static_cast<std::uint32_t>(value >> 32);
    return true;
}

std::optional<std::uint64_t> ReadVgprPair(const IsaState& state, const Operand& operand)
{
    if ((operand.kind != OperandKind::Vgpr && operand.kind != OperandKind::Vsrc) || operand.value + 1 >= 256)
        return std::nullopt;
    return static_cast<std::uint64_t>(state.vgpr[operand.value]) |
           (static_cast<std::uint64_t>(state.vgpr[operand.value + 1]) << 32);
}

bool WriteVgprPair(IsaState& state, const Operand& operand, std::uint64_t value)
{
    if (operand.kind != OperandKind::Vgpr || operand.value + 1 >= 256)
        return false;
    state.vgpr[operand.value] = static_cast<std::uint32_t>(value);
    state.vgpr[operand.value + 1] = static_cast<std::uint32_t>(value >> 32);
    return true;
}

std::optional<double> ReadF64(const IsaState& state, const Operand& operand)
{
    const auto bits = ReadVgprPair(state, operand);
    if (!bits)
        return std::nullopt;
    return std::bit_cast<double>(*bits);
}

bool WriteF64(IsaState& state, const Operand& operand, double value)
{
    return WriteVgprPair(state, operand, std::bit_cast<std::uint64_t>(value));
}

enum class CompareKind
{
    F16,
    F64,
    I16,
    U16,
    I64,
    U64,
};

struct CompareSpec
{
    CompareKind kind = CompareKind::I64;
    IL::CompareOp op = IL::CompareOp::IEq;
    std::uint8_t predicate = 0; // 0 compare, 1 false, 2 true, 3 ordered, 4 unordered
};

std::optional<bool> EvaluateCompare(const IsaState& state,
                                     const DecodedInstruction& instruction,
                                     const CompareSpec& spec)
{
    const auto read = [&](std::uint8_t index) -> std::optional<std::uint64_t> {
        if (spec.kind == CompareKind::F64)
        {
            const auto value = ReadF64(state, instruction.operands[index]);
            if (!value)
                return std::nullopt;
            return std::bit_cast<std::uint64_t>(*value);
        }
        if (spec.kind == CompareKind::I64 || spec.kind == CompareKind::U64)
            return ReadVgprPair(state, instruction.operands[index]);
        const auto value = ReadOperand(state, instruction.operands[index]);
        if (!value)
            return std::nullopt;
        return static_cast<std::uint64_t>(*value);
    };

    const auto left = read(1);
    const auto right = read(2);
    if (!left || !right)
        return std::nullopt;

    if (spec.predicate == 1)
        return false;
    if (spec.predicate == 2)
        return true;

    if (spec.kind == CompareKind::F16 || spec.kind == CompareKind::F64)
    {
        const auto asDouble = [&](std::uint64_t bits) {
            return spec.kind == CompareKind::F16
                       ? static_cast<double>(HalfToFloat(static_cast<std::uint16_t>(bits)))
                       : std::bit_cast<double>(bits);
        };
        const double a = asDouble(*left);
        const double b = asDouble(*right);
        const bool ordered = Ordered(static_cast<float>(a), static_cast<float>(b)) &&
                             (spec.kind == CompareKind::F16 || !(std::isnan(a) || std::isnan(b)));
        if (spec.predicate == 3)
            return ordered;
        if (spec.predicate == 4)
            return !ordered;

        switch (spec.op)
        {
        case IL::CompareOp::FOrdEq:
            return ordered && a == b;
        case IL::CompareOp::FOrdNe:
            return ordered && a != b;
        case IL::CompareOp::FOrdLt:
            return ordered && a < b;
        case IL::CompareOp::FOrdLe:
            return ordered && a <= b;
        case IL::CompareOp::FOrdGt:
            return ordered && a > b;
        case IL::CompareOp::FOrdGe:
            return ordered && a >= b;
        case IL::CompareOp::FUnordEq:
            return !ordered || a == b;
        case IL::CompareOp::FUnordNe:
            return !ordered || a != b;
        case IL::CompareOp::FUnordLt:
            return !ordered || a < b;
        case IL::CompareOp::FUnordLe:
            return !ordered || a <= b;
        case IL::CompareOp::FUnordGt:
            return !ordered || a > b;
        case IL::CompareOp::FUnordGe:
            return !ordered || a >= b;
        default:
            return std::nullopt;
        }
    }

    const auto signedValue = [&](std::uint64_t bits) -> std::int64_t {
        if (spec.kind == CompareKind::I16)
            return static_cast<std::int16_t>(bits & 0xFFFFu);
        return static_cast<std::int64_t>(bits);
    };
    const auto unsignedValue = [&](std::uint64_t bits) -> std::uint64_t {
        if (spec.kind == CompareKind::U16)
            return bits & 0xFFFFu;
        return bits;
    };
    const auto signedLeft = signedValue(*left);
    const auto signedRight = signedValue(*right);
    const auto unsignedLeft = unsignedValue(*left);
    const auto unsignedRight = unsignedValue(*right);

    switch (spec.op)
    {
    case IL::CompareOp::IEq:
    case IL::CompareOp::UEq:
        return unsignedLeft == unsignedRight;
    case IL::CompareOp::INe:
    case IL::CompareOp::UNe:
        return unsignedLeft != unsignedRight;
    case IL::CompareOp::ILt:
        return signedLeft < signedRight;
    case IL::CompareOp::ILe:
        return signedLeft <= signedRight;
    case IL::CompareOp::IGt:
        return signedLeft > signedRight;
    case IL::CompareOp::IGe:
        return signedLeft >= signedRight;
    case IL::CompareOp::ULt:
        return unsignedLeft < unsignedRight;
    case IL::CompareOp::ULe:
        return unsignedLeft <= unsignedRight;
    case IL::CompareOp::UGt:
        return unsignedLeft > unsignedRight;
    case IL::CompareOp::UGe:
        return unsignedLeft >= unsignedRight;
    default:
        return std::nullopt;
    }
}

std::optional<CompareSpec> CompareSpecOf(Op op)
{
    switch (op)
    {
        case Op::V_CmpLtF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdLt, 0};
        case Op::V_CmpEqF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdEq, 0};
        case Op::V_CmpLeF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdLe, 0};
        case Op::V_CmpGtF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdGt, 0};
        case Op::V_CmpLgF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdNe, 0};
        case Op::V_CmpGeF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdGe, 0};
        case Op::V_CmpFalseF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdEq, 1};
        case Op::V_CmpTrueF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdEq, 2};
        case Op::V_CmpOrdF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdEq, 3};
        case Op::V_CmpUnordF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdEq, 4};
        case Op::V_CmpNgeF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FUnordLt, 0};
        case Op::V_CmpNlgF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FUnordEq, 0};
        case Op::V_CmpNgtF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FUnordLe, 0};
        case Op::V_CmpNleF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FUnordGt, 0};
        case Op::V_CmpNeqF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FOrdNe, 0};
        case Op::V_CmpNltF16:
            return CompareSpec{CompareKind::F16, IL::CompareOp::FUnordGe, 0};
        case Op::V_CmpLtF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdLt, 0};
        case Op::V_CmpEqF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdEq, 0};
        case Op::V_CmpLeF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdLe, 0};
        case Op::V_CmpGtF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdGt, 0};
        case Op::V_CmpLgF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdNe, 0};
        case Op::V_CmpGeF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdGe, 0};
        case Op::V_CmpFalseF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdEq, 1};
        case Op::V_CmpTrueF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdEq, 2};
        case Op::V_CmpOrdF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdEq, 3};
        case Op::V_CmpUnordF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdEq, 4};
        case Op::V_CmpNgeF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FUnordLt, 0};
        case Op::V_CmpNlgF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FUnordEq, 0};
        case Op::V_CmpNgtF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FUnordLe, 0};
        case Op::V_CmpNleF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FUnordGt, 0};
        case Op::V_CmpNeqF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FOrdNe, 0};
        case Op::V_CmpNltF64:
            return CompareSpec{CompareKind::F64, IL::CompareOp::FUnordGe, 0};
        case Op::V_CmpLtI16:
            return CompareSpec{CompareKind::I16, IL::CompareOp::ILt, 0};
        case Op::V_CmpEqI16:
            return CompareSpec{CompareKind::I16, IL::CompareOp::IEq, 0};
        case Op::V_CmpLeI16:
            return CompareSpec{CompareKind::I16, IL::CompareOp::ILe, 0};
        case Op::V_CmpGtI16:
            return CompareSpec{CompareKind::I16, IL::CompareOp::IGt, 0};
        case Op::V_CmpNeI16:
            return CompareSpec{CompareKind::I16, IL::CompareOp::INe, 0};
        case Op::V_CmpGeI16:
            return CompareSpec{CompareKind::I16, IL::CompareOp::IGe, 0};
        case Op::V_CmpLtI64:
            return CompareSpec{CompareKind::I64, IL::CompareOp::ILt, 0};
        case Op::V_CmpEqI64:
            return CompareSpec{CompareKind::I64, IL::CompareOp::IEq, 0};
        case Op::V_CmpLeI64:
            return CompareSpec{CompareKind::I64, IL::CompareOp::ILe, 0};
        case Op::V_CmpGtI64:
            return CompareSpec{CompareKind::I64, IL::CompareOp::IGt, 0};
        case Op::V_CmpNeI64:
            return CompareSpec{CompareKind::I64, IL::CompareOp::INe, 0};
        case Op::V_CmpGeI64:
            return CompareSpec{CompareKind::I64, IL::CompareOp::IGe, 0};
        case Op::V_CmpFalseI64:
            return CompareSpec{CompareKind::I64, IL::CompareOp::IEq, 1};
        case Op::V_CmpTrueI64:
            return CompareSpec{CompareKind::I64, IL::CompareOp::IEq, 2};
        case Op::V_CmpLtU16:
            return CompareSpec{CompareKind::U16, IL::CompareOp::ULt, 0};
        case Op::V_CmpEqU16:
            return CompareSpec{CompareKind::U16, IL::CompareOp::UEq, 0};
        case Op::V_CmpLeU16:
            return CompareSpec{CompareKind::U16, IL::CompareOp::ULe, 0};
        case Op::V_CmpGtU16:
            return CompareSpec{CompareKind::U16, IL::CompareOp::UGt, 0};
        case Op::V_CmpNeU16:
            return CompareSpec{CompareKind::U16, IL::CompareOp::UNe, 0};
        case Op::V_CmpGeU16:
            return CompareSpec{CompareKind::U16, IL::CompareOp::UGe, 0};
        case Op::V_CmpLtU64:
            return CompareSpec{CompareKind::U64, IL::CompareOp::ULt, 0};
        case Op::V_CmpEqU64:
            return CompareSpec{CompareKind::U64, IL::CompareOp::UEq, 0};
        case Op::V_CmpLeU64:
            return CompareSpec{CompareKind::U64, IL::CompareOp::ULe, 0};
        case Op::V_CmpGtU64:
            return CompareSpec{CompareKind::U64, IL::CompareOp::UGt, 0};
        case Op::V_CmpNeU64:
            return CompareSpec{CompareKind::U64, IL::CompareOp::UNe, 0};
        case Op::V_CmpGeU64:
            return CompareSpec{CompareKind::U64, IL::CompareOp::UGe, 0};
        case Op::V_CmpFalseU64:
            return CompareSpec{CompareKind::U64, IL::CompareOp::UEq, 1};
        case Op::V_CmpTrueU64:
            return CompareSpec{CompareKind::U64, IL::CompareOp::UEq, 2};
    default:
        return std::nullopt;
    }
}

std::optional<std::uint32_t> BufferIndex(const IsaState& state,
                                         const DecodedInstruction& instruction,
                                         std::uint8_t addressIndex)
{
    const auto base = ReadOperand(state, instruction.operands[addressIndex]);
    if (!base)
        return std::nullopt;
    const std::uint32_t offset = instruction.operands[2].value;
    return (*base + offset) >> 2;
}

} // namespace

namespace {

#include "GfxMemoryTables.inc"

[[nodiscard]] const Operand* MemoryOperand(const DecodedInstruction& instruction, OperandKind kind)
{
    return FindOperand(instruction, kind);
}

[[nodiscard]] Result<void> ExecuteMemory(IsaState& state, const DecodedInstruction& instruction,
                                         std::uint32_t pc)
{
    const MemOpInfo* info = nullptr;
    for (const auto& entry : kMemoryOps)
    {
        if (entry.first == instruction.op)
        {
            info = &entry.second;
            break;
        }
    }
    if (info == nullptr)
        return std::unexpected(Error("SR-GFX-ISA-220",
                                     "unsupported memory instruction: " +
                                         std::string(ToString(instruction.op)),
                                     pc));

    switch (info->kind)
    {
    case MemKind::Inv:
    case MemKind::Wb:
    case MemKind::Wbinv:
        return {};
    default:
        break;
    }

    const Operand* address = MemoryOperand(instruction, OperandKind::GlobalVaddr);
    if (address == nullptr)
        return std::unexpected(Error("SR-GFX-ISA-221", "memory instruction has no address", pc));
    const Operand* offset = MemoryOperand(instruction, OperandKind::GlobalIoffset);
    std::uint32_t byteBase = state.vgpr[address->value];
    if (instruction.bufferOffen)
    {
        const Operand* soffset = MemoryOperand(instruction, OperandKind::Sgpr);
        if (soffset != nullptr)
            byteBase = static_cast<std::uint32_t>(byteBase + state.sgpr[soffset->value]);
    }
    const std::uint32_t byteAddress =
        static_cast<std::uint32_t>(byteBase + (offset ? offset->value : 0u));
    const std::uint32_t index = byteAddress >> 2;
    const std::uint32_t lane = byteAddress & 3u;

    const auto readDword = [&](std::uint32_t at) {
        const auto it = state.buffer.find(at);
        return it == state.buffer.end() ? 0u : it->second;
    };
    const auto writeDword = [&](std::uint32_t at, std::uint32_t value) { state.buffer[at] = value; };

    const auto destination = [&]() -> std::uint32_t {
        const Operand* dst = MemoryOperand(instruction, OperandKind::GlobalVdst);
        return dst == nullptr ? 0u : dst->value;
    };
    const auto sourceBase = [&]() -> std::optional<std::uint32_t> {
        const Operand* src = MemoryOperand(instruction, OperandKind::GlobalVsrc);
        if (src == nullptr)
            return std::nullopt;
        return src->value;
    };

    switch (info->kind)
    {
    case MemKind::LoadU8:
    case MemKind::LoadI8:
    case MemKind::LoadU16:
    case MemKind::LoadI16:
    {
        const std::uint32_t fieldMask = info->bytes == 1 ? 0xFFu : 0xFFFFu;
        const std::uint32_t field = (readDword(index) >> (lane * 8u)) & fieldMask;
        std::uint32_t value = field;
        if (info->kind == MemKind::LoadI8)
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(field)));
        else if (info->kind == MemKind::LoadI16)
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(field)));
        state.vgpr[destination()] = value;
        return {};
    }
    case MemKind::LoadD16U8:
    case MemKind::LoadD16I8:
    case MemKind::LoadD16B16:
    case MemKind::LoadD16HiU8:
    case MemKind::LoadD16HiI8:
    case MemKind::LoadD16HiB16:
    {
        const bool singleByte = info->kind == MemKind::LoadD16U8 || info->kind == MemKind::LoadD16I8 ||
                                info->kind == MemKind::LoadD16HiU8 ||
                                info->kind == MemKind::LoadD16HiI8;
        const std::uint32_t fieldMask = singleByte ? 0xFFu : 0xFFFFu;
        const std::uint32_t field = (readDword(index) >> (lane * 8u)) & fieldMask;
        std::uint32_t value = field;
        if (info->kind == MemKind::LoadD16I8 || info->kind == MemKind::LoadD16HiI8)
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(field)));
        const bool high = info->kind == MemKind::LoadD16HiU8 || info->kind == MemKind::LoadD16HiI8 ||
                          info->kind == MemKind::LoadD16HiB16;
        const std::uint32_t current = state.vgpr[destination()];
        state.vgpr[destination()] = high ? ((current & 0xFFFFu) | ((value & fieldMask) << 16))
                                         : ((current & 0xFFFF0000u) | (value & fieldMask));
        return {};
    }
    case MemKind::StoreB8:
    case MemKind::StoreB16:
    case MemKind::StoreD16HiB8:
    case MemKind::StoreD16HiB16:
    {
        const auto source = sourceBase();
        if (!source)
            return std::unexpected(Error("SR-GFX-ISA-222", "memory store has no data", pc));
        const bool singleByte = info->kind == MemKind::StoreB8 || info->kind == MemKind::StoreD16HiB8;
        const std::uint32_t fieldMask = singleByte ? 0xFFu : 0xFFFFu;
        const bool highHalf = info->kind == MemKind::StoreD16HiB8 || info->kind == MemKind::StoreD16HiB16;
        const std::uint32_t data = highHalf ? (state.vgpr[*source] >> 16) : state.vgpr[*source];
        const std::uint32_t mask = fieldMask << (lane * 8u);
        writeDword(index, (readDword(index) & ~mask) | ((data << (lane * 8u)) & mask));
        return {};
    }
    default:
        break;
    }

    if (info->kind == MemKind::LoadB32 || info->kind == MemKind::LoadB64 ||
        info->kind == MemKind::LoadB96 || info->kind == MemKind::LoadB128)
    {
        const std::uint32_t count = info->bytes / 4;
        for (std::uint32_t i = 0; i < count; ++i)
            state.vgpr[destination() + i] = readDword(index + i);
        return {};
    }
    if (info->kind == MemKind::StoreB32 || info->kind == MemKind::StoreB64 ||
        info->kind == MemKind::StoreB96 || info->kind == MemKind::StoreB128)
    {
        const auto source = sourceBase();
        if (!source)
            return std::unexpected(Error("SR-GFX-ISA-223", "memory store has no data", pc));
        const std::uint32_t count = info->bytes / 4;
        for (std::uint32_t i = 0; i < count; ++i)
            writeDword(index + i, state.vgpr[*source + i]);
        return {};
    }

    const bool wide = info->bytes == 8;
    const auto readWide = [&](std::uint32_t at) -> std::uint64_t {
        const std::uint64_t low = readDword(at);
        return wide ? (low | (static_cast<std::uint64_t>(readDword(at + 1)) << 32)) : low;
    };

    const std::uint32_t firstIndex = sourceBase().value_or(0u);
    const std::uint64_t first =
        wide ? (static_cast<std::uint64_t>(state.vgpr[firstIndex]) |
                (static_cast<std::uint64_t>(state.vgpr[firstIndex + 1]) << 32))
             : static_cast<std::uint64_t>(state.vgpr[firstIndex]);
    const std::uint64_t old = readWide(index);
    std::uint64_t result = old;
    std::uint64_t replacement = 0;
    if (info->kind == MemKind::AtomicCmpswapB32 || info->kind == MemKind::AtomicCmpswapB64)
    {
        const std::uint32_t at = firstIndex + (wide ? 2u : 1u);
        replacement = wide ? (static_cast<std::uint64_t>(state.vgpr[at]) |
                              (static_cast<std::uint64_t>(state.vgpr[at + 1]) << 32))
                           : static_cast<std::uint64_t>(state.vgpr[at]);
    }

    switch (info->kind)
    {
    case MemKind::AtomicSwapB32:
    case MemKind::AtomicSwapB64:
        result = first;
        break;
    case MemKind::AtomicCmpswapB32:
    case MemKind::AtomicCmpswapB64:
        result = old == first ? replacement : old;
        break;
    case MemKind::AtomicAddU32:
    case MemKind::AtomicAddU64:
    case MemKind::AtomicOrderedAddB64:
        result = old + first;
        break;
    case MemKind::AtomicSubU32:
    case MemKind::AtomicSubU64:
        result = old - first;
        break;
    case MemKind::AtomicSubClampU32:
        result = old >= first ? 0u : first - old;
        break;
    case MemKind::AtomicMinI32:
    case MemKind::AtomicMinI64:
        result = wide ? static_cast<std::uint64_t>(std::min(static_cast<std::int64_t>(old),
                                                            static_cast<std::int64_t>(first)))
                      : static_cast<std::uint64_t>(std::min(static_cast<std::int32_t>(old),
                                                            static_cast<std::int32_t>(first)));
        break;
    case MemKind::AtomicMaxI32:
    case MemKind::AtomicMaxI64:
        result = wide ? static_cast<std::uint64_t>(std::max(static_cast<std::int64_t>(old),
                                                            static_cast<std::int64_t>(first)))
                      : static_cast<std::uint64_t>(std::max(static_cast<std::int32_t>(old),
                                                            static_cast<std::int32_t>(first)));
        break;
    case MemKind::AtomicMinU32:
    case MemKind::AtomicMinU64:
        result = std::min(old, first);
        break;
    case MemKind::AtomicMaxU32:
    case MemKind::AtomicMaxU64:
        result = std::max(old, first);
        break;
    case MemKind::AtomicAndB32:
    case MemKind::AtomicAndB64:
        result = old & first;
        break;
    case MemKind::AtomicOrB32:
    case MemKind::AtomicOrB64:
        result = old | first;
        break;
    case MemKind::AtomicXorB32:
    case MemKind::AtomicXorB64:
        result = old ^ first;
        break;
    case MemKind::AtomicIncU32:
    case MemKind::AtomicIncU64:
        result = old >= first ? 0 : old + 1;
        break;
    case MemKind::AtomicDecU32:
    case MemKind::AtomicDecU64:
        result = old == 0 ? first : old - 1;
        break;
    case MemKind::AtomicCondSubU32:
        result = old >= first ? old - first : old;
        break;
    case MemKind::AtomicMinNumF32:
        result = static_cast<std::uint32_t>(
            std::bit_cast<std::uint32_t>(std::fmin(std::bit_cast<float>(static_cast<std::uint32_t>(old)),
                                                  std::bit_cast<float>(static_cast<std::uint32_t>(first)))));
        break;
    case MemKind::AtomicMaxNumF32:
        result = static_cast<std::uint32_t>(
            std::bit_cast<std::uint32_t>(std::fmax(std::bit_cast<float>(static_cast<std::uint32_t>(old)),
                                                  std::bit_cast<float>(static_cast<std::uint32_t>(first)))));
        break;
    case MemKind::AtomicAddF32:
        result = static_cast<std::uint32_t>(
            std::bit_cast<std::uint32_t>(std::bit_cast<float>(static_cast<std::uint32_t>(old)) +
                                         std::bit_cast<float>(static_cast<std::uint32_t>(first))));
        break;
    default:
        return std::unexpected(Error("SR-GFX-ISA-224",
                                     "unsupported memory instruction: " +
                                         std::string(ToString(instruction.op)),
                                     pc));
    }

    writeDword(index, static_cast<std::uint32_t>(result));
    if (wide)
        writeDword(index + 1, static_cast<std::uint32_t>(result >> 32));
    if (wide)
    {
        state.vgpr[destination()] = static_cast<std::uint32_t>(old);
        state.vgpr[destination() + 1] = static_cast<std::uint32_t>(old >> 32);
    }
    else
    {
        state.vgpr[destination()] = static_cast<std::uint32_t>(old);
    }
    return {};
}

} // namespace

namespace {

[[nodiscard]] std::optional<std::uint32_t> DsOperandValue(const DecodedInstruction& instruction,
                                                           OperandKind kind)
{
    const Operand* found = FindOperand(instruction, kind);
    if (found == nullptr)
        return std::nullopt;
    return found->value;
}

[[nodiscard]] std::optional<std::uint32_t> DsReadDword(const IsaState& state, std::uint32_t index)
{
    const auto it = state.local.find(index);
    return it == state.local.end() ? 0u : it->second;
}

[[nodiscard]] std::optional<std::uint64_t> DsReadQword(const IsaState& state, std::uint32_t index)
{
    const auto low = DsReadDword(state, index);
    const auto high = DsReadDword(state, index + 1);
    if (!low || !high)
        return std::nullopt;
    return static_cast<std::uint64_t>(*low) | (static_cast<std::uint64_t>(*high) << 32);
}

[[nodiscard]] Result<void> ExecuteDs(IsaState& state, const DecodedInstruction& instruction,
                                     std::uint32_t pc)
{
    const auto address = DsOperandValue(instruction, OperandKind::DsAddr);
    if (!address)
        return std::unexpected(Error("SR-GFX-ISA-200", "DS instruction has no address", pc));

    const std::uint32_t base = static_cast<std::uint32_t>(state.vgpr[*address] + instruction.dsOffset0);
    const std::uint32_t index = base >> 2;
    const std::uint32_t lane = base & 3u;

    const auto data = [&](OperandKind kind) -> std::optional<std::uint32_t> {
        const auto found = DsOperandValue(instruction, kind);
        if (!found)
            return std::nullopt;
        return state.vgpr[*found];
    };

    const auto writeVgpr = [&](std::uint32_t registerIndex, std::uint32_t value) {
        state.vgpr[registerIndex] = value;
    };

    switch (instruction.op)
    {
    case Op::DsNop:
        return {};

    case Op::DsLoadB32:
        writeVgpr(*DsOperandValue(instruction, OperandKind::DsDest), *DsReadDword(state, index));
        return {};
    case Op::DsLoadB64:
    {
        const std::uint32_t destination = *DsOperandValue(instruction, OperandKind::DsDest);
        const auto value = DsReadQword(state, index);
        if (!value)
            return std::unexpected(Error("SR-GFX-ISA-201", "invalid ds_load_b64", pc));
        writeVgpr(destination, static_cast<std::uint32_t>(*value));
        writeVgpr(destination + 1, static_cast<std::uint32_t>(*value >> 32));
        return {};
    }
    case Op::DsStoreB64:
    {
        const auto sourceIndex = DsOperandValue(instruction, OperandKind::DsData0);
        if (!sourceIndex)
            return std::unexpected(Error("SR-GFX-ISA-210", "invalid ds_store_b64 operand", pc));
        state.local[index] = state.vgpr[*sourceIndex];
        state.local[index + 1] = state.vgpr[*sourceIndex + 1];
        return {};
    }
    case Op::DsStore2addrB32:
    case Op::DsStore2addrStride64B32:
    case Op::DsStore2addrB64:
    case Op::DsStore2addrStride64B64:
    {
        const bool wide = (instruction.op == Op::DsStore2addrB64 ||
                           instruction.op == Op::DsStore2addrStride64B64);
        const bool strided = (instruction.op == Op::DsStore2addrStride64B32 ||
                              instruction.op == Op::DsStore2addrStride64B64);
        const std::uint32_t firstBase =
            static_cast<std::uint32_t>(state.vgpr[*address] +
                                       (strided ? instruction.dsOffset0 * 256u : instruction.dsOffset0));
        const std::uint32_t secondBase =
            static_cast<std::uint32_t>(state.vgpr[*address] +
                                       (strided ? instruction.dsOffset1 * 256u : instruction.dsOffset1));
        const auto firstIndex = DsOperandValue(instruction, OperandKind::DsData0);
        const auto secondIndex = DsOperandValue(instruction, OperandKind::DsData1);
        if (!firstIndex || !secondIndex)
            return std::unexpected(Error("SR-GFX-ISA-211", "invalid DS two-address store", pc));
        state.local[firstBase >> 2] = state.vgpr[*firstIndex];
        state.local[secondBase >> 2] = state.vgpr[*secondIndex];
        if (wide)
        {
            state.local[(firstBase >> 2) + 1] = state.vgpr[*firstIndex + 1];
            state.local[(secondBase >> 2) + 1] = state.vgpr[*secondIndex + 1];
        }
        return {};
    }
    case Op::DsLoad2addrB32:
    case Op::DsLoad2addrStride64B32:
    case Op::DsLoad2addrB64:
    case Op::DsLoad2addrStride64B64:
    {
        const bool wide = (instruction.op == Op::DsLoad2addrB64 ||
                           instruction.op == Op::DsLoad2addrStride64B64);
        const bool strided = (instruction.op == Op::DsLoad2addrStride64B32 ||
                              instruction.op == Op::DsLoad2addrStride64B64);
        const std::uint32_t firstBase =
            static_cast<std::uint32_t>(state.vgpr[*address] +
                                       (strided ? instruction.dsOffset0 * 256u : instruction.dsOffset0));
        const std::uint32_t secondBase =
            static_cast<std::uint32_t>(state.vgpr[*address] +
                                       (strided ? instruction.dsOffset1 * 256u : instruction.dsOffset1));
        const auto destination = DsOperandValue(instruction, OperandKind::DsDest);
        if (!destination)
            return std::unexpected(Error("SR-GFX-ISA-212", "invalid DS two-address load", pc));
        writeVgpr(*destination, *DsReadDword(state, firstBase >> 2));
        writeVgpr(*destination + (wide ? 2u : 1u), *DsReadDword(state, secondBase >> 2));
        if (wide)
        {
            writeVgpr(*destination + 1, *DsReadDword(state, (firstBase >> 2) + 1));
            writeVgpr(*destination + 3, *DsReadDword(state, (secondBase >> 2) + 1));
        }
        return {};
    }
    case Op::DsStoreB32:
    case Op::DsAddU32:
    case Op::DsSubU32:
    case Op::DsRsubU32:
    case Op::DsIncU32:
    case Op::DsDecU32:
    case Op::DsMinI32:
    case Op::DsMaxI32:
    case Op::DsMinU32:
    case Op::DsMaxU32:
    case Op::DsAndB32:
    case Op::DsOrB32:
    case Op::DsXorB32:
    case Op::DsMskorB32:
    case Op::DsCmpstoreB32:
    case Op::DsAddF32:
    case Op::DsMinNumF32:
    case Op::DsMaxNumF32:
    case Op::DsCondSubU32:
    case Op::DsSubClampU32:
    case Op::DsAddRtnU32:
    case Op::DsSubRtnU32:
    case Op::DsRsubRtnU32:
    case Op::DsIncRtnU32:
    case Op::DsDecRtnU32:
    case Op::DsMinRtnI32:
    case Op::DsMaxRtnI32:
    case Op::DsMinRtnU32:
    case Op::DsMaxRtnU32:
    case Op::DsAndRtnB32:
    case Op::DsOrRtnB32:
    case Op::DsXorRtnB32:
    case Op::DsMskorRtnB32:
    case Op::DsStorexchgRtnB32:
    case Op::DsCmpstoreRtnB32:
    case Op::DsAddRtnF32:
    case Op::DsMinNumRtnF32:
    case Op::DsMaxNumRtnF32:
    case Op::DsCondSubRtnU32:
    case Op::DsSubClampRtnU32:
    case Op::DsStorexchg2addrRtnB32:
    case Op::DsStorexchg2addrStride64RtnB32:
    {
        const auto first = data(OperandKind::DsData0);
        if (!first)
            return std::unexpected(Error("SR-GFX-ISA-202", "invalid DS data operand", pc));
        const std::uint32_t old = *DsReadDword(state, index);
        std::uint32_t result = old;

        const bool rtn = instruction.op == Op::DsAddRtnU32 || instruction.op == Op::DsSubRtnU32 ||
                         instruction.op == Op::DsRsubRtnU32 || instruction.op == Op::DsIncRtnU32 ||
                         instruction.op == Op::DsDecRtnU32 || instruction.op == Op::DsMinRtnI32 ||
                         instruction.op == Op::DsMaxRtnI32 || instruction.op == Op::DsMinRtnU32 ||
                         instruction.op == Op::DsMaxRtnU32 || instruction.op == Op::DsAndRtnB32 ||
                         instruction.op == Op::DsOrRtnB32 || instruction.op == Op::DsXorRtnB32 ||
                         instruction.op == Op::DsMskorRtnB32 ||
                         instruction.op == Op::DsStorexchgRtnB32 ||
                         instruction.op == Op::DsStorexchg2addrRtnB32 ||
                         instruction.op == Op::DsStorexchg2addrStride64RtnB32 ||
                         instruction.op == Op::DsCmpstoreRtnB32 || instruction.op == Op::DsAddRtnF32 ||
                         instruction.op == Op::DsMinNumRtnF32 || instruction.op == Op::DsMaxNumRtnF32 ||
                         instruction.op == Op::DsCondSubRtnU32 ||
                         instruction.op == Op::DsSubClampRtnU32;

        switch (instruction.op)
        {
        case Op::DsStoreB32:
            result = *first;
            break;
        case Op::DsAddU32:
        case Op::DsAddRtnU32:
            result = old + *first;
            break;
        case Op::DsSubU32:
        case Op::DsSubRtnU32:
            result = old - *first;
            break;
        case Op::DsRsubU32:
        case Op::DsRsubRtnU32:
            result = *first - old;
            break;
        case Op::DsIncU32:
        case Op::DsIncRtnU32:
            result = old >= *first ? 0u : old + 1u;
            break;
        case Op::DsDecU32:
        case Op::DsDecRtnU32:
            result = old == 0u ? *first : old - 1u;
            break;
        case Op::DsMinI32:
        case Op::DsMinRtnI32:
            result = static_cast<std::uint32_t>(
                std::min(static_cast<std::int32_t>(old), static_cast<std::int32_t>(*first)));
            break;
        case Op::DsMaxI32:
        case Op::DsMaxRtnI32:
            result = static_cast<std::uint32_t>(
                std::max(static_cast<std::int32_t>(old), static_cast<std::int32_t>(*first)));
            break;
        case Op::DsMinU32:
        case Op::DsMinRtnU32:
            result = std::min(old, *first);
            break;
        case Op::DsMaxU32:
        case Op::DsMaxRtnU32:
            result = std::max(old, *first);
            break;
        case Op::DsAndB32:
        case Op::DsAndRtnB32:
            result = old & *first;
            break;
        case Op::DsOrB32:
        case Op::DsOrRtnB32:
            result = old | *first;
            break;
        case Op::DsXorB32:
        case Op::DsXorRtnB32:
            result = old ^ *first;
            break;
        case Op::DsMskorB32:
        case Op::DsMskorRtnB32:
        case Op::DsCmpstoreB32:
        case Op::DsCmpstoreRtnB32:
        {
            const auto second = data(OperandKind::DsData1);
            if (!second)
                return std::unexpected(Error("SR-GFX-ISA-203", "invalid DS second data operand", pc));
            if (instruction.op == Op::DsCmpstoreB32 || instruction.op == Op::DsCmpstoreRtnB32)
                result = old == *first ? *second : old;
            else
                result = (old & *first) | *second;
            break;
        }
        case Op::DsStorexchgRtnB32:
        case Op::DsStorexchg2addrRtnB32:
        case Op::DsStorexchg2addrStride64RtnB32:
            result = *first;
            break;
        case Op::DsAddF32:
        case Op::DsAddRtnF32:
            result = std::bit_cast<std::uint32_t>(std::bit_cast<float>(old) + std::bit_cast<float>(*first));
            break;
        case Op::DsMinNumF32:
        case Op::DsMinNumRtnF32:
            result = std::bit_cast<std::uint32_t>(
                std::fmin(std::bit_cast<float>(old), std::bit_cast<float>(*first)));
            break;
        case Op::DsMaxNumF32:
        case Op::DsMaxNumRtnF32:
            result = std::bit_cast<std::uint32_t>(
                std::fmax(std::bit_cast<float>(old), std::bit_cast<float>(*first)));
            break;
        case Op::DsCondSubU32:
        case Op::DsCondSubRtnU32:
            result = old >= *first ? old - *first : old;
            break;
        case Op::DsSubClampU32:
        case Op::DsSubClampRtnU32:
            result = old >= *first ? 0u : *first - old;
            break;
        default:
            break;
        }

        state.local[index] = result;
        if (rtn)
            writeVgpr(*DsOperandValue(instruction, OperandKind::DsDest), old);
        return {};
    }

    case Op::DsLoadB96:
    case Op::DsLoadB128:
    case Op::DsStoreB96:
    case Op::DsStoreB128:
    {
        const std::uint32_t count =
            (instruction.op == Op::DsLoadB96 || instruction.op == Op::DsStoreB96) ? 3u : 4u;
        const bool store = instruction.op == Op::DsStoreB96 || instruction.op == Op::DsStoreB128;
        if (store)
        {
            const auto base = DsOperandValue(instruction, OperandKind::DsData0);
            if (!base)
                return std::unexpected(Error("SR-GFX-ISA-206", "invalid DS block store operand", pc));
            for (std::uint32_t i = 0; i < count; ++i)
                state.local[index + i] = state.vgpr[*base + i];
            return {};
        }
        const auto base = DsOperandValue(instruction, OperandKind::DsDest);
        if (!base)
            return std::unexpected(Error("SR-GFX-ISA-207", "invalid DS block load operand", pc));
        for (std::uint32_t i = 0; i < count; ++i)
            writeVgpr(*base + i, *DsReadDword(state, index + i));
        return {};
    }

    case Op::DsAddU64:
    case Op::DsSubU64:
    case Op::DsRsubU64:
    case Op::DsIncU64:
    case Op::DsDecU64:
    case Op::DsMinI64:
    case Op::DsMaxI64:
    case Op::DsMinU64:
    case Op::DsMaxU64:
    case Op::DsAndB64:
    case Op::DsOrB64:
    case Op::DsXorB64:
    case Op::DsMskorB64:
    case Op::DsCmpstoreB64:
    case Op::DsStorexchgRtnB64:
    case Op::DsStorexchg2addrRtnB64:
    case Op::DsStorexchg2addrStride64RtnB64:
    case Op::DsMinNumF64:
    case Op::DsMaxNumF64:
    case Op::DsAddRtnU64:
    case Op::DsSubRtnU64:
    case Op::DsRsubRtnU64:
    case Op::DsIncRtnU64:
    case Op::DsDecRtnU64:
    case Op::DsMinRtnI64:
    case Op::DsMaxRtnI64:
    case Op::DsMinRtnU64:
    case Op::DsMaxRtnU64:
    case Op::DsAndRtnB64:
    case Op::DsOrRtnB64:
    case Op::DsXorRtnB64:
    case Op::DsMskorRtnB64:
    case Op::DsCmpstoreRtnB64:
    case Op::DsMinNumRtnF64:
    case Op::DsMaxNumRtnF64:
    {
        const auto firstIndex = DsOperandValue(instruction, OperandKind::DsData0);
        if (!firstIndex)
            return std::unexpected(Error("SR-GFX-ISA-208", "invalid DS 64-bit data operand", pc));
        const std::uint64_t first = static_cast<std::uint64_t>(state.vgpr[*firstIndex]) |
                                    (static_cast<std::uint64_t>(state.vgpr[*firstIndex + 1]) << 32);
        const std::uint64_t old = DsReadQword(state, index).value_or(0);
        std::uint64_t result = old;

        const bool rtn = instruction.op == Op::DsAddRtnU64 || instruction.op == Op::DsSubRtnU64 ||
                         instruction.op == Op::DsRsubRtnU64 || instruction.op == Op::DsIncRtnU64 ||
                         instruction.op == Op::DsDecRtnU64 || instruction.op == Op::DsMinRtnI64 ||
                         instruction.op == Op::DsMaxRtnI64 || instruction.op == Op::DsMinRtnU64 ||
                         instruction.op == Op::DsMaxRtnU64 || instruction.op == Op::DsAndRtnB64 ||
                         instruction.op == Op::DsOrRtnB64 || instruction.op == Op::DsXorRtnB64 ||
                         instruction.op == Op::DsMskorRtnB64 ||
                         instruction.op == Op::DsCmpstoreRtnB64 ||
                         instruction.op == Op::DsStorexchgRtnB64 ||
                         instruction.op == Op::DsStorexchg2addrRtnB64 ||
                         instruction.op == Op::DsStorexchg2addrStride64RtnB64 ||
                         instruction.op == Op::DsMinNumRtnF64 || instruction.op == Op::DsMaxNumRtnF64;

        switch (instruction.op)
        {
        case Op::DsAddU64:
        case Op::DsAddRtnU64:
            result = old + first;
            break;
        case Op::DsSubU64:
        case Op::DsSubRtnU64:
            result = old - first;
            break;
        case Op::DsRsubU64:
        case Op::DsRsubRtnU64:
            result = first - old;
            break;
        case Op::DsIncU64:
        case Op::DsIncRtnU64:
            result = old >= first ? 0 : old + 1;
            break;
        case Op::DsDecU64:
        case Op::DsDecRtnU64:
            result = old == 0 ? first : old - 1;
            break;
        case Op::DsMinI64:
        case Op::DsMinRtnI64:
            result = static_cast<std::uint64_t>(
                std::min(static_cast<std::int64_t>(old), static_cast<std::int64_t>(first)));
            break;
        case Op::DsMaxI64:
        case Op::DsMaxRtnI64:
            result = static_cast<std::uint64_t>(
                std::max(static_cast<std::int64_t>(old), static_cast<std::int64_t>(first)));
            break;
        case Op::DsMinU64:
        case Op::DsMinRtnU64:
            result = std::min(old, first);
            break;
        case Op::DsMaxU64:
        case Op::DsMaxRtnU64:
            result = std::max(old, first);
            break;
        case Op::DsAndB64:
        case Op::DsAndRtnB64:
            result = old & first;
            break;
        case Op::DsOrB64:
        case Op::DsOrRtnB64:
            result = old | first;
            break;
        case Op::DsXorB64:
        case Op::DsXorRtnB64:
            result = old ^ first;
            break;
        case Op::DsMskorB64:
        case Op::DsMskorRtnB64:
        case Op::DsCmpstoreB64:
        case Op::DsCmpstoreRtnB64:
        {
            const auto secondIndex = DsOperandValue(instruction, OperandKind::DsData1);
            if (!secondIndex)
                return std::unexpected(Error("SR-GFX-ISA-209", "invalid DS 64-bit second operand", pc));
            const std::uint64_t second =
                static_cast<std::uint64_t>(state.vgpr[*secondIndex]) |
                (static_cast<std::uint64_t>(state.vgpr[*secondIndex + 1]) << 32);
            if (instruction.op == Op::DsCmpstoreB64 || instruction.op == Op::DsCmpstoreRtnB64)
                result = old == first ? second : old;
            else
                result = (old & first) | second;
            break;
        }
        case Op::DsStorexchgRtnB64:
        case Op::DsStorexchg2addrRtnB64:
        case Op::DsStorexchg2addrStride64RtnB64:
            result = first;
            break;
        case Op::DsMinNumF64:
        case Op::DsMinNumRtnF64:
            result = std::bit_cast<std::uint64_t>(
                std::fmin(std::bit_cast<double>(old), std::bit_cast<double>(first)));
            break;
        case Op::DsMaxNumF64:
        case Op::DsMaxNumRtnF64:
            result = std::bit_cast<std::uint64_t>(
                std::fmax(std::bit_cast<double>(old), std::bit_cast<double>(first)));
            break;
        default:
            break;
        }

        state.local[index] = static_cast<std::uint32_t>(result);
        state.local[index + 1] = static_cast<std::uint32_t>(result >> 32);
        if (rtn)
        {
            const std::uint32_t destination = *DsOperandValue(instruction, OperandKind::DsDest);
            writeVgpr(destination, static_cast<std::uint32_t>(old));
            writeVgpr(destination + 1, static_cast<std::uint32_t>(old >> 32));
        }
        return {};
    }

    case Op::DsLoadU8:
    case Op::DsLoadI8:
    case Op::DsLoadU16:
    case Op::DsLoadI16:
    {
        const std::uint32_t word = *DsReadDword(state, index);
        const std::uint32_t width = (instruction.op == Op::DsLoadU8 || instruction.op == Op::DsLoadI8) ? 8 : 16;
        const std::uint32_t field =
            (word >> (lane * 8u)) & (width == 8 ? 0xFFu : 0xFFFFu);
        std::uint32_t value = field;
        if (instruction.op == Op::DsLoadI8)
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(field)));
        else if (instruction.op == Op::DsLoadI16)
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(field)));
        writeVgpr(*DsOperandValue(instruction, OperandKind::DsDest), value);
        return {};
    }
    case Op::DsStoreB8:
    case Op::DsStoreB16:
    {
        const auto value = data(OperandKind::DsData0);
        if (!value)
            return std::unexpected(Error("SR-GFX-ISA-204", "invalid DS byte store operand", pc));
        const std::uint32_t width = instruction.op == Op::DsStoreB8 ? 8u : 16u;
        const std::uint32_t mask = (width == 8 ? 0xFFu : 0xFFFFu) << (lane * 8u);
        const std::uint32_t word = *DsReadDword(state, index);
        state.local[index] = (word & ~mask) | ((*value << (lane * 8u)) & mask);
        return {};
    }

    default:
        break;
    }

    return std::unexpected(Error("SR-GFX-ISA-205",
                                 "unsupported data-share instruction: " +
                                     std::string(ToString(instruction.op)),
                                 pc));
}

} // namespace

[[nodiscard]] Result<void> ExecuteScalarMemory(IsaState& state, const DecodedInstruction& instruction,
                                                std::uint32_t pc)
{
    switch (instruction.op)
    {
    case Op::S_DcacheInv:
    case Op::S_Gl1Inv:
    case Op::S_AtcProbe:
    case Op::S_AtcProbeBuffer:
    case Op::S_PrefetchInst:
    case Op::S_PrefetchInstPcRel:
    case Op::S_PrefetchData:
    case Op::S_BufferPrefetchData:
    case Op::S_PrefetchDataPcRel:
        return {};
    default:
        break;
    }

    if (instruction.operandCount < 2)
        return std::unexpected(Error("SR-GFX-ISA-230", "scalar load has no base register", pc));

    const std::uint32_t base = state.sgpr[instruction.operands[1].value];
    const std::uint32_t immediate =
        instruction.operandCount >= 3 ? instruction.operands[2].value : 0u;
    const std::uint32_t byteAddress = static_cast<std::uint32_t>(base + immediate);
    const std::uint32_t index = byteAddress >> 2;
    const std::uint32_t lane = byteAddress & 3u;

    const auto readDword = [&](std::uint32_t at) {
        const auto it = state.buffer.find(at);
        return it == state.buffer.end() ? 0u : it->second;
    };

    const std::uint32_t destination = instruction.operands[0].value;

    const auto singleByte = [&](void) {
        return instruction.op == Op::S_LoadI8 || instruction.op == Op::S_LoadU8 ||
               instruction.op == Op::S_BufferLoadI8 || instruction.op == Op::S_BufferLoadU8;
    };
    const auto singleHalf = [&](void) {
        return instruction.op == Op::S_LoadI16 || instruction.op == Op::S_LoadU16 ||
               instruction.op == Op::S_BufferLoadI16 || instruction.op == Op::S_BufferLoadU16;
    };

    if (singleByte() || singleHalf())
    {
        const std::uint32_t fieldMask = singleByte() ? 0xFFu : 0xFFFFu;
        const std::uint32_t field = (readDword(index) >> (lane * 8u)) & fieldMask;
        std::uint32_t value = field;
        if (instruction.op == Op::S_LoadI8 || instruction.op == Op::S_BufferLoadI8)
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(field)));
        else if (instruction.op == Op::S_LoadI16 || instruction.op == Op::S_BufferLoadI16)
            value = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(field)));
        state.sgpr[destination] = value;
        return {};
    }

    std::uint32_t bytes = 0;
    switch (instruction.op)
    {
    case Op::S_LoadB32:
    case Op::S_BufferLoadB32:
        bytes = 4;
        break;
    case Op::S_LoadB64:
    case Op::S_BufferLoadB64:
        bytes = 8;
        break;
    case Op::S_LoadB96:
    case Op::S_BufferLoadB96:
        bytes = 12;
        break;
    case Op::S_LoadB128:
    case Op::S_BufferLoadB128:
        bytes = 16;
        break;
    case Op::S_LoadB256:
    case Op::S_BufferLoadB256:
        bytes = 32;
        break;
    case Op::S_LoadB512:
    case Op::S_BufferLoadB512:
        bytes = 64;
        break;
    default:
        return std::unexpected(Error("SR-GFX-ISA-231", "unsupported scalar memory instruction", pc));
    }

    const std::uint32_t count = bytes / 4;
    for (std::uint32_t i = 0; i < count; ++i)
        state.sgpr[destination + i] = readDword(index + i);
    return {};
}

[[nodiscard]] bool CompareScalarFloat(Op op, float a, float b) noexcept
{
    const bool ordered = !std::isnan(a) && !std::isnan(b);
    switch (op)
    {
    case Op::S_CmpLtF32:
    case Op::S_CmpLtF16:
        return ordered && a < b;
    case Op::S_CmpEqF32:
    case Op::S_CmpEqF16:
        return ordered && a == b;
    case Op::S_CmpLeF32:
    case Op::S_CmpLeF16:
        return ordered && a <= b;
    case Op::S_CmpGtF32:
    case Op::S_CmpGtF16:
        return ordered && a > b;
    case Op::S_CmpLgF32:
    case Op::S_CmpLgF16:
        return ordered && a != b;
    case Op::S_CmpGeF32:
    case Op::S_CmpGeF16:
        return ordered && a >= b;
    case Op::S_CmpOF32:
    case Op::S_CmpOF16:
        return ordered;
    case Op::S_CmpUF32:
    case Op::S_CmpUF16:
        return !ordered;
    case Op::S_CmpNgeF32:
    case Op::S_CmpNgeF16:
        return !ordered || a < b;
    case Op::S_CmpNlgF32:
    case Op::S_CmpNlgF16:
        return !ordered || a == b;
    case Op::S_CmpNgtF32:
    case Op::S_CmpNgtF16:
        return !ordered || a <= b;
    case Op::S_CmpNleF32:
    case Op::S_CmpNleF16:
        return !ordered || a > b;
    case Op::S_CmpNeqF32:
    case Op::S_CmpNeqF16:
        return !ordered || a != b;
    case Op::S_CmpNltF32:
    case Op::S_CmpNltF16:
        return !ordered || a >= b;
    default:
        return false;
    }
}

[[nodiscard]] std::uint32_t ClassMaskOf(std::uint64_t bits, std::uint32_t exponentBits,
                                         std::uint32_t mantissaBits) noexcept
{
    const std::uint64_t exponentMax = (std::uint64_t{1} << exponentBits) - 1;
    const std::uint64_t mantissaMask = (std::uint64_t{1} << mantissaBits) - 1;
    const std::uint32_t signShift = exponentBits + mantissaBits;
    const bool negative = ((bits >> signShift) & 1u) != 0;
    const std::uint64_t exponent = (bits >> mantissaBits) & exponentMax;
    const std::uint64_t mantissa = bits & mantissaMask;
    const bool nan = exponent == exponentMax && mantissa != 0;
    const bool quiet = ((mantissa >> (mantissaBits - 1)) & 1u) != 0;
    const bool inf = exponent == exponentMax && mantissa == 0;
    const bool zero = exponent == 0 && mantissa == 0;
    const bool subnormal = exponent == 0 && mantissa != 0;
    const bool normal = exponent != 0 && exponent != exponentMax;

    std::uint32_t mask = 0;
    if (nan && !quiet)
        mask |= 1u << 0;
    if (nan && quiet)
        mask |= 1u << 1;
    if (inf)
        mask |= negative ? (1u << 2) : (1u << 9);
    if (normal)
        mask |= negative ? (1u << 3) : (1u << 8);
    if (subnormal)
        mask |= negative ? (1u << 4) : (1u << 7);
    if (zero)
        mask |= negative ? (1u << 5) : (1u << 6);
    return mask;
}

[[nodiscard]] inline std::int32_t ClampInt(std::int32_t value, std::int32_t low,
                                           std::int32_t high) noexcept
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

[[nodiscard]] inline float ClampFloat(float value, float low, float high) noexcept
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

[[nodiscard]] std::uint32_t RoundToIntegerIsa(float value, std::int32_t low, std::int32_t high) noexcept
{
    const float magnitude = std::fabs(value);
    std::uint32_t truncated = static_cast<std::uint32_t>(magnitude);
    const float fraction = magnitude - static_cast<float>(truncated);
    if (fraction > 0.5f || (fraction == 0.5f && (truncated & 1u) != 0))
        ++truncated;
    const std::int64_t signedValue =
        value < 0.0f ? -static_cast<std::int64_t>(truncated) : static_cast<std::int64_t>(truncated);
    if (signedValue > high)
        return static_cast<std::uint32_t>(high) & 0xFFFFu;
    if (signedValue < low)
        return static_cast<std::uint32_t>(low) & 0xFFFFu;
    return static_cast<std::uint32_t>(signedValue) & 0xFFFFu;
}

Result<IsaExecutionResult> RunIsa(std::span<const std::uint32_t> words,
                                  Arch arch,
                                  IsaState state,
                                  std::uint64_t maxSteps)
{
    if (words.empty())
        return std::unexpected(Error("SR-GFX-ISA-001", "no instruction words", 0));

    IsaExecutionResult result;
    result.state = std::move(state);

    std::uint32_t pc = 0;
    for (std::uint64_t step = 0; step < maxSteps; ++step)
    {
        if (pc % 4 != 0 || pc / 4 >= words.size())
            return std::unexpected(Error("SR-GFX-ISA-002", "pc is outside the code object", pc));

        const auto decoded = Decode(words.subspan(pc / 4), arch, pc);
        if (!decoded)
            return std::unexpected(decoded.error());

        const DecodedInstruction& instruction = *decoded;
        ++result.steps;

        const auto source = [&](std::uint8_t index) { return ReadOperand(result.state, instruction.operands[index]); };
        const auto write = [&](std::uint32_t value) { return WriteOperand(result.state, instruction.operands[0], value); };
        const auto setSccNonZero = [&](std::uint32_t value) { result.state.scc = value != 0; };

        bool advance = true;
        bool finished = false;

        if (instruction.encoding == Encoding::Flat || instruction.encoding == Encoding::Global ||
            instruction.encoding == Encoding::Scratch || instruction.encoding == Encoding::Buffer)
        {
            const auto status = ExecuteMemory(result.state, instruction, pc);
            if (!status)
                return std::unexpected(status.error());
            pc += instruction.wordCount * 4;
            continue;
        }

        if (instruction.encoding == Encoding::Ds)
        {
            const auto status = ExecuteDs(result.state, instruction, pc);
            if (!status)
                return std::unexpected(status.error());
            pc += instruction.wordCount * 4;
            continue;
        }

        if (const auto spec = CompareSpecOf(instruction.op))        {
            const auto condition = EvaluateCompare(result.state, instruction, *spec);
            if (!condition)
                return std::unexpected(Error("SR-GFX-ISA-165", "invalid typed compare operands", pc));
            const Operand* scalarDestination = FindOperand(instruction, OperandKind::Sdst);
            if (instruction.encoding == Encoding::Vop3 && scalarDestination != nullptr)
            {
                result.state.sgpr[scalarDestination->value] = *condition ? 0xFFFFFFFFu : 0u;
            }
            else
            {
                result.state.vcc = *condition;
                if (instruction.execMask)
                    result.state.exec = result.state.exec && *condition;
            }
            pc += instruction.wordCount * 4;
            continue;
        }

        switch (instruction.op)
        {
        case Op::S_MovB32:
        case Op::V_MovB32:
        {
            const auto value = source(1);
            if (!value || !write(*value))
                return std::unexpected(Error("SR-GFX-ISA-003", "invalid move operands", pc));
            break;
        }
        case Op::S_MovB64:
        case Op::S_NotB64:
        case Op::S_AndB64:
        case Op::S_OrB64:
        case Op::S_XorB64:
        case Op::S_CselectB64:
        {
            std::uint64_t value = 0;
            if (instruction.op == Op::S_CselectB64)
            {
                const auto left = ReadPair(result.state, instruction.operands[1]);
                const auto right = ReadPair(result.state, instruction.operands[2]);
                if (!left || !right)
                    return std::unexpected(Error("SR-GFX-ISA-090", "invalid cselect_b64 operands", pc));
                value = result.state.scc ? *left : *right;
            }
            else
            {
                const auto first = ReadPair(result.state, instruction.operands[1]);
                if (!first)
                    return std::unexpected(Error("SR-GFX-ISA-091", "invalid 64-bit scalar operand", pc));
                if (instruction.op == Op::S_MovB64)
                {
                    value = *first;
                }
                else if (instruction.op == Op::S_NotB64)
                {
                    value = ~*first;
                }
                else
                {
                    const auto second = ReadPair(result.state, instruction.operands[2]);
                    if (!second)
                        return std::unexpected(Error("SR-GFX-ISA-092", "invalid 64-bit scalar operand", pc));
                    if (instruction.op == Op::S_AndB64)
                        value = *first & *second;
                    else if (instruction.op == Op::S_OrB64)
                        value = *first | *second;
                    else
                        value = *first ^ *second;
                }
            }
            if (instruction.op != Op::S_MovB64 && instruction.op != Op::S_CselectB64)
                result.state.scc = value != 0;
            if (!WritePair(result.state, instruction.operands[0], value))
                return std::unexpected(Error("SR-GFX-ISA-093", "invalid 64-bit scalar destination", pc));
            break;
        }
        case Op::S_AddU32:
        case Op::S_AddI32:
        case Op::S_AddCoU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-004", "invalid add operands", pc));
            const std::uint32_t value = *left + *right;
            result.state.scc = value < *left;
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-005", "invalid add destination", pc));
            break;
        }
        case Op::S_SubU32:
        case Op::S_SubI32:
        case Op::S_SubCoU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-006", "invalid sub operands", pc));
            result.state.scc = *left < *right;
            if (!write(*left - *right))
                return std::unexpected(Error("SR-GFX-ISA-007", "invalid sub destination", pc));
            break;
        }
        case Op::S_AddcU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-008", "invalid addc operands", pc));
            const std::uint32_t carry = result.state.scc ? 1u : 0u;
            const std::uint32_t partial = *left + *right;
            const std::uint32_t value = partial + carry;
            result.state.scc = (partial < *left) || (value < partial);
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-009", "invalid addc destination", pc));
            break;
        }
        case Op::S_SubbU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-010", "invalid subb operands", pc));
            const std::uint32_t borrow = result.state.scc ? 1u : 0u;
            const std::uint32_t partial = *left - *right;
            const std::uint32_t value = partial - borrow;
            result.state.scc = (*left < *right) || (partial < borrow);
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-011", "invalid subb destination", pc));
            break;
        }
        case Op::S_AddkI32:
        {
            const auto sourceValue = ReadOperand(result.state, instruction.operands[0]);
            if (!sourceValue)
                return std::unexpected(Error("SR-GFX-ISA-012", "invalid addk source", pc));
            const std::uint32_t value = *sourceValue + SignExtend16(instruction.operands[1].value);
            result.state.scc = value < *sourceValue;
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-013", "invalid addk destination", pc));
            break;
        }
        case Op::S_MulkI32:
        {
            const auto sourceValue = ReadOperand(result.state, instruction.operands[0]);
            if (!sourceValue)
                return std::unexpected(Error("SR-GFX-ISA-014", "invalid mulk source", pc));
            if (!write(*sourceValue * SignExtend16(instruction.operands[1].value)))
                return std::unexpected(Error("SR-GFX-ISA-015", "invalid mulk destination", pc));
            break;
        }
        case Op::S_MovkI32:
        {
            if (!write(SignExtend16(instruction.operands[1].value)))
                return std::unexpected(Error("SR-GFX-ISA-016", "invalid movk destination", pc));
            break;
        }
        case Op::S_AndB32:
        case Op::S_OrB32:
        case Op::S_XorB32:
        case Op::S_LshlB32:
        case Op::S_LshrB32:
        case Op::S_AshrI32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-016", "invalid scalar bitwise operands", pc));
            std::uint32_t value = 0;
            switch (instruction.op)
            {
            case Op::S_AndB32:
                value = *left & *right;
                break;
            case Op::S_OrB32:
                value = *left | *right;
                break;
            case Op::S_XorB32:
                value = *left ^ *right;
                break;
            case Op::S_LshlB32:
                value = *left << (*right & 31u);
                break;
            case Op::S_LshrB32:
                value = *left >> (*right & 31u);
                break;
            case Op::S_AshrI32:
                value = static_cast<std::uint32_t>(AsSigned(*left) >> (*right & 31u));
                break;
            default:
                break;
            }
            setSccNonZero(value);
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-017", "invalid scalar bitwise destination", pc));
            break;
        }
        case Op::S_Andn2B32:
        case Op::S_AndNot1B32:
        case Op::S_Orn2B32:
        case Op::S_OrNot1B32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-018", "invalid inverted bitwise operands", pc));
            const bool orMode = instruction.op == Op::S_Orn2B32 || instruction.op == Op::S_OrNot1B32;
            const std::uint32_t value = orMode ? (*left | ~*right) : (*left & ~*right);
            setSccNonZero(value);
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-019", "invalid inverted bitwise destination", pc));
            break;
        }
        case Op::S_NandB32:
        case Op::S_NorB32:
        case Op::S_XnorB32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-020", "invalid inverted binary operands", pc));
            std::uint32_t value = 0;
            if (instruction.op == Op::S_NandB32)
                value = ~(*left & *right);
            else if (instruction.op == Op::S_NorB32)
                value = ~(*left | *right);
            else
                value = ~(*left ^ *right);
            setSccNonZero(value);
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-021", "invalid inverted binary destination", pc));
            break;
        }
        case Op::S_NotB32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-022", "invalid not operand", pc));
            const std::uint32_t resultValue = ~*value;
            setSccNonZero(resultValue);
            if (!write(resultValue))
                return std::unexpected(Error("SR-GFX-ISA-023", "invalid not destination", pc));
            break;
        }
        case Op::S_BrevB32:
        {
            const auto value = source(1);
            if (!value || !write(BitReverse(*value)))
                return std::unexpected(Error("SR-GFX-ISA-024", "invalid brev operands", pc));
            break;
        }
        case Op::S_Bcnt1I32B32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-025", "invalid bcnt operand", pc));
            const std::uint32_t count = static_cast<std::uint32_t>(std::popcount(*value));
            setSccNonZero(count);
            if (!write(count))
                return std::unexpected(Error("SR-GFX-ISA-026", "invalid bcnt destination", pc));
            break;
        }
        case Op::S_Ff1I32B32:
        case Op::S_FlbitI32B32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-027", "invalid bit scan operand", pc));
            std::uint32_t scan = 0xFFFFFFFFu;
            if (*value != 0)
                scan = instruction.op == Op::S_Ff1I32B32 ? static_cast<std::uint32_t>(std::countr_zero(*value))
                                                         : static_cast<std::uint32_t>(std::countl_zero(*value));
            if (!write(scan))
                return std::unexpected(Error("SR-GFX-ISA-028", "invalid bit scan destination", pc));
            break;
        }
        case Op::S_AbsI32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-029", "invalid abs operand", pc));
            const std::uint32_t resultValue = static_cast<std::uint32_t>(
                AsSigned(*value) < 0 ? -static_cast<std::int64_t>(AsSigned(*value)) : AsSigned(*value));
            setSccNonZero(resultValue);
            if (!write(resultValue))
                return std::unexpected(Error("SR-GFX-ISA-030", "invalid abs destination", pc));
            break;
        }
        case Op::S_AbsDiffI32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-076", "invalid absdiff operands", pc));
            const std::int64_t difference =
                static_cast<std::int64_t>(AsSigned(*left)) - static_cast<std::int64_t>(AsSigned(*right));
            const std::uint32_t resultValue =
                static_cast<std::uint32_t>(difference < 0 ? -difference : difference);
            setSccNonZero(resultValue);
            if (!write(resultValue))
                return std::unexpected(Error("SR-GFX-ISA-077", "invalid absdiff destination", pc));
            break;
        }
        case Op::S_BfmB32:
        {
            const auto offset = source(1);
            const auto size = source(2);
            if (!offset || !size)
                return std::unexpected(Error("SR-GFX-ISA-078", "invalid bfm operands", pc));
            if (!write(BitMask(*size) << (*offset & 31u)))
                return std::unexpected(Error("SR-GFX-ISA-079", "invalid bfm destination", pc));
            break;
        }
        case Op::S_BfeU32:
        case Op::S_BfeI32:
        {
            const auto value = source(1);
            const auto control = source(2);
            if (!value || !control)
                return std::unexpected(Error("SR-GFX-ISA-080", "invalid bfe operands", pc));
            const std::uint32_t offset = *control & 31u;
            const std::uint32_t width = (*control >> 16) & 0x7Fu;
            const std::uint32_t mask = BitMask(width);
            std::uint32_t extracted = (*value >> offset) & mask;
            if (instruction.op == Op::S_BfeI32 && width != 0 && width < 32)
            {
                const std::uint32_t shift = 32u - width;
                extracted = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(extracted << shift) >> shift);
            }
            setSccNonZero(extracted);
            if (!write(extracted))
                return std::unexpected(Error("SR-GFX-ISA-081", "invalid bfe destination", pc));
            break;
        }
        case Op::S_MulI32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right || !write(*left * *right))
                return std::unexpected(Error("SR-GFX-ISA-031", "invalid mul operands", pc));
            break;
        }
        case Op::S_MinI32:
        case Op::S_MaxI32:
        case Op::S_MinU32:
        case Op::S_MaxU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-032", "invalid min/max operands", pc));
            const bool signedOp = instruction.op == Op::S_MinI32 || instruction.op == Op::S_MaxI32;
            const bool maximum = instruction.op == Op::S_MaxI32 || instruction.op == Op::S_MaxU32;
            bool first = false;
            std::uint32_t value = 0;
            if (signedOp)
            {
                first = maximum ? AsSigned(*left) >= AsSigned(*right) : AsSigned(*left) <= AsSigned(*right);
                value = static_cast<std::uint32_t>(first ? AsSigned(*left) : AsSigned(*right));
            }
            else
            {
                first = maximum ? *left >= *right : *left <= *right;
                value = first ? *left : *right;
            }
            result.state.scc = first;
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-033", "invalid min/max destination", pc));
            break;
        }
        case Op::S_CselectB32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right || !write(result.state.scc ? *left : *right))
                return std::unexpected(Error("SR-GFX-ISA-034", "invalid cselect operands", pc));
            break;
        }
        case Op::S_CmpEqI32:
        case Op::S_CmpLgI32:
        case Op::S_CmpLtI32:
        case Op::S_CmpLeI32:
        case Op::S_CmpGtI32:
        case Op::S_CmpGeI32:
        case Op::S_CmpEqU32:
        case Op::S_CmpLgU32:
        case Op::S_CmpLtU32:
        case Op::S_CmpLeU32:
        case Op::S_CmpGtU32:
        case Op::S_CmpGeU32:
        {
            const auto left = source(0);
            const auto right = source(1);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-035", "invalid compare operands", pc));
            switch (instruction.op)
            {
            case Op::S_CmpEqI32:
            case Op::S_CmpEqU32:
                result.state.scc = *left == *right;
                break;
            case Op::S_CmpLgI32:
            case Op::S_CmpLgU32:
                result.state.scc = *left != *right;
                break;
            case Op::S_CmpLtI32:
                result.state.scc = AsSigned(*left) < AsSigned(*right);
                break;
            case Op::S_CmpLeI32:
                result.state.scc = AsSigned(*left) <= AsSigned(*right);
                break;
            case Op::S_CmpGtI32:
                result.state.scc = AsSigned(*left) > AsSigned(*right);
                break;
            case Op::S_CmpGeI32:
                result.state.scc = AsSigned(*left) >= AsSigned(*right);
                break;
            case Op::S_CmpLtU32:
                result.state.scc = *left < *right;
                break;
            case Op::S_CmpLeU32:
                result.state.scc = *left <= *right;
                break;
            case Op::S_CmpGtU32:
                result.state.scc = *left > *right;
                break;
            case Op::S_CmpGeU32:
                result.state.scc = *left >= *right;
                break;
            default:
                break;
            }
            break;
        }
        case Op::S_Branch:
        {
            const auto relative = static_cast<std::int16_t>(instruction.operands[0].value & 0xFFFFu);
            const std::int64_t target = static_cast<std::int64_t>(pc) + 4 + static_cast<std::int64_t>(relative) * 4;
            if (target < 0 || static_cast<std::uint64_t>(target) / 4 >= words.size())
                return std::unexpected(Error("SR-GFX-ISA-036", "branch target is out of range", pc));
            pc = static_cast<std::uint32_t>(target);
            advance = false;
            break;
        }
        case Op::S_CbranchScc0:
        case Op::S_CbranchScc1:
        case Op::S_CbranchVccz:
        case Op::S_CbranchVccnz:
        case Op::S_CbranchExecz:
        case Op::S_CbranchExecnz:
        {
            bool taken = false;
            switch (instruction.op)
            {
            case Op::S_CbranchScc0:
                taken = !result.state.scc;
                break;
            case Op::S_CbranchScc1:
                taken = result.state.scc;
                break;
            case Op::S_CbranchVccz:
                taken = !result.state.vcc;
                break;
            case Op::S_CbranchVccnz:
                taken = result.state.vcc;
                break;
            case Op::S_CbranchExecz:
                taken = !result.state.exec;
                break;
            case Op::S_CbranchExecnz:
                taken = result.state.exec;
                break;
            default:
                break;
            }
            if (taken)
            {
                const auto relative = static_cast<std::int16_t>(instruction.operands[0].value & 0xFFFFu);
                const std::int64_t target =
                    static_cast<std::int64_t>(pc) + 4 + static_cast<std::int64_t>(relative) * 4;
                if (target < 0 || static_cast<std::uint64_t>(target) / 4 >= words.size())
                    return std::unexpected(Error("SR-GFX-ISA-037", "branch target is out of range", pc));
                pc = static_cast<std::uint32_t>(target);
            }
            else
            {
                pc += 4;
            }
            advance = false;
            break;
        }
        case Op::S_Endpgm:
            finished = true;
            break;
        case Op::S_Barrier:
            break;
        case Op::V_AddF32:
        case Op::V_SubF32:
        case Op::V_MulF32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-038", "invalid float binary operands", pc));
            float value = 0.0f;
            if (instruction.op == Op::V_AddF32)
                value = FloatSource(instruction, 1, *left) + FloatSource(instruction, 2, *right);
            else if (instruction.op == Op::V_SubF32)
                value = FloatSource(instruction, 1, *left) - FloatSource(instruction, 2, *right);
            else
                value = FloatSource(instruction, 1, *left) * FloatSource(instruction, 2, *right);
            if (!write(FloatResult(instruction, value)))
                return std::unexpected(Error("SR-GFX-ISA-039", "invalid float binary destination", pc));
            break;
        }
        case Op::V_FmaF32:
        case Op::V_MacF32:
        {
            const auto a = source(1);
            const auto b = source(2);
            if (!a || !b)
                return std::unexpected(Error("SR-GFX-ISA-040", "invalid fma operands", pc));
            std::uint32_t c = 0;
            if (instruction.op == Op::V_MacF32)
            {
                const auto accumulator = ReadOperand(result.state, instruction.operands[0]);
                if (!accumulator)
                    return std::unexpected(Error("SR-GFX-ISA-041", "invalid mac accumulator", pc));
                c = *accumulator;
            }
            else
            {
                const auto third = source(3);
                if (!third)
                    return std::unexpected(Error("SR-GFX-ISA-042", "invalid fma third operand", pc));
                c = *third;
            }
            const float third = instruction.op == Op::V_MacF32 ? AsFloat(c)
                                                               : FloatSource(instruction, 3, c);
            if (!write(FloatResult(instruction,
                                   std::fma(FloatSource(instruction, 1, *a),
                                            FloatSource(instruction, 2, *b),
                                            third))))
                return std::unexpected(Error("SR-GFX-ISA-043", "invalid fma destination", pc));
            break;
        }
        case Op::V_MadF32:
        {
            const auto a = source(1);
            const auto b = source(2);
            const auto c = source(3);
            if (!a || !b || !c)
                return std::unexpected(Error("SR-GFX-ISA-044", "invalid mad operands", pc));
            const float product = FloatSource(instruction, 1, *a) * FloatSource(instruction, 2, *b);
            const float value = product + FloatSource(instruction, 3, *c);
            if (!write(FloatResult(instruction, value)))
                return std::unexpected(Error("SR-GFX-ISA-045", "invalid mad destination", pc));
            break;
        }
        case Op::V_AndB32:
        case Op::V_OrB32:
        case Op::V_XorB32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-046", "invalid vector bitwise operands", pc));
            std::uint32_t value = 0;
            if (instruction.op == Op::V_AndB32)
                value = *left & *right;
            else if (instruction.op == Op::V_OrB32)
                value = *left | *right;
            else
                value = *left ^ *right;
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-047", "invalid vector bitwise destination", pc));
            break;
        }
        case Op::V_MinI32:
        case Op::V_MaxI32:
        case Op::V_MinU32:
        case Op::V_MaxU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-048", "invalid vector min/max operands", pc));
            const bool signedOp = instruction.op == Op::V_MinI32 || instruction.op == Op::V_MaxI32;
            const bool maximum = instruction.op == Op::V_MaxI32 || instruction.op == Op::V_MaxU32;
            std::uint32_t value = 0;
            if (signedOp)
                value = static_cast<std::uint32_t>(maximum ? std::max(AsSigned(*left), AsSigned(*right))
                                                           : std::min(AsSigned(*left), AsSigned(*right)));
            else
                value = maximum ? std::max(*left, *right) : std::min(*left, *right);
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-049", "invalid vector min/max destination", pc));
            break;
        }
        case Op::V_MinF32:
        case Op::V_MaxF32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-050", "invalid float min/max operands", pc));
            const float value = instruction.op == Op::V_MinF32
                                    ? std::fmin(FloatSource(instruction, 1, *left), FloatSource(instruction, 2, *right))
                                    : std::fmax(FloatSource(instruction, 1, *left), FloatSource(instruction, 2, *right));
            if (!write(FloatResult(instruction, value)))
                return std::unexpected(Error("SR-GFX-ISA-051", "invalid float min/max destination", pc));
            break;
        }
        case Op::V_Min3F32:
        case Op::V_Max3F32:
        {
            const auto a = source(1);
            const auto b = source(2);
            const auto c = source(3);
            if (!a || !b || !c)
                return std::unexpected(Error("SR-GFX-ISA-052", "invalid min3/max3 operands", pc));
            const float first = instruction.op == Op::V_Min3F32
                                    ? std::fmin(FloatSource(instruction, 1, *a), FloatSource(instruction, 2, *b))
                                    : std::fmax(FloatSource(instruction, 1, *a), FloatSource(instruction, 2, *b));
            const float value = instruction.op == Op::V_Min3F32
                                    ? std::fmin(first, FloatSource(instruction, 3, *c))
                                    : std::fmax(first, FloatSource(instruction, 3, *c));
            if (!write(FloatResult(instruction, value)))
                return std::unexpected(Error("SR-GFX-ISA-053", "invalid min3/max3 destination", pc));
            break;
        }
        case Op::V_AddNcU32:
        case Op::V_SubNcU32:
        case Op::V_MulLoU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-054", "invalid integer VALU operands", pc));
            std::uint32_t value = 0;
            if (instruction.op == Op::V_AddNcU32)
                value = *left + *right;
            else if (instruction.op == Op::V_SubNcU32)
                value = *left - *right;
            else
                value = *left * *right;
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-055", "invalid integer VALU destination", pc));
            break;
        }
        case Op::V_SubrevNcU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right || !write(*right - *left))
                return std::unexpected(Error("SR-GFX-ISA-106", "invalid subrev_nc operands", pc));
            break;
        }
        case Op::V_SubrevF32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-107", "invalid subrev_f32 operands", pc));
            const float value = FloatSource(instruction, 2, *right) - FloatSource(instruction, 1, *left);
            if (!write(FloatResult(instruction, value)))
                return std::unexpected(Error("SR-GFX-ISA-108", "invalid subrev_f32 destination", pc));
            break;
        }
        case Op::V_MulDx9ZeroF32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-109", "invalid dx9 multiply operands", pc));
            const float a = FloatSource(instruction, 1, *left);
            const float b = FloatSource(instruction, 2, *right);
            const float value = (a == 0.0f || b == 0.0f) ? 0.0f : a * b;
            if (!write(FloatResult(instruction, value)))
                return std::unexpected(Error("SR-GFX-ISA-110", "invalid dx9 multiply destination", pc));
            break;
        }
        case Op::V_MulI32I24:
        case Op::V_MulHiI32I24:
        case Op::V_MulU32U24:
        case Op::V_MulHiU32U24:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-111", "invalid 24-bit multiply operands", pc));
            const bool isSigned = instruction.op == Op::V_MulI32I24 || instruction.op == Op::V_MulHiI32I24;
            const bool high = instruction.op == Op::V_MulHiI32I24 || instruction.op == Op::V_MulHiU32U24;
            std::uint32_t a = *left & 0xFFFFFFu;
            std::uint32_t b = *right & 0xFFFFFFu;
            if (isSigned)
            {
                a = static_cast<std::uint32_t>(static_cast<std::int32_t>(a << 8) >> 8);
                b = static_cast<std::uint32_t>(static_cast<std::int32_t>(b << 8) >> 8);
            }
            std::uint32_t value = 0;
            if (high)
            {
                const std::uint64_t product = isSigned
                                                  ? static_cast<std::uint64_t>(
                                                        static_cast<std::int64_t>(AsSigned(a)) *
                                                        static_cast<std::int64_t>(AsSigned(b)))
                                                  : static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
                value = static_cast<std::uint32_t>(product >> 32);
            }
            else
            {
                value = a * b;
            }
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-112", "invalid 24-bit multiply destination", pc));
            break;
        }
        case Op::V_XnorB32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right || !write(~(*left ^ *right)))
                return std::unexpected(Error("SR-GFX-ISA-113", "invalid xnor operands", pc));
            break;
        }
        case Op::V_AddCoCiU32:
        case Op::V_SubCoCiU32:
        case Op::V_SubrevCoCiU32:
        {
            const auto first = source(1);
            const auto second = source(2);
            if (!first || !second)
                return std::unexpected(Error("SR-GFX-ISA-114", "invalid carry VALU operands", pc));
            const bool subtract = instruction.op != Op::V_AddCoCiU32;
            const bool reverse = instruction.op == Op::V_SubrevCoCiU32;
            const std::uint32_t left = reverse ? *second : *first;
            const std::uint32_t right = reverse ? *first : *second;
            const std::uint32_t carryIn = result.state.vcc ? 1u : 0u;
            const std::uint32_t partial = subtract ? left - right : left + right;
            const bool firstFlag = subtract ? left < right : partial < left;
            const std::uint32_t value = subtract ? partial - carryIn : partial + carryIn;
            const bool secondFlag = subtract ? partial < carryIn : value < partial;
            result.state.vcc = firstFlag || secondFlag;
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-115", "invalid carry VALU destination", pc));
            break;
        }
        case Op::V_LshlrevB64:
        {
            const auto count = source(1);
            if (!count)
                return std::unexpected(Error("SR-GFX-ISA-116", "invalid 64-bit shift count", pc));
            const auto value = ReadVgprPair(result.state, instruction.operands[2]);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-117", "invalid 64-bit shift source", pc));
            if (!WriteVgprPair(result.state, instruction.operands[0], *value << (*count & 63u)))
                return std::unexpected(Error("SR-GFX-ISA-118", "invalid 64-bit shift destination", pc));
            break;
        }
        case Op::V_Add3U32:
        case Op::V_AddLshlU32:
        case Op::V_AndOrB32:
        case Op::V_Or3B32:
        case Op::V_Xor3B32:
        case Op::V_LshlAddU32:
        case Op::V_LshlOrB32:
        case Op::V_XadU32:
        case Op::V_Min3I32:
        case Op::V_Min3U32:
        case Op::V_Med3I32:
        case Op::V_Med3U32:
        case Op::V_Max3I32:
        case Op::V_Max3U32:
        case Op::V_SadU32:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-119", "invalid 3-input VALU operands", pc));
            std::uint32_t value = 0;
            switch (instruction.op)
            {
            case Op::V_Add3U32:
                value = *first + *second + *third;
                break;
            case Op::V_AddLshlU32:
                value = (*first + *second) << (*third & 31u);
                break;
            case Op::V_AndOrB32:
                value = (*first & *second) | *third;
                break;
            case Op::V_Or3B32:
                value = *first | *second | *third;
                break;
            case Op::V_Xor3B32:
                value = *first ^ *second ^ *third;
                break;
            case Op::V_LshlAddU32:
                value = (*first << (*second & 31u)) + *third;
                break;
            case Op::V_LshlOrB32:
                value = (*first << (*second & 31u)) | *third;
                break;
            case Op::V_XadU32:
                value = (*first ^ *second) + *third;
                break;
            case Op::V_Min3I32:
                value = static_cast<std::uint32_t>(
                    std::min({AsSigned(*first), AsSigned(*second), AsSigned(*third)}));
                break;
            case Op::V_Min3U32:
                value = std::min({*first, *second, *third});
                break;
            case Op::V_Max3I32:
                value = static_cast<std::uint32_t>(
                    std::max({AsSigned(*first), AsSigned(*second), AsSigned(*third)}));
                break;
            case Op::V_Max3U32:
                value = std::max({*first, *second, *third});
                break;
            case Op::V_Med3I32:
            {
                const std::int32_t a = AsSigned(*first);
                const std::int32_t b = AsSigned(*second);
                const std::int32_t c = AsSigned(*third);
                const std::int32_t low = std::min(a, b);
                const std::int32_t high = std::max(a, b);
                value = static_cast<std::uint32_t>(std::min(std::max(low, c), high));
                break;
            }
            case Op::V_Med3U32:
            {
                const std::uint32_t low = std::min(*first, *second);
                const std::uint32_t high = std::max(*first, *second);
                value = std::min(std::max(low, *third), high);
                break;
            }
            case Op::V_SadU32:
                value = (*first > *second ? *first - *second : *second - *first) + *third;
                break;
            default:
                break;
            }
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-120", "invalid 3-input VALU destination", pc));
            break;
        }
        case Op::V_MulHiI32:
        case Op::V_MulHiU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-121", "invalid mul_hi operands", pc));
            std::uint32_t value = 0;
            if (instruction.op == Op::V_MulHiI32)
            {
                const std::int64_t product = static_cast<std::int64_t>(AsSigned(*left)) *
                                             static_cast<std::int64_t>(AsSigned(*right));
                value = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
            }
            else
            {
                const std::uint64_t product = static_cast<std::uint64_t>(*left) *
                                              static_cast<std::uint64_t>(*right);
                value = static_cast<std::uint32_t>(product >> 32);
            }
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-122", "invalid mul_hi destination", pc));
            break;
        }
        case Op::V_MadI32I24:
        case Op::V_MadU32U24:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-123", "invalid mad24 operands", pc));
            std::uint32_t a = *first & 0xFFFFFFu;
            std::uint32_t b = *second & 0xFFFFFFu;
            if (instruction.op == Op::V_MadI32I24)
            {
                a = static_cast<std::uint32_t>(static_cast<std::int32_t>(a << 8) >> 8);
                b = static_cast<std::uint32_t>(static_cast<std::int32_t>(b << 8) >> 8);
            }
            if (!write(a * b + *third))
                return std::unexpected(Error("SR-GFX-ISA-124", "invalid mad24 destination", pc));
            break;
        }
        case Op::V_LdexpF32:
        {
            const auto value = source(1);
            const auto exponent = source(2);
            if (!value || !exponent)
                return std::unexpected(Error("SR-GFX-ISA-125", "invalid ldexp operands", pc));
            const float scale = std::exp2(static_cast<float>(AsSigned(*exponent)));
            if (!write(FloatResult(instruction, FloatSource(instruction, 1, *value) * scale)))
                return std::unexpected(Error("SR-GFX-ISA-126", "invalid ldexp destination", pc));
            break;
        }
        case Op::V_LshlrevB32:
        case Op::V_LshrrevB32:
        case Op::V_AshrrevI32:
        {
            const auto count = source(1);
            const auto valueOperand = source(2);
            if (!count || !valueOperand)
                return std::unexpected(Error("SR-GFX-ISA-056", "invalid reverse shift operands", pc));
            std::uint32_t value = 0;
            if (instruction.op == Op::V_LshlrevB32)
                value = *valueOperand << (*count & 31u);
            else if (instruction.op == Op::V_LshrrevB32)
                value = *valueOperand >> (*count & 31u);
            else
                value = static_cast<std::uint32_t>(AsSigned(*valueOperand) >> (*count & 31u));
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-057", "invalid reverse shift destination", pc));
            break;
        }
        case Op::V_NotB32:
        {
            const auto value = source(1);
            if (!value || !write(~*value))
                return std::unexpected(Error("SR-GFX-ISA-058", "invalid vector not operands", pc));
            break;
        }
        case Op::V_CvtF32I32:
        case Op::V_CvtF32U32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-059", "invalid convert operand", pc));
            const float converted = instruction.op == Op::V_CvtF32I32 ? static_cast<float>(AsSigned(*value))
                                                                      : static_cast<float>(*value);
            if (!write(AsBits(converted)))
                return std::unexpected(Error("SR-GFX-ISA-060", "invalid convert destination", pc));
            break;
        }
        case Op::V_CvtI32F32:
        case Op::V_CvtU32F32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-061", "invalid convert operand", pc));
            const std::uint32_t converted =
                instruction.op == Op::V_CvtI32F32 ? static_cast<std::uint32_t>(static_cast<std::int32_t>(AsFloat(*value)))
                                                  : static_cast<std::uint32_t>(AsFloat(*value));
            if (!write(converted))
                return std::unexpected(Error("SR-GFX-ISA-062", "invalid convert destination", pc));
            break;
        }
        case Op::V_FfbhU32:
        case Op::V_ClzI32U32:
        case Op::V_FfblB32:
        case Op::V_CtzI32B32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-063", "invalid vector bit scan operand", pc));
            std::uint32_t scan = 0xFFFFFFFFu;
            if (*value != 0)
                scan = (instruction.op == Op::V_FfbhU32 || instruction.op == Op::V_ClzI32U32)
                           ? static_cast<std::uint32_t>(std::countl_zero(*value))
                           : static_cast<std::uint32_t>(std::countr_zero(*value));
            if (!write(scan))
                return std::unexpected(Error("SR-GFX-ISA-064", "invalid vector bit scan destination", pc));
            break;
        }
        case Op::V_BcntU32B32:
        {
            const auto value = source(1);
            const auto addend = source(2);
            if (!value || !addend)
                return std::unexpected(Error("SR-GFX-ISA-065", "invalid bcnt operands", pc));
            if (!write(static_cast<std::uint32_t>(std::popcount(*value)) + *addend))
                return std::unexpected(Error("SR-GFX-ISA-066", "invalid bcnt destination", pc));
            break;
        }
        case Op::V_Nop:
            break;
        case Op::V_ReadfirstlaneB32:
        {
            const auto value = source(1);
            if (!value || !write(*value))
                return std::unexpected(Error("SR-GFX-ISA-094", "invalid readfirstlane operands", pc));
            break;
        }
        case Op::V_CvtNearestI32F32:
        case Op::V_CvtFloorI32F32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-095", "invalid rounded convert operand", pc));
            const float input = AsFloat(*value);
            const float rounded = instruction.op == Op::V_CvtNearestI32F32 ? std::nearbyint(input)
                                                                           : std::floor(input);
            if (!write(static_cast<std::uint32_t>(static_cast<std::int32_t>(rounded))))
                return std::unexpected(Error("SR-GFX-ISA-096", "invalid rounded convert destination", pc));
            break;
        }
        case Op::V_CvtF32Ubyte0:
        case Op::V_CvtF32Ubyte1:
        case Op::V_CvtF32Ubyte2:
        case Op::V_CvtF32Ubyte3:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-097", "invalid byte convert operand", pc));
            std::uint32_t byteIndex = 0;
            if (instruction.op == Op::V_CvtF32Ubyte1)
                byteIndex = 1;
            else if (instruction.op == Op::V_CvtF32Ubyte2)
                byteIndex = 2;
            else if (instruction.op == Op::V_CvtF32Ubyte3)
                byteIndex = 3;
            const std::uint32_t byte = (*value >> (8u * byteIndex)) & 0xFFu;
            if (!write(AsBits(static_cast<float>(byte))))
                return std::unexpected(Error("SR-GFX-ISA-098", "invalid byte convert destination", pc));
            break;
        }
        case Op::V_FractF32:
        case Op::V_TruncF32:
        case Op::V_CeilF32:
        case Op::V_RndneF32:
        case Op::V_FloorF32:
        case Op::V_SinF32:
        case Op::V_CosF32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-099", "invalid unary math operand", pc));
            const float input = FloatSource(instruction, 1, *value);
            float converted = 0.0f;
            switch (instruction.op)
            {
            case Op::V_FractF32:
                converted = input - std::floor(input);
                break;
            case Op::V_TruncF32:
                converted = std::trunc(input);
                break;
            case Op::V_CeilF32:
                converted = std::ceil(input);
                break;
            case Op::V_RndneF32:
                converted = std::nearbyint(input);
                break;
            case Op::V_FloorF32:
                converted = std::floor(input);
                break;
            case Op::V_SinF32:
                converted = std::sin(input);
                break;
            case Op::V_CosF32:
                converted = std::cos(input);
                break;
            default:
                break;
            }
            if (!write(FloatResult(instruction, converted)))
                return std::unexpected(Error("SR-GFX-ISA-100", "invalid unary math destination", pc));
            break;
        }
        case Op::V_BfrevB32:
        {
            const auto value = source(1);
            if (!value || !write(BitReverse(*value)))
                return std::unexpected(Error("SR-GFX-ISA-101", "invalid bfrev operands", pc));
            break;
        }
        case Op::V_ClsI32:
        {
            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-102", "invalid cls operand", pc));
            const std::uint32_t sign = static_cast<std::uint32_t>(AsSigned(*value) >> 31);
            const std::uint32_t different = *value ^ sign;
            const std::uint32_t result = different == 0
                                             ? 0xFFFFFFFFu
                                             : static_cast<std::uint32_t>(std::countl_zero(different));
            if (!write(result))
                return std::unexpected(Error("SR-GFX-ISA-103", "invalid cls destination", pc));
            break;
        }
        case Op::V_CvtI32I16:
            {
                const auto value = source(1);
                if (!value || !write(static_cast<std::uint32_t>(static_cast<std::int32_t>(
                                 static_cast<std::int16_t>(*value & 0xFFFFu)))))
                    return std::unexpected(Error("SR-GFX-ISA-104", "invalid cvt_i32_i16 operands", pc));
                break;
            }
        case Op::V_CvtU32U16:
            {
                const auto value = source(1);
                if (!value || !write(*value & 0xFFFFu))
                    return std::unexpected(Error("SR-GFX-ISA-105", "invalid cvt_u32_u16 operands", pc));
                break;
            }
        case Op::S_SextI32I8:
            {
                const auto value = source(1);
                if (!value || !write(static_cast<std::uint32_t>(static_cast<std::int32_t>(
                        static_cast<std::int8_t>(*value & 0xFFu)))))
                    return std::unexpected(Error("SR-GFX-ISA-169", "invalid sext_i32_i8 operands", pc));
                break;
            }
        case Op::S_SextI32I16:
            {
                const auto value = source(1);
                if (!value || !write(static_cast<std::uint32_t>(static_cast<std::int32_t>(
                        static_cast<std::int16_t>(*value & 0xFFFFu)))))
                    return std::unexpected(Error("SR-GFX-ISA-170", "invalid sext_i32_i16 operands", pc));
                break;
            }
        case Op::S_BrevB64:
            {
                const auto value = ReadPair(result.state, instruction.operands[1]);
                if (!value || !WritePair(result.state, instruction.operands[0], BitReverse64(*value)))
                    return std::unexpected(Error("SR-GFX-ISA-171", "invalid brev_b64 operands", pc));
                break;
            }
        case Op::S_Bcnt0I32B32:
            {
                const auto value = source(1);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-172", "invalid bcnt0_b32 operand", pc));
                const std::uint32_t count = 32u - static_cast<std::uint32_t>(std::popcount(*value));
                result.state.scc = count != 0;
                if (!write(count))
                    return std::unexpected(Error("SR-GFX-ISA-173", "invalid bcnt0_b32 destination", pc));
                break;
            }
        case Op::S_Bcnt0I32B64:
        case Op::S_Bcnt1I32B64:
            {
                const auto value = ReadPair(result.state, instruction.operands[1]);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-174", "invalid bcnt_b64 operand", pc));
                const std::uint32_t ones = static_cast<std::uint32_t>(std::popcount(*value));
                const std::uint32_t count = instruction.op == Op::S_Bcnt1I32B64 ? ones : 64u - ones;
                result.state.scc = count != 0;
                if (!write(count))
                    return std::unexpected(Error("SR-GFX-ISA-175", "invalid bcnt_b64 destination", pc));
                break;
            }
        case Op::S_Bitset0B32:
        case Op::S_Bitset1B32:
            {
                const auto offset = source(1);
                if (!offset)
                    return std::unexpected(Error("SR-GFX-ISA-176", "invalid bitset_b32 operand", pc));
                const std::uint32_t mask = 1u << (*offset & 31u);
                const std::uint32_t base = result.state.sgpr[instruction.operands[0].value];
                const std::uint32_t value = instruction.op == Op::S_Bitset1B32 ? (base | mask)
                                                                               : (base & ~mask);
                if (!write(value))
                    return std::unexpected(Error("SR-GFX-ISA-177", "invalid bitset_b32 destination", pc));
                break;
            }
        case Op::S_Bitset0B64:
        case Op::S_Bitset1B64:
            {
                const auto offset = source(1);
                const std::uint32_t index = instruction.operands[0].value;
                if (!offset || index + 1 >= 256)
                    return std::unexpected(Error("SR-GFX-ISA-178", "invalid bitset_b64 operands", pc));
                const std::uint64_t base = static_cast<std::uint64_t>(result.state.sgpr[index]) |
                                           (static_cast<std::uint64_t>(result.state.sgpr[index + 1]) << 32);
                const std::uint64_t mask = std::uint64_t{1} << (*offset & 63u);
                const std::uint64_t value = instruction.op == Op::S_Bitset1B64 ? (base | mask)
                                                                              : (base & ~mask);
                if (!WritePair(result.state, instruction.operands[0], value))
                    return std::unexpected(Error("SR-GFX-ISA-179", "invalid bitset_b64 destination", pc));
                break;
            }
        case Op::S_LshlB64:
        case Op::S_LshrB64:
        case Op::S_AshrI64:
            {
                const auto value = ReadPair(result.state, instruction.operands[1]);
                const auto count = source(2);
                if (!value || !count)
                    return std::unexpected(Error("SR-GFX-ISA-180", "invalid 64-bit shift operands", pc));
                std::uint64_t shifted = 0;
                if (instruction.op == Op::S_LshlB64)
                    shifted = *value << (*count & 63u);
                else if (instruction.op == Op::S_LshrB64)
                    shifted = *value >> (*count & 63u);
                else
                    shifted = static_cast<std::uint64_t>(static_cast<std::int64_t>(*value) >> (*count & 63u));
                result.state.scc = shifted != 0;
                if (!WritePair(result.state, instruction.operands[0], shifted))
                    return std::unexpected(Error("SR-GFX-ISA-181", "invalid 64-bit shift destination", pc));
                break;
            }
        case Op::S_BfmB64:
            {
                const auto offset = source(1);
                const auto size = source(2);
                if (!offset || !size)
                    return std::unexpected(Error("SR-GFX-ISA-182", "invalid bfm_b64 operands", pc));
                const std::uint64_t mask = *size >= 64 ? ~std::uint64_t{0}
                                                       : ((std::uint64_t{1} << *size) - 1);
                if (!WritePair(result.state, instruction.operands[0], mask << (*offset & 63u)))
                    return std::unexpected(Error("SR-GFX-ISA-183", "invalid bfm_b64 destination", pc));
                break;
            }
        case Op::S_BfeI64:
        case Op::S_BfeU64:
            {
                const auto value = ReadPair(result.state, instruction.operands[1]);
                const auto control = source(2);
                if (!value || !control)
                    return std::unexpected(Error("SR-GFX-ISA-184", "invalid bfe_b64 operands", pc));
                const std::uint32_t offset = *control & 63u;
                const std::uint32_t width = (*control >> 16) & 0x7Fu;
                const std::uint64_t mask = width >= 64 ? ~std::uint64_t{0}
                                                       : ((std::uint64_t{1} << width) - 1);
                std::uint64_t extracted = (*value >> offset) & mask;
                if (instruction.op == Op::S_BfeI64 && width != 0 && width < 64)
                {
                    const std::uint32_t amount = 64u - width;
                    extracted = static_cast<std::uint64_t>(static_cast<std::int64_t>(extracted << amount) >> amount);
                }
                result.state.scc = extracted != 0;
                if (!WritePair(result.state, instruction.operands[0], extracted))
                    return std::unexpected(Error("SR-GFX-ISA-185", "invalid bfe_b64 destination", pc));
                break;
            }
        case Op::S_MulHiI32:
        case Op::S_MulHiU32:
            {
                const auto left = source(1);
                const auto right = source(2);
                if (!left || !right)
                    return std::unexpected(Error("SR-GFX-ISA-186", "invalid mul_hi operands", pc));
                std::uint32_t high = 0;
                if (instruction.op == Op::S_MulHiI32)
                {
                    const std::int64_t product = static_cast<std::int64_t>(AsSigned(*left)) *
                                                 static_cast<std::int64_t>(AsSigned(*right));
                    high = static_cast<std::uint32_t>(static_cast<std::uint64_t>(product) >> 32);
                }
                else
                {
                    high = static_cast<std::uint32_t>((static_cast<std::uint64_t>(*left) *
                                                       static_cast<std::uint64_t>(*right)) >> 32);
                }
                if (!write(high))
                    return std::unexpected(Error("SR-GFX-ISA-187", "invalid mul_hi destination", pc));
                break;
            }
        case Op::S_Ff0I32B32:
            {
                const auto value = source(1);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-188", "invalid ff0_b32 operand", pc));
                const std::uint32_t inverted = ~*value;
                const std::uint32_t count = inverted == 0 ? 0xFFFFFFFFu
                                                          : static_cast<std::uint32_t>(std::countr_zero(inverted));
                if (!write(count))
                    return std::unexpected(Error("SR-GFX-ISA-189", "invalid ff0_b32 destination", pc));
                break;
            }
        case Op::S_Ff1I32B64:
        case Op::S_FlbitI32B64:
            {
                const auto value = ReadPair(result.state, instruction.operands[1]);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-190", "invalid 64-bit bit scan operand", pc));
                std::uint32_t count = 0xFFFFFFFFu;
                if (*value != 0)
                {
                    count = instruction.op == Op::S_Ff1I32B64
                                ? static_cast<std::uint32_t>(std::countr_zero(*value))
                                : static_cast<std::uint32_t>(std::countl_zero(*value));
                }
                if (!write(count))
                    return std::unexpected(Error("SR-GFX-ISA-191", "invalid 64-bit bit scan destination", pc));
                break;
            }
        case Op::S_FlbitI32:
            {
                const auto value = source(1);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-192", "invalid flbit_i32 operand", pc));
                const std::uint32_t sign = static_cast<std::uint32_t>(AsSigned(*value) >> 31);
                const std::uint32_t different = *value ^ sign;
                const std::uint32_t count = different == 0
                                                ? 0xFFFFFFFFu
                                                : static_cast<std::uint32_t>(std::countl_zero(different));
                if (!write(count))
                    return std::unexpected(Error("SR-GFX-ISA-193", "invalid flbit_i32 destination", pc));
                break;
            }
        case Op::S_FlbitI32I64:
            {
                const auto value = ReadPair(result.state, instruction.operands[1]);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-194", "invalid flbit_i32_i64 operand", pc));
                const std::uint64_t sign = static_cast<std::uint64_t>(static_cast<std::int64_t>(*value) >> 63);
                const std::uint64_t different = *value ^ sign;
                const std::uint32_t count = different == 0
                                                ? 0xFFFFFFFFu
                                                : static_cast<std::uint32_t>(std::countl_zero(different));
                if (!write(count))
                    return std::unexpected(Error("SR-GFX-ISA-195", "invalid flbit_i32_i64 destination", pc));
                break;
            }
        case Op::V_PkAddI16:
        case Op::V_PkSubI16:
        case Op::V_PkMulLoU16:
        case Op::V_PkMadI16:
        case Op::V_PkMinI16:
        case Op::V_PkMaxI16:
        case Op::V_PkAddU16:
        case Op::V_PkSubU16:
        case Op::V_PkMadU16:
        case Op::V_PkMinU16:
        case Op::V_PkMaxU16:
        case Op::V_PkLshlrevB16:
        case Op::V_PkLshrrevB16:
        case Op::V_PkAshrrevI16:
        case Op::V_PkAddF16:
        case Op::V_PkMulF16:
        case Op::V_PkFmaF16:
        case Op::V_PkMinF16:
        case Op::V_PkMaxF16:
            {
                const auto first = source(1);
                const auto second = source(2);
                if (!first || !second)
                    return std::unexpected(Error("SR-GFX-ISA-166", "invalid packed operands", pc));
                const bool ternary = instruction.op == Op::V_PkMadI16 ||
                                     instruction.op == Op::V_PkMadU16 ||
                                     instruction.op == Op::V_PkFmaF16;
                std::uint32_t third = 0;
                if (ternary)
                {
                    const auto value = source(3);
                    if (!value)
                        return std::unexpected(Error("SR-GFX-ISA-167", "invalid packed third operand", pc));
                    third = *value;
                }
                const auto compute = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) -> std::uint32_t {
                    switch (instruction.op)
                    {
                    case Op::V_PkAddI16:
                    case Op::V_PkAddU16:
                        return (a + b) & 0xFFFFu;
                    case Op::V_PkSubI16:
                    case Op::V_PkSubU16:
                        return (a - b) & 0xFFFFu;
                    case Op::V_PkMulLoU16:
                        return (a * b) & 0xFFFFu;
                    case Op::V_PkMadI16:
                    case Op::V_PkMadU16:
                        return (a * b + c) & 0xFFFFu;
                    case Op::V_PkMinI16:
                        return static_cast<std::uint32_t>(std::min(static_cast<std::int16_t>(a),
                                                                   static_cast<std::int16_t>(b)));
                    case Op::V_PkMaxI16:
                        return static_cast<std::uint32_t>(std::max(static_cast<std::int16_t>(a),
                                                                   static_cast<std::int16_t>(b)));
                    case Op::V_PkMinU16:
                        return std::min(a & 0xFFFFu, b & 0xFFFFu);
                    case Op::V_PkMaxU16:
                        return std::max(a & 0xFFFFu, b & 0xFFFFu);
                    case Op::V_PkLshlrevB16:
                        return (b << (a & 15u)) & 0xFFFFu;
                    case Op::V_PkLshrrevB16:
                        return (b & 0xFFFFu) >> (a & 15u);
                    case Op::V_PkAshrrevI16:
                        return static_cast<std::uint32_t>(static_cast<std::int16_t>(b)) >>
                               (a & 15u);
                    case Op::V_PkAddF16:
                        return FloatToHalf(HalfToFloat(static_cast<std::uint16_t>(a)) +
                                           HalfToFloat(static_cast<std::uint16_t>(b)));
                    case Op::V_PkMulF16:
                        return FloatToHalf(HalfToFloat(static_cast<std::uint16_t>(a)) *
                                           HalfToFloat(static_cast<std::uint16_t>(b)));
                    case Op::V_PkFmaF16:
                        return FloatToHalf(std::fma(HalfToFloat(static_cast<std::uint16_t>(a)),
                                                    HalfToFloat(static_cast<std::uint16_t>(b)),
                                                    HalfToFloat(static_cast<std::uint16_t>(c))));
                    case Op::V_PkMinF16:
                        return FloatToHalf(std::fmin(HalfToFloat(static_cast<std::uint16_t>(a)),
                                                     HalfToFloat(static_cast<std::uint16_t>(b))));
                    case Op::V_PkMaxF16:
                        return FloatToHalf(std::fmax(HalfToFloat(static_cast<std::uint16_t>(a)),
                                                     HalfToFloat(static_cast<std::uint16_t>(b))));
                    default:
                        return 0;
                    }
                };
                const std::uint32_t low = compute(*first & 0xFFFFu, *second & 0xFFFFu, third & 0xFFFFu);
                const std::uint32_t high = compute(*first >> 16, *second >> 16, third >> 16);
                if (!write((high << 16) | (low & 0xFFFFu)))
                    return std::unexpected(Error("SR-GFX-ISA-168", "invalid packed destination", pc));
                break;
            }
        case Op::V_CvtF64I32:
            {
                const auto value = source(1);
                if (!value || !WriteF64(result.state, instruction.operands[0],
                                        static_cast<double>(AsSigned(*value))))
                    return std::unexpected(Error("SR-GFX-ISA-151", "invalid cvt_f64_i32 operands", pc));
                break;
            }
        case Op::V_CvtF64U32:
            {
                const auto value = source(1);
                if (!value || !WriteF64(result.state, instruction.operands[0],
                                        static_cast<double>(*value)))
                    return std::unexpected(Error("SR-GFX-ISA-152", "invalid cvt_f64_u32 operands", pc));
                break;
            }
        case Op::V_CvtI32F64:
            {
                const auto value = ReadF64(result.state, instruction.operands[1]);
                if (!value || !write(static_cast<std::uint32_t>(static_cast<std::int32_t>(*value))))
                    return std::unexpected(Error("SR-GFX-ISA-153", "invalid cvt_i32_f64 operands", pc));
                break;
            }
        case Op::V_CvtU32F64:
            {
                const auto value = ReadF64(result.state, instruction.operands[1]);
                if (!value || !write(static_cast<std::uint32_t>(*value)))
                    return std::unexpected(Error("SR-GFX-ISA-154", "invalid cvt_u32_f64 operands", pc));
                break;
            }
        case Op::V_CvtF64F32:
            {
                const auto value = source(1);
                if (!value || !WriteF64(result.state, instruction.operands[0],
                                        static_cast<double>(AsFloat(*value))))
                    return std::unexpected(Error("SR-GFX-ISA-155", "invalid cvt_f64_f32 operands", pc));
                break;
            }
        case Op::V_CvtF32F64:
            {
                const auto value = ReadF64(result.state, instruction.operands[1]);
                if (!value || !write(AsBits(static_cast<float>(*value))))
                    return std::unexpected(Error("SR-GFX-ISA-156", "invalid cvt_f32_f64 operands", pc));
                break;
            }
        case Op::V_TruncF64:
        case Op::V_CeilF64:
        case Op::V_RndneF64:
        case Op::V_FloorF64:
        case Op::V_RcpF64:
        case Op::V_RsqF64:
        case Op::V_SqrtF64:
        case Op::V_FractF64:
            {
                const auto value = ReadF64(result.state, instruction.operands[1]);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-157", "invalid f64 unary operand", pc));
                double converted = 0.0;
                switch (instruction.op)
                {
                case Op::V_TruncF64:
                    converted = std::trunc(*value);
                    break;
                case Op::V_CeilF64:
                    converted = std::ceil(*value);
                    break;
                case Op::V_RndneF64:
                    converted = std::nearbyint(*value);
                    break;
                case Op::V_FloorF64:
                    converted = std::floor(*value);
                    break;
                case Op::V_RcpF64:
                    converted = 1.0 / *value;
                    break;
                case Op::V_RsqF64:
                    converted = 1.0 / std::sqrt(*value);
                    break;
                case Op::V_SqrtF64:
                    converted = std::sqrt(*value);
                    break;
                case Op::V_FractF64:
                    converted = *value - std::floor(*value);
                    break;
                default:
                    break;
                }
                if (!WriteF64(result.state, instruction.operands[0], converted))
                    return std::unexpected(Error("SR-GFX-ISA-158", "invalid f64 unary destination", pc));
                break;
            }
        case Op::V_AddF64:
        case Op::V_MulF64:
        case Op::V_MinF64:
        case Op::V_MaxF64:
            {
                const auto first = ReadF64(result.state, instruction.operands[1]);
                const auto second = ReadF64(result.state, instruction.operands[2]);
                if (!first || !second)
                    return std::unexpected(Error("SR-GFX-ISA-159", "invalid f64 binary operands", pc));
                double value = 0.0;
                if (instruction.op == Op::V_AddF64)
                    value = *first + *second;
                else if (instruction.op == Op::V_MulF64)
                    value = *first * *second;
                else if (instruction.op == Op::V_MinF64)
                    value = std::fmin(*first, *second);
                else
                    value = std::fmax(*first, *second);
                if (!WriteF64(result.state, instruction.operands[0], value))
                    return std::unexpected(Error("SR-GFX-ISA-160", "invalid f64 binary destination", pc));
                break;
            }
        case Op::V_FmaF64:
            {
                const auto a = ReadF64(result.state, instruction.operands[1]);
                const auto b = ReadF64(result.state, instruction.operands[2]);
                const auto c = ReadF64(result.state, instruction.operands[3]);
                if (!a || !b || !c)
                    return std::unexpected(Error("SR-GFX-ISA-161", "invalid f64 fma operands", pc));
                if (!WriteF64(result.state, instruction.operands[0], std::fma(*a, *b, *c)))
                    return std::unexpected(Error("SR-GFX-ISA-162", "invalid f64 fma destination", pc));
                break;
            }
        case Op::V_LdexpF64:
            {
                const auto value = ReadF64(result.state, instruction.operands[1]);
                const auto exponent = source(2);
                if (!value || !exponent)
                    return std::unexpected(Error("SR-GFX-ISA-163", "invalid f64 ldexp operands", pc));
                const double scale = std::exp2(static_cast<double>(AsSigned(*exponent)));
                if (!WriteF64(result.state, instruction.operands[0], *value * scale))
                    return std::unexpected(Error("SR-GFX-ISA-164", "invalid f64 ldexp destination", pc));
                break;
            }
        case Op::V_CvtF16F32:            {
                const auto value = source(1);
                if (!value || !write(FloatToHalf(AsFloat(*value))))
                    return std::unexpected(Error("SR-GFX-ISA-127", "invalid cvt_f16_f32 operands", pc));
                break;
            }
        case Op::V_CvtF32F16:
            {
                const auto value = source(1);
                if (!value || !write(AsBits(HalfToFloat(static_cast<std::uint16_t>(*value)))))
                    return std::unexpected(Error("SR-GFX-ISA-128", "invalid cvt_f32_f16 operands", pc));
                break;
            }
        case Op::V_CvtF16U16:
            {
                const auto value = source(1);
                if (!value || !write(FloatToHalf(static_cast<float>(*value & 0xFFFFu))))
                    return std::unexpected(Error("SR-GFX-ISA-129", "invalid cvt_f16_u16 operands", pc));
                break;
            }
        case Op::V_CvtF16I16:
            {
                const auto value = source(1);
                if (!value || !write(FloatToHalf(static_cast<float>(static_cast<std::int16_t>(*value & 0xFFFFu)))))
                    return std::unexpected(Error("SR-GFX-ISA-130", "invalid cvt_f16_i16 operands", pc));
                break;
            }
        case Op::V_CvtU16F16:
            {
                const auto value = source(1);
                if (!value || !write(static_cast<std::uint32_t>(HalfToFloat(static_cast<std::uint16_t>(*value)))))
                    return std::unexpected(Error("SR-GFX-ISA-131", "invalid cvt_u16_f16 operands", pc));
                break;
            }
        case Op::V_CvtI16F16:
            {
                const auto value = source(1);
                if (!value || !write(static_cast<std::uint32_t>(
                        static_cast<std::int32_t>(HalfToFloat(static_cast<std::uint16_t>(*value))))))
                    return std::unexpected(Error("SR-GFX-ISA-132", "invalid cvt_i16_f16 operands", pc));
                break;
            }
        case Op::V_CvtNormI16F16:
        case Op::V_CvtNormU16F16:
            {
                const auto value = source(1);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-133", "invalid norm conversion operand", pc));
                const float input = HalfToFloat(static_cast<std::uint16_t>(*value));
                const std::uint32_t result = instruction.op == Op::V_CvtNormI16F16
                                                 ? static_cast<std::uint32_t>(
                                                       static_cast<std::int32_t>(input * 32767.0f)) & 0xFFFFu
                                                 : static_cast<std::uint32_t>(input * 65535.0f) & 0xFFFFu;
                if (!write(result))
                    return std::unexpected(Error("SR-GFX-ISA-134", "invalid norm conversion destination", pc));
                break;
            }
        case Op::V_RcpF16:
        case Op::V_RsqF16:
        case Op::V_SqrtF16:
        case Op::V_ExpF16:
        case Op::V_LogF16:
        case Op::V_FloorF16:
        case Op::V_CeilF16:
        case Op::V_TruncF16:
        case Op::V_RndneF16:
        case Op::V_FractF16:
        case Op::V_SinF16:
        case Op::V_CosF16:
            {
                const auto value = source(1);
                if (!value)
                    return std::unexpected(Error("SR-GFX-ISA-135", "invalid f16 unary operand", pc));
                const float input = HalfToFloat(static_cast<std::uint16_t>(*value));
                float converted = 0.0f;
                switch (instruction.op)
                {
                case Op::V_RcpF16:
                    converted = 1.0f / input;
                    break;
                case Op::V_RsqF16:
                    converted = 1.0f / std::sqrt(input);
                    break;
                case Op::V_SqrtF16:
                    converted = std::sqrt(input);
                    break;
                case Op::V_ExpF16:
                    converted = std::exp2(input);
                    break;
                case Op::V_LogF16:
                    converted = std::log2(input);
                    break;
                case Op::V_FloorF16:
                    converted = std::floor(input);
                    break;
                case Op::V_CeilF16:
                    converted = std::ceil(input);
                    break;
                case Op::V_TruncF16:
                    converted = std::trunc(input);
                    break;
                case Op::V_RndneF16:
                    converted = std::nearbyint(input);
                    break;
                case Op::V_FractF16:
                    converted = input - std::floor(input);
                    break;
                case Op::V_SinF16:
                    converted = std::sin(input);
                    break;
                case Op::V_CosF16:
                    converted = std::cos(input);
                    break;
                default:
                    break;
                }
                if (!write(FloatToHalf(converted)))
                    return std::unexpected(Error("SR-GFX-ISA-136", "invalid f16 unary destination", pc));
                break;
            }
        case Op::V_MovB16:
            {
                const auto value = source(1);
                if (!value || !write(*value & 0xFFFFu))
                    return std::unexpected(Error("SR-GFX-ISA-137", "invalid mov_b16 operands", pc));
                break;
            }
        case Op::V_NotB16:
            {
                const auto value = source(1);
                if (!value || !write(~*value & 0xFFFFu))
                    return std::unexpected(Error("SR-GFX-ISA-138", "invalid not_b16 operands", pc));
                break;
            }
        case Op::V_AddF16:
        case Op::V_SubF16:
        case Op::V_MulF16:
        case Op::V_MinF16:
        case Op::V_MaxF16:
            {
                const auto first = source(1);
                const auto second = source(2);
                if (!first || !second)
                    return std::unexpected(Error("SR-GFX-ISA-139", "invalid f16 binary operands", pc));
                const float a = HalfToFloat(static_cast<std::uint16_t>(*first));
                const float b = HalfToFloat(static_cast<std::uint16_t>(*second));
                float value = 0.0f;
                if (instruction.op == Op::V_AddF16)
                    value = a + b;
                else if (instruction.op == Op::V_SubF16)
                    value = a - b;
                else if (instruction.op == Op::V_MulF16)
                    value = a * b;
                else if (instruction.op == Op::V_MinF16)
                    value = std::fmin(a, b);
                else
                    value = std::fmax(a, b);
                if (!write(FloatToHalf(value)))
                    return std::unexpected(Error("SR-GFX-ISA-140", "invalid f16 binary destination", pc));
                break;
            }
        case Op::V_SubrevF16:
            {
                const auto first = source(1);
                const auto second = source(2);
                if (!first || !second)
                    return std::unexpected(Error("SR-GFX-ISA-141", "invalid f16 subrev operands", pc));
                const float value = HalfToFloat(static_cast<std::uint16_t>(*second)) -
                                    HalfToFloat(static_cast<std::uint16_t>(*first));
                if (!write(FloatToHalf(value)))
                    return std::unexpected(Error("SR-GFX-ISA-142", "invalid f16 subrev destination", pc));
                break;
            }
        case Op::V_FmaF16:
        case Op::V_FmacF16:
            {
                const auto first = source(1);
                const auto second = source(2);
                if (!first || !second)
                    return std::unexpected(Error("SR-GFX-ISA-143", "invalid f16 fma operands", pc));
                float c = 0.0f;
                if (instruction.op == Op::V_FmacF16)
                {
                    const auto accumulator = ReadOperand(result.state, instruction.operands[0]);
                    if (!accumulator)
                        return std::unexpected(Error("SR-GFX-ISA-144", "invalid f16 fma accumulator", pc));
                    c = HalfToFloat(static_cast<std::uint16_t>(*accumulator));
                }
                else
                {
                    const auto third = source(3);
                    if (!third)
                        return std::unexpected(Error("SR-GFX-ISA-145", "invalid f16 fma third operand", pc));
                    c = HalfToFloat(static_cast<std::uint16_t>(*third));
                }
                const float value = std::fma(HalfToFloat(static_cast<std::uint16_t>(*first)),
                                             HalfToFloat(static_cast<std::uint16_t>(*second)), c);
                if (!write(FloatToHalf(value)))
                    return std::unexpected(Error("SR-GFX-ISA-146", "invalid f16 fma destination", pc));
                break;
            }
        case Op::V_LdexpF16:
            {
                const auto value = source(1);
                const auto exponent = source(2);
                if (!value || !exponent)
                    return std::unexpected(Error("SR-GFX-ISA-147", "invalid f16 ldexp operands", pc));
                const float scale = std::exp2(static_cast<float>(AsSigned(*exponent)));
                if (!write(FloatToHalf(HalfToFloat(static_cast<std::uint16_t>(*value)) * scale)))
                    return std::unexpected(Error("SR-GFX-ISA-148", "invalid f16 ldexp destination", pc));
                break;
            }
        case Op::V_Min3F16:
        case Op::V_Max3F16:
            {
                const auto first = source(1);
                const auto second = source(2);
                const auto third = source(3);
                if (!first || !second || !third)
                    return std::unexpected(Error("SR-GFX-ISA-149", "invalid f16 min3/max3 operands", pc));
                const float a = HalfToFloat(static_cast<std::uint16_t>(*first));
                const float b = HalfToFloat(static_cast<std::uint16_t>(*second));
                const float c = HalfToFloat(static_cast<std::uint16_t>(*third));
                const float value = instruction.op == Op::V_Min3F16 ? std::fmin(std::fmin(a, b), c)
                                                                    : std::fmax(std::fmax(a, b), c);
                if (!write(FloatToHalf(value)))
                    return std::unexpected(Error("SR-GFX-ISA-150", "invalid f16 min3/max3 destination", pc));
                break;
            }
        case Op::V_RcpF32:
        case Op::V_RsqF32:
        case Op::V_SqrtF32:
        case Op::V_ExpF32:
        case Op::V_LogF32:
        {            const auto value = source(1);
            if (!value)
                return std::unexpected(Error("SR-GFX-ISA-067", "invalid math operand", pc));
            const float input = FloatSource(instruction, 1, *value);
            float converted = 0.0f;
            switch (instruction.op)
            {
            case Op::V_RcpF32:
                converted = 1.0f / input;
                break;
            case Op::V_RsqF32:
                converted = 1.0f / std::sqrt(input);
                break;
            case Op::V_SqrtF32:
                converted = std::sqrt(input);
                break;
            case Op::V_ExpF32:
                converted = std::exp2(input);
                break;
            case Op::V_LogF32:
                converted = std::log2(input);
                break;
            default:
                break;
            }
            if (!write(FloatResult(instruction, converted)))
                return std::unexpected(Error("SR-GFX-ISA-068", "invalid math destination", pc));
            break;
        }
        case Op::V_CmpLtI32:
        case Op::V_CmpEqI32:
        case Op::V_CmpLeI32:
        case Op::V_CmpGtI32:
        case Op::V_CmpNeI32:
        case Op::V_CmpGeI32:
        case Op::V_CmpLtU32:
        case Op::V_CmpEqU32:
        case Op::V_CmpLeU32:
        case Op::V_CmpGtU32:
        case Op::V_CmpNeU32:
        case Op::V_CmpGeU32:
        case Op::V_CmpFalseI32:
        case Op::V_CmpTrueI32:
        case Op::V_CmpFalseU32:
        case Op::V_CmpTrueU32:
        case Op::V_CmpLtF32:
        case Op::V_CmpEqF32:
        case Op::V_CmpLeF32:
        case Op::V_CmpGtF32:
        case Op::V_CmpLgF32:
        case Op::V_CmpGeF32:
        case Op::V_CmpFalseF32:
        case Op::V_CmpTrueF32:
        case Op::V_CmpOrdF32:
        case Op::V_CmpUnordF32:
        case Op::V_CmpNgeF32:
        case Op::V_CmpNlgF32:
        case Op::V_CmpNgtF32:
        case Op::V_CmpNleF32:
        case Op::V_CmpNeqF32:
        case Op::V_CmpNltF32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-069", "invalid vector compare operands", pc));
            bool condition = false;
            switch (instruction.op)
            {
            case Op::V_CmpLtI32:
                condition = AsSigned(*left) < AsSigned(*right);
                break;
            case Op::V_CmpEqI32:
                condition = *left == *right;
                break;
            case Op::V_CmpLeI32:
                condition = AsSigned(*left) <= AsSigned(*right);
                break;
            case Op::V_CmpGtI32:
                condition = AsSigned(*left) > AsSigned(*right);
                break;
            case Op::V_CmpNeI32:
                condition = *left != *right;
                break;
            case Op::V_CmpGeI32:
                condition = AsSigned(*left) >= AsSigned(*right);
                break;
            case Op::V_CmpLtU32:
                condition = *left < *right;
                break;
            case Op::V_CmpEqU32:
                condition = *left == *right;
                break;
            case Op::V_CmpLeU32:
                condition = *left <= *right;
                break;
            case Op::V_CmpGtU32:
                condition = *left > *right;
                break;
            case Op::V_CmpNeU32:
                condition = *left != *right;
                break;
            case Op::V_CmpGeU32:
                condition = *left >= *right;
                break;
            case Op::V_CmpFalseI32:
            case Op::V_CmpFalseU32:
                condition = false;
                break;
            case Op::V_CmpTrueI32:
            case Op::V_CmpTrueU32:
                condition = true;
                break;
            default:
            {
                const float a = FloatSource(instruction, 1, *left);
                const float b = FloatSource(instruction, 2, *right);
                const bool ordered = Ordered(a, b);
                switch (instruction.op)
                {
                case Op::V_CmpLtF32:
                    condition = ordered && a < b;
                    break;
                case Op::V_CmpEqF32:
                    condition = ordered && a == b;
                    break;
                case Op::V_CmpLeF32:
                    condition = ordered && a <= b;
                    break;
                case Op::V_CmpGtF32:
                    condition = ordered && a > b;
                    break;
                case Op::V_CmpLgF32:
                    condition = ordered && a != b;
                    break;
                case Op::V_CmpGeF32:
                    condition = ordered && a >= b;
                    break;
                case Op::V_CmpFalseF32:
                    condition = false;
                    break;
                case Op::V_CmpTrueF32:
                    condition = true;
                    break;
                case Op::V_CmpOrdF32:
                    condition = ordered;
                    break;
                case Op::V_CmpUnordF32:
                    condition = !ordered;
                    break;
                case Op::V_CmpNgeF32:
                    condition = !ordered || a < b;
                    break;
                case Op::V_CmpNlgF32:
                    condition = !ordered || a == b;
                    break;
                case Op::V_CmpNgtF32:
                    condition = !ordered || a <= b;
                    break;
                case Op::V_CmpNleF32:
                    condition = !ordered || a > b;
                    break;
                case Op::V_CmpNeqF32:
                    condition = ordered && a != b;
                    break;
                case Op::V_CmpNltF32:
                    condition = !ordered || a >= b;
                    break;
                default:
                    break;
                }
                break;
            }
            }
            if (instruction.encoding == Encoding::Vop3)
            {
                const Operand* scalarDestination = FindOperand(instruction, OperandKind::Sdst);
                if (scalarDestination != nullptr)
                {
                    result.state.sgpr[scalarDestination->value] = condition ? 0xFFFFFFFFu : 0u;
                    break;
                }
            }
            result.state.vcc = condition;
            if (instruction.execMask)
                result.state.exec = result.state.exec && condition;
            break;
        }
        case Op::V_CndmaskB32:
        case Op::V_CndmaskB16:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-070", "invalid cndmask operands", pc));
            bool condition = result.state.vcc;
            if (instruction.encoding == Encoding::Vop3 && instruction.operandCount >= 4)
            {
                const auto mask = source(3);
                if (!mask)
                    return std::unexpected(Error("SR-GFX-ISA-071", "invalid cndmask mask", pc));
                condition = (*mask & 1u) != 0;
            }
            const std::uint32_t resultValue =
                instruction.op == Op::V_CndmaskB16
                    ? ((condition ? (*left & 0xFFFFu) : (*right & 0xFFFFu)) |
                       (condition ? (*left & 0xFFFF0000u) : (*right & 0xFFFF0000u)))
                    : (condition ? *left : *right);
            if (!write(resultValue))
                return std::unexpected(Error("SR-GFX-ISA-072", "invalid cndmask destination", pc));
            break;
        }
        case Op::V_BfeU32:
        case Op::V_BfeI32:
        {
            const auto value = source(1);
            const auto offsetValue = source(2);
            const auto widthValue = source(3);
            if (!value || !offsetValue || !widthValue)
                return std::unexpected(Error("SR-GFX-ISA-082", "invalid v_bfe operands", pc));
            const std::uint32_t offset = *offsetValue & 31u;
            const std::uint32_t width = *widthValue & 0x7Fu;
            const std::uint32_t mask = BitMask(width);
            std::uint32_t extracted = (*value >> offset) & mask;
            if (instruction.op == Op::V_BfeI32 && width != 0 && width < 32)
            {
                const std::uint32_t shift = 32u - width;
                extracted = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(extracted << shift) >> shift);
            }
            if (!write(extracted))
                return std::unexpected(Error("SR-GFX-ISA-083", "invalid v_bfe destination", pc));
            break;
        }
        case Op::V_BfiB32:
        {
            const auto mask = source(1);
            const auto value = source(2);
            const auto base = source(3);
            if (!mask || !value || !base)
                return std::unexpected(Error("SR-GFX-ISA-084", "invalid v_bfi operands", pc));
            if (!write((*base & ~*mask) | (*value & *mask)))
                return std::unexpected(Error("SR-GFX-ISA-085", "invalid v_bfi destination", pc));
            break;
        }
        case Op::V_BfmB32:
        {
            const auto offset = source(1);
            const auto size = source(2);
            if (!offset || !size)
                return std::unexpected(Error("SR-GFX-ISA-086", "invalid v_bfm operands", pc));
            if (!write(BitMask(*size) << (*offset & 31u)))
                return std::unexpected(Error("SR-GFX-ISA-087", "invalid v_bfm destination", pc));
            break;
        }
        case Op::V_AlignbitB32:
        case Op::V_AlignbyteB32:
        {
            const auto high = source(1);
            const auto low = source(2);
            const auto shiftValue = source(3);
            if (!high || !low || !shiftValue)
                return std::unexpected(Error("SR-GFX-ISA-088", "invalid v_align operands", pc));
            const std::uint32_t shift = instruction.op == Op::V_AlignbyteB32 ? ((*shiftValue & 7u) * 8u)
                                                                             : (*shiftValue & 31u);
            const std::uint64_t value = (static_cast<std::uint64_t>(*high) << 32) |
                                        static_cast<std::uint64_t>(*low);
            if (!write(static_cast<std::uint32_t>(value >> shift)))
                return std::unexpected(Error("SR-GFX-ISA-089", "invalid v_align destination", pc));
            break;
        }
        case Op::GlobalStoreB32:
        {
            const auto index = BufferIndex(result.state, instruction, 0);
            const auto value = ReadOperand(result.state, instruction.operands[1]);
            if (!index || !value)
                return std::unexpected(Error("SR-GFX-ISA-071", "invalid global store operands", pc));
            result.state.buffer[*index] = *value;
            break;
        }
        case Op::S_LoadB32:
        case Op::S_LoadB64:
        case Op::S_LoadB96:
        case Op::S_LoadB128:
        case Op::S_LoadB256:
        case Op::S_LoadB512:
        case Op::S_LoadI8:
        case Op::S_LoadU8:
        case Op::S_LoadI16:
        case Op::S_LoadU16:
        case Op::S_BufferLoadB32:
        case Op::S_BufferLoadB64:
        case Op::S_BufferLoadB96:
        case Op::S_BufferLoadB128:
        case Op::S_BufferLoadB256:
        case Op::S_BufferLoadB512:
        case Op::S_BufferLoadI8:
        case Op::S_BufferLoadU8:
        case Op::S_BufferLoadI16:
        case Op::S_BufferLoadU16:
        case Op::S_DcacheInv:
        case Op::S_Gl1Inv:
        case Op::S_AtcProbe:
        case Op::S_AtcProbeBuffer:
        case Op::S_PrefetchInst:
        case Op::S_PrefetchInstPcRel:
        case Op::S_PrefetchData:
        case Op::S_BufferPrefetchData:
        case Op::S_PrefetchDataPcRel:
        {
            const auto status = ExecuteScalarMemory(result.state, instruction, pc);
            if (!status)
                return std::unexpected(status.error());
            break;
        }
        case Op::S_CmpEqU64:
        case Op::S_CmpLgU64:
        {
            const auto left = ReadPair(result.state, instruction.operands[0]);
            const auto right = ReadPair(result.state, instruction.operands[1]);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-240", "invalid 64-bit scalar compare", pc));
            result.state.scc = instruction.op == Op::S_CmpEqU64 ? *left == *right : *left != *right;
            break;
        }
        case Op::S_Bitcmp0B32:
        case Op::S_Bitcmp1B32:
        {
            const auto value = source(0);
            const auto index = source(1);
            if (!value || !index)
                return std::unexpected(Error("SR-GFX-ISA-241", "invalid bit compare", pc));
            const bool bit = ((*value >> (*index & 31u)) & 1u) != 0;
            result.state.scc = instruction.op == Op::S_Bitcmp1B32 ? bit : !bit;
            break;
        }
        case Op::S_Bitcmp0B64:
        case Op::S_Bitcmp1B64:
        {
            const auto value = ReadPair(result.state, instruction.operands[0]);
            const auto index = source(1);
            if (!value || !index)
                return std::unexpected(Error("SR-GFX-ISA-242", "invalid 64-bit bit compare", pc));
            const bool bit = ((*value >> (*index & 63u)) & 1u) != 0;
            result.state.scc = instruction.op == Op::S_Bitcmp1B64 ? bit : !bit;
            break;
        }
        case Op::S_CmpLtF32:
        case Op::S_CmpEqF32:
        case Op::S_CmpLeF32:
        case Op::S_CmpGtF32:
        case Op::S_CmpLgF32:
        case Op::S_CmpGeF32:
        case Op::S_CmpOF32:
        case Op::S_CmpUF32:
        case Op::S_CmpNgeF32:
        case Op::S_CmpNlgF32:
        case Op::S_CmpNgtF32:
        case Op::S_CmpNleF32:
        case Op::S_CmpNeqF32:
        case Op::S_CmpNltF32:
        {
            const auto left = source(0);
            const auto right = source(1);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-243", "invalid float compare", pc));
            result.state.scc = CompareScalarFloat(instruction.op, AsFloat(*left), AsFloat(*right));
            break;
        }
        case Op::S_CmpLtF16:
        case Op::S_CmpEqF16:
        case Op::S_CmpLeF16:
        case Op::S_CmpGtF16:
        case Op::S_CmpLgF16:
        case Op::S_CmpGeF16:
        case Op::S_CmpOF16:
        case Op::S_CmpUF16:
        case Op::S_CmpNgeF16:
        case Op::S_CmpNlgF16:
        case Op::S_CmpNgtF16:
        case Op::S_CmpNleF16:
        case Op::S_CmpNeqF16:
        case Op::S_CmpNltF16:
        {
            const auto left = source(0);
            const auto right = source(1);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-244", "invalid half compare", pc));
            result.state.scc = CompareScalarFloat(
                instruction.op, HalfToFloat(static_cast<std::uint16_t>(*left)),
                HalfToFloat(static_cast<std::uint16_t>(*right)));
            break;
        }
        case Op::S_AddF32:
        case Op::S_SubF32:
        case Op::S_MulF32:
        case Op::S_MinNumF32:
        case Op::S_MaxNumF32:
        case Op::S_MinimumF32:
        case Op::S_MaximumF32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-245", "invalid scalar float operands", pc));
            const float a = AsFloat(*left);
            const float b = AsFloat(*right);
            float value = 0.0f;
            switch (instruction.op)
            {
            case Op::S_AddF32:
                value = a + b;
                break;
            case Op::S_SubF32:
                value = a - b;
                break;
            case Op::S_MulF32:
                value = a * b;
                break;
            case Op::S_MinNumF32:
                value = std::fmin(a, b);
                break;
            case Op::S_MaxNumF32:
                value = std::fmax(a, b);
                break;
            case Op::S_MinimumF32:
                value = (std::isnan(a) || std::isnan(b)) ? std::numeric_limits<float>::quiet_NaN()
                                                         : std::fmin(a, b);
                break;
            default:
                value = (std::isnan(a) || std::isnan(b)) ? std::numeric_limits<float>::quiet_NaN()
                                                         : std::fmax(a, b);
                break;
            }
            if (!write(AsBits(value)))
                return std::unexpected(Error("SR-GFX-ISA-246", "invalid scalar float destination", pc));
            break;
        }
        case Op::S_AddF16:
        case Op::S_SubF16:
        case Op::S_MulF16:
        case Op::S_MinNumF16:
        case Op::S_MaxNumF16:
        case Op::S_MinimumF16:
        case Op::S_MaximumF16:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-247", "invalid scalar half operands", pc));
            const float a = HalfToFloat(static_cast<std::uint16_t>(*left));
            const float b = HalfToFloat(static_cast<std::uint16_t>(*right));
            float value = 0.0f;
            switch (instruction.op)
            {
            case Op::S_AddF16:
                value = a + b;
                break;
            case Op::S_SubF16:
                value = a - b;
                break;
            case Op::S_MulF16:
                value = a * b;
                break;
            case Op::S_MinNumF16:
                value = std::fmin(a, b);
                break;
            case Op::S_MaxNumF16:
                value = std::fmax(a, b);
                break;
            case Op::S_MinimumF16:
                value = (std::isnan(a) || std::isnan(b)) ? std::numeric_limits<float>::quiet_NaN()
                                                         : std::fmin(a, b);
                break;
            default:
                value = (std::isnan(a) || std::isnan(b)) ? std::numeric_limits<float>::quiet_NaN()
                                                         : std::fmax(a, b);
                break;
            }
            if (!write(static_cast<std::uint32_t>(FloatToHalf(value))))
                return std::unexpected(Error("SR-GFX-ISA-248", "invalid scalar half destination", pc));
            break;
        }
        case Op::S_FmacF32:
        {
            const auto current = ReadOperand(result.state, instruction.operands[0]);
            const auto left = source(1);
            const auto right = source(2);
            if (!current || !left || !right)
                return std::unexpected(Error("SR-GFX-ISA-249", "invalid scalar fmac operands", pc));
            const float value = std::fma(AsFloat(*left), AsFloat(*right), AsFloat(*current));
            if (!write(AsBits(value)))
                return std::unexpected(Error("SR-GFX-ISA-250", "invalid scalar fmac destination", pc));
            break;
        }
        case Op::S_FmacF16:
        {
            const auto current = ReadOperand(result.state, instruction.operands[0]);
            const auto left = source(1);
            const auto right = source(2);
            if (!current || !left || !right)
                return std::unexpected(Error("SR-GFX-ISA-251", "invalid scalar half fmac", pc));
            const float value = std::fma(HalfToFloat(static_cast<std::uint16_t>(*left)),
                                         HalfToFloat(static_cast<std::uint16_t>(*right)),
                                         HalfToFloat(static_cast<std::uint16_t>(*current)));
            if (!write(static_cast<std::uint32_t>(FloatToHalf(value))))
                return std::unexpected(Error("SR-GFX-ISA-252", "invalid scalar half fmac destination", pc));
            break;
        }
        case Op::S_PackLlB32B16:
        case Op::S_PackLhB32B16:
        case Op::S_PackHhB32B16:
        case Op::S_PackHlB32B16:
        {
            const auto first = source(1);
            const auto second = source(2);
            if (!first || !second)
                return std::unexpected(Error("SR-GFX-ISA-253", "invalid scalar pack operands", pc));
            const bool firstHigh = instruction.op == Op::S_PackHhB32B16 ||
                                   instruction.op == Op::S_PackHlB32B16;
            const bool secondHigh = instruction.op == Op::S_PackHhB32B16 ||
                                    instruction.op == Op::S_PackLhB32B16;
            const std::uint32_t low = firstHigh ? (*first >> 16) : (*first & 0xFFFFu);
            const std::uint32_t high = secondHigh ? (*second >> 16) : (*second & 0xFFFFu);
            if (!write((high << 16) | low))
                return std::unexpected(Error("SR-GFX-ISA-254", "invalid scalar pack destination", pc));
            break;
        }
        case Op::S_Lshl1AddU32:
        case Op::S_Lshl2AddU32:
        case Op::S_Lshl3AddU32:
        case Op::S_Lshl4AddU32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-255", "invalid lshl-add operands", pc));
            const std::uint32_t shift = instruction.op == Op::S_Lshl1AddU32   ? 1u
                                        : instruction.op == Op::S_Lshl2AddU32 ? 2u
                                        : instruction.op == Op::S_Lshl3AddU32 ? 3u
                                                                              : 4u;
            const std::uint32_t shifted = *left << shift;
            const std::uint32_t sum = shifted + *right;
            result.state.scc = sum < shifted;
            if (!write(sum))
                return std::unexpected(Error("SR-GFX-ISA-256", "invalid lshl-add destination", pc));
            break;
        }
        case Op::S_AndNot1B64:
        case Op::S_OrNot1B64:
        case Op::S_NandB64:
        case Op::S_NorB64:
        case Op::S_XnorB64:
        {
            const auto left = ReadPair(result.state, instruction.operands[1]);
            const auto right = ReadPair(result.state, instruction.operands[2]);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-257", "invalid 64-bit scalar bitwise", pc));
            std::uint64_t value = 0;
            switch (instruction.op)
            {
            case Op::S_AndNot1B64:
                value = *left & ~*right;
                break;
            case Op::S_OrNot1B64:
                value = *left | ~*right;
                break;
            case Op::S_NandB64:
                value = ~(*left & *right);
                break;
            case Op::S_NorB64:
                value = ~(*left | *right);
                break;
            default:
                value = ~(*left ^ *right);
                break;
            }
            if (!WritePair(result.state, instruction.operands[0], value))
                return std::unexpected(Error("SR-GFX-ISA-258", "invalid 64-bit scalar destination", pc));
            break;
        }
        case Op::S_AddNcU64:
        case Op::S_SubNcU64:
        case Op::S_MulU64:
        {
            const auto left = ReadPair(result.state, instruction.operands[1]);
            const auto right = ReadPair(result.state, instruction.operands[2]);
            if (!left || !right)
                return std::unexpected(Error("SR-GFX-ISA-259", "invalid 64-bit arithmetic", pc));
            const std::uint64_t value = instruction.op == Op::S_AddNcU64   ? *left + *right
                                        : instruction.op == Op::S_SubNcU64 ? *left - *right
                                                                           : *left * *right;
            if (!WritePair(result.state, instruction.operands[0], value))
                return std::unexpected(Error("SR-GFX-ISA-260", "invalid 64-bit destination", pc));
            break;
        }
        case Op::S_CmovB32:
        {
            if (result.state.scc)
            {
                const auto source_ = source(1);
                if (!source_ || !write(*source_))
                    return std::unexpected(Error("SR-GFX-ISA-261", "invalid scalar cmov", pc));
            }
            break;
        }
        case Op::S_CmovB64:
        {
            if (result.state.scc)
            {
                const auto source_ = ReadPair(result.state, instruction.operands[1]);
                if (!source_ || !WritePair(result.state, instruction.operands[0], *source_))
                    return std::unexpected(Error("SR-GFX-ISA-262", "invalid 64-bit scalar cmov", pc));
            }
            break;
        }
        case Op::S_BitreplicateB64B32:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-263", "invalid bitreplicate operand", pc));
            std::uint64_t spread = *source_;
            spread = (spread | (spread << 16)) & 0x0000FFFF0000FFFFull;
            spread = (spread | (spread << 8)) & 0x00FF00FF00FF00FFull;
            spread = (spread | (spread << 4)) & 0x0F0F0F0F0F0F0F0Full;
            spread = (spread | (spread << 2)) & 0x3333333333333333ull;
            spread = (spread | (spread << 1)) & 0x5555555555555555ull;
            const std::uint64_t value = spread | (spread << 1);
            if (!WritePair(result.state, instruction.operands[0], value))
                return std::unexpected(Error("SR-GFX-ISA-264", "invalid bitreplicate destination", pc));
            break;
        }
        case Op::S_QuadmaskB32:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-265", "invalid quadmask operand", pc));
            std::uint32_t any = *source_;
            any |= any >> 1;
            any |= any >> 2;
            std::uint32_t mask = 0;
            for (std::uint32_t i = 0; i < 8; ++i)
                mask |= (any >> (3u * i)) & (1u << i);
            result.state.scc = mask != 0;
            if (!write(mask))
                return std::unexpected(Error("SR-GFX-ISA-266", "invalid quadmask destination", pc));
            break;
        }
        case Op::S_QuadmaskB64:
        {
            const auto source_ = ReadPair(result.state, instruction.operands[1]);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-267", "invalid 64-bit quadmask operand", pc));
            std::uint64_t any = *source_;
            any |= any >> 1;
            any |= any >> 2;
            std::uint32_t mask = 0;
            for (std::uint32_t i = 0; i < 16; ++i)
                mask |= static_cast<std::uint32_t>((any >> (3u * i)) & 1u) << i;
            result.state.scc = mask != 0;
            if (!write(mask))
                return std::unexpected(Error("SR-GFX-ISA-268", "invalid quadmask destination", pc));
            break;
        }
        case Op::S_CeilF32:
        case Op::S_FloorF32:
        case Op::S_TruncF32:
        case Op::S_RndneF32:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-269", "invalid scalar float round", pc));
            const float value = AsFloat(*source_);
            const float rounded = instruction.op == Op::S_CeilF32    ? std::ceil(value)
                                  : instruction.op == Op::S_FloorF32 ? std::floor(value)
                                  : instruction.op == Op::S_TruncF32 ? std::trunc(value)
                                                                     : std::nearbyint(value);
            if (!write(AsBits(rounded)))
                return std::unexpected(Error("SR-GFX-ISA-270", "invalid round destination", pc));
            break;
        }
        case Op::S_CeilF16:
        case Op::S_FloorF16:
        case Op::S_TruncF16:
        case Op::S_RndneF16:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-271", "invalid scalar half round", pc));
            const float value = HalfToFloat(static_cast<std::uint16_t>(*source_));
            const float rounded = instruction.op == Op::S_CeilF16    ? std::ceil(value)
                                  : instruction.op == Op::S_FloorF16 ? std::floor(value)
                                  : instruction.op == Op::S_TruncF16 ? std::trunc(value)
                                                                     : std::nearbyint(value);
            if (!write(static_cast<std::uint32_t>(FloatToHalf(rounded))))
                return std::unexpected(Error("SR-GFX-ISA-272", "invalid half round destination", pc));
            break;
        }
        case Op::S_CvtF32I32:
        case Op::S_CvtF32U32:
        case Op::S_CvtI32F32:
        case Op::S_CvtU32F32:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-273", "invalid scalar conversion", pc));
            std::uint32_t value = 0;
            if (instruction.op == Op::S_CvtF32I32)
                value = AsBits(static_cast<float>(AsSigned(*source_)));
            else if (instruction.op == Op::S_CvtF32U32)
                value = AsBits(static_cast<float>(*source_));
            else if (instruction.op == Op::S_CvtI32F32)
                value = static_cast<std::uint32_t>(static_cast<std::int32_t>(AsFloat(*source_)));
            else
                value = static_cast<std::uint32_t>(AsFloat(*source_));
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-274", "invalid conversion destination", pc));
            break;
        }
        case Op::S_CvtF16F32:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-275", "invalid f16 conversion", pc));
            if (!write(static_cast<std::uint32_t>(FloatToHalf(AsFloat(*source_)))))
                return std::unexpected(Error("SR-GFX-ISA-276", "invalid f16 destination", pc));
            break;
        }
        case Op::S_CvtF32F16:
        case Op::S_CvtHiF32F16:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-277", "invalid f32 conversion", pc));
            const std::uint32_t bits = instruction.op == Op::S_CvtHiF32F16 ? (*source_ >> 16)
                                                                           : (*source_ & 0xFFFFu);
            if (!write(AsBits(HalfToFloat(static_cast<std::uint16_t>(bits)))))
                return std::unexpected(Error("SR-GFX-ISA-278", "invalid f32 destination", pc));
            break;
        }
        case Op::S_Nop:
        case Op::S_Clause:
        case Op::S_DelayAlu:
        case Op::S_WaitAlu:
        case Op::S_Waitcnt:
        case Op::S_WaitIdle:
        case Op::S_WaitEvent:
        case Op::S_RoundMode:
        case Op::S_DenormMode:
        case Op::S_BarrierWait:
        case Op::S_Wakeup:
        case Op::S_Setprio:
        case Op::S_Incperflevel:
        case Op::S_Decperflevel:
        case Op::S_Ttracedata:
        case Op::S_TtracedataImm:
        case Op::S_IcacheInv:
        case Op::S_WaitLoadcnt:
        case Op::S_WaitStorecnt:
        case Op::S_WaitSamplecnt:
        case Op::S_WaitBvhcnt:
        case Op::S_WaitExpcnt:
        case Op::S_WaitDscnt:
        case Op::S_WaitKmcnt:
        case Op::S_WaitLoadcntDscnt:
        case Op::S_WaitStorecntDscnt:
            break;
        case Op::S_CodeEnd:
        case Op::S_EndpgmSaved:
            finished = true;
            advance = false;
            break;
        case Op::S_CmovkI32:
        {
            if (result.state.scc)
            {
                const std::uint32_t immediate = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(static_cast<std::int16_t>(instruction.operands[1].value & 0xFFFFu)));
                if (!write(immediate))
                    return std::unexpected(Error("SR-GFX-ISA-280", "invalid cmovk destination", pc));
            }
            break;
        }
        case Op::S_AddkCoI32:
        {
            const std::uint32_t immediate = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(static_cast<std::int16_t>(instruction.operands[1].value & 0xFFFFu)));
            const std::uint32_t current = result.state.sgpr[instruction.operands[0].value];
            const std::uint32_t sum = static_cast<std::uint32_t>(current + immediate);
            result.state.scc = sum < current;
            if (!write(sum))
                return std::unexpected(Error("SR-GFX-ISA-281", "invalid addk destination", pc));
            break;
        }
        case Op::V_CmpClassF16:
        case Op::V_CmpClassF32:
        case Op::V_CmpClassF64:
        {
            const auto wantedBits = source(2);
            if (!wantedBits)
                return std::unexpected(Error("SR-GFX-ISA-282", "invalid class mask", pc));
            std::uint32_t classes = 0;
            if (instruction.op == Op::V_CmpClassF64)
            {
                const auto pair = ReadVgprPair(result.state, instruction.operands[1]);
                if (!pair)
                    return std::unexpected(Error("SR-GFX-ISA-283", "invalid class operand", pc));
                classes = ClassMaskOf(*pair, 11, 52);
            }
            else
            {
                const auto raw = source(1);
                if (!raw)
                    return std::unexpected(Error("SR-GFX-ISA-284", "invalid class operand", pc));
                classes = instruction.op == Op::V_CmpClassF16
                              ? ClassMaskOf(*raw & 0xFFFFu, 5, 10)
                              : ClassMaskOf(*raw, 8, 23);
            }
            const bool condition = (classes & *wantedBits) != 0;
            if (instruction.encoding == Encoding::Vop3)
            {
                const Operand* scalarDestination = FindOperand(instruction, OperandKind::Sdst);
                if (scalarDestination != nullptr)
                {
                    result.state.sgpr[scalarDestination->value] = condition ? 0xFFFFFFFFu : 0u;
                    break;
                }
            }
            result.state.vcc = condition;
            if (instruction.execMask)
                result.state.exec = result.state.exec && condition;
            break;
        }
        case Op::V_SwapB32:
        case Op::V_SwapB16:
        {
            if (instruction.operandCount < 2 ||
                (instruction.operands[1].kind != OperandKind::Vgpr &&
                 instruction.operands[1].kind != OperandKind::Vsrc))
                return std::unexpected(Error("SR-GFX-ISA-285", "invalid swap source", pc));
            const Operand* sourceOperand = &instruction.operands[1];
            std::uint32_t& destinationValue = result.state.vgpr[instruction.operands[0].value];
            std::uint32_t& sourceValue = result.state.vgpr[sourceOperand->value];
            if (instruction.op == Op::V_SwapB16)
            {
                const std::uint32_t destinationLow = destinationValue & 0xFFFFu;
                const std::uint32_t sourceLow = sourceValue & 0xFFFFu;
                destinationValue = (destinationValue & 0xFFFF0000u) | sourceLow;
                sourceValue = (sourceValue & 0xFFFF0000u) | destinationLow;
            }
            else
            {
                std::swap(destinationValue, sourceValue);
            }
            break;
        }
        case Op::V_SatPkU8I16:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-286", "invalid sat_pk operand", pc));
            const auto saturate = [](std::int32_t value) -> std::uint32_t {
                if (value < 0)
                    return 0;
                if (value > 255)
                    return 255;
                return static_cast<std::uint32_t>(value);
            };
            const std::uint32_t low = saturate(static_cast<std::int16_t>(*source_ & 0xFFFFu));
            const std::uint32_t high = saturate(static_cast<std::int16_t>(*source_ >> 16));
            if (!write((high << 8) | low))
                return std::unexpected(Error("SR-GFX-ISA-287", "invalid sat_pk destination", pc));
            break;
        }
        case Op::V_Pipeflush:
            break;
        case Op::V_AddNcU16:
        case Op::V_SubNcU16:
        case Op::V_MulLoU16:
        case Op::V_AddNcI16:
        case Op::V_SubNcI16:
        case Op::V_MinU16:
        case Op::V_MaxU16:
        case Op::V_MinI16:
        case Op::V_MaxI16:
        {
            const auto first = source(1);
            const auto second = source(2);
            if (!first || !second)
                return std::unexpected(Error("SR-GFX-ISA-290", "invalid 16-bit operands", pc));
            const std::uint32_t a = *first & 0xFFFFu;
            const std::uint32_t b = *second & 0xFFFFu;
            const std::int16_t sa = static_cast<std::int16_t>(a);
            const std::int16_t sb = static_cast<std::int16_t>(b);
            std::uint32_t result = 0;
            switch (instruction.op)
            {
            case Op::V_AddNcU16:
            case Op::V_AddNcI16:
                result = (a + b) & 0xFFFFu;
                break;
            case Op::V_SubNcU16:
            case Op::V_SubNcI16:
                result = (a - b) & 0xFFFFu;
                break;
            case Op::V_MulLoU16:
                result = (a * b) & 0xFFFFu;
                break;
            case Op::V_MinU16:
                result = std::min(a, b);
                break;
            case Op::V_MaxU16:
                result = std::max(a, b);
                break;
            case Op::V_MinI16:
                result = static_cast<std::uint32_t>(std::min(sa, sb)) & 0xFFFFu;
                break;
            default:
                result = static_cast<std::uint32_t>(std::max(sa, sb)) & 0xFFFFu;
                break;
            }
            if (!write(result))
                return std::unexpected(Error("SR-GFX-ISA-291", "invalid 16-bit destination", pc));
            break;
        }
        case Op::V_LshlrevB16:
        case Op::V_LshrrevB16:
        case Op::V_AshrrevI16:
        {
            const auto count = source(1);
            const auto value = source(2);
            if (!count || !value)
                return std::unexpected(Error("SR-GFX-ISA-292", "invalid 16-bit shift", pc));
            const std::uint32_t shift = *count & 15u;
            std::uint32_t result = 0;
            if (instruction.op == Op::V_LshlrevB16)
                result = (*value << shift) & 0xFFFFu;
            else if (instruction.op == Op::V_LshrrevB16)
                result = (*value & 0xFFFFu) >> shift;
            else
                result = static_cast<std::uint32_t>(static_cast<std::int16_t>(*value) >> shift) & 0xFFFFu;
            if (!write(result))
                return std::unexpected(Error("SR-GFX-ISA-293", "invalid 16-bit shift destination", pc));
            break;
        }
        case Op::V_MadU16:
        case Op::V_MadI16:
        case Op::V_MadU32U16:
        case Op::V_MadI32I16:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-294", "invalid mad operands", pc));
            const bool isSigned = instruction.op == Op::V_MadI16 || instruction.op == Op::V_MadI32I16;
            const bool wide = instruction.op == Op::V_MadU32U16 || instruction.op == Op::V_MadI32I16;
            const std::int64_t a = isSigned ? static_cast<std::int16_t>(*first) : (*first & 0xFFFFu);
            const std::int64_t b = isSigned ? static_cast<std::int16_t>(*second) : (*second & 0xFFFFu);
            const std::int64_t c = wide ? (isSigned ? AsSigned(*third) : *third) : (*third & 0xFFFFu);
            const std::uint32_t result = static_cast<std::uint32_t>(a * b + c);
            if (!write(wide ? result : (result & 0xFFFFu)))
                return std::unexpected(Error("SR-GFX-ISA-295", "invalid mad destination", pc));
            break;
        }
        case Op::V_Min3I16:
        case Op::V_Min3U16:
        case Op::V_Max3I16:
        case Op::V_Max3U16:
        case Op::V_Med3I16:
        case Op::V_Med3U16:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-296", "invalid ternary operands", pc));
            const bool isSigned = instruction.op == Op::V_Min3I16 || instruction.op == Op::V_Max3I16 ||
                                  instruction.op == Op::V_Med3I16;
            std::int32_t a = static_cast<std::int32_t>(*first & 0xFFFFu);
            std::int32_t b = static_cast<std::int32_t>(*second & 0xFFFFu);
            std::int32_t c = static_cast<std::int32_t>(*third & 0xFFFFu);
            if (isSigned)
            {
                a = static_cast<std::int16_t>(*first);
                b = static_cast<std::int16_t>(*second);
                c = static_cast<std::int16_t>(*third);
            }
            std::int32_t result = 0;
            if (instruction.op == Op::V_Min3I16 || instruction.op == Op::V_Min3U16)
                result = std::min({a, b, c});
            else if (instruction.op == Op::V_Max3I16 || instruction.op == Op::V_Max3U16)
                result = std::max({a, b, c});
            else
                result = std::max(std::min(a, b), std::min(std::max(a, b), c));
            if (!write(static_cast<std::uint32_t>(result) & 0xFFFFu))
                return std::unexpected(Error("SR-GFX-ISA-297", "invalid ternary destination", pc));
            break;
        }
        case Op::V_MinmaxU32:
        case Op::V_MaxminU32:
        case Op::V_MinmaxI32:
        case Op::V_MaxminI32:
        case Op::V_MinimummaximumF32:
        case Op::V_MaximumminimumF32:
        case Op::V_MinmaxNumF32:
        case Op::V_MaxminNumF32:
        case Op::V_Minimum3F32:
        case Op::V_Maximum3F32:
        case Op::V_Min3NumF16:
        case Op::V_Max3NumF16:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-298", "invalid minmax operands", pc));
            const bool isFloat = instruction.op == Op::V_MinimummaximumF32 ||
                                 instruction.op == Op::V_MaximumminimumF32 ||
                                 instruction.op == Op::V_MinmaxNumF32 ||
                                 instruction.op == Op::V_MaxminNumF32 ||
                                 instruction.op == Op::V_Minimum3F32 ||
                                 instruction.op == Op::V_Maximum3F32 ||
                                 instruction.op == Op::V_Min3NumF16 ||
                                 instruction.op == Op::V_Max3NumF16;
            const bool halfPrecision = instruction.op == Op::V_Min3NumF16 ||
                                       instruction.op == Op::V_Max3NumF16;
            const bool isSigned = instruction.op == Op::V_MinmaxI32 ||
                                  instruction.op == Op::V_MaxminI32;
            const auto toFloat = [&](std::uint32_t bits) {
                return halfPrecision ? HalfToFloat(static_cast<std::uint16_t>(bits)) : AsFloat(bits);
            };
            const auto fromFloat = [&](float value) {
                return halfPrecision ? static_cast<std::uint32_t>(FloatToHalf(value)) : AsBits(value);
            };
            std::uint32_t result = *first;
            if (isFloat)
            {
                const float a = toFloat(*first);
                const float b = toFloat(*second);
                const float c = toFloat(*third);
                float value = 0.0f;
                if (instruction.op == Op::V_Minimum3F32)
                    value = std::fmin(a, std::fmin(b, c));
                else if (instruction.op == Op::V_Maximum3F32)
                    value = std::fmax(a, std::fmax(b, c));
                else if (instruction.op == Op::V_Min3NumF16)
                    value = std::fmin(a, std::fmin(b, c));
                else if (instruction.op == Op::V_Max3NumF16)
                    value = std::fmax(a, std::fmax(b, c));
                else if (instruction.op == Op::V_MinimummaximumF32 ||
                         instruction.op == Op::V_MinmaxNumF32)
                    value = std::fmax(std::fmin(a, b), c);
                else
                    value = std::fmin(std::fmax(a, b), c);
                result = fromFloat(value);
            }
            else if (isSigned)
            {
                const std::int32_t a = AsSigned(*first);
                const std::int32_t b = AsSigned(*second);
                const std::int32_t c = AsSigned(*third);
                const std::int32_t value =
                    (instruction.op == Op::V_MinmaxI32) ? std::max(std::min(a, b), c)
                                                        : std::min(std::max(a, b), c);
                result = static_cast<std::uint32_t>(value);
            }
            else
            {
                const std::uint32_t a = *first;
                const std::uint32_t b = *second;
                const std::uint32_t c = *third;
                result = (instruction.op == Op::V_MinmaxU32) ? std::max(std::min(a, b), c)
                                                             : std::min(std::max(a, b), c);
            }
            if (!write(result))
                return std::unexpected(Error("SR-GFX-ISA-299", "invalid minmax destination", pc));
            break;
        }
        case Op::V_CvtPkI16F32:
        case Op::V_CvtPkU16F32:
        case Op::V_CvtPkI16I32:
        case Op::V_CvtPkU16U32:
        case Op::V_CvtPkNormI16F32:
        case Op::V_CvtPkNormU16F32:
        case Op::V_CvtPkRtzF16F32:
        {
            const auto first = source(1);
            const auto second = source(2);
            if (!first || !second)
                return std::unexpected(Error("SR-GFX-ISA-300", "invalid packed convert operands", pc));
            const auto convert = [&](std::uint32_t bits) -> std::uint32_t {
                switch (instruction.op)
                {
                case Op::V_CvtPkI16I32:
                {
                    const std::int32_t value = AsSigned(bits);
                    return static_cast<std::uint32_t>(ClampInt(value, -32768, 32767)) & 0xFFFFu;
                }
                case Op::V_CvtPkU16U32:
                    return std::min(bits, 0xFFFFu);
                case Op::V_CvtPkI16F32:
                    return RoundToIntegerIsa(AsFloat(bits), -32768, 32767);
                case Op::V_CvtPkU16F32:
                    return RoundToIntegerIsa(AsFloat(bits), 0, 65535);
                case Op::V_CvtPkNormI16F32:
                    return RoundToIntegerIsa(ClampFloat(AsFloat(bits), -1.0f, 1.0f) * 32767.0f, -32768, 32767);
                case Op::V_CvtPkNormU16F32:
                    return RoundToIntegerIsa(ClampFloat(AsFloat(bits), 0.0f, 1.0f) * 65535.0f, 0, 65535);
                default:
                {
                    const float source_ = AsFloat(bits);
                    std::uint32_t result = static_cast<std::uint32_t>(FloatToHalf(source_));
                    const float truncated = HalfToFloat(static_cast<std::uint16_t>(result));
                    if (std::fabs(truncated) > std::fabs(source_) && (result & 0x7FFFu) != 0)
                        --result;
                    return result & 0xFFFFu;
                }
                }
            };
            const std::uint32_t low = convert(*first);
            const std::uint32_t high = convert(*second);
            if (!write((high << 16) | (low & 0xFFFFu)))
                return std::unexpected(Error("SR-GFX-ISA-301", "invalid packed convert destination", pc));
            break;
        }
        case Op::V_PackB32F16:
        {
            const auto first = source(1);
            const auto second = source(2);
            if (!first || !second)
                return std::unexpected(Error("SR-GFX-ISA-302", "invalid pack operands", pc));
            if (!write(((*second & 0xFFFFu) << 16) | (*first & 0xFFFFu)))
                return std::unexpected(Error("SR-GFX-ISA-303", "invalid pack destination", pc));
            break;
        }
        case Op::V_SadU8:
        case Op::V_SadHiU8:
        case Op::V_MsadU8:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto addend = source(3);
            if (!first || !second || !addend)
                return std::unexpected(Error("SR-GFX-ISA-304", "invalid sad operands", pc));
            std::uint32_t sum = 0;
            for (std::uint32_t i = 0; i < 4; ++i)
            {
                const std::uint32_t a = (*first >> (8 * i)) & 0xFFu;
                const std::uint32_t b = (*second >> (8 * i)) & 0xFFu;
                if (instruction.op == Op::V_MsadU8 && b == 0)
                    continue;
                sum += a > b ? a - b : b - a;
            }
            if (instruction.op == Op::V_SadHiU8)
                sum <<= 16;
            if (!write(sum + *addend))
                return std::unexpected(Error("SR-GFX-ISA-305", "invalid sad destination", pc));
            break;
        }
        case Op::V_LerpU8:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto control = source(3);
            if (!first || !second || !control)
                return std::unexpected(Error("SR-GFX-ISA-306", "invalid lerp operands", pc));
            std::uint32_t result = 0;
            for (std::uint32_t i = 0; i < 4; ++i)
            {
                const std::uint32_t a = (*first >> (8 * i)) & 0xFFu;
                const std::uint32_t b = (*second >> (8 * i)) & 0xFFu;
                const std::uint32_t round = (*control >> (8 * i)) & 1u;
                result |= ((a + b + round) >> 1) << (8 * i);
            }
            if (!write(result))
                return std::unexpected(Error("SR-GFX-ISA-307", "invalid lerp destination", pc));
            break;
        }
        case Op::V_CubeidF32:
        case Op::V_CubemaF32:
        case Op::V_CubescF32:
        case Op::V_CubetcF32:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-308", "invalid cube operands", pc));
            const float x = AsFloat(*first);
            const float y = AsFloat(*second);
            const float z = AsFloat(*third);
            const float ax = std::fabs(x);
            const float ay = std::fabs(y);
            const float az = std::fabs(z);
            const bool xMajor = ax >= ay && ax >= az;
            const bool yMajor = !xMajor && ay >= az;
            const float major = xMajor ? x : (yMajor ? y : z);
            if (instruction.op == Op::V_CubeidF32)
            {
                const std::uint32_t id = xMajor ? (x > 0.0f ? 0u : 1u)
                                                : yMajor ? (y > 0.0f ? 2u : 3u)
                                                         : (z > 0.0f ? 4u : 5u);
                if (!write(id))
                    return std::unexpected(Error("SR-GFX-ISA-309", "invalid cubeid destination", pc));
            }
            else if (instruction.op == Op::V_CubemaF32)
            {
                if (!write(AsBits(major * 2.0f)))
                    return std::unexpected(Error("SR-GFX-ISA-310", "invalid cubema destination", pc));
            }
            else if (instruction.op == Op::V_CubescF32)
            {
                const float value = xMajor ? (x > 0.0f ? -z : z) : (yMajor ? x : (z > 0.0f ? x : -x));
                if (!write(AsBits(value)))
                    return std::unexpected(Error("SR-GFX-ISA-311", "invalid cubesc destination", pc));
            }
            else
            {
                const float value = xMajor ? -y : (yMajor ? (y > 0.0f ? z : -z) : -y);
                if (!write(AsBits(value)))
                    return std::unexpected(Error("SR-GFX-ISA-312", "invalid cubetc destination", pc));
            }
            break;
        }
        case Op::V_MullitF32:
        {
            const auto first = source(1);
            const auto second = source(2);
            if (!first || !second)
                return std::unexpected(Error("SR-GFX-ISA-313", "invalid mullit operands", pc));
            const float a = AsFloat(*first);
            const float b = AsFloat(*second);
            if (!write(AsBits((a == 0.0f || b == 0.0f) ? 0.0f : a * b)))
                return std::unexpected(Error("SR-GFX-ISA-314", "invalid mullit destination", pc));
            break;
        }
        case Op::V_FmaDx9ZeroF32:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-315", "invalid fma operands", pc));
            const float a = AsFloat(*first);
            const float b = AsFloat(*second);
            const float c = AsFloat(*third);
            const float value = (a == 0.0f || b == 0.0f) ? c : std::fma(a, b, c);
            if (!write(AsBits(value)))
                return std::unexpected(Error("SR-GFX-ISA-316", "invalid fma destination", pc));
            break;
        }
        case Op::V_DivFixupF32:
        {
            const auto quotient = source(1);
            const auto denominator = source(2);
            const auto numerator = source(3);
            if (!quotient || !denominator || !numerator)
                return std::unexpected(Error("SR-GFX-ISA-317", "invalid div_fixup operands", pc));
            const float q = AsFloat(*quotient);
            const float d = AsFloat(*denominator);
            const float n = AsFloat(*numerator);
            const float inf = std::numeric_limits<float>::infinity();
            const float nan = std::numeric_limits<float>::quiet_NaN();
            float value = q;
            if (std::isnan(d) || std::isnan(n))
                value = nan;
            else if (d == 0.0f)
                value = (n == 0.0f) ? nan : std::copysign(inf, n);
            else if (std::isinf(d) && std::isinf(n))
                value = nan;
            else if (std::isinf(d))
                value = std::copysign(0.0f, q);
            else if (std::isinf(n))
                value = std::copysign(inf, q);
            if (!write(AsBits(value)))
                return std::unexpected(Error("SR-GFX-ISA-318", "invalid div_fixup destination", pc));
            break;
        }
        case Op::V_DivFmasF32:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-319", "invalid div_fmas operands", pc));
            const float value = std::fma(AsFloat(*first), AsFloat(*second), AsFloat(*third));
            const float scaled = result.state.vcc ? value * 18446744073709551616.0f : value;
            if (!write(AsBits(scaled)))
                return std::unexpected(Error("SR-GFX-ISA-320", "invalid div_fmas destination", pc));
            break;
        }
        case Op::V_MinimumF32:
        case Op::V_MaximumF32:
        {
            const auto first = source(1);
            const auto second = source(2);
            if (!first || !second)
                return std::unexpected(Error("SR-GFX-ISA-330", "invalid min/max operands", pc));
            const float a = AsFloat(*first);
            const float b = AsFloat(*second);
            const bool maximum = instruction.op == Op::V_MaximumF32;
            const float value =
                (std::isnan(a) || std::isnan(b))
                    ? std::numeric_limits<float>::quiet_NaN()
                    : (maximum ? std::fmax(a, b) : std::fmin(a, b));
            if (!write(AsBits(value)))
                return std::unexpected(Error("SR-GFX-ISA-331", "invalid min/max destination", pc));
            break;
        }
        case Op::V_Med3NumF32:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-332", "invalid med3 operands", pc));
            const float a = AsFloat(*first);
            const float b = AsFloat(*second);
            const float c = AsFloat(*third);
            const float value = std::fmax(std::fmin(a, b), std::fmin(std::fmax(a, b), c));
            if (!write(AsBits(value)))
                return std::unexpected(Error("SR-GFX-ISA-333", "invalid med3 destination", pc));
            break;
        }
        case Op::V_Dot2F32F16:
        case Op::V_Dot2F32Bf16:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto accumulator = source(3);
            if (!first || !second || !accumulator)
                return std::unexpected(Error("SR-GFX-ISA-340", "invalid dot2 operands", pc));
            const bool bfloat = instruction.op == Op::V_Dot2F32Bf16;
            const auto lane = [&](std::uint32_t packed, std::uint32_t shift) {
                const std::uint32_t bits = (packed >> shift) & 0xFFFFu;
                return bfloat ? std::bit_cast<float>(bits << 16)
                              : HalfToFloat(static_cast<std::uint16_t>(bits));
            };
            const float value = AsFloat(*accumulator) + lane(*first, 0) * lane(*second, 0) +
                                lane(*first, 16) * lane(*second, 16);
            if (!write(AsBits(value)))
                return std::unexpected(Error("SR-GFX-ISA-341", "invalid dot2 destination", pc));
            break;
        }
        case Op::V_Dot4I32Iu8:
        case Op::V_Dot4U32U8:
        case Op::V_Dot8I32Iu4:
        case Op::V_Dot8U32U4:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto accumulator = source(3);
            if (!first || !second || !accumulator)
                return std::unexpected(Error("SR-GFX-ISA-342", "invalid dot operands", pc));
            const bool nibbles = instruction.op == Op::V_Dot8I32Iu4 || instruction.op == Op::V_Dot8U32U4;
            const bool signedFirst =
                instruction.op == Op::V_Dot4I32Iu8 || instruction.op == Op::V_Dot8I32Iu4;
            const std::uint32_t elements = nibbles ? 8u : 4u;
            const std::uint32_t bitsPerElement = nibbles ? 4u : 8u;
            const std::uint32_t mask = (1u << bitsPerElement) - 1u;
            const auto widen = [&](std::uint32_t value, bool isSigned) -> std::int32_t {
                if (!isSigned)
                    return static_cast<std::int32_t>(value);
                const std::uint32_t signBit = 1u << (bitsPerElement - 1u);
                if ((value & signBit) == 0)
                    return static_cast<std::int32_t>(value);
                return static_cast<std::int32_t>(value) - static_cast<std::int32_t>(1u << bitsPerElement);
            };
            std::int64_t sum = static_cast<std::int64_t>(AsSigned(*accumulator));
            for (std::uint32_t i = 0; i < elements; ++i)
            {
                const std::uint32_t a = (*first >> (bitsPerElement * i)) & mask;
                const std::uint32_t b = (*second >> (bitsPerElement * i)) & mask;
                sum += static_cast<std::int64_t>(widen(a, signedFirst)) *
                       static_cast<std::int64_t>(widen(b, false));
            }
            if (!write(static_cast<std::uint32_t>(sum)))
                return std::unexpected(Error("SR-GFX-ISA-343", "invalid dot destination", pc));
            break;
        }
        case Op::V_FmaMixF32:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            if (!first || !second || !third)
                return std::unexpected(Error("SR-GFX-ISA-344", "invalid fma_mix operands", pc));
            const std::uint32_t firstShift = (instruction.opSel & 1u) * 16u;
            const std::uint32_t secondShift = ((instruction.opSel >> 1) & 1u) * 16u;
            const float a = HalfToFloat(static_cast<std::uint16_t>(*first >> firstShift));
            const float b = HalfToFloat(static_cast<std::uint16_t>(*second >> secondShift));
            if (!write(AsBits(std::fma(a, b, AsFloat(*third)))))
                return std::unexpected(Error("SR-GFX-ISA-345", "invalid fma_mix destination", pc));
            break;
        }
        case Op::V_FmaMixloF16:
        case Op::V_FmaMixhiF16:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto third = source(3);
            const auto current = ReadOperand(result.state, instruction.operands[0]);
            if (!first || !second || !third || !current)
                return std::unexpected(Error("SR-GFX-ISA-346", "invalid fma_mix f16 operands", pc));
            const bool high = instruction.op == Op::V_FmaMixhiF16;
            const std::uint32_t firstShift = (instruction.opSel & 1u) * 16u;
            const std::uint32_t secondShift = ((instruction.opSel >> 1) & 1u) * 16u;
            const float a = HalfToFloat(static_cast<std::uint16_t>(*first >> firstShift));
            const float b = HalfToFloat(static_cast<std::uint16_t>(*second >> secondShift));
            const std::uint32_t accumulatorBits =
                high ? (*third >> 16) : (*third & 0xFFFFu);
            const float value =
                std::fma(a, b, HalfToFloat(static_cast<std::uint16_t>(accumulatorBits)));
            const std::uint32_t bits = static_cast<std::uint32_t>(FloatToHalf(value));
            const std::uint32_t merged =
                high ? ((*current & 0xFFFFu) | (bits << 16)) : ((*current & 0xFFFF0000u) | bits);
            if (!write(merged))
                return std::unexpected(Error("SR-GFX-ISA-347", "invalid fma_mix f16 destination", pc));
            break;
        }
        case Op::V_CvtF32Fp8:
        case Op::V_CvtF32Bf8:
        case Op::V_CvtPkF32Fp8:
        case Op::V_CvtPkF32Bf8:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-350", "invalid fp8 conversion", pc));
            const bool bf8 = instruction.op == Op::V_CvtF32Bf8 || instruction.op == Op::V_CvtPkF32Bf8;
            const bool packed = instruction.op == Op::V_CvtPkF32Fp8 ||
                                instruction.op == Op::V_CvtPkF32Bf8;
            const auto decode = [&](std::uint32_t byte) {
                const std::uint32_t mantissaBits = bf8 ? 2u : 3u;
                const std::uint32_t exponentBits = bf8 ? 5u : 4u;
                const std::uint32_t bias = bf8 ? 15u : 7u;
                const std::uint32_t exponentMax = (1u << exponentBits) - 1u;
                const std::uint32_t mantissaMask = (1u << mantissaBits) - 1u;
                const bool negative = ((byte >> 7) & 1u) != 0;
                const std::uint32_t exponent = (byte >> mantissaBits) & exponentMax;
                const std::uint32_t mantissa = byte & mantissaMask;
                float value = 0.0f;
                if (exponent == 0 && mantissa == 0)
                    value = 0.0f;
                else if (bf8 && exponent == exponentMax)
                    value = mantissa == 0 ? std::numeric_limits<float>::infinity()
                                          : std::numeric_limits<float>::quiet_NaN();
                else if (!bf8 && exponent == exponentMax && mantissa == mantissaMask)
                    value = std::numeric_limits<float>::quiet_NaN();
                else
                {
                    const int unbiased = exponent == 0 ? 1 - static_cast<int>(bias)
                                                       : static_cast<int>(exponent) - static_cast<int>(bias);
                    const float significand =
                        exponent == 0
                            ? static_cast<float>(mantissa) /
                                  static_cast<float>(1u << mantissaBits)
                            : 1.0f + static_cast<float>(mantissa) /
                                         static_cast<float>(1u << mantissaBits);
                    value = std::ldexp(significand, unbiased);
                }
                return negative ? -value : value;
            };
            if (!packed)
            {
                if (!write(AsBits(decode(*source_ & 0xFFu))))
                    return std::unexpected(Error("SR-GFX-ISA-351", "invalid fp8 destination", pc));
            }
            else
            {
                const std::uint32_t destination = instruction.operands[0].value;
                result.state.vgpr[destination] = AsBits(decode(*source_ & 0xFFu));
                result.state.vgpr[destination + 1] = AsBits(decode((*source_ >> 8) & 0xFFu));
            }
            break;
        }
        case Op::V_Dot4F32Fp8Fp8:
        case Op::V_Dot4F32Bf8Bf8:
        case Op::V_Dot4F32Fp8Bf8:
        case Op::V_Dot4F32Bf8Fp8:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto accumulator = source(3);
            if (!first || !second || !accumulator)
                return std::unexpected(Error("SR-GFX-ISA-352", "invalid fp8 dot operands", pc));
            const bool firstBf8 = instruction.op == Op::V_Dot4F32Bf8Bf8 ||
                                  instruction.op == Op::V_Dot4F32Bf8Fp8;
            const bool secondBf8 = instruction.op == Op::V_Dot4F32Bf8Bf8 ||
                                   instruction.op == Op::V_Dot4F32Fp8Bf8;
            const auto decode = [&](std::uint32_t byte, bool bf8) {
                const std::uint32_t mantissaBits = bf8 ? 2u : 3u;
                const std::uint32_t exponentBits = bf8 ? 5u : 4u;
                const std::uint32_t bias = bf8 ? 15u : 7u;
                const std::uint32_t exponentMax = (1u << exponentBits) - 1u;
                const std::uint32_t mantissaMask = (1u << mantissaBits) - 1u;
                const bool negative = ((byte >> 7) & 1u) != 0;
                const std::uint32_t exponent = (byte >> mantissaBits) & exponentMax;
                const std::uint32_t mantissa = byte & mantissaMask;
                float value = 0.0f;
                if (exponent == 0 && mantissa == 0)
                    value = 0.0f;
                else if (bf8 && exponent == exponentMax)
                    value = mantissa == 0 ? std::numeric_limits<float>::infinity()
                                          : std::numeric_limits<float>::quiet_NaN();
                else if (!bf8 && exponent == exponentMax && mantissa == mantissaMask)
                    value = std::numeric_limits<float>::quiet_NaN();
                else
                {
                    const int unbiased = exponent == 0 ? 1 - static_cast<int>(bias)
                                                       : static_cast<int>(exponent) - static_cast<int>(bias);
                    const float significand =
                        exponent == 0
                            ? static_cast<float>(mantissa) /
                                  static_cast<float>(1u << mantissaBits)
                            : 1.0f + static_cast<float>(mantissa) /
                                         static_cast<float>(1u << mantissaBits);
                    value = std::ldexp(significand, unbiased);
                }
                return negative ? -value : value;
            };
            float sum = AsFloat(*accumulator);
            for (std::uint32_t i = 0; i < 4; ++i)
            {
                const std::uint32_t a = (*first >> (8 * i)) & 0xFFu;
                const std::uint32_t b = (*second >> (8 * i)) & 0xFFu;
                sum += decode(a, firstBf8) * decode(b, secondBf8);
            }
            if (!write(AsBits(sum)))
                return std::unexpected(Error("SR-GFX-ISA-353", "invalid fp8 dot destination", pc));
            break;
        }
        case Op::V_CvtPkU8F32:
        {
            const auto value = source(1);
            const auto select = source(2);
            const auto base = source(3);
            if (!value || !select || !base)
                return std::unexpected(Error("SR-GFX-ISA-360", "invalid byte pack operands", pc));
            const std::uint32_t byte = RoundToIntegerIsa(AsFloat(*value), 0, 255);
            const std::uint32_t shift = (*select & 3u) * 8u;
            const std::uint32_t mask = 0xFFu << shift;
            if (!write((*base & ~mask) | (byte << shift)))
                return std::unexpected(Error("SR-GFX-ISA-361", "invalid byte pack destination", pc));
            break;
        }
        case Op::V_SadU16:
        {
            const auto first = source(1);
            const auto second = source(2);
            const auto addend = source(3);
            if (!first || !second || !addend)
                return std::unexpected(Error("SR-GFX-ISA-362", "invalid sad_u16 operands", pc));
            std::uint32_t sum = 0;
            for (std::uint32_t i = 0; i < 2; ++i)
            {
                const std::uint32_t a = (*first >> (16 * i)) & 0xFFFFu;
                const std::uint32_t b = (*second >> (16 * i)) & 0xFFFFu;
                sum += a > b ? a - b : b - a;
            }
            if (!write(sum + *addend))
                return std::unexpected(Error("SR-GFX-ISA-363", "invalid sad_u16 destination", pc));
            break;
        }
        case Op::V_LshrrevB64:
        case Op::V_AshrrevI64:
        {
            if (instruction.operands[1].kind != OperandKind::Vgpr ||
                instruction.operands[2].kind != OperandKind::Vgpr)
                return std::unexpected(Error("SR-GFX-ISA-364", "invalid 64-bit shift operands", pc));
            const std::uint32_t count = result.state.vgpr[instruction.operands[1].value];
            const std::uint32_t lowIndex = instruction.operands[2].value;
            const std::uint64_t value = static_cast<std::uint64_t>(result.state.vgpr[lowIndex]) |
                                        (static_cast<std::uint64_t>(result.state.vgpr[lowIndex + 1])
                                         << 32);
            const std::uint64_t shifted =
                instruction.op == Op::V_AshrrevI64
                    ? static_cast<std::uint64_t>(static_cast<std::int64_t>(value) >> (count & 63u))
                    : (value >> (count & 63u));
            const std::uint32_t destination = instruction.operands[0].value;
            result.state.vgpr[destination] = static_cast<std::uint32_t>(shifted);
            result.state.vgpr[destination + 1] = static_cast<std::uint32_t>(shifted >> 32);
            break;
        }
        case Op::V_FrexpExpI32F32:
        case Op::V_FrexpMantF32:
        case Op::V_FrexpExpI16F16:
        case Op::V_FrexpMantF16:
        {
            const auto source_ = source(1);
            if (!source_)
                return std::unexpected(Error("SR-GFX-ISA-370", "invalid frexp operand", pc));
            const bool half = instruction.op == Op::V_FrexpExpI16F16 ||
                              instruction.op == Op::V_FrexpMantF16;
            const bool mantissa = instruction.op == Op::V_FrexpMantF32 ||
                                  instruction.op == Op::V_FrexpMantF16;
            const std::uint32_t mantissaBits = half ? 10u : 23u;
            const std::uint32_t bias = half ? 15u : 127u;
            const std::uint32_t exponentMax = half ? 31u : 255u;
            const std::uint32_t mantissaMask = (1u << mantissaBits) - 1u;
            const std::uint32_t signShift = half ? 15u : 31u;

            const std::uint32_t bits = half ? (*source_ & 0xFFFFu) : *source_;
            const std::uint32_t sign = (bits >> signShift) & 1u;
            const std::uint32_t mantissaField = bits & mantissaMask;
            const std::uint32_t exponentField = (bits >> mantissaBits) & exponentMax;
            const std::uint32_t targetExponent = bias - 1u;

            std::uint32_t value = 0;
            if (exponentField == exponentMax)
            {
                value = mantissa ? bits : 0u;
            }
            else if (exponentField != 0)
            {
                if (mantissa)
                {
                    value = (sign << signShift) |
                            (targetExponent << mantissaBits) | mantissaField;
                }
                else
                {
                    value = exponentField - (bias - 1u);
                }
            }
            else if (mantissaField != 0)
            {
                const std::uint32_t leading = std::countl_zero(mantissaField);
                const std::uint32_t highest = 31u - leading;
                if (mantissa)
                {
                    const std::uint32_t shift = mantissaBits - highest;
                    value = (sign << signShift) | (targetExponent << mantissaBits) |
                            ((mantissaField << shift) & mantissaMask);
                }
                else
                {
                    value = highest + 2u - bias - mantissaBits;
                }
            }
            else
            {
                value = 0;
            }
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-371", "invalid frexp destination", pc));
            break;
        }
        case Op::Invalid:
            return std::unexpected(Error("SR-GFX-ISA-074", "invalid instruction", pc));
        default:
            break;
        }

        if (finished)
            return result;
        if (advance)
            pc += instruction.wordCount * 4;
    }

    return std::unexpected(Error("SR-GFX-ISA-075", "step limit exceeded", pc));
}

} // namespace SR::Frontend::Gfx
