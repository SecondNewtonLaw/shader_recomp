#include "SR/Common/IL/Dump.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "SR/Common/IL/Attributes.hpp"

namespace SR::IL {
namespace {

std::string Hex(std::uint64_t value)
{
    std::ostringstream stream;
    stream << "0x" << std::hex << value;
    return stream.str();
}

std::string DescribeConstant(const Module& module, const Constant& constant)
{
    const Type& type = module.types.Get(constant.type);

    switch (type.kind)
    {
    case TypeKind::Bool:
        return constant.bits != 0 ? "true" : "false";
    case TypeKind::Int:
        if (type.isSigned)
        {
            if (type.width == 64)
                return std::to_string(static_cast<std::int64_t>(constant.bits));
            if (type.width == 32)
                return std::to_string(static_cast<std::int32_t>(constant.bits));
            return std::to_string(static_cast<std::int32_t>(constant.bits));
        }
        return std::to_string(constant.bits);
    case TypeKind::Float:
        if (type.floatFormat == FloatFormat::F32)
        {
            const float value = std::bit_cast<float>(static_cast<std::uint32_t>(constant.bits));
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }
        if (type.floatFormat == FloatFormat::F64)
        {
            const double value = std::bit_cast<double>(constant.bits);
            std::ostringstream stream;
            stream << value;
            return stream.str();
        }
        return Hex(constant.bits);
    default:
        return Hex(constant.bits);
    }
}

std::string DescribePayload(const Module& module, PayloadId id)
{
    if (id == NoPayload || id > module.payloads.size())
        return {};

    const Payload& payload = module.payloads.Get(id);
    return std::visit(
        [](const auto& value) -> std::string {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, std::monostate>)
                return {};
            else if constexpr (std::is_same_v<T, SpecialRegPayload>)
                return "special-reg " + std::string(ToString(value.id));
            else if constexpr (std::is_same_v<T, VotePayload>)
                return std::string("vote ") +
                       (value.mode == VoteMode::Any ? "any" : value.mode == VoteMode::All ? "all" : "uniform");
            else if constexpr (std::is_same_v<T, ShufflePayload>)
                return "shuffle " + std::to_string(static_cast<int>(value.mode));
            else if constexpr (std::is_same_v<T, BarrierPayload>)
                return "barrier " + std::string(ToString(value.order)) + " " + std::string(ToString(value.scope));
            else if constexpr (std::is_same_v<T, FencePayload>)
                return "fence " + std::string(ToString(value.order)) + " " + std::string(ToString(value.scope));
            else if constexpr (std::is_same_v<T, SampleDesc>)
                return "sample dim=" + std::to_string(value.dimensions);
            else if constexpr (std::is_same_v<T, DotDesc>)
                return "dot bits=" + std::to_string(value.inputBits) + " n=" + std::to_string(value.elements);
            else if constexpr (std::is_same_v<T, TestBufferDesc>)
                return "buffer set=" + std::to_string(value.set) + " binding=" + std::to_string(value.binding);
            else if constexpr (std::is_same_v<T, ImageAccessDesc>)
                return "image set=" + std::to_string(value.set) + " binding=" + std::to_string(value.binding) +
                       (value.sampled ? " sampler=" + std::to_string(value.samplerBinding) : std::string()) +
                       " component=" + std::to_string(value.component);
            else if constexpr (std::is_same_v<T, UserDataDesc>)
                return "user-data[" + std::to_string(value.index) + "]";
            else if constexpr (std::is_same_v<T, StageIoDesc>)
                return (value.space == Space::Input ? "in " : "out ")
                     + (value.builtin != Builtin::None ? "builtin " + std::to_string(static_cast<int>(value.builtin))
                                                      : "loc " + std::to_string(value.location))
                     + "." + std::to_string(value.component);
            else
                return "send " + Hex(value.message);
        },
        payload);
}

std::string FormatOperandList(const std::vector<ValueId>& operands)
{
    std::string text;
    for (const ValueId operand : operands)
    {
        if (!text.empty())
            text += ", ";
        text += "%" + std::to_string(operand);
    }
    return text;
}

std::string FormatTerminator(const Terminator& terminator)
{
    switch (terminator.kind)
    {
    case TerminatorKind::Br:
        return "br block" + std::to_string(terminator.target);
    case TerminatorKind::CondBr:
        return "condbr " + (terminator.uniformHint ? std::string("uniform ") : std::string()) + "%" +
               std::to_string(terminator.condition) + ", block" + std::to_string(terminator.trueTarget) +
               ", block" + std::to_string(terminator.falseTarget);
    case TerminatorKind::Switch:
    {
        std::string text = "switch %" + std::to_string(terminator.selector) + ", default block" +
                           std::to_string(terminator.defaultTarget);
        for (const auto& entry : terminator.cases)
        {
            text += ", case " + std::to_string(entry.literal) + " -> block" + std::to_string(entry.target);
        }
        return text;
    }
    case TerminatorKind::IndirectBr:
    {
        std::string text = "indirectbr";
        for (const BlockId target : terminator.targets)
            text += " block" + std::to_string(target);
        return text;
    }
    case TerminatorKind::Ret:
        return terminator.hasReturnValue ? "ret %" + std::to_string(terminator.returnValue) : "ret";
    case TerminatorKind::Unreachable:
        return "unreachable";
    }

    return "?";
}

std::string FormatInstruction(const Module& module, const Instruction& instruction)
{
    std::string text;
    if (instruction.guard != InvalidValue)
        text += "if %" + std::to_string(instruction.guard) + " ";
    if (!instruction.results.empty())
    {
        if (instruction.results.size() == 1)
            text += "%" + std::to_string(instruction.results[0]) + " = ";
        else
            text += "{" + FormatOperandList(instruction.results) + "} = ";
    }

    text += std::string(ToString(instruction.op));

    switch (instruction.op)
    {
    case Op::Unary:
        text += "." + std::string(ToString(static_cast<UnaryOp>(instruction.subop)));
        break;
    case Op::Binary:
        text += "." + std::string(ToString(static_cast<BinaryOp>(instruction.subop)));
        break;
    case Op::Ternary:
        text += "." + std::string(ToString(static_cast<TernaryOp>(instruction.subop)));
        break;
    case Op::Compare:
        text += "." + std::string(ToString(static_cast<CompareOp>(instruction.subop)));
        break;
    case Op::Convert:
        text += "." + std::string(ToString(static_cast<ConvertOp>(instruction.subop)));
        break;
    case Op::Intrinsic:
        text += "." + std::string(ToString(static_cast<IntrinsicId>(instruction.subop)));
        break;
    case Op::Kill:
        text += instruction.subop == static_cast<std::uint16_t>(KillKind::Demote) ? ".demote" : ".discard";
        break;
    default:
        break;
    }

    if (!instruction.operands.empty())
        text += " " + FormatOperandList(instruction.operands);

    if (instruction.op == Op::Phi)
    {
        for (const auto& incoming : instruction.incoming)
        {
            text += " [%" + std::to_string(incoming.value) + ", block" + std::to_string(incoming.block) + "]";
        }
    }

    if (instruction.op == Op::Call && instruction.callee != InvalidFunctionId)
        text += " callee=fn" + std::to_string(instruction.callee);

    const std::string attrs = DescribeAttrs(instruction.attrs);
    if (!attrs.empty())
        text += " {" + attrs + "}";

    if (instruction.payload != NoPayload)
    {
        const std::string payload = DescribePayload(module, instruction.payload);
        text += payload.empty() ? " payload=" + std::to_string(instruction.payload)
                                : " " + payload;
    }

    if (instruction.sourcePc != 0)
        text += " ; pc=" + Hex(instruction.sourcePc);

    return text;
}

} // namespace

std::string DumpType(const Module& module, TypeId id)
{
    if (id == InvalidType || !module.types.contains(id))
        return "?";

    const Type& type = module.types.Get(id);
    switch (type.kind)
    {
    case TypeKind::Vector:
        return "v" + std::to_string(type.count) + "<" + DumpType(module, type.element) + ">";
    case TypeKind::Packed:
        return "packed" + std::to_string(type.count) + "<" + DumpType(module, type.element) + ">";
    case TypeKind::Array:
        return "array" + std::to_string(type.count) + "<" + DumpType(module, type.element) + ">";
    case TypeKind::Pointer:
        return "ptr<" + std::string(ToString(type.space)) + ", " + DumpType(module, type.element) + ">";
    case TypeKind::Struct:
    {
        std::string text = "struct{";
        for (std::size_t i = 0; i < type.members.size(); ++i)
        {
            if (i != 0)
                text += ", ";
            text += DumpType(module, type.members[i]);
        }
        text += "}";
        return text;
    }
    default:
        return DescribeType(type);
    }
}

std::string DumpModule(const Module& module)
{
    std::ostringstream out;
    out << "il 1\n";
    out << "stage " << ToString(module.stage) << "\n";
    out << "entry " << (module.entryPoint.empty() ? "<none>" : module.entryPoint) << "\n";
    out << "arch " << (module.architecture.empty() ? "<none>" : module.architecture) << "\n";

    out << "types:\n";
    for (TypeId id = 0; id < module.types.size(); ++id)
        out << "  %t" << id << " = " << DumpType(module, id) << "\n";

    out << "constants:\n";
    for (std::size_t i = 0; i < module.constants.size(); ++i)
    {
        out << "  #" << i << " = " << DumpType(module, module.constants[i].type) << " "
            << DescribeConstant(module, module.constants[i]) << "\n";
    }

    out << "resources:\n";
    for (const auto& resource : module.resources)
    {
        out << "  #" << resource.id << " = " << ToString(resource.kind) << " set=" << resource.set
            << " binding=" << resource.binding << " element=" << DumpType(module, resource.element) << "\n";
    }

    for (const SignatureDecl& input : module.inputs)
        out << "input " << input.location << "." << input.component << " " << DumpType(module, input.type) << "\n";
    for (const SignatureDecl& output : module.outputs)
        out << "output " << output.location << "." << output.component << " " << DumpType(module, output.type) << "\n";

    for (const Function& function : module.functions)
    {
        out << "function fn" << function.id << " " << function.name << "(";
        for (std::size_t i = 0; i < function.params.size(); ++i)
        {
            if (i != 0)
                out << ", ";
            out << DumpType(module, function.params[i]);
        }
        out << ") -> " << DumpType(module, function.returnType) << "\n";

        out << "values:\n";
        for (ValueId id = 0; id < function.values.size(); ++id)
        {
            const Value& value = function.values[id];
            out << "  %" << id << " : " << DumpType(module, value.type) << " = ";
            switch (value.kind)
            {
            case ValueKind::Undef:
                out << "undef";
                break;
            case ValueKind::Constant:
                out << "const #" << value.index;
                break;
            case ValueKind::Instruction:
                out << "inst block" << value.block;
                break;
            case ValueKind::Parameter:
                out << "param " << value.index;
                break;
            case ValueKind::Resource:
                out << "resource #" << value.index;
                break;
            }
            if (value.uniform)
                out << " uniform";
            out << "\n";
        }

        for (BlockId block = 0; block < function.blocks.size(); ++block)
        {
            const Block& body = function.blocks[block];
            out << "block" << block << ":" << (body.hint.empty() ? "" : " ; " + body.hint) << "\n";
            for (const Instruction& instruction : body.body)
                out << "  " << FormatInstruction(module, instruction) << "\n";
            out << "  " << FormatTerminator(body.terminator) << "\n";
        }
    }

    return out.str();
}

} // namespace SR::IL
