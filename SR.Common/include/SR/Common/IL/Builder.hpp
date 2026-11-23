#pragma once

#include <cstdint>
#include <vector>

#include "SR/Common/IL/Module.hpp"

namespace SR::IL {

// Builds one function. Instructions are appended to the builder's current
// block; values are created in deterministic order.
class FunctionBuilder
{
public:
    FunctionBuilder(Module& module, Function& function);

    [[nodiscard]] BlockId CreateBlock(std::string hint = {});
    [[nodiscard]] BlockId Entry() const noexcept { return m_function.entry; }
    void SetEntry(BlockId block) noexcept { m_function.entry = block; }
    [[nodiscard]] BlockId CurrentBlock() const noexcept { return m_currentBlock; }
    void SetCurrentBlock(BlockId block) noexcept { m_currentBlock = block; }

    [[nodiscard]] Block& GetBlock(BlockId block) noexcept { return m_function.blocks[block]; }
    [[nodiscard]] const Block& GetBlock(BlockId block) const noexcept { return m_function.blocks[block]; }

    [[nodiscard]] ValueId Undef(TypeId type, bool uniform = false);
    [[nodiscard]] ValueId ConstantBool(bool value);
    [[nodiscard]] ValueId ConstantI32(std::int32_t value);
    [[nodiscard]] ValueId ConstantU32(std::uint32_t value);
    [[nodiscard]] ValueId ConstantI64(std::int64_t value);
    [[nodiscard]] ValueId ConstantU64(std::uint64_t value);
    [[nodiscard]] ValueId ConstantF16(std::uint16_t bits);
    [[nodiscard]] ValueId ConstantF32(float value);
    [[nodiscard]] ValueId ConstantF64(double value);

    // Appends a result-producing instruction and returns its value.
    [[nodiscard]] ValueId Emit(TypeId resultType,
                              Op op,
                              std::uint16_t subop,
                              std::vector<ValueId> operands,
                              Attrs attrs = {},
                              PayloadId payload = NoPayload,
                              std::uint32_t sourcePc = 0,
                              std::uint16_t sourceOpcode = 0,
                              bool uniform = false);

    // Appends an instruction with an explicit result count (multi-result ops).
    [[nodiscard]] std::vector<ValueId> EmitMulti(const std::vector<TypeId>& resultTypes,
                                                Op op,
                                                std::uint16_t subop,
                                                std::vector<ValueId> operands,
                                                Attrs attrs = {},
                                                PayloadId payload = NoPayload,
                                                std::uint32_t sourcePc = 0,
                                                std::uint16_t sourceOpcode = 0,
                                                bool uniform = false);

    void EmitVoid(Op op,
                  std::uint16_t subop,
                  std::vector<ValueId> operands,
                  Attrs attrs = {},
                  PayloadId payload = NoPayload,
                  std::uint32_t sourcePc = 0,
                  std::uint16_t sourceOpcode = 0);

    [[nodiscard]] ValueId Phi(BlockId block,
                             TypeId type,
                             std::vector<PhiIncoming> incoming,
                             bool uniform = false);

    // Function-local storage. The pointer type is explicit; the alloca is
    // inserted at the start of `block` (entry by default), after other allocas.
    [[nodiscard]] ValueId Alloca(TypeId pointerType, BlockId block = InvalidBlockId, bool uniform = true);
    [[nodiscard]] ValueId Load(TypeId type, ValueId pointer, std::uint32_t sourcePc = 0, bool uniform = false);
    void Store(ValueId pointer, ValueId value, std::uint32_t sourcePc = 0);

    [[nodiscard]] ValueId SelectValue(TypeId type,
                                      ValueId condition,
                                      ValueId ifTrue,
                                      ValueId ifFalse,
                                      bool uniform = false);
    [[nodiscard]] ValueId CompareValue(CompareOp op, ValueId left, ValueId right, bool uniform = false);
    [[nodiscard]] ValueId UnaryValue(UnaryOp op, TypeId type, ValueId operand, bool uniform = false);
    [[nodiscard]] ValueId ConvertValue(TypeId type, ConvertOp op, ValueId operand, bool uniform = false);

    void Terminate(BlockId block, Terminator terminator);

    [[nodiscard]] ValueId SpecialReg(SpecialRegId id, TypeId type, bool uniform = true);
    [[nodiscard]] ValueId IntrinsicValue(TypeId type,
                                         IntrinsicId id,
                                         std::vector<ValueId> operands,
                                         PayloadId payload = NoPayload,
                                         std::uint32_t sourcePc = 0,
                                         bool uniform = false);
    void IntrinsicVoid(IntrinsicId id,
                       std::vector<ValueId> operands,
                       PayloadId payload = NoPayload,
                       std::uint32_t sourcePc = 0);

    [[nodiscard]] Module& ModuleRef() noexcept { return m_module; }
    [[nodiscard]] Function& FunctionRef() noexcept { return m_function; }

private:
    [[nodiscard]] ValueId AddValue(TypeId type, ValueKind kind, std::uint32_t index, BlockId block, bool uniform);
    [[nodiscard]] Instruction& Append(Instruction instruction);

    Module& m_module;
    Function& m_function;
    BlockId m_currentBlock = InvalidBlockId;
};

// Appends a fresh function and returns its id.
[[nodiscard]] Function& CreateFunction(Module& module, std::string name, TypeId returnType);

} // namespace SR::IL
