#include "SR/Common/IL/Module.hpp"

#include <utility>

namespace SR::IL {

PayloadId PayloadPool::Add(Payload payload)
{
    m_payloads.push_back(std::move(payload));
    return static_cast<PayloadId>(m_payloads.size());
}

const Payload& PayloadPool::Get(PayloadId id) const
{
    return m_payloads[id - 1];
}

Function* Module::FindFunction(FunctionId id) noexcept
{
    if (id == InvalidFunctionId || id >= functions.size())
        return nullptr;
    return &functions[id];
}

const Function* Module::FindFunction(FunctionId id) const noexcept
{
    if (id == InvalidFunctionId || id >= functions.size())
        return nullptr;
    return &functions[id];
}

Function& Module::GetFunction(FunctionId id)
{
    return functions[id];
}

const Function& Module::GetFunction(FunctionId id) const
{
    return functions[id];
}

const Type& Module::TypeOf(const Function& function, ValueId value) const
{
    return types.Get(function.values[value].type);
}

ValueId Module::AddConstant(TypeId type, std::uint64_t bits)
{
    for (std::size_t i = 0; i < constants.size(); ++i)
    {
        if (constants[i].type == type && constants[i].bits == bits)
            return static_cast<ValueId>(i);
    }

    constants.push_back(Constant{type, bits});
    return static_cast<ValueId>(constants.size() - 1);
}

ValueId Module::AddUndef(Function& function, TypeId type, bool uniform)
{
    Value value;
    value.type = type;
    value.kind = ValueKind::Undef;
    value.uniform = uniform;
    function.values.push_back(value);
    return static_cast<ValueId>(function.values.size() - 1);
}

ValueId Module::AddInstructionValue(Function& function, TypeId type, BlockId block, bool uniform)
{
    Value value;
    value.type = type;
    value.kind = ValueKind::Instruction;
    value.block = block;
    value.uniform = uniform;
    function.values.push_back(value);
    return static_cast<ValueId>(function.values.size() - 1);
}

} // namespace SR::IL
