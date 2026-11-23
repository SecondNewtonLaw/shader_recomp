#include "SR/Frontend/Arch/Gfx/GfxLifter.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "SR/Common/IL/Builder.hpp"

namespace SR::Frontend::Gfx {
namespace {

Diagnostic LiftError(std::string code, std::string message, std::uint32_t pc)
{
    return Diagnostic::Error(std::move(code), std::move(message), pc);
}

constexpr std::uint32_t kLastSgpr = 105; // 106..127 are VCC/SCC/M0/EXEC

class Lifter
{
public:
    Lifter(IL::Module& module, IL::Function& function, IL::FunctionBuilder& builder, Arch arch)
        : m_module(module)
        , m_function(function)
        , m_builder(builder)
        , m_arch(arch)
    {
        m_sgprSlots.fill(IL::InvalidValue);
        m_vgprSlots.fill(IL::InvalidValue);
    }

    [[nodiscard]] Result<void> Run(std::span<const std::uint32_t> words)
    {
        const auto decoded = DecodeProgram(words);
        if (!decoded)
            return std::unexpected(decoded.error());

        m_items = std::move(decoded->items);
        m_byPc = std::move(decoded->byPc);

        if (m_items.empty())
            return std::unexpected(LiftError("SR-GFX-LIFT-001", "code object has no instructions", 0));

        const auto leaders = ComputeLeaders();
        if (!leaders)
            return std::unexpected(leaders.error());

        m_leaders = *leaders;
        m_blockByPc.clear();
        m_pcByBlock.clear();

        for (std::size_t i = 0; i < m_leaders.size(); ++i)
        {
            const IL::BlockId block = m_builder.CreateBlock("pc=" + std::to_string(m_leaders[i]));
            m_blockByPc[m_leaders[i]] = block;
            m_pcByBlock.push_back(m_leaders[i]);
        }

        m_entry = m_blockByPc.at(m_leaders.front());
        m_builder.SetEntry(m_entry);

        for (std::size_t blockIndex = 0; blockIndex < m_leaders.size(); ++blockIndex)
        {
            const auto status = LiftBlock(blockIndex);
            if (!status)
                return status;
        }

        return {};
    }

private:
    struct Program
    {
        std::vector<DecodedInstruction> items;
        std::unordered_map<std::uint32_t, std::size_t> byPc;
    };

    [[nodiscard]] Result<Program> DecodeProgram(std::span<const std::uint32_t> words) const
    {
        Program program;
        std::size_t index = 0;
        while (index < words.size())
        {
            const std::uint32_t pc = static_cast<std::uint32_t>(index * sizeof(std::uint32_t));
            const auto instruction = Decode(words.subspan(index), m_arch, pc);
            if (!instruction)
                return std::unexpected(instruction.error());

            program.byPc.emplace(pc, program.items.size());
            program.items.push_back(*instruction);

            if (instruction->op == Op::S_Endpgm)
                break;

            index += instruction->wordCount;
        }

        return program;
    }

    [[nodiscard]] Result<std::uint32_t> BranchTarget(const DecodedInstruction& instruction) const
    {
        if (instruction.operands.empty() || instruction.operands[0].kind != OperandKind::Label)
            return std::unexpected(LiftError("SR-GFX-LIFT-002", "branch has no label operand", instruction.pc));

        const auto relative = static_cast<std::int16_t>(instruction.operands[0].value & 0xFFFFu);
        const std::int64_t target = static_cast<std::int64_t>(instruction.pc) + 4 + static_cast<std::int64_t>(relative) * 4;
        if (target < 0)
            return std::unexpected(LiftError("SR-GFX-LIFT-003", "branch target is before the code object", instruction.pc));

        const auto pc = static_cast<std::uint32_t>(target);
        if (m_byPc.find(pc) == m_byPc.end())
            return std::unexpected(LiftError("SR-GFX-LIFT-004", "branch target is not an instruction boundary", instruction.pc));

        return pc;
    }

    [[nodiscard]] static bool IsBranch(Op op) noexcept
    {
        switch (op)
        {
        case Op::S_Branch:
        case Op::S_CbranchScc0:
        case Op::S_CbranchScc1:
        case Op::S_CbranchVccz:
        case Op::S_CbranchVccnz:
        case Op::S_CbranchExecz:
        case Op::S_CbranchExecnz:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] Result<std::vector<std::uint32_t>> ComputeLeaders() const
    {
        std::set<std::uint32_t> leaders{0};

        for (const DecodedInstruction& instruction : m_items)
        {
            if (!IsBranch(instruction.op))
                continue;

            const auto target = BranchTarget(instruction);
            if (!target)
                return std::unexpected(target.error());
            leaders.insert(*target);

            if (instruction.op != Op::S_Branch)
            {
                const std::uint32_t fallthrough = instruction.pc + 4;
                if (m_byPc.find(fallthrough) == m_byPc.end())
                    return std::unexpected(LiftError("SR-GFX-LIFT-005",
                                                     "conditional branch has no fallthrough instruction",
                                                     instruction.pc));
                leaders.insert(fallthrough);
            }
        }

        return std::vector<std::uint32_t>(leaders.begin(), leaders.end());
    }

    [[nodiscard]] IL::BlockId BlockForPc(std::uint32_t pc, std::uint32_t sourcePc) const
    {
        const auto it = m_blockByPc.find(pc);
        if (it == m_blockByPc.end())
            return IL::InvalidBlockId;
        (void)sourcePc;
        return it->second;
    }

    [[nodiscard]] IL::ValueId Slot(std::array<IL::ValueId, 256>& slots, std::uint32_t index, IL::RegisterClass kind)
    {
        IL::ValueId& slot = slots[index];
        if (slot == IL::InvalidValue)
        {
            const IL::TypeId pointer = m_module.types.Pointer(IL::Space::Private, m_module.types.U32());
            slot = m_builder.Alloca(pointer, m_entry);
            m_module.registerSlots.push_back({kind, index, slot});
        }
        return slot;
    }

    [[nodiscard]] IL::ValueId SccSlot()
    {
        if (m_sccSlot == IL::InvalidValue)
        {
            const IL::TypeId pointer = m_module.types.Pointer(IL::Space::Private, m_module.types.Bool());
            m_sccSlot = m_builder.Alloca(pointer, m_entry);
            m_module.registerSlots.push_back({IL::RegisterClass::Scc, 0, m_sccSlot});
        }
        return m_sccSlot;
    }

    [[nodiscard]] IL::ValueId VccSlot()
    {
        if (m_vccSlot == IL::InvalidValue)
        {
            const IL::TypeId pointer = m_module.types.Pointer(IL::Space::Private, m_module.types.Bool());
            m_vccSlot = m_builder.Alloca(pointer, m_entry);
            m_module.registerSlots.push_back({IL::RegisterClass::Vcc, 0, m_vccSlot});
        }
        return m_vccSlot;
    }

    [[nodiscard]] Result<IL::ValueId> ReadSgpr(std::uint32_t index, std::uint32_t pc)
    {
        if (index > kLastSgpr)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-006",
                                             "special scalar register " + std::to_string(index) +
                                                 " (VCC/SCC/M0/EXEC) is not supported yet",
                                             pc));
        }
        return m_builder.Load(m_module.types.U32(), Slot(m_sgprSlots, index, IL::RegisterClass::Sgpr), pc, true);
    }

    [[nodiscard]] Result<IL::ValueId> ReadVgpr(std::uint32_t index, std::uint32_t pc)
    {
        return m_builder.Load(m_module.types.U32(), Slot(m_vgprSlots, index, IL::RegisterClass::Vgpr), pc, false);
    }

    [[nodiscard]] Result<IL::ValueId> ReadValue(const Operand& operand, std::uint32_t pc)
    {
        switch (operand.kind)
        {
        case OperandKind::Sgpr:
            if (operand.value >= 256)
                return std::unexpected(LiftError("SR-GFX-LIFT-007", "SGPR index out of range", pc));
            return ReadSgpr(operand.value, pc);
        case OperandKind::Vgpr:
        case OperandKind::Vsrc:
            if (operand.value >= 256)
                return std::unexpected(LiftError("SR-GFX-LIFT-008", "VGPR index out of range", pc));
            return ReadVgpr(operand.value, pc);
        case OperandKind::Literal:
            return m_builder.ConstantU32(operand.value);
        case OperandKind::Ssrc:
        case OperandKind::Src:
            if (operand.isInline)
                return InlineConstant(operand.value, pc);
            return std::unexpected(LiftError("SR-GFX-LIFT-009", "unresolved source operand", pc));
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-010", "operand is not a value", pc));
        }
    }

    [[nodiscard]] Result<IL::ValueId> InlineConstant(std::uint32_t index, std::uint32_t pc)
    {
        if (const auto bits = InlineFloatBits(index))
            return m_builder.ConstantU32(*bits);

        if ((index >= 128 && index <= 192) || (index >= 193 && index <= 208))
            return m_builder.ConstantU32(InlineInteger(index));

        return std::unexpected(LiftError("SR-GFX-LIFT-011",
                                         "unsupported inline constant index " + std::to_string(index),
                                         pc));
    }

    [[nodiscard]] IL::ValueId Bitcast(IL::TypeId type, IL::ValueId value)
    {
        if (m_function.values[value].type == type)
            return value;
        const bool uniform = m_function.values[value].uniform;
        return m_builder.Emit(type, IL::Op::Bitcast, 0, {value}, {}, IL::NoPayload, 0, 0, uniform);
    }

    [[nodiscard]] Result<IL::ValueId> ReadBits(const Operand& operand, std::uint32_t pc)
    {
        const auto value = ReadValue(operand, pc);
        if (!value)
            return value;
        return Bitcast(m_module.types.U32(), *value);
    }

    [[nodiscard]] Result<IL::ValueId> ReadFloat(const Operand& operand, std::uint32_t pc)
    {
        const auto value = ReadValue(operand, pc);
        if (!value)
            return value;
        return Bitcast(m_module.types.F32(), *value);
    }

    [[nodiscard]] Result<IL::ValueId> ReadSigned(const Operand& operand, std::uint32_t pc)
    {
        const auto value = ReadValue(operand, pc);
        if (!value)
            return value;
        return Bitcast(m_module.types.I32(), *value);
    }

    [[nodiscard]] Result<IL::ValueId> ReadScc(std::uint32_t pc)
    {
        return m_builder.Load(m_module.types.Bool(), SccSlot(), pc, true);
    }

    [[nodiscard]] Result<IL::ValueId> ReadVcc(std::uint32_t pc)
    {
        return m_builder.Load(m_module.types.Bool(), VccSlot(), pc, false);
    }

    [[nodiscard]] Result<void> WriteScc(IL::ValueId value, std::uint32_t pc)
    {
        m_builder.Store(SccSlot(), value, pc);
        return {};
    }

    [[nodiscard]] Result<void> WriteVcc(IL::ValueId value, std::uint32_t pc)
    {
        m_builder.Store(VccSlot(), value, pc);
        return {};
    }

    [[nodiscard]] Result<void> WriteSccIfNonZero(IL::ValueId value, std::uint32_t pc, bool uniform = true)
    {
        const IL::ValueId zero = m_builder.ConstantU32(0);
        const IL::ValueId nonzero = m_builder.CompareValue(IL::CompareOp::UNe, value, zero, uniform);
        return WriteScc(nonzero, pc);
    }

    [[nodiscard]] Result<void> WriteDest(const Operand& destination, IL::ValueId value, std::uint32_t pc)
    {
        switch (destination.kind)
        {
        case OperandKind::Sdst:
            if (destination.value > kLastSgpr)
            {
                return std::unexpected(LiftError("SR-GFX-LIFT-012",
                                                 "write to special scalar register " + std::to_string(destination.value),
                                                 pc));
            }
            m_builder.Store(Slot(m_sgprSlots, destination.value, IL::RegisterClass::Sgpr), value, pc);
            return {};
        case OperandKind::Vgpr:
            m_builder.Store(Slot(m_vgprSlots, destination.value, IL::RegisterClass::Vgpr), value, pc);
            return {};
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-013", "instruction has no writable destination", pc));
        }
    }

    [[nodiscard]] Result<void> LiftBinary(const DecodedInstruction& instruction,
                                          IL::BinaryOp subop,
                                          bool isFloat,
                                          bool uniform,
                                          bool updatesScc = false)
    {
        const auto left = isFloat ? ReadFloat(instruction.operands[1], instruction.pc)
                                  : ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = isFloat ? ReadFloat(instruction.operands[2], instruction.pc)
                                   : ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::TypeId type = isFloat ? m_module.types.F32() : m_module.types.U32();
        IL::ValueId result = m_builder.Emit(type,
                                            IL::Op::Binary,
                                            static_cast<std::uint16_t>(subop),
                                            {*left, *right},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            uniform);
        if (isFloat)
            result = Bitcast(m_module.types.U32(), result);

        if (updatesScc)
        {
            const auto scc = WriteSccIfNonZero(result, instruction.pc, uniform);
            if (!scc)
                return scc;
        }

        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftCarry(const DecodedInstruction& instruction, bool subtract)
    {
        const auto leftBits = ReadBits(instruction.operands[1], instruction.pc);
        if (!leftBits)
            return std::unexpected(leftBits.error());
        const auto rightBits = ReadBits(instruction.operands[2], instruction.pc);
        if (!rightBits)
            return std::unexpected(rightBits.error());

        const auto result = m_builder.Emit(m_module.types.U32(),
                                           IL::Op::Binary,
                                           static_cast<std::uint16_t>(subtract ? IL::BinaryOp::ISub : IL::BinaryOp::IAdd),
                                           {*leftBits, *rightBits},
                                           {},
                                           IL::NoPayload,
                                           instruction.pc,
                                           static_cast<std::uint16_t>(instruction.op),
                                           true);

        // Carry/borrow is unsigned: add carries when result < left, sub borrows
        // when left < right.
        const IL::ValueId carryLeft = subtract ? *leftBits : result;
        const IL::ValueId carryRight = subtract ? *rightBits : *leftBits;
        const IL::ValueId carry = m_builder.CompareValue(IL::CompareOp::ULt, carryLeft, carryRight, true);
        const auto scc = WriteScc(carry, instruction.pc);
        if (!scc)
            return scc;

        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // S_ADDC_U32 / S_SUBB_U32: carry/borrow in from SCC plus carry/borrow out.
    [[nodiscard]] Result<void> LiftCarryIn(const DecodedInstruction& instruction, bool subtract)
    {
        const auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());
        const auto carryIn = ReadScc(instruction.pc);
        if (!carryIn)
            return std::unexpected(carryIn.error());

        const IL::ValueId one = m_builder.ConstantU32(1);
        const IL::ValueId zero = m_builder.ConstantU32(0);
        const IL::ValueId carryValue = m_builder.SelectValue(m_module.types.U32(), *carryIn, one, zero, true);

        const IL::ValueId partial = m_builder.Emit(m_module.types.U32(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(subtract ? IL::BinaryOp::ISub
                                                                                       : IL::BinaryOp::IAdd),
                                                   {*left, *right},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   true);
        const IL::ValueId firstFlag = subtract ? m_builder.CompareValue(IL::CompareOp::ULt, *left, *right, true)
                                               : m_builder.CompareValue(IL::CompareOp::ULt, partial, *left, true);
        const IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(subtract ? IL::BinaryOp::ISub
                                                                                      : IL::BinaryOp::IAdd),
                                                  {partial, carryValue},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  true);
        const IL::ValueId secondFlag = subtract ? m_builder.CompareValue(IL::CompareOp::ULt, partial, carryValue, true)
                                                : m_builder.CompareValue(IL::CompareOp::ULt, result, partial, true);
        const IL::ValueId carry = m_builder.Emit(m_module.types.Bool(),
                                                 IL::Op::Binary,
                                                 static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                                 {firstFlag, secondFlag},
                                                 {},
                                                 IL::NoPayload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op),
                                                 true);
        const auto scc = WriteScc(carry, instruction.pc);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftVectorCompare(const DecodedInstruction& instruction,
                                                 IL::CompareOp compare,
                                                 bool isFloat,
                                                 bool isSigned)
    {
        const auto left = isFloat ? ReadFloat(instruction.operands[1], instruction.pc)
                                  : isSigned ? ReadSigned(instruction.operands[1], instruction.pc)
                                             : ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = isFloat ? ReadFloat(instruction.operands[2], instruction.pc)
                                   : isSigned ? ReadSigned(instruction.operands[2], instruction.pc)
                                              : ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::ValueId condition = m_builder.CompareValue(compare, *left, *right, false);
        return WriteVcc(condition, instruction.pc);
    }

    // V_CNDMASK_B32: select src0 when VCC is set, otherwise src1.
    [[nodiscard]] Result<void> LiftCndmask(const DecodedInstruction& instruction)
    {
        const auto condition = ReadVcc(instruction.pc);
        if (!condition)
            return std::unexpected(condition.error());
        const auto ifTrue = ReadBits(instruction.operands[1], instruction.pc);
        if (!ifTrue)
            return std::unexpected(ifTrue.error());
        const auto ifFalse = ReadBits(instruction.operands[2], instruction.pc);
        if (!ifFalse)
            return std::unexpected(ifFalse.error());

        const IL::ValueId selected =
            m_builder.SelectValue(m_module.types.U32(), *condition, *ifTrue, *ifFalse, false);
        return WriteDest(instruction.operands[0], selected, instruction.pc);
    }

    // V_MAC_F32 / V_FMAC_F32: destination = src0 * src1 + destination.
    [[nodiscard]] Result<void> LiftMac(const DecodedInstruction& instruction)
    {
        const auto a = ReadFloat(instruction.operands[1], instruction.pc);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadFloat(instruction.operands[2], instruction.pc);
        if (!b)
            return std::unexpected(b.error());
        const auto accumulator = ReadFloat(instruction.operands[0], instruction.pc);
        if (!accumulator)
            return std::unexpected(accumulator.error());

        IL::ValueId result = m_builder.Emit(m_module.types.F32(),
                                            IL::Op::Ternary,
                                            static_cast<std::uint16_t>(IL::TernaryOp::Fma),
                                            {*a, *b, *accumulator},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftTernaryFloat(const DecodedInstruction& instruction, IL::TernaryOp subop)
    {
        const auto a = ReadFloat(instruction.operands[1], instruction.pc);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadFloat(instruction.operands[2], instruction.pc);
        if (!b)
            return std::unexpected(b.error());
        const auto c = ReadFloat(instruction.operands[3], instruction.pc);
        if (!c)
            return std::unexpected(c.error());

        IL::ValueId result = m_builder.Emit(m_module.types.F32(),
                                            IL::Op::Ternary,
                                            static_cast<std::uint16_t>(subop),
                                            {*a, *b, *c},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftUnaryFloat(const DecodedInstruction& instruction, IL::UnaryOp subop)
    {
        const auto source = ReadFloat(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        IL::ValueId result = m_builder.UnaryValue(subop, m_module.types.F32(), *source, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] static IL::ValueId SignExtended16(std::uint32_t raw, IL::FunctionBuilder& builder)
    {
        const auto immediate = static_cast<std::int16_t>(raw & 0xFFFFu);
        return builder.ConstantU32(static_cast<std::uint32_t>(static_cast<std::int32_t>(immediate)));
    }

    // S_ADDK_I32 / S_ADDK_CO_I32: the destination is also the first source.
    [[nodiscard]] Result<void> LiftAddImmediate(const DecodedInstruction& instruction)
    {
        const auto source = ReadSgpr(instruction.operands[0].value, instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const IL::ValueId immediate = SignExtended16(instruction.operands[1].value, m_builder);

        const IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                                  {*source, immediate},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  true);
        const IL::ValueId carry = m_builder.CompareValue(IL::CompareOp::ULt, result, *source, true);
        const auto scc = WriteScc(carry, instruction.pc);
        if (!scc)
            return scc;

        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // S_MULK_I32: no SCC update, the destination is also the first source.
    [[nodiscard]] Result<void> LiftMulImmediate(const DecodedInstruction& instruction)
    {
        const auto source = ReadSgpr(instruction.operands[0].value, instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const IL::ValueId immediate = SignExtended16(instruction.operands[1].value, m_builder);

        const IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::IMul),
                                                  {*source, immediate},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  true);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // S_ANDN2_B32 / S_AND_NOT1_B32 and S_ORN2_B32 / S_OR_NOT1_B32.
    [[nodiscard]] Result<void> LiftAndOrNot(const DecodedInstruction& instruction, bool orMode)
    {
        const auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::ValueId inverted = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U32(), *right, true);
        const IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(orMode ? IL::BinaryOp::Or
                                                                                    : IL::BinaryOp::And),
                                                  {*left, inverted},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  true);
        const auto scc = WriteSccIfNonZero(result, instruction.pc, true);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // Scalar S_FF1/S_FLBIT and vector FFBH/FFBL/CLZ/CTZ all report -1 on a zero
    // input.
    [[nodiscard]] Result<void> LiftBitScan(const DecodedInstruction& instruction, bool leading, bool uniform)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        const IL::ValueId zero = m_builder.ConstantU32(0);
        const IL::ValueId isZero = m_builder.CompareValue(IL::CompareOp::UEq, *source, zero, uniform);
        const IL::ValueId count = m_builder.UnaryValue(leading ? IL::UnaryOp::Clz : IL::UnaryOp::Ctz,
                                                       m_module.types.U32(),
                                                       *source,
                                                       uniform);
        const IL::ValueId minusOne = m_builder.ConstantU32(0xFFFFFFFFu);
        const IL::ValueId result = m_builder.SelectValue(m_module.types.U32(), isZero, minusOne, count, uniform);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // V_BCNT_U32_B32: popcount(src0) + src1.
    [[nodiscard]] Result<void> LiftBitCount(const DecodedInstruction& instruction)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const auto addend = ReadBits(instruction.operands[2], instruction.pc);
        if (!addend)
            return std::unexpected(addend.error());

        const IL::ValueId count = m_builder.UnaryValue(IL::UnaryOp::PopCount, m_module.types.U32(), *source, false);
        const IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                                  {count, *addend},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // Test ABI addressing: the shader supplies a byte address, which maps to a
    // dword index in the single storage buffer at set 0, binding 0. The SMEM
    // base pair and the GLOBAL SADDR are descriptor references that are not
    // decoded yet; their low word is added so a caller can relocate the base.
    [[nodiscard]] Result<IL::ValueId> TestBufferIndex(const DecodedInstruction& instruction,
                                                      std::uint8_t addressIndex,
                                                      bool uniform)
    {
        const auto base = ReadBits(instruction.operands[addressIndex], instruction.pc);
        if (!base)
            return std::unexpected(base.error());

        IL::ValueId address = *base;
        const std::uint32_t immediate = instruction.operands[2].value;
        if (immediate != 0)
        {
            const IL::ValueId offset = m_builder.ConstantU32(immediate);
            address = m_builder.Emit(m_module.types.U32(),
                                     IL::Op::Binary,
                                     static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                     {address, offset},
                                     {},
                                     IL::NoPayload,
                                     instruction.pc,
                                     static_cast<std::uint16_t>(instruction.op),
                                     uniform);
        }

        const IL::ValueId two = m_builder.ConstantU32(2);
        return m_builder.Emit(m_module.types.U32(),
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                              {address, two},
                              {},
                              IL::NoPayload,
                              instruction.pc,
                              static_cast<std::uint16_t>(instruction.op),
                              uniform);
    }

    [[nodiscard]] Result<void> LiftBufferLoad(const DecodedInstruction& instruction)
    {
        if (!m_module.testAbi)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-025",
                                             "scalar and global loads require --test-abi (no descriptor model yet)",
                                             instruction.pc));
        }

        const bool scalar = instruction.encoding == Encoding::Smem;
        const auto index = TestBufferIndex(instruction, 1, scalar);
        if (!index)
            return std::unexpected(index.error());

        const IL::PayloadId payload = m_module.payloads.Add(IL::TestBufferDesc{0, 0});
        const IL::ValueId result = m_builder.IntrinsicValue(m_module.types.U32(),
                                                            IL::IntrinsicId::TestBufferLoad,
                                                            {*index},
                                                            payload,
                                                            instruction.pc,
                                                            scalar);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftCompare(const DecodedInstruction& instruction)
    {
        const bool isSigned = instruction.op == Op::S_CmpEqI32 || instruction.op == Op::S_CmpLgI32 ||
                              instruction.op == Op::S_CmpLtI32 || instruction.op == Op::S_CmpLeI32 ||
                              instruction.op == Op::S_CmpGtI32 || instruction.op == Op::S_CmpGeI32;

        const auto left = isSigned ? ReadSigned(instruction.operands[0], instruction.pc)
                                   : ReadBits(instruction.operands[0], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = isSigned ? ReadSigned(instruction.operands[1], instruction.pc)
                                    : ReadBits(instruction.operands[1], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        IL::CompareOp op = IL::CompareOp::IEq;
        switch (instruction.op)
        {
        case Op::S_CmpEqI32:
        case Op::S_CmpEqU32:
            op = IL::CompareOp::IEq;
            break;
        case Op::S_CmpLgI32:
        case Op::S_CmpLgU32:
            op = IL::CompareOp::INe;
            break;
        case Op::S_CmpLtI32:
            op = IL::CompareOp::ILt;
            break;
        case Op::S_CmpLeI32:
            op = IL::CompareOp::ILe;
            break;
        case Op::S_CmpGtI32:
            op = IL::CompareOp::IGt;
            break;
        case Op::S_CmpGeI32:
            op = IL::CompareOp::IGe;
            break;
        case Op::S_CmpLtU32:
            op = IL::CompareOp::ULt;
            break;
        case Op::S_CmpLeU32:
            op = IL::CompareOp::ULe;
            break;
        case Op::S_CmpGtU32:
            op = IL::CompareOp::UGt;
            break;
        case Op::S_CmpGeU32:
            op = IL::CompareOp::UGe;
            break;
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-014", "not a scalar compare", instruction.pc));
        }

        const IL::ValueId comparison = m_builder.CompareValue(op, *left, *right, true);
        return WriteScc(comparison, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftCselect(const DecodedInstruction& instruction)
    {
        const auto condition = ReadScc(instruction.pc);
        if (!condition)
            return std::unexpected(condition.error());
        const auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::ValueId selected = m_builder.SelectValue(m_module.types.U32(), *condition, *left, *right, true);
        return WriteDest(instruction.operands[0], selected, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftFma(const DecodedInstruction& instruction)
    {
        const auto a = ReadFloat(instruction.operands[1], instruction.pc);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadFloat(instruction.operands[2], instruction.pc);
        if (!b)
            return std::unexpected(b.error());
        const auto c = ReadFloat(instruction.operands[3], instruction.pc);
        if (!c)
            return std::unexpected(c.error());

        IL::ValueId result = m_builder.Emit(m_module.types.F32(),
                                            IL::Op::Ternary,
                                            static_cast<std::uint16_t>(IL::TernaryOp::Fma),
                                            {*a, *b, *c},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> SetBranch(IL::BlockId block, std::uint32_t targetPc, std::uint32_t pc)
    {
        const IL::BlockId target = BlockForPc(targetPc, pc);
        if (target == IL::InvalidBlockId)
            return std::unexpected(LiftError("SR-GFX-LIFT-015", "branch target has no basic block", pc));

        IL::Terminator terminator;
        terminator.kind = IL::TerminatorKind::Br;
        terminator.target = target;
        terminator.sourcePc = pc;
        m_builder.Terminate(block, std::move(terminator));
        return {};
    }

    [[nodiscard]] Result<void> SetCondBranch(IL::BlockId block,
                                             IL::ValueId condition,
                                             std::uint32_t targetPc,
                                             std::uint32_t fallthroughPc,
                                             std::uint32_t pc,
                                             bool uniform)
    {
        const IL::BlockId target = BlockForPc(targetPc, pc);
        const IL::BlockId fallthrough = BlockForPc(fallthroughPc, pc);
        if (target == IL::InvalidBlockId || fallthrough == IL::InvalidBlockId)
            return std::unexpected(LiftError("SR-GFX-LIFT-016", "conditional branch target has no basic block", pc));

        IL::Terminator terminator;
        terminator.kind = IL::TerminatorKind::CondBr;
        terminator.condition = condition;
        terminator.trueTarget = target;
        terminator.falseTarget = fallthrough;
        terminator.uniformHint = uniform;
        terminator.sourcePc = pc;
        m_builder.Terminate(block, std::move(terminator));
        return {};
    }

    [[nodiscard]] Result<void> LiftBranch(const DecodedInstruction& instruction)
    {
        const IL::BlockId block = m_builder.CurrentBlock();
        const auto target = BranchTarget(instruction);
        if (!target)
            return std::unexpected(target.error());

        switch (instruction.op)
        {
        case Op::S_Branch:
            return SetBranch(block, *target, instruction.pc);
        case Op::S_CbranchScc0:
        case Op::S_CbranchScc1:
        {
            const auto scc = ReadScc(instruction.pc);
            if (!scc)
                return std::unexpected(scc.error());
            IL::ValueId condition = *scc;
            if (instruction.op == Op::S_CbranchScc0)
                condition = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), condition, true);
            return SetCondBranch(block, condition, *target, instruction.pc + 4, instruction.pc, true);
        }
        case Op::S_CbranchExecz:
        case Op::S_CbranchExecnz:
            // No instruction in the lifted subset writes EXEC, so the wave mask
            // is all-active: EXECZ never branches and EXECNZ always does.
            if (instruction.op == Op::S_CbranchExecz)
                return SetBranch(block, instruction.pc + 4, instruction.pc);
            return SetBranch(block, *target, instruction.pc);
        case Op::S_CbranchVccz:
        case Op::S_CbranchVccnz:
        {
            const auto vcc = ReadVcc(instruction.pc);
            if (!vcc)
                return std::unexpected(vcc.error());
            IL::ValueId condition = *vcc;
            if (instruction.op == Op::S_CbranchVccz)
                condition = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), condition, false);
            return SetCondBranch(block, condition, *target, instruction.pc + 4, instruction.pc, false);
        }
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-018", "not a branch", instruction.pc));
        }
    }

    [[nodiscard]] Result<void> LiftUnaryBits(const DecodedInstruction& instruction,
                                             IL::UnaryOp subop,
                                             bool uniform,
                                             bool updatesScc = false)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const IL::ValueId result = m_builder.UnaryValue(subop, m_module.types.U32(), *source, uniform);
        if (updatesScc)
        {
            const auto scc = WriteSccIfNonZero(result, instruction.pc, uniform);
            if (!scc)
                return scc;
        }
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMinMax(const DecodedInstruction& instruction,
                                          IL::CompareOp compare,
                                          bool maximum,
                                          bool isSigned,
                                          bool uniform = true,
                                          bool updatesScc = false)
    {
        (void)maximum;
        const IL::TypeId type = isSigned ? m_module.types.I32() : m_module.types.U32();
        const auto left = isSigned ? ReadSigned(instruction.operands[1], instruction.pc)
                                   : ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = isSigned ? ReadSigned(instruction.operands[2], instruction.pc)
                                    : ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::ValueId condition = m_builder.CompareValue(compare, *left, *right, uniform);
        IL::ValueId selected = m_builder.SelectValue(type,
                                                     condition,
                                                     *left,
                                                     *right,
                                                     uniform);
        if (isSigned)
            selected = Bitcast(m_module.types.U32(), selected);
        if (updatesScc)
        {
            const auto scc = WriteScc(condition, instruction.pc);
            if (!scc)
                return scc;
        }
        return WriteDest(instruction.operands[0], selected, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftReverseShift(const DecodedInstruction& instruction,
                                                IL::BinaryOp subop,
                                                bool signedInput)
    {
        const auto count = ReadBits(instruction.operands[1], instruction.pc);
        if (!count)
            return std::unexpected(count.error());
        const auto value = signedInput ? ReadSigned(instruction.operands[2], instruction.pc)
                                       : ReadBits(instruction.operands[2], instruction.pc);
        if (!value)
            return std::unexpected(value.error());

        IL::ValueId result = m_builder.Emit(signedInput ? m_module.types.I32() : m_module.types.U32(),
                                            IL::Op::Binary,
                                            static_cast<std::uint16_t>(subop),
                                            {*value, *count},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            false);
        if (signedInput)
            result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftInvertedBinary(const DecodedInstruction& instruction, IL::BinaryOp subop)
    {        const auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::ValueId value = m_builder.Emit(m_module.types.U32(),
                                                 IL::Op::Binary,
                                                 static_cast<std::uint16_t>(subop),
                                                 {*left, *right},
                                                 {},
                                                 IL::NoPayload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op),
                                                 true);
        const IL::ValueId inverted = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U32(), value, true);
        const auto scc = WriteSccIfNonZero(inverted, instruction.pc, true);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], inverted, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftArithmeticShift(const DecodedInstruction& instruction)
    {
        const auto left = ReadSigned(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadSigned(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        IL::ValueId result = m_builder.Emit(m_module.types.I32(),
                                            IL::Op::Binary,
                                            static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                            {*left, *right},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            true);
        result = Bitcast(m_module.types.U32(), result);
        const auto scc = WriteSccIfNonZero(result, instruction.pc, true);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftAbs(const DecodedInstruction& instruction)
    {
        const auto source = ReadSigned(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        const IL::ValueId zero = m_builder.ConstantI32(0);
        const IL::ValueId negative = m_builder.CompareValue(IL::CompareOp::ILt, *source, zero, true);
        const IL::ValueId negated = m_builder.UnaryValue(IL::UnaryOp::Neg, m_module.types.I32(), *source, true);
        const IL::ValueId selected = m_builder.SelectValue(m_module.types.I32(), negative, negated, *source, true);
        const IL::ValueId bits = Bitcast(m_module.types.U32(), selected);
        const auto scc = WriteSccIfNonZero(bits, instruction.pc, true);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], bits, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMovK(const DecodedInstruction& instruction)
    {
        const auto immediate = static_cast<std::int16_t>(instruction.operands[1].value & 0xFFFFu);
        const IL::ValueId value = m_builder.ConstantU32(static_cast<std::uint32_t>(static_cast<std::int32_t>(immediate)));
        return WriteDest(instruction.operands[0], value, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftVectorConvert(const DecodedInstruction& instruction,
                                                 IL::ConvertOp subop,
                                                 IL::TypeId resultType,
                                                 bool sourceIsFloat,
                                                 bool sourceSigned = false)
    {
        const auto source = sourceIsFloat ? ReadFloat(instruction.operands[1], instruction.pc)
                                          : sourceSigned ? ReadSigned(instruction.operands[1], instruction.pc)
                                                         : ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        IL::ValueId result = m_builder.ConvertValue(resultType, subop, *source, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftOne(const DecodedInstruction& instruction)
    {
        if (instruction.abs != 0 || instruction.neg != 0 || instruction.omod != 0 || instruction.clamp)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-019",
                                             std::string("unsupported modifiers on ") +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        }

        switch (instruction.op)
        {
        case Op::S_MovB32:
        case Op::V_MovB32:
        {
            const auto value = ReadBits(instruction.operands[1], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            return WriteDest(instruction.operands[0], *value, instruction.pc);
        }
        case Op::S_AddU32:
        case Op::S_AddI32:
            return LiftCarry(instruction, false);
        case Op::S_SubU32:
        case Op::S_SubI32:
            return LiftCarry(instruction, true);
        case Op::S_AddcU32:
            return LiftCarryIn(instruction, false);
        case Op::S_SubbU32:
            return LiftCarryIn(instruction, true);
        case Op::S_AddCoU32:
            return LiftCarry(instruction, false);
        case Op::S_SubCoU32:
            return LiftCarry(instruction, true);
        case Op::S_AddkI32:
            return LiftAddImmediate(instruction);
        case Op::S_MulkI32:
            return LiftMulImmediate(instruction);
        case Op::S_AndB32:
            return LiftBinary(instruction, IL::BinaryOp::And, false, true, true);
        case Op::S_OrB32:
            return LiftBinary(instruction, IL::BinaryOp::Or, false, true, true);
        case Op::S_XorB32:
            return LiftBinary(instruction, IL::BinaryOp::Xor, false, true, true);
        case Op::S_Andn2B32:
        case Op::S_AndNot1B32:
            return LiftAndOrNot(instruction, false);
        case Op::S_Orn2B32:
        case Op::S_OrNot1B32:
            return LiftAndOrNot(instruction, true);
        case Op::S_LshlB32:
            return LiftBinary(instruction, IL::BinaryOp::Shl, false, true, true);
        case Op::S_LshrB32:
            return LiftBinary(instruction, IL::BinaryOp::LShr, false, true, true);
        case Op::S_CselectB32:
            return LiftCselect(instruction);
        case Op::S_NotB32:
            return LiftUnaryBits(instruction, IL::UnaryOp::Not, true, true);
        case Op::S_BrevB32:
            return LiftUnaryBits(instruction, IL::UnaryOp::BitReverse, true);
        case Op::S_Bcnt1I32B32:
            return LiftUnaryBits(instruction, IL::UnaryOp::PopCount, true, true);
        case Op::S_Ff1I32B32:
            return LiftBitScan(instruction, false, true);
        case Op::S_FlbitI32B32:
            return LiftBitScan(instruction, true, true);
        case Op::S_AbsI32:
            return LiftAbs(instruction);
        case Op::S_MulI32:
            return LiftBinary(instruction, IL::BinaryOp::IMul, false, true);
        case Op::S_MinI32:
            return LiftMinMax(instruction, IL::CompareOp::ILt, false, true, true, true);
        case Op::S_MaxI32:
            return LiftMinMax(instruction, IL::CompareOp::IGt, true, true, true, true);
        case Op::S_MinU32:
            return LiftMinMax(instruction, IL::CompareOp::ULt, false, false, true, true);
        case Op::S_MaxU32:
            return LiftMinMax(instruction, IL::CompareOp::UGt, true, false, true, true);
        case Op::S_NandB32:
            return LiftInvertedBinary(instruction, IL::BinaryOp::And);
        case Op::S_NorB32:
            return LiftInvertedBinary(instruction, IL::BinaryOp::Or);
        case Op::S_XnorB32:
            return LiftInvertedBinary(instruction, IL::BinaryOp::Xor);
        case Op::S_AshrI32:
            return LiftArithmeticShift(instruction);
        case Op::S_MovkI32:
            return LiftMovK(instruction);
        case Op::V_NotB32:
            return LiftUnaryBits(instruction, IL::UnaryOp::Not, false);
        case Op::V_CvtF32I32:
            return LiftVectorConvert(instruction, IL::ConvertOp::SIToFP, m_module.types.F32(), false, true);
        case Op::V_CvtF32U32:
            return LiftVectorConvert(instruction, IL::ConvertOp::UIToFP, m_module.types.F32(), false);
        case Op::V_CvtI32F32:
            return LiftVectorConvert(instruction, IL::ConvertOp::FPToSI, m_module.types.I32(), true);
        case Op::V_CvtU32F32:
            return LiftVectorConvert(instruction, IL::ConvertOp::FPToUI, m_module.types.U32(), true);
        case Op::V_MinI32:
            return LiftMinMax(instruction, IL::CompareOp::ILt, false, true, false);
        case Op::V_MaxI32:
            return LiftMinMax(instruction, IL::CompareOp::IGt, true, true, false);
        case Op::V_MinU32:
            return LiftMinMax(instruction, IL::CompareOp::ULt, false, false, false);
        case Op::V_MaxU32:
            return LiftMinMax(instruction, IL::CompareOp::UGt, true, false, false);
        case Op::V_LshlrevB32:
            return LiftReverseShift(instruction, IL::BinaryOp::Shl, false);
        case Op::V_AshrrevI32:
            return LiftReverseShift(instruction, IL::BinaryOp::AShr, true);
        case Op::V_LshrrevB32:
            return LiftReverseShift(instruction, IL::BinaryOp::LShr, false);
        case Op::V_MulLoU32:
            return LiftBinary(instruction, IL::BinaryOp::IMul, false, false);
        case Op::V_AddNcU32:
            return LiftBinary(instruction, IL::BinaryOp::IAdd, false, false);
        case Op::V_SubNcU32:
            return LiftBinary(instruction, IL::BinaryOp::ISub, false, false);
        case Op::V_MinF32:
            return LiftBinary(instruction, IL::BinaryOp::FMin, true, false);
        case Op::V_MaxF32:
            return LiftBinary(instruction, IL::BinaryOp::FMax, true, false);
        case Op::V_Min3F32:
            return LiftTernaryFloat(instruction, IL::TernaryOp::FMin3);
        case Op::V_Max3F32:
            return LiftTernaryFloat(instruction, IL::TernaryOp::FMax3);
        case Op::V_MacF32:
            return LiftMac(instruction);
        case Op::V_MadF32:
            return LiftTernaryFloat(instruction, IL::TernaryOp::MulAdd);
        case Op::V_RcpF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Rcp);
        case Op::V_RsqF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Rsq);
        case Op::V_SqrtF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Sqrt);
        case Op::V_ExpF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Exp2);
        case Op::V_LogF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Log2);
        case Op::V_CmpLtI32:
            return LiftVectorCompare(instruction, IL::CompareOp::ILt, false, true);
        case Op::V_CmpEqI32:
            return LiftVectorCompare(instruction, IL::CompareOp::IEq, false, true);
        case Op::V_CmpLeI32:
            return LiftVectorCompare(instruction, IL::CompareOp::ILe, false, true);
        case Op::V_CmpGtI32:
            return LiftVectorCompare(instruction, IL::CompareOp::IGt, false, true);
        case Op::V_CmpNeI32:
            return LiftVectorCompare(instruction, IL::CompareOp::INe, false, true);
        case Op::V_CmpGeI32:
            return LiftVectorCompare(instruction, IL::CompareOp::IGe, false, true);
        case Op::V_CmpLtU32:
            return LiftVectorCompare(instruction, IL::CompareOp::ULt, false, false);
        case Op::V_CmpEqU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UEq, false, false);
        case Op::V_CmpLeU32:
            return LiftVectorCompare(instruction, IL::CompareOp::ULe, false, false);
        case Op::V_CmpGtU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UGt, false, false);
        case Op::V_CmpNeU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UNe, false, false);
        case Op::V_CmpGeU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UGe, false, false);
        case Op::V_CmpLtF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdLt, true, false);
        case Op::V_CmpEqF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdEq, true, false);
        case Op::V_CmpLeF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdLe, true, false);
        case Op::V_CmpGtF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdGt, true, false);
        case Op::V_CmpLgF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdNe, true, false);
        case Op::V_CmpGeF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdGe, true, false);
        case Op::V_CndmaskB32:
            return LiftCndmask(instruction);
        case Op::V_FfbhU32:
        case Op::V_ClzI32U32:
            return LiftBitScan(instruction, true, false);
        case Op::V_FfblB32:
        case Op::V_CtzI32B32:
            return LiftBitScan(instruction, false, false);
        case Op::V_BcntU32B32:
            return LiftBitCount(instruction);
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
            return LiftCompare(instruction);
        case Op::V_AddF32:
            return LiftBinary(instruction, IL::BinaryOp::FAdd, true, false);
        case Op::V_SubF32:
            return LiftBinary(instruction, IL::BinaryOp::FSub, true, false);
        case Op::V_MulF32:
            return LiftBinary(instruction, IL::BinaryOp::FMul, true, false);
        case Op::V_AndB32:
            return LiftBinary(instruction, IL::BinaryOp::And, false, false);
        case Op::V_OrB32:
            return LiftBinary(instruction, IL::BinaryOp::Or, false, false);
        case Op::V_XorB32:
            return LiftBinary(instruction, IL::BinaryOp::Xor, false, false);
        case Op::V_FmaF32:
            return LiftFma(instruction);
        case Op::GlobalStoreB32:
        {
            if (!m_module.testAbi)
            {
                return std::unexpected(LiftError("SR-GFX-LIFT-024",
                                                 "global_store_b32 requires --test-abi (no descriptor model yet)",
                                                 instruction.pc));
            }

            const auto index = TestBufferIndex(instruction, 0, false);
            if (!index)
                return std::unexpected(index.error());
            const auto value = ReadBits(instruction.operands[1], instruction.pc);
            if (!value)
                return std::unexpected(value.error());

            const IL::PayloadId payload = m_module.payloads.Add(IL::TestBufferDesc{0, 0});
            m_builder.IntrinsicVoid(IL::IntrinsicId::TestBufferStore, {*index, *value}, payload, instruction.pc);
            return {};
        }
        case Op::S_LoadB32:
        case Op::S_BufferLoadB32:
        case Op::GlobalLoadB32:
            return LiftBufferLoad(instruction);
        case Op::S_Branch:
        case Op::S_CbranchScc0:
        case Op::S_CbranchScc1:
        case Op::S_CbranchVccz:
        case Op::S_CbranchVccnz:
        case Op::S_CbranchExecz:
        case Op::S_CbranchExecnz:
            return LiftBranch(instruction);
        case Op::S_Endpgm:
        {
            IL::Terminator terminator;
            terminator.kind = IL::TerminatorKind::Ret;
            terminator.sourcePc = instruction.pc;
            m_builder.Terminate(m_builder.CurrentBlock(), std::move(terminator));
            return {};
        }
        case Op::S_Barrier:
            return std::unexpected(LiftError("SR-GFX-LIFT-020", "s_barrier is not implemented yet", instruction.pc));
        case Op::Invalid:
            break;
        }

        return std::unexpected(LiftError("SR-GFX-LIFT-022",
                                         std::string("unsupported instruction: ") + std::string(ToString(instruction.op)),
                                         instruction.pc));
    }

    [[nodiscard]] Result<void> LiftBlock(std::size_t blockIndex)
    {
        const std::uint32_t leaderPc = m_leaders[blockIndex];
        const IL::BlockId block = m_blockByPc.at(leaderPc);
        m_builder.SetCurrentBlock(block);

        const std::size_t start = m_byPc.at(leaderPc);
        const std::size_t end = blockIndex + 1 < m_leaders.size() ? m_byPc.at(m_leaders[blockIndex + 1]) : m_items.size();

        bool terminated = false;
        for (std::size_t i = start; i < end; ++i)
        {
            const auto status = LiftOne(m_items[i]);
            if (!status)
                return status;

            if (m_items[i].op == Op::S_Endpgm || IsBranch(m_items[i].op))
            {
                terminated = true;
                break;
            }
        }

        if (!terminated)
        {
            if (blockIndex + 1 >= m_leaders.size())
                return std::unexpected(LiftError("SR-GFX-LIFT-023", "basic block has no terminator", leaderPc));

            const auto status = SetBranch(block, m_leaders[blockIndex + 1], leaderPc);
            if (!status)
                return status;
        }

        return {};
    }

    IL::Module& m_module;
    IL::Function& m_function;
    IL::FunctionBuilder& m_builder;
    Arch m_arch;

    std::vector<DecodedInstruction> m_items;
    std::unordered_map<std::uint32_t, std::size_t> m_byPc;
    std::vector<std::uint32_t> m_leaders;
    std::map<std::uint32_t, IL::BlockId> m_blockByPc;
    std::vector<std::uint32_t> m_pcByBlock;
    IL::BlockId m_entry = IL::InvalidBlockId;

    std::array<IL::ValueId, 256> m_sgprSlots;
    std::array<IL::ValueId, 256> m_vgprSlots;
    IL::ValueId m_sccSlot = IL::InvalidValue;
    IL::ValueId m_vccSlot = IL::InvalidValue;
};

} // namespace

Result<IL::Module> Lift(const DecodedShader& shader, Arch arch, std::string_view entryPoint, LiftOptions options)
{
    if (shader.rawBytecode.empty())
        return std::unexpected(LiftError("SR-GFX-LIFT-030", "code object has no .text payload", 0));

    if (shader.rawBytecode.size() % 4 != 0)
        return std::unexpected(LiftError("SR-GFX-LIFT-031", "code object size is not a multiple of 4", 0));

    std::vector<std::uint32_t> words(shader.rawBytecode.size() / 4);
    std::memcpy(words.data(), shader.rawBytecode.data(), words.size() * sizeof(std::uint32_t));

    IL::Module module;
    module.stage = IL::Stage::Compute;
    module.entryPoint = std::string(entryPoint);
    module.architecture = std::string(GetArchInfo(arch).name);
    module.executionModes.workgroupSize = {64, 1, 1};
    module.testAbi = options.testAbi;

    IL::Function& function = IL::CreateFunction(module, std::string(entryPoint), module.types.Void());
    function.isEntry = true;
    module.entryFunction = function.id;

    IL::FunctionBuilder builder(module, function);
    Lifter lifter(module, function, builder, arch);
    const auto status = lifter.Run(words);
    if (!status)
        return std::unexpected(status.error());

    return module;
}

} // namespace SR::Frontend::Gfx
