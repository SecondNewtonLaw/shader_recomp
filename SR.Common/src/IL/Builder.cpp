#include "SR/Common/IL/Builder.hpp"

#include <bit>
#include <utility>

namespace SR::IL {

FunctionBuilder::FunctionBuilder(Module& module, Function& function)
    : m_module(module)
    , m_function(function)
{
    if (m_function.entry != InvalidBlockId)
        m_currentBlock = m_function.entry;
}

BlockId FunctionBuilder::CreateBlock(std::string hint)
{
    Block block;
    block.hint = std::move(hint);
    m_function.blocks.push_back(std::move(block));
    const auto id = static_cast<BlockId>(m_function.blocks.size() - 1);

    if (m_function.entry == InvalidBlockId)
        m_function.entry = id;
    if (m_currentBlock == InvalidBlockId)
        m_currentBlock = id;

    return id;
}

ValueId FunctionBuilder::AddValue(TypeId type, ValueKind kind, std::uint32_t index, BlockId block, bool uniform)
{
    Value value;
    value.type = type;
    value.kind = kind;
    value.index = index;
    value.block = block;
    value.uniform = uniform;
    m_function.values.push_back(value);
    return static_cast<ValueId>(m_function.values.size() - 1);
}

ValueId FunctionBuilder::Undef(TypeId type, bool uniform)
{
    return m_module.AddUndef(m_function, type, uniform);
}

ValueId FunctionBuilder::ConstantBool(bool value)
{
    const auto constant = m_module.AddConstant(m_module.types.Bool(), value ? 1u : 0u);
    return AddValue(m_module.types.Bool(), ValueKind::Constant, constant, InvalidBlockId, true);
}

ValueId FunctionBuilder::ConstantI32(std::int32_t value)
{
    const TypeId type = m_module.types.I32();
    const auto constant = m_module.AddConstant(type, static_cast<std::uint64_t>(static_cast<std::uint32_t>(value)));
    return AddValue(type, ValueKind::Constant, constant, InvalidBlockId, true);
}

ValueId FunctionBuilder::ConstantU32(std::uint32_t value)
{
    const TypeId type = m_module.types.U32();
    const auto constant = m_module.AddConstant(type, value);
    return AddValue(type, ValueKind::Constant, constant, InvalidBlockId, true);
}

ValueId FunctionBuilder::ConstantI64(std::int64_t value)
{
    const TypeId type = m_module.types.I64();
    const auto constant = m_module.AddConstant(type, static_cast<std::uint64_t>(value));
    return AddValue(type, ValueKind::Constant, constant, InvalidBlockId, true);
}

ValueId FunctionBuilder::ConstantU64(std::uint64_t value)
{
    const TypeId type = m_module.types.U64();
    const auto constant = m_module.AddConstant(type, value);
    return AddValue(type, ValueKind::Constant, constant, InvalidBlockId, true);
}

ValueId FunctionBuilder::ConstantF16(std::uint16_t bits)
{
    const TypeId type = m_module.types.Float(FloatFormat::F16);
    const auto constant = m_module.AddConstant(type, bits);
    return AddValue(type, ValueKind::Constant, constant, InvalidBlockId, true);
}

ValueId FunctionBuilder::ConstantF32(float value)
{
    const TypeId type = m_module.types.F32();
    const auto constant = m_module.AddConstant(type, std::bit_cast<std::uint32_t>(value));
    return AddValue(type, ValueKind::Constant, constant, InvalidBlockId, true);
}

ValueId FunctionBuilder::ConstantF64(double value)
{
    const TypeId type = m_module.types.Float(FloatFormat::F64);
    const auto constant = m_module.AddConstant(type, std::bit_cast<std::uint64_t>(value));
    return AddValue(type, ValueKind::Constant, constant, InvalidBlockId, true);
}

Instruction& FunctionBuilder::Append(Instruction instruction)
{
    auto& body = m_function.blocks[m_currentBlock].body;
    body.push_back(std::move(instruction));
    return body.back();
}

std::vector<ValueId> FunctionBuilder::EmitMulti(const std::vector<TypeId>& resultTypes,
                                                Op op,
                                                std::uint16_t subop,
                                                std::vector<ValueId> operands,
                                                Attrs attrs,
                                                PayloadId payload,
                                                std::uint32_t sourcePc,
                                                std::uint16_t sourceOpcode,
                                                bool uniform)
{
    Instruction instruction;
    instruction.op = op;
    instruction.subop = subop;
    instruction.operands = std::move(operands);
    instruction.attrs = attrs;
    instruction.payload = payload;
    instruction.sourcePc = sourcePc;
    instruction.sourceOpcode = sourceOpcode;

    std::vector<ValueId> results;
    results.reserve(resultTypes.size());
    for (const TypeId type : resultTypes)
        results.push_back(AddValue(type, ValueKind::Instruction, 0, m_currentBlock, uniform));

    instruction.results = results;
    (void)Append(std::move(instruction));
    return results;
}

ValueId FunctionBuilder::Emit(TypeId resultType,
                               Op op,
                               std::uint16_t subop,
                               std::vector<ValueId> operands,
                               Attrs attrs,
                               PayloadId payload,
                               std::uint32_t sourcePc,
                               std::uint16_t sourceOpcode,
                               bool uniform)
{
    const auto results = EmitMulti({resultType}, op, subop, std::move(operands), attrs, payload, sourcePc, sourceOpcode, uniform);
    return results.front();
}

void FunctionBuilder::EmitVoid(Op op,
                               std::uint16_t subop,
                               std::vector<ValueId> operands,
                               Attrs attrs,
                               PayloadId payload,
                               std::uint32_t sourcePc,
                               std::uint16_t sourceOpcode)
{
    (void)EmitMulti({}, op, subop, std::move(operands), attrs, payload, sourcePc, sourceOpcode, false);
}

ValueId FunctionBuilder::Phi(BlockId block, TypeId type, std::vector<PhiIncoming> incoming, bool uniform)
{
    const ValueId result = AddValue(type, ValueKind::Instruction, 0, block, uniform);

    Instruction instruction;
    instruction.op = Op::Phi;
    instruction.results.push_back(result);
    instruction.incoming = std::move(incoming);

    auto& body = m_function.blocks[block].body;
    std::size_t position = 0;
    while (position < body.size() && body[position].op == Op::Phi)
        ++position;
    body.insert(body.begin() + static_cast<std::ptrdiff_t>(position), std::move(instruction));
    return result;
}

void FunctionBuilder::Terminate(BlockId block, Terminator terminator)
{
    m_function.blocks[block].terminator = std::move(terminator);
}

ValueId FunctionBuilder::Alloca(TypeId pointerType, BlockId block, bool uniform)
{
    if (block == InvalidBlockId)
        block = m_currentBlock;

    const ValueId result = AddValue(pointerType, ValueKind::Instruction, 0, block, uniform);

    Instruction instruction;
    instruction.op = Op::Alloca;
    instruction.results.push_back(result);

    auto& body = m_function.blocks[block].body;
    std::size_t position = 0;
    while (position < body.size() && (body[position].op == Op::Phi || body[position].op == Op::Alloca))
        ++position;
    body.insert(body.begin() + static_cast<std::ptrdiff_t>(position), std::move(instruction));

    return result;
}

ValueId FunctionBuilder::Load(TypeId type, ValueId pointer, std::uint32_t sourcePc, bool uniform)
{
    return Emit(type, Op::Load, 0, {pointer}, {}, NoPayload, sourcePc, 0, uniform);
}

void FunctionBuilder::Store(ValueId pointer, ValueId value, std::uint32_t sourcePc)
{
    EmitVoid(Op::Store, 0, {pointer, value}, {}, NoPayload, sourcePc);
}

ValueId FunctionBuilder::SelectValue(TypeId type, ValueId condition, ValueId ifTrue, ValueId ifFalse, bool uniform)
{
    return Emit(type, Op::Select, 0, {condition, ifTrue, ifFalse}, {}, NoPayload, 0, 0, uniform);
}

ValueId FunctionBuilder::CompareValue(CompareOp op, ValueId left, ValueId right, bool uniform)
{
    return Emit(m_module.types.Bool(), Op::Compare, static_cast<std::uint16_t>(op), {left, right}, {}, NoPayload, 0, 0, uniform);
}

ValueId FunctionBuilder::UnaryValue(UnaryOp op, TypeId type, ValueId operand, bool uniform)
{
    return Emit(type, Op::Unary, static_cast<std::uint16_t>(op), {operand}, {}, NoPayload, 0, 0, uniform);
}

ValueId FunctionBuilder::ConvertValue(TypeId type, ConvertOp op, ValueId operand, bool uniform)
{
    return Emit(type, Op::Convert, static_cast<std::uint16_t>(op), {operand}, {}, NoPayload, 0, 0, uniform);
}

ValueId FunctionBuilder::SpecialReg(SpecialRegId id, TypeId type, bool uniform)
{
    const PayloadId payload = m_module.payloads.Add(SpecialRegPayload{id});
    return IntrinsicValue(type, IntrinsicId::SpecialReg, {}, payload, 0, uniform);
}

ValueId FunctionBuilder::IntrinsicValue(TypeId type,
                                        IntrinsicId id,
                                        std::vector<ValueId> operands,
                                        PayloadId payload,
                                        std::uint32_t sourcePc,
                                        bool uniform)
{
    return Emit(type, Op::Intrinsic, static_cast<std::uint16_t>(id), std::move(operands), {}, payload, sourcePc, 0, uniform);
}

void FunctionBuilder::IntrinsicVoid(IntrinsicId id,
                                    std::vector<ValueId> operands,
                                    PayloadId payload,
                                    std::uint32_t sourcePc)
{
    EmitVoid(Op::Intrinsic, static_cast<std::uint16_t>(id), std::move(operands), {}, payload, sourcePc);
}

Function& CreateFunction(Module& module, std::string name, TypeId returnType)
{
    Function function;
    function.id = static_cast<FunctionId>(module.functions.size());
    function.name = std::move(name);
    function.returnType = returnType;
    module.functions.push_back(std::move(function));
    return module.functions.back();
}

} // namespace SR::IL
