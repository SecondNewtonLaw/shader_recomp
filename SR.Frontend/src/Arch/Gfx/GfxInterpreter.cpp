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

        if (const auto spec = CompareSpecOf(instruction.op))
        {
            const auto condition = EvaluateCompare(result.state, instruction, *spec);
            if (!condition)
                return std::unexpected(Error("SR-GFX-ISA-165", "invalid typed compare operands", pc));
            result.state.vcc = *condition;
            if (instruction.execMask)
                result.state.exec = result.state.exec && *condition;
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
            result.state.vcc = condition;
            if (instruction.execMask)
                result.state.exec = result.state.exec && condition;
            break;
        }
        case Op::V_CndmaskB32:
        {
            const auto left = source(1);
            const auto right = source(2);
            if (!left || !right || !write(result.state.vcc ? *left : *right))
                return std::unexpected(Error("SR-GFX-ISA-070", "invalid cndmask operands", pc));
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
        case Op::GlobalLoadB32:
        case Op::S_LoadB32:
        case Op::S_BufferLoadB32:
        {
            const auto index = BufferIndex(result.state, instruction, 1);
            if (!index)
                return std::unexpected(Error("SR-GFX-ISA-072", "invalid load address", pc));
            const auto it = result.state.buffer.find(*index);
            const std::uint32_t value = it == result.state.buffer.end() ? 0u : it->second;
            if (!write(value))
                return std::unexpected(Error("SR-GFX-ISA-073", "invalid load destination", pc));
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
