#include "SR/Common/IL/Interpreter.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
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
        return type.floatFormat == FloatFormat::F64 ? ~0ull : 0xFFFFFFFFull;
    case TypeKind::Pointer:
        return 0xFFFFFFFFull;
    default:
        return ~0ull;
    }
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
    return static_cast<double>(std::bit_cast<float>(static_cast<std::uint32_t>(bits)));
}

std::uint64_t FromFloat(double value, const Type& type)
{
    if (type.floatFormat == FloatFormat::F64)
        return std::bit_cast<std::uint64_t>(value);
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
