#include "SR/Common/IL/Interpreter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace SR::IL {
namespace {

Diagnostic ExecutionError(std::string code, std::string message, std::uint32_t pc = 0)
{
    return Diagnostic::Error(std::move(code), std::move(message), pc);
}

std::uint64_t MaskFor(const Module& module, TypeId typeId)
{
    if (typeId == InvalidType || !module.types.contains(typeId))
        return 0;

    const Type& type = module.types.Get(typeId);
    switch (type.kind)
    {
    case TypeKind::Bool:
        return 1;
    case TypeKind::Int:
    case TypeKind::Mask:
        if (type.width == 64)
            return ~0ull;
        return (1ull << type.width) - 1;
    case TypeKind::Float:
        if (type.floatFormat == FloatFormat::F64)
            return ~0ull;
        if (type.floatFormat == FloatFormat::F16 || type.floatFormat == FloatFormat::BF16)
            return 0xFFFFull;
        return 0xFFFFFFFFull;
    case TypeKind::Pointer:
        return 0xFFFFFFFFull;
    default:
        return ~0ull;
    }
}

float HalfToFloat(std::uint16_t half)
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
            // Subnormal half: normalize into a float.
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

std::uint16_t FloatToHalf(float value)
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

std::int64_t AsSigned(std::uint64_t value, std::uint32_t width)
{
    if (width >= 64)
        return static_cast<std::int64_t>(value);
    const std::uint64_t sign = 1ull << (width - 1);
    const std::uint64_t mask = (1ull << width) - 1;
    value &= mask;
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

double AsFloat(std::uint64_t bits, const Type& type)
{
    if (type.floatFormat == FloatFormat::F64)
        return std::bit_cast<double>(bits);
    if (type.floatFormat == FloatFormat::F16)
        return static_cast<double>(HalfToFloat(static_cast<std::uint16_t>(bits)));
    if (type.floatFormat == FloatFormat::BF16)
        return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits) << 16));
    return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
}

std::uint64_t FromFloat(double value, const Type& type)
{
    if (type.floatFormat == FloatFormat::F64)
        return std::bit_cast<std::uint64_t>(value);
    if (type.floatFormat == FloatFormat::F16)
        return FloatToHalf(static_cast<float>(value));
    if (type.floatFormat == FloatFormat::BF16)
        return std::bit_cast<std::uint32_t>(static_cast<float>(value)) >> 16;
    return std::bit_cast<std::uint32_t>(static_cast<float>(value));
}

std::uint64_t FloatBits(double value, const Type& type)
{
    return FromFloat(value, type);
}

bool FloatCompare(CompareOp op, double left, double right)
{
    const bool ordered = !std::isnan(left) && !std::isnan(right);

    switch (op)
    {
    case CompareOp::FOrdEq:
        return ordered && left == right;
    case CompareOp::FOrdNe:
        return ordered && left != right;
    case CompareOp::FOrdLt:
        return ordered && left < right;
    case CompareOp::FOrdLe:
        return ordered && left <= right;
    case CompareOp::FOrdGt:
        return ordered && left > right;
    case CompareOp::FOrdGe:
        return ordered && left >= right;
    case CompareOp::FUnordEq:
        return !ordered || left == right;
    case CompareOp::FUnordNe:
        return !ordered || left != right;
    case CompareOp::FUnordLt:
        return !ordered || left < right;
    case CompareOp::FUnordLe:
        return !ordered || left <= right;
    case CompareOp::FUnordGt:
        return !ordered || left > right;
    case CompareOp::FUnordGe:
        return !ordered || left >= right;
    default:
        return false;
    }
}

} // namespace

Result<ExecutionResult> Interpret(const Module& module, const ExecutionOptions& options)
{
    const std::uint64_t maxSteps = options.maxSteps;
    if (module.entryFunction == InvalidFunctionId || module.entryFunction >= module.functions.size())
        return std::unexpected(ExecutionError("SR-IL-INT-001", "module has no entry function"));

    const Function& function = module.functions[module.entryFunction];
    if (function.blocks.empty() || function.entry == InvalidBlockId)
        return std::unexpected(ExecutionError("SR-IL-INT-002", "entry function has no blocks"));

    ExecutionResult result;
    result.values.assign(function.values.size(), 0);
    result.buffer = options.testBuffer;
    result.local = options.localMemory;

    for (std::size_t i = 0; i < function.values.size(); ++i)
    {
        const Value& value = function.values[i];
        if (value.kind == ValueKind::Constant && value.index < module.constants.size())
            result.values[i] = module.constants[value.index].bits;
    }

    std::vector<std::uint64_t>& values = result.values;
    BlockId current = function.entry;
    BlockId previous = InvalidBlockId;

    const auto read = [&](ValueId value) -> std::uint64_t { return value < values.size() ? values[value] : 0; };

    for (std::uint64_t step = 0; step < maxSteps; ++step)
    {
        if (current == InvalidBlockId || current >= function.blocks.size())
            return std::unexpected(ExecutionError("SR-IL-INT-003", "execution reached an invalid block"));
        result.finalBlock = current;
        const Block& block = function.blocks[current];

        for (const Instruction& instruction : block.body)
        {
            if (instruction.guard != InvalidValue && read(instruction.guard) == 0)
                continue;

            ++result.steps;
            if (result.steps > maxSteps)
                return std::unexpected(ExecutionError("SR-IL-INT-004", "step limit exceeded", instruction.sourcePc));

            const auto resultId = [&]() -> ValueId {
                return instruction.results.empty() ? InvalidValue : instruction.results[0];
            };

            switch (instruction.op)
            {
            case Op::Phi:
            {
                const ValueId id = resultId();
                bool found = false;
                for (const auto& incoming : instruction.incoming)
                {
                    if (incoming.block == previous)
                    {
                        values[id] = read(incoming.value);
                        found = true;
                        break;
                    }
                }
                if (!found)
                    return std::unexpected(ExecutionError("SR-IL-INT-013", "phi has no incoming value for the predecessor", instruction.sourcePc));
                break;
            }
            case Op::Nop:
                break;
            case Op::Alloca:
            {
                const ValueId id = resultId();
                result.memory[id] = 0;
                values[id] = id;
                break;
            }
            case Op::Store:
            {
                const ValueId pointer = static_cast<ValueId>(read(instruction.operands[0]));
                result.memory[pointer] = read(instruction.operands[1]);
                break;
            }
            case Op::Load:
            {
                const ValueId id = resultId();
                const ValueId pointer = static_cast<ValueId>(read(instruction.operands[0]));
                const auto it = result.memory.find(pointer);
                values[id] = it == result.memory.end() ? 0 : it->second;
                break;
            }
            case Op::Binary:
            {
                const ValueId id = resultId();
                const Type& type = module.types.Get(function.values[id].type);
                const std::uint64_t left = read(instruction.operands[0]);
                const std::uint64_t right = read(instruction.operands[1]);
                const auto subop = static_cast<BinaryOp>(instruction.subop);

                if (type.kind == TypeKind::Float)
                {
                    const double a = AsFloat(left, type);
                    const double b = AsFloat(right, type);
                    double value = 0;
                    switch (subop)
                    {
                    case BinaryOp::FAdd:
                        value = a + b;
                        break;
                    case BinaryOp::FSub:
                        value = a - b;
                        break;
                    case BinaryOp::FMul:
                        value = a * b;
                        break;
                    case BinaryOp::FDiv:
                        value = a / b;
                        break;
                    case BinaryOp::FRem:
                        value = std::fmod(a, b);
                        break;
                    case BinaryOp::FMin:
                        value = std::fmin(a, b);
                        break;
                    case BinaryOp::FMax:
                        value = std::fmax(a, b);
                        break;
                    default:
                        return std::unexpected(ExecutionError("SR-IL-INT-005", "unsupported float binary op", instruction.sourcePc));
                    }
                    values[id] = FloatBits(value, type);
                }
                else
                {
                    const std::uint32_t width = type.width;
                    const std::uint64_t mask = MaskFor(module, function.values[id].type);
                    const std::uint64_t shift = width == 0 ? 0 : (right & (width - 1));
                    std::uint64_t value = 0;
                    switch (subop)
                    {
                    case BinaryOp::IAdd:
                        value = left + right;
                        break;
                    case BinaryOp::ISub:
                        value = left - right;
                        break;
                    case BinaryOp::IMul:
                        value = left * right;
                        break;
                    case BinaryOp::UDiv:
                        value = right == 0 ? 0 : left / right;
                        break;
                    case BinaryOp::SDiv:
                        value = right == 0 ? 0 : static_cast<std::uint64_t>(AsSigned(left, width) / AsSigned(right, width));
                        break;
                    case BinaryOp::URem:
                        value = right == 0 ? 0 : left % right;
                        break;
                    case BinaryOp::SRem:
                        value = right == 0 ? 0 : static_cast<std::uint64_t>(AsSigned(left, width) % AsSigned(right, width));
                        break;
                    case BinaryOp::And:
                        value = left & right;
                        break;
                    case BinaryOp::Or:
                        value = left | right;
                        break;
                    case BinaryOp::Xor:
                        value = left ^ right;
                        break;
                    case BinaryOp::Shl:
                        value = left << shift;
                        break;
                    case BinaryOp::LShr:
                        value = (left & mask) >> shift;
                        break;
                    case BinaryOp::AShr:
                        value = static_cast<std::uint64_t>(AsSigned(left, width) >> shift);
                        break;
                    case BinaryOp::SMin:
                        value = static_cast<std::uint64_t>(std::min(AsSigned(left, width), AsSigned(right, width)));
                        break;
                    case BinaryOp::SMax:
                        value = static_cast<std::uint64_t>(std::max(AsSigned(left, width), AsSigned(right, width)));
                        break;
                    case BinaryOp::UMin:
                        value = std::min(left & mask, right & mask);
                        break;
                    case BinaryOp::UMax:
                        value = std::max(left & mask, right & mask);
                        break;
                    case BinaryOp::RotL:
                    case BinaryOp::RotR:
                    {
                        const std::uint64_t a = left & mask;
                        const std::uint64_t r = right & (width - 1);
                        value = subop == BinaryOp::RotL ? ((a << r) | (a >> (width - r))) : ((a >> r) | (a << (width - r)));
                        break;
                    }
                    default:
                        return std::unexpected(ExecutionError("SR-IL-INT-006", "unsupported integer binary op", instruction.sourcePc));
                    }
                    values[id] = value & mask;
                }
                break;
            }
            case Op::Compare:
            {
                const ValueId id = resultId();
                const ValueId leftId = instruction.operands[0];
                const Type& operandType = module.types.Get(function.values[leftId].type);
                const std::uint64_t left = read(leftId);
                const std::uint64_t right = read(instruction.operands[1]);
                const auto subop = static_cast<CompareOp>(instruction.subop);

                bool value = false;
                if (operandType.kind == TypeKind::Float)
                {
                    value = FloatCompare(subop, AsFloat(left, operandType), AsFloat(right, operandType));
                }
                else
                {
                    const std::uint32_t width = operandType.width;
                    switch (subop)
                    {
                    case CompareOp::IEq:
                    case CompareOp::UEq:
                        value = (left & MaskFor(module, function.values[leftId].type)) ==
                                (right & MaskFor(module, function.values[leftId].type));
                        break;
                    case CompareOp::INe:
                    case CompareOp::UNe:
                        value = left != right;
                        break;
                    case CompareOp::ILt:
                        value = AsSigned(left, width) < AsSigned(right, width);
                        break;
                    case CompareOp::ILe:
                        value = AsSigned(left, width) <= AsSigned(right, width);
                        break;
                    case CompareOp::IGt:
                        value = AsSigned(left, width) > AsSigned(right, width);
                        break;
                    case CompareOp::IGe:
                        value = AsSigned(left, width) >= AsSigned(right, width);
                        break;
                    case CompareOp::ULt:
                        value = left < right;
                        break;
                    case CompareOp::ULe:
                        value = left <= right;
                        break;
                    case CompareOp::UGt:
                        value = left > right;
                        break;
                    case CompareOp::UGe:
                        value = left >= right;
                        break;
                    default:
                        return std::unexpected(ExecutionError("SR-IL-INT-007", "unsupported compare", instruction.sourcePc));
                    }
                }
                values[id] = value ? 1 : 0;
                break;
            }
            case Op::Select:
            {
                const ValueId id = resultId();
                values[id] = read(instruction.operands[0]) != 0 ? read(instruction.operands[1]) : read(instruction.operands[2]);
                break;
            }
            case Op::Bitcast:
            {
                const ValueId id = resultId();
                values[id] = read(instruction.operands[0]) & MaskFor(module, function.values[id].type);
                break;
            }
            case Op::Unary:
            {
                const ValueId id = resultId();
                const Type& type = module.types.Get(function.values[id].type);
                const std::uint64_t operand = read(instruction.operands[0]);
                const auto subop = static_cast<UnaryOp>(instruction.subop);
                switch (subop)
                {
                case UnaryOp::Neg:
                    if (type.kind == TypeKind::Float)
                        values[id] = FloatBits(-AsFloat(operand, type), type);
                    else
                        values[id] = static_cast<std::uint64_t>(-AsSigned(operand, type.width)) & MaskFor(module, function.values[id].type);
                    break;
                case UnaryOp::Not:
                    if (type.kind == TypeKind::Bool)
                        values[id] = operand == 0 ? 1 : 0;
                    else
                        values[id] = ~operand & MaskFor(module, function.values[id].type);
                    break;
                case UnaryOp::FAbs:
                    values[id] = FloatBits(std::fabs(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Sqrt:
                    values[id] = FloatBits(std::sqrt(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Rcp:
                    values[id] = FloatBits(1.0 / AsFloat(operand, type), type);
                    break;
                case UnaryOp::Rsq:
                    values[id] = FloatBits(1.0 / std::sqrt(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Exp2:
                    values[id] = FloatBits(std::exp2(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Log2:
                    values[id] = FloatBits(std::log2(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Trunc:
                    values[id] = FloatBits(std::trunc(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Ceil:
                    values[id] = FloatBits(std::ceil(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::RoundEven:
                    values[id] = FloatBits(std::nearbyint(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Floor:
                    values[id] = FloatBits(std::floor(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Sin:
                    values[id] = FloatBits(std::sin(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Cos:
                    values[id] = FloatBits(std::cos(AsFloat(operand, type)), type);
                    break;
                case UnaryOp::Fract:
                {
                    const double value = AsFloat(operand, type);
                    values[id] = FloatBits(value - std::floor(value), type);
                    break;
                }
                case UnaryOp::BitReverse:
                {
                    const std::uint32_t width = type.width;
                    std::uint64_t input = operand & MaskFor(module, function.values[id].type);
                    std::uint64_t output = 0;
                    for (std::uint32_t i = 0; i < width; ++i)
                        output = (output << 1) | ((input >> i) & 1);
                    values[id] = output;
                    break;
                }
                case UnaryOp::PopCount:
                    values[id] = static_cast<std::uint64_t>(std::popcount(operand));
                    break;
                case UnaryOp::Clz:
                {
                    const std::uint32_t width = type.width;
                    std::uint64_t input = operand & MaskFor(module, function.values[id].type);
                    std::uint32_t count = 0;
                    for (std::int32_t bit = static_cast<std::int32_t>(width) - 1; bit >= 0 && ((input >> bit) & 1) == 0; --bit)
                        ++count;
                    values[id] = count;
                    break;
                }
                case UnaryOp::Ctz:
                {
                    const std::uint64_t input = operand & MaskFor(module, function.values[id].type);
                    values[id] = input == 0 ? type.width : static_cast<std::uint64_t>(std::countr_zero(input));
                    break;
                }
                case UnaryOp::FindFirstSet:
                {
                    const std::uint64_t input = operand & MaskFor(module, function.values[id].type);
                    values[id] = input == 0 ? 0 : static_cast<std::uint64_t>(std::countr_zero(input) + 1);
                    break;
                }
                default:
                    return std::unexpected(ExecutionError("SR-IL-INT-008", "unsupported unary op", instruction.sourcePc));
                }
                break;
            }
            case Op::Ternary:
            {
                const ValueId id = resultId();
                const Type& type = module.types.Get(function.values[id].type);
                const auto subop = static_cast<TernaryOp>(instruction.subop);
                const double a = AsFloat(read(instruction.operands[0]), type);
                const double b = AsFloat(read(instruction.operands[1]), type);
                const double c = AsFloat(read(instruction.operands[2]), type);
                switch (subop)
                {
                case TernaryOp::Fma:
                    values[id] = FloatBits(std::fma(a, b, c), type);
                    break;
                case TernaryOp::FMin3:
                    values[id] = FloatBits(std::fmin(a, std::fmin(b, c)), type);
                    break;
                case TernaryOp::FMax3:
                    values[id] = FloatBits(std::fmax(a, std::fmax(b, c)), type);
                    break;
                case TernaryOp::Clamp:
                    values[id] = FloatBits(std::fmin(std::fmax(a, b), c), type);
                    break;
                case TernaryOp::Lerp:
                    values[id] = FloatBits(a + (b - a) * c, type);
                    break;
                case TernaryOp::MulAdd:
                {
                    double value = a * b + c;
                    if (type.floatFormat == FloatFormat::F32)
                    {
                        const float product = static_cast<float>(a) * static_cast<float>(b);
                        value = static_cast<double>(static_cast<float>(product + static_cast<float>(c)));
                    }
                    values[id] = FloatBits(value, type);
                    break;
                }
                default:
                    return std::unexpected(ExecutionError("SR-IL-INT-009", "unsupported ternary op", instruction.sourcePc));
                }
                break;
            }
            case Op::Convert:
            {
                const ValueId id = resultId();
                const Type& target = module.types.Get(function.values[id].type);
                const Type& source = module.types.Get(function.values[instruction.operands[0]].type);
                const std::uint64_t operand = read(instruction.operands[0]);
                const auto subop = static_cast<ConvertOp>(instruction.subop);

                if (target.kind == TypeKind::Float)
                {
                    double value = 0;
                    if (source.kind == TypeKind::Float)
                        value = AsFloat(operand, source);
                    else if (source.isSigned)
                        value = static_cast<double>(AsSigned(operand, source.width));
                    else
                        value = static_cast<double>(operand);
                    values[id] = FloatBits(value, target);
                }
                else
                {
                    std::uint64_t value = 0;
                    if (source.kind == TypeKind::Float)
                    {
                        const double number = AsFloat(operand, source);
                        value = subop == ConvertOp::FPToSI ? static_cast<std::uint64_t>(static_cast<std::int64_t>(number))
                                                           : static_cast<std::uint64_t>(number);
                    }
                    else
                    {
                        value = subop == ConvertOp::SExt && source.width < target.width
                                    ? static_cast<std::uint64_t>(AsSigned(operand, source.width))
                                    : operand;
                    }
                    values[id] = value & MaskFor(module, function.values[id].type);
                }
                break;
            }
            case Op::Intrinsic:
            {
                const auto id = static_cast<IntrinsicId>(instruction.subop);
                if (id == IntrinsicId::TestBufferStore)
                {
                    const auto offset = static_cast<std::uint32_t>(read(instruction.operands[0]));
                    const auto value = static_cast<std::uint32_t>(read(instruction.operands[1]));
                    result.buffer[offset] = value;
                    break;
                }
                if (id == IntrinsicId::TestBufferLoad)
                {
                    const ValueId id = resultId();
                    const auto offset = static_cast<std::uint32_t>(read(instruction.operands[0]));
                    const auto it = result.buffer.find(offset);
                    values[id] = it == result.buffer.end() ? 0 : it->second;
                    break;
                }
                if (id == IntrinsicId::LocalStore)
                {
                    const auto offset = static_cast<std::uint32_t>(read(instruction.operands[0]));
                    const auto value = static_cast<std::uint32_t>(read(instruction.operands[1]));
                    result.local[offset] = value;
                    break;
                }
                if (id == IntrinsicId::LocalLoad)
                {
                    const ValueId id = resultId();
                    const auto offset = static_cast<std::uint32_t>(read(instruction.operands[0]));
                    const auto it = result.local.find(offset);
                    values[id] = it == result.local.end() ? 0 : it->second;
                    break;
                }

                if (id == IntrinsicId::SpecialReg)
                {
                    const auto* payload = instruction.payload != NoPayload
                                              ? std::get_if<SpecialRegPayload>(&module.payloads.Get(instruction.payload))
                                              : nullptr;
                    if (payload == nullptr)
                        return std::unexpected(ExecutionError("SR-IL-INT-016",
                                                              "special register without a descriptor",
                                                              instruction.sourcePc));

                    const ValueId slot = resultId();
                    std::uint64_t value = 0;
                    switch (payload->id)
                    {
                    case SpecialRegId::ThreadIdX:
                        value = options.threadId[0];
                        break;
                    case SpecialRegId::ThreadIdY:
                        value = options.threadId[1];
                        break;
                    case SpecialRegId::ThreadIdZ:
                        value = options.threadId[2];
                        break;
                    case SpecialRegId::LocalInvocationId:
                        value = options.threadId[0];
                        break;
                    case SpecialRegId::WorkgroupId:
                        value = options.workgroupId[0];
                        break;
                    case SpecialRegId::WorkgroupSize:
                        value = options.workgroupSize[0];
                        break;
                    case SpecialRegId::GlobalInvocationId:
                        value = static_cast<std::uint64_t>(options.workgroupId[0]) * options.workgroupSize[0]
                              + options.threadId[0];
                        break;
                    case SpecialRegId::SubgroupLocalInvocationId:
                        value = options.threadId[0];
                        break;
                    case SpecialRegId::SubgroupSize:
                        value = 1; // one invocation is a one-lane wave here
                        break;
                    default:
                        return std::unexpected(ExecutionError("SR-IL-INT-017",
                                                              std::string("unsupported special register: ")
                                                                  + std::string(ToString(payload->id)),
                                                              instruction.sourcePc));
                    }
                    values[slot] = value;
                    break;
                }

                if (id == IntrinsicId::ImageQuerySize)
                {
                    const auto* image = instruction.payload != NoPayload
                                            ? std::get_if<ImageAccessDesc>(&module.payloads.Get(instruction.payload))
                                            : nullptr;
                    if (image == nullptr)
                        return std::unexpected(ExecutionError("SR-IL-INT-019",
                                                              "image query without a descriptor",
                                                              instruction.sourcePc));
                    const auto extent = options.imageExtents.find(image->binding);
                    const std::uint32_t component = image->component & 3;
                    std::uint64_t value = 1;
                    if (extent != options.imageExtents.end())
                    {
                        if (component == 0)
                            value = extent->second[0];
                        else if (component == 1)
                            value = extent->second[1];
                    }
                    values[resultId()] = value;
                    break;
                }

                if (id == IntrinsicId::ImageQueryLod)
                {
                    // One level: the level is zero and its unclamped value too.
                    (void)read(instruction.operands[0]);
                    (void)read(instruction.operands[1]);
                    values[resultId()] = 0;
                    break;
                }

                if (id == IntrinsicId::ImageGather)
                {
                    const auto* image = instruction.payload != NoPayload
                                            ? std::get_if<ImageAccessDesc>(&module.payloads.Get(instruction.payload))
                                            : nullptr;
                    if (image == nullptr)
                        return std::unexpected(ExecutionError("SR-IL-INT-019",
                                                              "image gather without a descriptor",
                                                              instruction.sourcePc));
                    // Texel coordinates: the 2x2 footprint around the sample. A
                    // sampled image takes normalized coordinates, so the extent
                    // scales them; a storage image is already in texels.
                    float x = std::bit_cast<float>(static_cast<std::uint32_t>(read(instruction.operands[0])));
                    float y = std::bit_cast<float>(static_cast<std::uint32_t>(read(instruction.operands[1])));
                    if (image->sampled)
                    {
                        if (const auto extent = options.imageExtents.find(image->binding);
                            extent != options.imageExtents.end())
                        {
                            x *= static_cast<float>(extent->second[0]);
                            y *= static_cast<float>(extent->second[1]);
                        }
                    }
                    const auto baseX = static_cast<std::int32_t>(std::floor(x - 0.5f));
                    const auto baseY = static_cast<std::int32_t>(std::floor(y - 0.5f));
                    const auto texel = [&](std::int32_t tx, std::int32_t ty) -> std::uint32_t {
                        const std::uint64_t key =
                            (static_cast<std::uint64_t>(image->binding) << 40) |
                            ((static_cast<std::uint64_t>(ty) & 0xFFFFF) << 20) |
                            (static_cast<std::uint64_t>(tx) & 0xFFFFF);
                        const std::uint32_t channel = image->channel & 3;
                        if (const auto written = result.images.find(key); written != result.images.end())
                            return written->second[channel];
                        const auto it = options.images.find(key);
                        return it == options.images.end() ? 0 : it->second[channel];
                    };
                    // The data register order is the one the hardware delivers,
                    // measured on the RX 9060 XT: the two texels of the upper
                    // row first, then the lower row right to left.
                    const std::array<std::uint32_t, 4> gathered = {
                        texel(baseX, baseY + 1),
                        texel(baseX + 1, baseY + 1),
                        texel(baseX + 1, baseY),
                        texel(baseX, baseY),
                    };
                    values[resultId()] = gathered[image->component & 3];
                    break;
                }

                if (id == IntrinsicId::ImageAtomic)
                {
                    const auto* image = instruction.payload != NoPayload
                                            ? std::get_if<ImageAccessDesc>(&module.payloads.Get(instruction.payload))
                                            : nullptr;
                    if (image == nullptr)
                        return std::unexpected(ExecutionError("SR-IL-INT-019",
                                                              "image atomic without a descriptor",
                                                              instruction.sourcePc));
                    const auto x = read(instruction.operands[0]);
                    const auto y = read(instruction.operands[1]);
                    const std::uint64_t key = (static_cast<std::uint64_t>(image->binding) << 40) |
                                              ((y & 0xFFFFF) << 20) | (x & 0xFFFFF);
                    std::array<std::uint32_t, 4> texel{};
                    if (const auto written = result.images.find(key); written != result.images.end())
                        texel = written->second;
                    else if (const auto initial = options.images.find(key); initial != options.images.end())
                        texel = initial->second;

                    const auto old = texel[0];
                    const auto asSigned = [](std::uint32_t v) { return static_cast<std::int32_t>(v); };
                    std::uint32_t value = 0;
                    if (image->atomicKind == AtomicKind::CompareExchange)
                        value = static_cast<std::uint32_t>(read(instruction.operands[3]));
                    else if (instruction.operands.size() > 2)
                        value = static_cast<std::uint32_t>(read(instruction.operands[2]));

                    switch (image->atomicKind)
                    {
                    case AtomicKind::Add:
                        texel[0] = old + value;
                        break;
                    case AtomicKind::Sub:
                        texel[0] = old - value;
                        break;
                    case AtomicKind::SMin:
                        texel[0] = static_cast<std::uint32_t>(std::min(asSigned(old), asSigned(value)));
                        break;
                    case AtomicKind::UMin:
                        texel[0] = std::min(old, value);
                        break;
                    case AtomicKind::SMax:
                        texel[0] = static_cast<std::uint32_t>(std::max(asSigned(old), asSigned(value)));
                        break;
                    case AtomicKind::UMax:
                        texel[0] = std::max(old, value);
                        break;
                    case AtomicKind::And:
                        texel[0] = old & value;
                        break;
                    case AtomicKind::Or:
                        texel[0] = old | value;
                        break;
                    case AtomicKind::Xor:
                        texel[0] = old ^ value;
                        break;
                    case AtomicKind::Exchange:
                        texel[0] = value;
                        break;
                    case AtomicKind::CompareExchange:
                        if (old == static_cast<std::uint32_t>(read(instruction.operands[2])))
                            texel[0] = value;
                        break;
                    case AtomicKind::Increment:
                        texel[0] = old + 1;
                        break;
                    case AtomicKind::Decrement:
                        texel[0] = old - 1;
                        break;
                    }
                    result.images[key] = texel;
                    values[resultId()] = old;
                    break;
                }

                if (id == IntrinsicId::ImageLoad || id == IntrinsicId::ImageStore ||
                    id == IntrinsicId::ImageSample)
                {
                    const auto* image = instruction.payload != NoPayload
                                            ? std::get_if<ImageAccessDesc>(&module.payloads.Get(instruction.payload))
                                            : nullptr;
                    if (image == nullptr)
                        return std::unexpected(ExecutionError("SR-IL-INT-019",
                                                              "image access without a descriptor",
                                                              instruction.sourcePc));

                    const std::uint64_t x = read(instruction.operands[0]);
                    const std::uint64_t y = read(instruction.operands[1]);
                    const std::uint64_t key = (static_cast<std::uint64_t>(image->binding) << 40) | ((y & 0xFFFFF) << 20) |
                                              (x & 0xFFFFF);

                    if (id == IntrinsicId::ImageLoad || id == IntrinsicId::ImageSample)
                    {
                        const ValueId slot = resultId();
                        // A write earlier in the same invocation wins over the
                        // initial contents, so a store/load round trip works.
                        if (const auto written = result.images.find(key); written != result.images.end())
                        {
                            values[slot] = written->second[image->component & 3];
                            break;
                        }
                        const auto it = options.images.find(key);
                        values[slot] = it == options.images.end() ? 0 : it->second[image->component & 3];
                        break;
                    }

                    // A store replaces the whole texel: four components.
                    auto& texel = result.images[key];
                    for (std::uint32_t component = 0; component < 4; ++component)
                        texel[component] = static_cast<std::uint32_t>(read(instruction.operands[2 + component]));
                    break;
                }

                if (id == IntrinsicId::DeviceLoad)
                {
                    const ValueId slot = resultId();
                    const std::uint64_t address = read(instruction.operands[0]);
                    // A store earlier in the same invocation wins over the
                    // initial contents, so a store/load round trip works.
                    if (const auto written = result.device.find(address); written != result.device.end())
                    {
                        values[slot] = written->second;
                        break;
                    }
                    const auto it = options.deviceMemory.find(address);
                    values[slot] = it == options.deviceMemory.end() ? 0 : it->second;
                    break;
                }
                if (id == IntrinsicId::DeviceStore)
                {
                    const std::uint64_t address = read(instruction.operands[0]);
                    result.device[address] = static_cast<std::uint32_t>(read(instruction.operands[1]));
                    break;
                }

                if (id == IntrinsicId::UserData)
                {
                    const auto* payload = instruction.payload != NoPayload
                                              ? std::get_if<UserDataDesc>(&module.payloads.Get(instruction.payload))
                                              : nullptr;
                    if (payload == nullptr)
                        return std::unexpected(ExecutionError("SR-IL-INT-018",
                                                              "user data without a descriptor",
                                                              instruction.sourcePc));
                    const ValueId slot = resultId();
                    values[slot] = payload->index < options.userData.size() ? options.userData[payload->index] : 0;
                    break;
                }

                if (id == IntrinsicId::WaveBroadcastFirst)
                {
                    // One invocation is a one-lane wave: the value is its own.
                    const ValueId slot = resultId();
                    values[slot] = read(instruction.operands[0]);
                    break;
                }

                if (id == IntrinsicId::WaveBallot)
                {
                    // One invocation is a one-lane wave: the mask holds bit 0.
                    const ValueId slot = resultId();
                    const std::uint64_t predicate = read(instruction.operands[0]);
                    values[slot] = predicate != 0 ? 1u : 0u;
                    break;
                }

                if (id == IntrinsicId::GeometryStageStore)
                {
                    const auto index = static_cast<std::uint32_t>(read(instruction.operands[0]));
                    result.geometryStage[index] = static_cast<std::uint32_t>(read(instruction.operands[1]));
                    break;
                }

                if (id == IntrinsicId::GeometryStageLoad)
                {
                    const auto index = static_cast<std::uint32_t>(read(instruction.operands[0]));
                    const ValueId slot = resultId();
                    const auto it = result.geometryStage.find(index);
                    values[slot] = it == result.geometryStage.end() ? 0 : it->second;
                    break;
                }

                if (id == IntrinsicId::EmitVertex)
                {
                    // Emission captures the outputs as they are now: the next
                    // vertex overwrites them.
                    result.vertices.push_back(result.outputs);
                    break;
                }

                if (id == IntrinsicId::EndPrimitive)
                    break;

                if (id == IntrinsicId::WaveShuffle)
                {
                    // One invocation is a one-lane wave: only an index that
                    // names this lane can be answered.
                    const auto* payload = instruction.payload != NoPayload
                                              ? std::get_if<ShufflePayload>(&module.payloads.Get(instruction.payload))
                                              : nullptr;
                    if (payload == nullptr || payload->mode != ShuffleMode::Index)
                        return std::unexpected(ExecutionError("SR-IL-INT-020",
                                                              "only an index shuffle is modelled",
                                                              instruction.sourcePc));
                    const ValueId slot = resultId();
                    const std::uint64_t index = read(instruction.operands[1]);
                    values[slot] = index == 0 ? read(instruction.operands[0]) : 0;
                    break;
                }

                if (id == IntrinsicId::StageInput || id == IntrinsicId::StageOutput)
                {
                    const auto* io = instruction.payload != NoPayload
                                         ? std::get_if<StageIoDesc>(&module.payloads.Get(instruction.payload))
                                         : nullptr;
                    if (io == nullptr)
                        return std::unexpected(ExecutionError("SR-IL-INT-015",
                                                              "stage IO without a signature descriptor",
                                                              instruction.sourcePc));

                    const std::uint64_t key = StageIoKey(io->builtin, io->location, io->component);
                    if (id == IntrinsicId::StageInput)
                    {
                        const ValueId slot = resultId();
                        if (instruction.operands.empty())
                        {
                            const auto it = options.stageInputs.find(key);
                            values[slot] = it == options.stageInputs.end() ? 0 : it->second;
                        }
                        else
                        {
                            // A geometry input names the input vertex; its
                            // values are per vertex.
                            const std::uint64_t vertexKey = StageInputVertexKey(
                                io->builtin,
                                io->location,
                                io->component,
                                static_cast<std::uint32_t>(read(instruction.operands[0])));
                            const auto it = options.stageInputVertices.find(vertexKey);
                            values[slot] = it == options.stageInputVertices.end() ? 0 : it->second;
                        }
                    }
                    else
                    {
                        result.outputs[key] = read(instruction.operands[0]);
                    }
                    break;
                }
                return std::unexpected(ExecutionError("SR-IL-INT-014",
                                                      std::string("unsupported intrinsic: ") + std::string(ToString(id)),
                                                      instruction.sourcePc));
            }
            case Op::Atomic:
            case Op::Call:
            case Op::Kill:
            case Op::Extract:
            case Op::Insert:
            case Op::Construct:
            case Op::Shuffle:
                return std::unexpected(ExecutionError("SR-IL-INT-010",
                                                      std::string("unsupported IL op: ") + std::string(ToString(instruction.op)),
                                                      instruction.sourcePc));
            }
        }

        const Terminator& terminator = block.terminator;
        previous = current;
        switch (terminator.kind)
        {
        case TerminatorKind::Br:
            current = terminator.target;
            break;
        case TerminatorKind::CondBr:
            current = read(terminator.condition) != 0 ? terminator.trueTarget : terminator.falseTarget;
            break;
        case TerminatorKind::Switch:
        {
            current = terminator.defaultTarget;
            const std::uint64_t selector = read(terminator.selector);
            for (const auto& entry : terminator.cases)
            {
                if (entry.literal == selector)
                {
                    current = entry.target;
                    break;
                }
            }
            break;
        }
        case TerminatorKind::Ret:
            return result;
        case TerminatorKind::Unreachable:
            return result;
        case TerminatorKind::IndirectBr:
            return std::unexpected(ExecutionError("SR-IL-INT-011", "indirect branches are not supported", terminator.sourcePc));
        }
    }

    return std::unexpected(ExecutionError("SR-IL-INT-012", "step limit exceeded"));
}

Result<ExecutionResult> Interpret(const Module& module, std::uint64_t maxSteps)
{
    ExecutionOptions options;
    options.maxSteps = maxSteps;
    return Interpret(module, options);
}

} // namespace SR::IL
