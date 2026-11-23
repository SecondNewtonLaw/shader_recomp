#include "SR/Frontend/Arch/Gfx/GfxInterpreter.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

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
        }

        if (finished)
            return result;
        if (advance)
            pc += instruction.wordCount * 4;
    }

    return std::unexpected(Error("SR-GFX-ISA-075", "step limit exceeded", pc));
}

} // namespace SR::Frontend::Gfx
