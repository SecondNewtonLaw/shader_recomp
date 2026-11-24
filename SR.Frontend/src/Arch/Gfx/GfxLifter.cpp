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

        // A single-invocation EXEC starts fully active.
        m_builder.SetCurrentBlock(m_entry);
        m_builder.Store(ExecSlot(), m_builder.ConstantBool(true), 0);

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

    [[nodiscard]] IL::ValueId ExecSlot()
    {
        if (m_execSlot == IL::InvalidValue)
        {
            const IL::TypeId pointer = m_module.types.Pointer(IL::Space::Private, m_module.types.Bool());
            m_execSlot = m_builder.Alloca(pointer, m_entry);
            m_module.registerSlots.push_back({IL::RegisterClass::Exec, 0, m_execSlot});
        }
        return m_execSlot;
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

    [[nodiscard]] Result<IL::ValueId> ReadSgpr64(std::uint32_t index, std::uint32_t pc)
    {
        if (index + 1 > kLastSgpr)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-040", "64-bit scalar operand is out of range", pc));
        }

        const auto low = ReadSgpr(index, pc);
        if (!low)
            return std::unexpected(low.error());
        const auto high = ReadSgpr(index + 1, pc);
        if (!high)
            return std::unexpected(high.error());

        const IL::ValueId low64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *low, true);
        const IL::ValueId high64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *high, true);
        const IL::ValueId shifted = m_builder.Emit(m_module.types.U64(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                   {high64, m_builder.ConstantU64(32)},
                                                   {},
                                                   IL::NoPayload,
                                                   pc,
                                                   0,
                                                   true);
        return m_builder.Emit(m_module.types.U64(),
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(IL::BinaryOp::Or),
                              {low64, shifted},
                              {},
                              IL::NoPayload,
                              pc,
                              0,
                              true);
    }

    [[nodiscard]] Result<IL::ValueId> ReadPairSource(const Operand& operand, std::uint32_t pc)
    {
        switch (operand.kind)
        {
        case OperandKind::Sgpr:
            return ReadSgpr64(operand.value, pc);
        case OperandKind::Ssrc:
        case OperandKind::Src:
            if (operand.isInline)
            {
                const std::uint32_t bits = InlineInteger(operand.value);
                const std::int64_t extended = static_cast<std::int32_t>(bits);
                return m_builder.ConstantU64(static_cast<std::uint64_t>(extended));
            }
            if (operand.value < 128)
                return ReadSgpr64(operand.value, pc);
            return std::unexpected(LiftError("SR-GFX-LIFT-041", "unsupported 64-bit source", pc));
        case OperandKind::Literal:
            return m_builder.ConstantU64(operand.value);
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-042", "unsupported 64-bit operand kind", pc));
        }
    }

    [[nodiscard]] Result<IL::ValueId> ReadVgpr(std::uint32_t index, std::uint32_t pc)
    {
        return m_builder.Load(m_module.types.U32(), Slot(m_vgprSlots, index, IL::RegisterClass::Vgpr), pc, false);
    }

    [[nodiscard]] Result<IL::ValueId> ReadVgpr64(std::uint32_t index, std::uint32_t pc)
    {
        if (index + 1 >= 256)
            return std::unexpected(LiftError("SR-GFX-LIFT-046", "64-bit vector operand is out of range", pc));

        const auto low = ReadVgpr(index, pc);
        if (!low)
            return std::unexpected(low.error());
        const auto high = ReadVgpr(index + 1, pc);
        if (!high)
            return std::unexpected(high.error());

        const IL::ValueId low64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *low, false);
        const IL::ValueId high64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *high, false);
        const IL::ValueId shifted = m_builder.Emit(m_module.types.U64(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                   {high64, m_builder.ConstantU64(32)},
                                                   {},
                                                   IL::NoPayload,
                                                   pc,
                                                   0,
                                                   false);
        return m_builder.Emit(m_module.types.U64(),
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(IL::BinaryOp::Or),
                              {low64, shifted},
                              {},
                              IL::NoPayload,
                              pc,
                              0,
                              false);
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

    // VOP3 attaches abs/neg to each of the first three sources, bit 0 for
    // source 0 and so on.
    [[nodiscard]] Result<IL::ValueId> ReadFloatSource(const DecodedInstruction& instruction,
                                                      std::uint8_t index,
                                                      bool uniform = false)
    {
        const auto value = ReadFloat(instruction.operands[index], instruction.pc);
        if (!value)
            return value;

        IL::ValueId result = *value;
        const std::uint8_t bit = static_cast<std::uint8_t>(index - 1);
        if ((instruction.abs & (1u << bit)) != 0)
            result = m_builder.UnaryValue(IL::UnaryOp::FAbs, m_module.types.F32(), result, uniform);
        if ((instruction.neg & (1u << bit)) != 0)
            result = m_builder.UnaryValue(IL::UnaryOp::Neg, m_module.types.F32(), result, uniform);
        return result;
    }

    // VOP3 omod doubles/quadruples/halves the result, then clamp fixes it to
    // [0, 1].
    [[nodiscard]] IL::ValueId ApplyOutputModifiers(const DecodedInstruction& instruction,
                                                   IL::ValueId value,
                                                   bool uniform)
    {
        float scale = 0.0f;
        if (instruction.omod == 1)
            scale = 2.0f;
        else if (instruction.omod == 2)
            scale = 4.0f;
        else if (instruction.omod == 3)
            scale = 0.5f;

        if (scale != 0.0f)
        {
            value = m_builder.Emit(m_module.types.F32(),
                                   IL::Op::Binary,
                                   static_cast<std::uint16_t>(IL::BinaryOp::FMul),
                                   {value, m_builder.ConstantF32(scale)},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   uniform);
        }

        if (instruction.clamp)
        {
            value = m_builder.Emit(m_module.types.F32(),
                                   IL::Op::Ternary,
                                   static_cast<std::uint16_t>(IL::TernaryOp::Clamp),
                                   {value, m_builder.ConstantF32(0.0f), m_builder.ConstantF32(1.0f)},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   uniform);
        }
        return value;
    }

    [[nodiscard]] static bool SupportsFloatModifiers(Op op) noexcept
    {
        switch (op)
        {
        case Op::V_AddF32:
        case Op::V_SubF32:
        case Op::V_MulF32:
        case Op::V_FmaF32:
        case Op::V_MacF32:
        case Op::V_MadF32:
        case Op::V_MinF32:
        case Op::V_MaxF32:
        case Op::V_Min3F32:
        case Op::V_Max3F32:
        case Op::V_RcpF32:
        case Op::V_RsqF32:
        case Op::V_SqrtF32:
        case Op::V_ExpF32:
        case Op::V_LogF32:
        case Op::V_CmpLtF32:
        case Op::V_CmpEqF32:
        case Op::V_CmpLeF32:
        case Op::V_CmpGtF32:
        case Op::V_CmpLgF32:
        case Op::V_CmpGeF32:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] Result<IL::ValueId> ReadScc(std::uint32_t pc)
    {
        return m_builder.Load(m_module.types.Bool(), SccSlot(), pc, true);
    }

    [[nodiscard]] Result<IL::ValueId> ReadVcc(std::uint32_t pc)
    {
        return m_builder.Load(m_module.types.Bool(), VccSlot(), pc, false);
    }

    [[nodiscard]] Result<IL::ValueId> ReadExec(std::uint32_t pc)
    {
        return m_builder.Load(m_module.types.Bool(), ExecSlot(), pc, true);
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

    [[nodiscard]] Result<void> WriteExec(IL::ValueId value, std::uint32_t pc)
    {
        m_builder.Store(ExecSlot(), value, pc);
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

    [[nodiscard]] Result<void> WriteVgpr64(const Operand& destination, IL::ValueId value, std::uint32_t pc)
    {
        if (destination.kind != OperandKind::Vgpr)
            return std::unexpected(LiftError("SR-GFX-LIFT-047", "64-bit destination is not a vector register", pc));
        const std::uint32_t index = destination.value;
        if (index + 1 >= 256)
            return std::unexpected(LiftError("SR-GFX-LIFT-048", "64-bit vector destination is out of range", pc));

        const IL::ValueId low = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, false);
        const IL::ValueId high64 = m_builder.Emit(m_module.types.U64(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                                  {value, m_builder.ConstantU64(32)},
                                                  {},
                                                  IL::NoPayload,
                                                  pc,
                                                  0,
                                                  false);
        const IL::ValueId high = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, false);
        m_builder.Store(Slot(m_vgprSlots, index, IL::RegisterClass::Vgpr), low, pc);
        m_builder.Store(Slot(m_vgprSlots, index + 1, IL::RegisterClass::Vgpr), high, pc);
        return {};
    }

    [[nodiscard]] Result<void> WriteSgpr64(const Operand& destination, IL::ValueId value, std::uint32_t pc)    {
        if (destination.kind != OperandKind::Sdst)
            return std::unexpected(LiftError("SR-GFX-LIFT-043", "64-bit destination is not a scalar register", pc));
        const std::uint32_t index = destination.value;
        if (index + 1 > kLastSgpr)
            return std::unexpected(LiftError("SR-GFX-LIFT-044", "64-bit scalar destination is out of range", pc));

        const IL::ValueId low = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, true);
        const IL::ValueId high64 = m_builder.Emit(m_module.types.U64(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                                  {value, m_builder.ConstantU64(32)},
                                                  {},
                                                  IL::NoPayload,
                                                  pc,
                                                  0,
                                                  true);
        const IL::ValueId high = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, true);

        m_builder.Store(Slot(m_sgprSlots, index, IL::RegisterClass::Sgpr), low, pc);
        m_builder.Store(Slot(m_sgprSlots, index + 1, IL::RegisterClass::Sgpr), high, pc);
        return {};
    }

    [[nodiscard]] Result<void> LiftSccFrom64(IL::ValueId value, std::uint32_t pc)
    {
        const IL::ValueId nonzero = m_builder.CompareValue(IL::CompareOp::UNe,
                                                           value,
                                                           m_builder.ConstantU64(0),
                                                           true);
        return WriteScc(nonzero, pc);
    }

    // S_MOV_B64 / S_NOT_B64 / S_AND_B64 / S_OR_B64 / S_XOR_B64 / S_CSELECT_B64.
    [[nodiscard]] Result<void> LiftScalar64(const DecodedInstruction& instruction)
    {
        IL::ValueId value = IL::InvalidValue;
        switch (instruction.op)
        {
        case Op::S_MovB64:
        {
            const auto source = ReadPairSource(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            value = *source;
            break;
        }
        case Op::S_NotB64:
        {
            const auto source = ReadPairSource(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            value = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U64(), *source, true);
            break;
        }
        case Op::S_AndB64:
        case Op::S_OrB64:
        case Op::S_XorB64:
        {
            const auto left = ReadPairSource(instruction.operands[1], instruction.pc);
            if (!left)
                return std::unexpected(left.error());
            const auto right = ReadPairSource(instruction.operands[2], instruction.pc);
            if (!right)
                return std::unexpected(right.error());
            const IL::BinaryOp subop = instruction.op == Op::S_AndB64 ? IL::BinaryOp::And
                                       : instruction.op == Op::S_OrB64 ? IL::BinaryOp::Or
                                                                       : IL::BinaryOp::Xor;
            value = m_builder.Emit(m_module.types.U64(),
                                   IL::Op::Binary,
                                   static_cast<std::uint16_t>(subop),
                                   {*left, *right},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   true);
            break;
        }
        case Op::S_CselectB64:
        {
            const auto condition = ReadScc(instruction.pc);
            if (!condition)
                return std::unexpected(condition.error());
            const auto left = ReadPairSource(instruction.operands[1], instruction.pc);
            if (!left)
                return std::unexpected(left.error());
            const auto right = ReadPairSource(instruction.operands[2], instruction.pc);
            if (!right)
                return std::unexpected(right.error());
            value = m_builder.SelectValue(m_module.types.U64(), *condition, *left, *right, true);
            break;
        }
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-045", "not a 64-bit scalar operation", instruction.pc));
        }

        if (instruction.op != Op::S_MovB64 && instruction.op != Op::S_CselectB64)
        {
            const auto scc = LiftSccFrom64(value, instruction.pc);
            if (!scc)
                return scc;
        }
        return WriteSgpr64(instruction.operands[0], value, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftBinary(const DecodedInstruction& instruction,
                                          IL::BinaryOp subop,
                                          bool isFloat,
                                          bool uniform,
                                          bool updatesScc = false)
    {
        const auto left = isFloat ? ReadFloatSource(instruction, 1, uniform)
                                  : ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = isFloat ? ReadFloatSource(instruction, 2, uniform)
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
        {
            result = ApplyOutputModifiers(instruction, result, uniform);
            result = Bitcast(m_module.types.U32(), result);
        }

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

    enum class VectorPredicate
    {
        Compare,
        False,
        True,
        Ordered,
        Unordered,
    };

    [[nodiscard]] Result<void> LiftVectorCompare(const DecodedInstruction& instruction,
                                                 IL::CompareOp compare,
                                                 VectorPredicate predicate,
                                                 bool isFloat,
                                                 bool isSigned)
    {
        IL::ValueId condition = IL::InvalidValue;
        if (predicate == VectorPredicate::False || predicate == VectorPredicate::True)
        {
            condition = m_builder.ConstantBool(predicate == VectorPredicate::True);
        }
        else if (predicate == VectorPredicate::Ordered || predicate == VectorPredicate::Unordered)
        {
            const auto left = ReadFloatSource(instruction, 1);
            if (!left)
                return std::unexpected(left.error());
            const auto right = ReadFloatSource(instruction, 2);
            if (!right)
                return std::unexpected(right.error());

            const IL::ValueId leftOrdered = m_builder.CompareValue(IL::CompareOp::FOrdEq, *left, *left, false);
            const IL::ValueId rightOrdered = m_builder.CompareValue(IL::CompareOp::FOrdEq, *right, *right, false);
            condition = m_builder.Emit(m_module.types.Bool(),
                                       IL::Op::Binary,
                                       static_cast<std::uint16_t>(IL::BinaryOp::And),
                                       {leftOrdered, rightOrdered},
                                       {},
                                       IL::NoPayload,
                                       instruction.pc,
                                       static_cast<std::uint16_t>(instruction.op),
                                       false);
            if (predicate == VectorPredicate::Unordered)
                condition = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), condition, false);
        }
        else
        {
            const auto left = isFloat ? ReadFloatSource(instruction, 1)
                                      : isSigned ? ReadSigned(instruction.operands[1], instruction.pc)
                                                 : ReadBits(instruction.operands[1], instruction.pc);
            if (!left)
                return std::unexpected(left.error());
            const auto right = isFloat ? ReadFloatSource(instruction, 2)
                                       : isSigned ? ReadSigned(instruction.operands[2], instruction.pc)
                                                  : ReadBits(instruction.operands[2], instruction.pc);
            if (!right)
                return std::unexpected(right.error());
            condition = m_builder.CompareValue(compare, *left, *right, false);
        }

        const auto vcc = WriteVcc(condition, instruction.pc);
        if (!vcc)
            return vcc;

        if (instruction.execMask)
        {
            const auto exec = ReadExec(instruction.pc);
            if (!exec)
                return std::unexpected(exec.error());
            const IL::ValueId updated = m_builder.Emit(m_module.types.Bool(),
                                                       IL::Op::Binary,
                                                       static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                       {*exec, condition},
                                                       {},
                                                       IL::NoPayload,
                                                       instruction.pc,
                                                       static_cast<std::uint16_t>(instruction.op),
                                                       false);
            return WriteExec(updated, instruction.pc);
        }
        return {};
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
        const auto a = ReadFloatSource(instruction, 1);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadFloatSource(instruction, 2);
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
        result = ApplyOutputModifiers(instruction, result, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftTernaryFloat(const DecodedInstruction& instruction, IL::TernaryOp subop)
    {
        const auto a = ReadFloatSource(instruction, 1);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadFloatSource(instruction, 2);
        if (!b)
            return std::unexpected(b.error());
        const auto c = ReadFloatSource(instruction, 3);
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
        result = ApplyOutputModifiers(instruction, result, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftConvertRound(const DecodedInstruction& instruction, IL::UnaryOp round)
    {
        const auto source = ReadFloatSource(instruction, 1);
        if (!source)
            return std::unexpected(source.error());
        IL::ValueId rounded = m_builder.UnaryValue(round, m_module.types.F32(), *source, false);
        IL::ValueId result = m_builder.ConvertValue(m_module.types.I32(), IL::ConvertOp::FPToSI, rounded, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftByteToFloat(const DecodedInstruction& instruction, std::uint8_t byteIndex)
    {
        auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        IL::ValueId value = *source;
        if (byteIndex != 0)
        {
            value = m_builder.Emit(m_module.types.U32(),
                                   IL::Op::Binary,
                                   static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                   {value, m_builder.ConstantU32(8u * byteIndex)},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   false);
        }
        value = m_builder.Emit(m_module.types.U32(),
                               IL::Op::Binary,
                               static_cast<std::uint16_t>(IL::BinaryOp::And),
                               {value, m_builder.ConstantU32(0xFF)},
                               {},
                               IL::NoPayload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op),
                               false);
        IL::ValueId result = m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::UIToFP, value, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftConvert16(const DecodedInstruction& instruction, bool isSigned)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        IL::ValueId result = *source;
        if (isSigned)
        {
            IL::ValueId shifted = m_builder.Emit(m_module.types.U32(),
                                                 IL::Op::Binary,
                                                 static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                 {result, m_builder.ConstantU32(16)},
                                                 {},
                                                 IL::NoPayload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op),
                                                 false);
            shifted = m_builder.Emit(m_module.types.I32(),
                                     IL::Op::Binary,
                                     static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                     {Bitcast(m_module.types.I32(), shifted), m_builder.ConstantU32(16)},
                                     {},
                                     IL::NoPayload,
                                     instruction.pc,
                                     static_cast<std::uint16_t>(instruction.op),
                                     false);
            result = Bitcast(m_module.types.U32(), shifted);
        }
        else
        {
            result = m_builder.Emit(m_module.types.U32(),
                                    IL::Op::Binary,
                                    static_cast<std::uint16_t>(IL::BinaryOp::And),
                                    {result, m_builder.ConstantU32(0xFFFF)},
                                    {},
                                    IL::NoPayload,
                                    instruction.pc,
                                    static_cast<std::uint16_t>(instruction.op),
                                    false);
        }
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftCls(const DecodedInstruction& instruction)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        const IL::ValueId sign = m_builder.Emit(m_module.types.U32(),
                                                IL::Op::Binary,
                                                static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                                {Bitcast(m_module.types.I32(), *source),
                                                 m_builder.ConstantU32(31)},
                                                {},
                                                IL::NoPayload,
                                                instruction.pc,
                                                static_cast<std::uint16_t>(instruction.op),
                                                false);
        const IL::ValueId different = m_builder.Emit(m_module.types.U32(),
                                                     IL::Op::Binary,
                                                     static_cast<std::uint16_t>(IL::BinaryOp::Xor),
                                                     {*source, Bitcast(m_module.types.U32(), sign)},
                                                     {},
                                                     IL::NoPayload,
                                                     instruction.pc,
                                                     static_cast<std::uint16_t>(instruction.op),
                                                     false);
        const IL::ValueId allSame = m_builder.CompareValue(IL::CompareOp::UEq,
                                                           different,
                                                           m_builder.ConstantU32(0),
                                                           false);
        const IL::ValueId count = m_builder.UnaryValue(IL::UnaryOp::Clz, m_module.types.U32(), different, false);
        const IL::ValueId minusOne = m_builder.ConstantU32(0xFFFFFFFFu);
        const IL::ValueId result =
            m_builder.SelectValue(m_module.types.U32(), allSame, minusOne, count, false);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] IL::ValueId SignExtend24(IL::ValueId value)
    {
        IL::ValueId shifted = m_builder.Emit(m_module.types.U32(),
                                             IL::Op::Binary,
                                             static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                             {value, m_builder.ConstantU32(8)},
                                             {},
                                             IL::NoPayload,
                                             0,
                                             0,
                                             false);
        shifted = m_builder.Emit(m_module.types.I32(),
                                 IL::Op::Binary,
                                 static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                 {Bitcast(m_module.types.I32(), shifted), m_builder.ConstantU32(8)},
                                 {},
                                 IL::NoPayload,
                                 0,
                                 0,
                                 false);
        return Bitcast(m_module.types.U32(), shifted);
    }

    [[nodiscard]] Result<void> LiftF32Reverse(const DecodedInstruction& instruction)
    {
        const auto left = ReadFloatSource(instruction, 1);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadFloatSource(instruction, 2);
        if (!right)
            return std::unexpected(right.error());

        IL::ValueId result = m_builder.Emit(m_module.types.F32(),
                                            IL::Op::Binary,
                                            static_cast<std::uint16_t>(IL::BinaryOp::FSub),
                                            {*right, *left},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            false);
        result = ApplyOutputModifiers(instruction, result, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMulDx9Zero(const DecodedInstruction& instruction)
    {
        const auto left = ReadFloatSource(instruction, 1);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadFloatSource(instruction, 2);
        if (!right)
            return std::unexpected(right.error());

        const IL::ValueId zero = m_builder.ConstantF32(0.0f);
        const IL::ValueId leftZero = m_builder.CompareValue(IL::CompareOp::FOrdEq, *left, zero, false);
        const IL::ValueId rightZero = m_builder.CompareValue(IL::CompareOp::FOrdEq, *right, zero, false);
        const IL::ValueId anyZero = m_builder.Emit(m_module.types.Bool(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                                   {leftZero, rightZero},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   false);
        const IL::ValueId product = m_builder.Emit(m_module.types.F32(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::FMul),
                                                   {*left, *right},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   false);
        IL::ValueId result =
            m_builder.SelectValue(m_module.types.F32(), anyZero, zero, product, false);
        result = ApplyOutputModifiers(instruction, result, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMul24(const DecodedInstruction& instruction, bool isSigned, bool high)
    {
        auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        IL::ValueId a = *left;
        IL::ValueId b = *right;
        if (isSigned)
        {
            a = SignExtend24(a);
            b = SignExtend24(b);
        }
        else
        {
            a = m_builder.Emit(m_module.types.U32(),
                               IL::Op::Binary,
                               static_cast<std::uint16_t>(IL::BinaryOp::And),
                               {a, m_builder.ConstantU32(0xFFFFFF)},
                               {},
                               IL::NoPayload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op),
                               false);
            b = m_builder.Emit(m_module.types.U32(),
                               IL::Op::Binary,
                               static_cast<std::uint16_t>(IL::BinaryOp::And),
                               {b, m_builder.ConstantU32(0xFFFFFF)},
                               {},
                               IL::NoPayload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op),
                               false);
        }

        IL::ValueId result = IL::InvalidValue;
        if (high)
        {
            const IL::ConvertOp extend = isSigned ? IL::ConvertOp::SExt : IL::ConvertOp::ZExt;
            const IL::ValueId a64 = m_builder.ConvertValue(m_module.types.U64(), extend, a, false);
            const IL::ValueId b64 = m_builder.ConvertValue(m_module.types.U64(), extend, b, false);
            const IL::ValueId product = m_builder.Emit(m_module.types.U64(),
                                                       IL::Op::Binary,
                                                       static_cast<std::uint16_t>(IL::BinaryOp::IMul),
                                                       {a64, b64},
                                                       {},
                                                       IL::NoPayload,
                                                       instruction.pc,
                                                       static_cast<std::uint16_t>(instruction.op),
                                                       false);
            const IL::ValueId shifted = m_builder.Emit(isSigned ? m_module.types.I64() : m_module.types.U64(),
                                                       IL::Op::Binary,
                                                       static_cast<std::uint16_t>(isSigned ? IL::BinaryOp::AShr
                                                                                          : IL::BinaryOp::LShr),
                                                       {isSigned ? Bitcast(m_module.types.I64(), product) : product,
                                                        m_builder.ConstantU64(32)},
                                                       {},
                                                       IL::NoPayload,
                                                       instruction.pc,
                                                       static_cast<std::uint16_t>(instruction.op),
                                                       false);
            result = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, shifted, false);
        }
        else
        {
            result = m_builder.Emit(m_module.types.U32(),
                                    IL::Op::Binary,
                                    static_cast<std::uint16_t>(IL::BinaryOp::IMul),
                                    {a, b},
                                    {},
                                    IL::NoPayload,
                                    instruction.pc,
                                    static_cast<std::uint16_t>(instruction.op),
                                    false);
        }
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftXnor(const DecodedInstruction& instruction)
    {
        const auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::ValueId xored = m_builder.Emit(m_module.types.U32(),
                                                 IL::Op::Binary,
                                                 static_cast<std::uint16_t>(IL::BinaryOp::Xor),
                                                 {*left, *right},
                                                 {},
                                                 IL::NoPayload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op),
                                                 false);
        const IL::ValueId result =
            m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U32(), xored, false);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftVectorCarry(const DecodedInstruction& instruction,
                                               bool subtract,
                                               bool reverse)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto vcc = ReadVcc(instruction.pc);
        if (!vcc)
            return std::unexpected(vcc.error());

        const IL::ValueId left = reverse ? *second : *first;
        const IL::ValueId right = reverse ? *first : *second;
        const IL::ValueId carryValue = m_builder.SelectValue(m_module.types.U32(),
                                                             *vcc,
                                                             m_builder.ConstantU32(1),
                                                             m_builder.ConstantU32(0),
                                                             false);
        const IL::ValueId partial = m_builder.Emit(m_module.types.U32(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(subtract ? IL::BinaryOp::ISub
                                                                                       : IL::BinaryOp::IAdd),
                                                   {left, right},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   false);
        const IL::ValueId firstFlag = subtract
                                          ? m_builder.CompareValue(IL::CompareOp::ULt, left, right, false)
                                          : m_builder.CompareValue(IL::CompareOp::ULt, partial, left, false);
        const IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(subtract ? IL::BinaryOp::ISub
                                                                                      : IL::BinaryOp::IAdd),
                                                  {partial, carryValue},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        const IL::ValueId secondFlag = subtract
                                           ? m_builder.CompareValue(IL::CompareOp::ULt, partial, carryValue, false)
                                           : m_builder.CompareValue(IL::CompareOp::ULt, result, partial, false);
        const IL::ValueId carry = m_builder.Emit(m_module.types.Bool(),
                                                 IL::Op::Binary,
                                                 static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                                 {firstFlag, secondFlag},
                                                 {},
                                                 IL::NoPayload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op),
                                                 false);
        const auto vccWrite = WriteVcc(carry, instruction.pc);
        if (!vccWrite)
            return vccWrite;
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftLshlrevB64(const DecodedInstruction& instruction)
    {
        const auto count = ReadBits(instruction.operands[1], instruction.pc);
        if (!count)
            return std::unexpected(count.error());
        if (instruction.operands[2].kind != OperandKind::Vgpr &&
            instruction.operands[2].kind != OperandKind::Vsrc)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-049", "64-bit shift source is not a VGPR pair",
                                             instruction.pc));
        }
        const auto value = ReadVgpr64(instruction.operands[2].value, instruction.pc);
        if (!value)
            return std::unexpected(value.error());

        const IL::ValueId count64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt,
                                                           MaskShiftCount(*count, 63, false), false);
        const IL::ValueId result = m_builder.Emit(m_module.types.U64(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                  {*value, count64},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteVgpr64(instruction.operands[0], result, instruction.pc);
    }

    enum class IntegerTernary
    {
        Add3,
        AddLshl,
        AndOr,
        Or3,
        Xor3,
        LshlAdd,
        LshlOr,
        Xad,
        Min3Signed,
        Min3Unsigned,
        Med3Signed,
        Med3Unsigned,
        Max3Signed,
        Max3Unsigned,
        Sad,
    };

    [[nodiscard]] IL::ValueId SelectMin(IL::ValueId a, IL::ValueId b, bool isSigned)
    {
        const IL::ValueId condition =
            m_builder.CompareValue(isSigned ? IL::CompareOp::ILt : IL::CompareOp::ULt, a, b, false);
        return m_builder.SelectValue(m_module.types.U32(), condition, a, b, false);
    }

    [[nodiscard]] IL::ValueId SelectMax(IL::ValueId a, IL::ValueId b, bool isSigned)
    {
        const IL::ValueId condition =
            m_builder.CompareValue(isSigned ? IL::CompareOp::IGt : IL::CompareOp::UGt, a, b, false);
        return m_builder.SelectValue(m_module.types.U32(), condition, a, b, false);
    }

    [[nodiscard]] Result<void> LiftIntegerTernary(const DecodedInstruction& instruction,
                                                  IntegerTernary kind)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto third = ReadBits(instruction.operands[3], instruction.pc);
        if (!third)
            return std::unexpected(third.error());

        const auto emit = [&](IL::BinaryOp subop, IL::ValueId left, IL::ValueId right) {
            return m_builder.Emit(m_module.types.U32(),
                                  IL::Op::Binary,
                                  static_cast<std::uint16_t>(subop),
                                  {left, right},
                                  {},
                                  IL::NoPayload,
                                  instruction.pc,
                                  static_cast<std::uint16_t>(instruction.op),
                                  false);
        };

        IL::ValueId result = IL::InvalidValue;
        switch (kind)
        {
        case IntegerTernary::Add3:
            result = emit(IL::BinaryOp::IAdd, emit(IL::BinaryOp::IAdd, *first, *second), *third);
            break;
        case IntegerTernary::AddLshl:
        {
            const IL::ValueId sum = emit(IL::BinaryOp::IAdd, *first, *second);
            result = emit(IL::BinaryOp::Shl, sum, MaskShiftCount(*third, 31, false));
            break;
        }
        case IntegerTernary::AndOr:
            result = emit(IL::BinaryOp::Or, emit(IL::BinaryOp::And, *first, *second), *third);
            break;
        case IntegerTernary::Or3:
            result = emit(IL::BinaryOp::Or, emit(IL::BinaryOp::Or, *first, *second), *third);
            break;
        case IntegerTernary::Xor3:
            result = emit(IL::BinaryOp::Xor, emit(IL::BinaryOp::Xor, *first, *second), *third);
            break;
        case IntegerTernary::LshlAdd:
        {
            const IL::ValueId shifted = emit(IL::BinaryOp::Shl, *first, MaskShiftCount(*second, 31, false));
            result = emit(IL::BinaryOp::IAdd, shifted, *third);
            break;
        }
        case IntegerTernary::LshlOr:
        {
            const IL::ValueId shifted = emit(IL::BinaryOp::Shl, *first, MaskShiftCount(*second, 31, false));
            result = emit(IL::BinaryOp::Or, shifted, *third);
            break;
        }
        case IntegerTernary::Xad:
            result = emit(IL::BinaryOp::IAdd, emit(IL::BinaryOp::Xor, *first, *second), *third);
            break;
        case IntegerTernary::Min3Signed:
        case IntegerTernary::Min3Unsigned:
        {
            const bool isSigned = kind == IntegerTernary::Min3Signed;
            result = SelectMin(SelectMin(*first, *second, isSigned), *third, isSigned);
            break;
        }
        case IntegerTernary::Max3Signed:
        case IntegerTernary::Max3Unsigned:
        {
            const bool isSigned = kind == IntegerTernary::Max3Signed;
            result = SelectMax(SelectMax(*first, *second, isSigned), *third, isSigned);
            break;
        }
        case IntegerTernary::Med3Signed:
        case IntegerTernary::Med3Unsigned:
        {
            const bool isSigned = kind == IntegerTernary::Med3Signed;
            const IL::ValueId low = SelectMin(*first, *second, isSigned);
            const IL::ValueId high = SelectMax(*first, *second, isSigned);
            result = SelectMin(SelectMax(low, *third, isSigned), high, isSigned);
            break;
        }
        case IntegerTernary::Sad:
        {
            const IL::ValueId leftLess =
                m_builder.CompareValue(IL::CompareOp::ULt, *first, *second, false);
            const IL::ValueId forward = emit(IL::BinaryOp::ISub, *first, *second);
            const IL::ValueId reverse = emit(IL::BinaryOp::ISub, *second, *first);
            const IL::ValueId difference =
                m_builder.SelectValue(m_module.types.U32(), leftLess, reverse, forward, false);
            result = emit(IL::BinaryOp::IAdd, difference, *third);
            break;
        }
        }
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMulHi32(const DecodedInstruction& instruction, bool isSigned)
    {
        const auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::ConvertOp extend = isSigned ? IL::ConvertOp::SExt : IL::ConvertOp::ZExt;
        const IL::TypeId wide = isSigned ? m_module.types.I64() : m_module.types.U64();
        IL::ValueId a64 = m_builder.ConvertValue(wide, extend, *left, false);
        IL::ValueId b64 = m_builder.ConvertValue(wide, extend, *right, false);
        const IL::ValueId product = m_builder.Emit(m_module.types.U64(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::IMul),
                                                   {a64, b64},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   false);
        const IL::ValueId shifted = m_builder.Emit(wide,
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(isSigned ? IL::BinaryOp::AShr
                                                                                      : IL::BinaryOp::LShr),
                                                   {isSigned ? Bitcast(wide, product) : product,
                                                    m_builder.ConstantU64(32)},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   false);
        const IL::ValueId result = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc,
                                                          shifted, false);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMad24(const DecodedInstruction& instruction, bool isSigned)
    {
        auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto third = ReadBits(instruction.operands[3], instruction.pc);
        if (!third)
            return std::unexpected(third.error());

        IL::ValueId a = *first;
        IL::ValueId b = *second;
        if (isSigned)
        {
            a = SignExtend24(a);
            b = SignExtend24(b);
        }
        else
        {
            a = m_builder.Emit(m_module.types.U32(), IL::Op::Binary,
                               static_cast<std::uint16_t>(IL::BinaryOp::And),
                               {a, m_builder.ConstantU32(0xFFFFFF)}, {}, IL::NoPayload,
                               instruction.pc, static_cast<std::uint16_t>(instruction.op), false);
            b = m_builder.Emit(m_module.types.U32(), IL::Op::Binary,
                               static_cast<std::uint16_t>(IL::BinaryOp::And),
                               {b, m_builder.ConstantU32(0xFFFFFF)}, {}, IL::NoPayload,
                               instruction.pc, static_cast<std::uint16_t>(instruction.op), false);
        }
        const IL::ValueId product = m_builder.Emit(m_module.types.U32(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::IMul),
                                                   {a, b},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   false);
        const IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                                  {product, *third},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftLdexp(const DecodedInstruction& instruction)
    {
        const auto value = ReadFloatSource(instruction, 1);
        if (!value)
            return std::unexpected(value.error());
        const auto exponent = ReadSigned(instruction.operands[2], instruction.pc);
        if (!exponent)
            return std::unexpected(exponent.error());

        IL::ValueId exponentFloat =
            m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::SIToFP, *exponent, false);
        const IL::ValueId scale =
            m_builder.UnaryValue(IL::UnaryOp::Exp2, m_module.types.F32(), exponentFloat, false);
        IL::ValueId result = m_builder.Emit(m_module.types.F32(),
                                            IL::Op::Binary,
                                            static_cast<std::uint16_t>(IL::BinaryOp::FMul),
                                            {*value, scale},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            false);
        result = ApplyOutputModifiers(instruction, result, false);
        result = Bitcast(m_module.types.U32(), result);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] IL::TypeId F16Type() { return m_module.types.Float(IL::FloatFormat::F16); }
    [[nodiscard]] IL::TypeId U16Type() { return m_module.types.Int(16, false); }
    [[nodiscard]] IL::TypeId I16Type() { return m_module.types.Int(16, true); }

    [[nodiscard]] Result<IL::ValueId> ReadF16(const Operand& operand, std::uint32_t pc)
    {
        const auto bits = ReadBits(operand, pc);
        if (!bits)
            return bits;
        const IL::ValueId low = m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, *bits, false);
        return Bitcast(F16Type(), low);
    }

    [[nodiscard]] Result<void> WriteF16(const Operand& destination, IL::ValueId value, std::uint32_t pc)
    {
        const IL::ValueId bits = Bitcast(U16Type(), value);
        const IL::ValueId wide = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt, bits, false);
        return WriteDest(destination, wide, pc);
    }

    [[nodiscard]] Result<void> LiftF16Unary(const DecodedInstruction& instruction, IL::UnaryOp subop)
    {
        const auto source = ReadF16(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const IL::ValueId result = m_builder.UnaryValue(subop, F16Type(), *source, false);
        return WriteF16(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftF16Binary(const DecodedInstruction& instruction,
                                             IL::BinaryOp subop,
                                             bool reverse = false)
    {
        const auto first = ReadF16(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadF16(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());

        const IL::ValueId result = m_builder.Emit(F16Type(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(subop),
                                                  {reverse ? *second : *first, reverse ? *first : *second},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteF16(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftF16Fma(const DecodedInstruction& instruction, bool accumulate)
    {
        const auto a = ReadF16(instruction.operands[1], instruction.pc);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadF16(instruction.operands[2], instruction.pc);
        if (!b)
            return std::unexpected(b.error());

        IL::ValueId c = IL::InvalidValue;
        if (accumulate)
        {
            const auto accumulator = ReadF16(instruction.operands[0], instruction.pc);
            if (!accumulator)
                return std::unexpected(accumulator.error());
            c = *accumulator;
        }
        else
        {
            const auto third = ReadF16(instruction.operands[3], instruction.pc);
            if (!third)
                return std::unexpected(third.error());
            c = *third;
        }

        const IL::ValueId result = m_builder.Emit(F16Type(),
                                                  IL::Op::Ternary,
                                                  static_cast<std::uint16_t>(IL::TernaryOp::Fma),
                                                  {*a, *b, c},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteF16(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftF16Ternary(const DecodedInstruction& instruction, IL::TernaryOp subop)
    {
        const auto a = ReadF16(instruction.operands[1], instruction.pc);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadF16(instruction.operands[2], instruction.pc);
        if (!b)
            return std::unexpected(b.error());
        const auto c = ReadF16(instruction.operands[3], instruction.pc);
        if (!c)
            return std::unexpected(c.error());

        const IL::ValueId result = m_builder.Emit(F16Type(),
                                                  IL::Op::Ternary,
                                                  static_cast<std::uint16_t>(subop),
                                                  {*a, *b, *c},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteF16(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftF16Convert(const DecodedInstruction& instruction)
    {
        switch (instruction.op)
        {
        case Op::V_CvtF16F32:
        {
            const auto source = ReadFloat(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId result = m_builder.ConvertValue(F16Type(), IL::ConvertOp::FPTrunc, *source, false);
            return WriteF16(instruction.operands[0], result, instruction.pc);
        }
        case Op::V_CvtF32F16:
        {
            const auto source = ReadF16(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            IL::ValueId result = m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::FPExt, *source, false);
            result = Bitcast(m_module.types.U32(), result);
            return WriteDest(instruction.operands[0], result, instruction.pc);
        }
        case Op::V_CvtF16U16:
        case Op::V_CvtF16I16:
        {
            const auto source = ReadBits(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId low = m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, *source, false);
            const IL::ValueId input = instruction.op == Op::V_CvtF16I16 ? Bitcast(I16Type(), low) : low;
            const IL::ValueId result = m_builder.ConvertValue(
                F16Type(), instruction.op == Op::V_CvtF16I16 ? IL::ConvertOp::SIToFP : IL::ConvertOp::UIToFP,
                input, false);
            return WriteF16(instruction.operands[0], result, instruction.pc);
        }
        case Op::V_CvtU16F16:
        case Op::V_CvtI16F16:
        {
            const auto source = ReadF16(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const bool isSigned = instruction.op == Op::V_CvtI16F16;
            const IL::ValueId result = m_builder.ConvertValue(
                isSigned ? m_module.types.I32() : m_module.types.U32(),
                isSigned ? IL::ConvertOp::FPToSI : IL::ConvertOp::FPToUI,
                *source, false);
            return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result), instruction.pc);
        }
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-050", "not an f16 conversion", instruction.pc));
        }
    }

    [[nodiscard]] Result<void> LiftF16Norm(const DecodedInstruction& instruction, bool isSigned)
    {
        const auto source = ReadF16(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const IL::ValueId wide =
            m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::FPExt, *source, false);
        const IL::ValueId scaled = m_builder.Emit(m_module.types.F32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::FMul),
                                                  {wide, m_builder.ConstantF32(isSigned ? 32767.0f : 65535.0f)},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        const IL::ValueId converted = m_builder.ConvertValue(
            isSigned ? m_module.types.I32() : m_module.types.U32(),
            isSigned ? IL::ConvertOp::FPToSI : IL::ConvertOp::FPToUI,
            scaled, false);
        const IL::ValueId masked = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                  {Bitcast(m_module.types.U32(), converted),
                                                   m_builder.ConstantU32(0xFFFF)},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteDest(instruction.operands[0], masked, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftF16Ldexp(const DecodedInstruction& instruction)
    {
        const auto value = ReadF16(instruction.operands[1], instruction.pc);
        if (!value)
            return std::unexpected(value.error());
        const auto exponent = ReadSigned(instruction.operands[2], instruction.pc);
        if (!exponent)
            return std::unexpected(exponent.error());

        const IL::ValueId exponentF16 =
            m_builder.ConvertValue(F16Type(), IL::ConvertOp::SIToFP, *exponent, false);
        const IL::ValueId scale =
            m_builder.UnaryValue(IL::UnaryOp::Exp2, F16Type(), exponentF16, false);
        const IL::ValueId result = m_builder.Emit(F16Type(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::FMul),
                                                  {*value, scale},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteF16(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftBit16(const DecodedInstruction& instruction, bool invert)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        IL::ValueId value = m_builder.Emit(m_module.types.U32(),
                                           IL::Op::Binary,
                                           static_cast<std::uint16_t>(IL::BinaryOp::And),
                                           {*source, m_builder.ConstantU32(0xFFFF)},
                                           {},
                                           IL::NoPayload,
                                           instruction.pc,
                                           static_cast<std::uint16_t>(instruction.op),
                                           false);
        if (invert)
        {
            value = m_builder.Emit(m_module.types.U32(),
                                   IL::Op::Binary,
                                   static_cast<std::uint16_t>(IL::BinaryOp::Xor),
                                   {value, m_builder.ConstantU32(0xFFFF)},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   false);
        }
        return WriteDest(instruction.operands[0], value, instruction.pc);
    }

    [[nodiscard]] IL::TypeId F64Type() { return m_module.types.Float(IL::FloatFormat::F64); }

    [[nodiscard]] Result<IL::ValueId> ReadF64(const Operand& operand, std::uint32_t pc)
    {
        if (operand.kind != OperandKind::Vgpr && operand.kind != OperandKind::Vsrc)
            return std::unexpected(LiftError("SR-GFX-LIFT-051", "f64 operand is not a VGPR pair", pc));
        const auto bits = ReadVgpr64(operand.value, pc);
        if (!bits)
            return bits;
        return Bitcast(F64Type(), *bits);
    }

    [[nodiscard]] Result<void> WriteF64(const Operand& destination, IL::ValueId value, std::uint32_t pc)
    {
        const IL::ValueId bits = Bitcast(m_module.types.U64(), value);
        return WriteVgpr64(destination, bits, pc);
    }

    [[nodiscard]] Result<void> LiftF64Unary(const DecodedInstruction& instruction, IL::UnaryOp subop)
    {
        const auto source = ReadF64(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const IL::ValueId result = m_builder.UnaryValue(subop, F64Type(), *source, false);
        return WriteF64(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftF64Binary(const DecodedInstruction& instruction, IL::BinaryOp subop)
    {
        const auto first = ReadF64(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadF64(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const IL::ValueId result = m_builder.Emit(F64Type(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(subop),
                                                  {*first, *second},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteF64(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftF64Fma(const DecodedInstruction& instruction)
    {
        const auto a = ReadF64(instruction.operands[1], instruction.pc);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadF64(instruction.operands[2], instruction.pc);
        if (!b)
            return std::unexpected(b.error());
        const auto c = ReadF64(instruction.operands[3], instruction.pc);
        if (!c)
            return std::unexpected(c.error());
        const IL::ValueId result = m_builder.Emit(F64Type(),
                                                  IL::Op::Ternary,
                                                  static_cast<std::uint16_t>(IL::TernaryOp::Fma),
                                                  {*a, *b, *c},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteF64(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftF64Convert(const DecodedInstruction& instruction)
    {
        switch (instruction.op)
        {
        case Op::V_CvtF64I32:
        {
            const auto source = ReadSigned(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId result = m_builder.ConvertValue(F64Type(), IL::ConvertOp::SIToFP, *source, false);
            return WriteF64(instruction.operands[0], result, instruction.pc);
        }
        case Op::V_CvtF64U32:
        {
            const auto source = ReadBits(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId result = m_builder.ConvertValue(F64Type(), IL::ConvertOp::UIToFP, *source, false);
            return WriteF64(instruction.operands[0], result, instruction.pc);
        }
        case Op::V_CvtI32F64:
        {
            const auto source = ReadF64(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId result = m_builder.ConvertValue(m_module.types.I32(), IL::ConvertOp::FPToSI,
                                                              *source, false);
            return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result), instruction.pc);
        }
        case Op::V_CvtU32F64:
        {
            const auto source = ReadF64(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId result = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::FPToUI,
                                                              *source, false);
            return WriteDest(instruction.operands[0], result, instruction.pc);
        }
        case Op::V_CvtF64F32:
        {
            const auto source = ReadFloat(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId result = m_builder.ConvertValue(F64Type(), IL::ConvertOp::FPExt, *source, false);
            return WriteF64(instruction.operands[0], result, instruction.pc);
        }
        case Op::V_CvtF32F64:
        {
            const auto source = ReadF64(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            IL::ValueId result = m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::FPTrunc,
                                                        *source, false);
            result = Bitcast(m_module.types.U32(), result);
            return WriteDest(instruction.operands[0], result, instruction.pc);
        }
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-052", "not an f64 conversion", instruction.pc));
        }
    }

    [[nodiscard]] Result<void> LiftF64Ldexp(const DecodedInstruction& instruction)
    {
        const auto value = ReadF64(instruction.operands[1], instruction.pc);
        if (!value)
            return std::unexpected(value.error());
        const auto exponent = ReadSigned(instruction.operands[2], instruction.pc);
        if (!exponent)
            return std::unexpected(exponent.error());
        // GLSL Exp2 has no 64-bit form; scale in f32, then widen.
        const IL::ValueId exponentF32 =
            m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::SIToFP, *exponent, false);
        const IL::ValueId scaleF32 =
            m_builder.UnaryValue(IL::UnaryOp::Exp2, m_module.types.F32(), exponentF32, false);
        const IL::ValueId scale = m_builder.ConvertValue(F64Type(), IL::ConvertOp::FPExt, scaleF32, false);
        const IL::ValueId result = m_builder.Emit(F64Type(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::FMul),
                                                  {*value, scale},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteF64(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftUnaryFloat(const DecodedInstruction& instruction, IL::UnaryOp subop)
    {
        const auto source = ReadFloatSource(instruction, 1);
        if (!source)
            return std::unexpected(source.error());
        IL::ValueId result = m_builder.UnaryValue(subop, m_module.types.F32(), *source, false);
        result = ApplyOutputModifiers(instruction, result, false);
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

    [[nodiscard]] IL::ValueId BuildBitMask(IL::ValueId width, bool uniform)
    {
        const IL::ValueId one64 = m_builder.ConstantU64(1);
        const IL::ValueId width64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, width, uniform);
        IL::ValueId shifted = m_builder.Emit(m_module.types.U64(),
                                             IL::Op::Binary,
                                             static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                             {one64, width64},
                                             {},
                                             IL::NoPayload,
                                             0,
                                             0,
                                             uniform);
        shifted = m_builder.Emit(m_module.types.U64(),
                                 IL::Op::Binary,
                                 static_cast<std::uint16_t>(IL::BinaryOp::ISub),
                                 {shifted, one64},
                                 {},
                                 IL::NoPayload,
                                 0,
                                 0,
                                 uniform);
        return m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, shifted, uniform);
    }

    [[nodiscard]] IL::ValueId MaskShiftCount(IL::ValueId value, std::uint32_t mask, bool uniform)
    {
        return m_builder.Emit(m_module.types.U32(),
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(IL::BinaryOp::And),
                              {value, m_builder.ConstantU32(mask)},
                              {},
                              IL::NoPayload,
                              0,
                              0,
                              uniform);
    }

    [[nodiscard]] Result<void> LiftAbsDiff(const DecodedInstruction& instruction)
    {
        const auto left = ReadSigned(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadSigned(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());

        const IL::ValueId difference = m_builder.Emit(m_module.types.I32(),
                                                      IL::Op::Binary,
                                                      static_cast<std::uint16_t>(IL::BinaryOp::ISub),
                                                      {*left, *right},
                                                      {},
                                                      IL::NoPayload,
                                                      instruction.pc,
                                                      static_cast<std::uint16_t>(instruction.op),
                                                      true);
        const IL::ValueId negative = m_builder.CompareValue(IL::CompareOp::ILt,
                                                            difference,
                                                            m_builder.ConstantI32(0),
                                                            true);
        const IL::ValueId negated = m_builder.UnaryValue(IL::UnaryOp::Neg,
                                                         m_module.types.I32(),
                                                         difference,
                                                         true);
        const IL::ValueId absolute = m_builder.SelectValue(m_module.types.I32(),
                                                           negative,
                                                           negated,
                                                           difference,
                                                           true);
        const IL::ValueId bits = Bitcast(m_module.types.U32(), absolute);
        const auto scc = WriteSccIfNonZero(bits, instruction.pc, true);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], bits, instruction.pc);
    }

    // S_BFM_B32 / V_BFM_B32: mask = ((1 << size) - 1) << offset.
    [[nodiscard]] Result<void> LiftBitMask(const DecodedInstruction& instruction, bool uniform)
    {
        const auto offset = ReadBits(instruction.operands[1], instruction.pc);
        if (!offset)
            return std::unexpected(offset.error());
        const auto size = ReadBits(instruction.operands[2], instruction.pc);
        if (!size)
            return std::unexpected(size.error());

        const IL::ValueId mask = BuildBitMask(*size, uniform);
        const IL::ValueId shifted = m_builder.Emit(m_module.types.U32(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                   {mask, MaskShiftCount(*offset, 31, uniform)},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   uniform);
        return WriteDest(instruction.operands[0], shifted, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftBitFieldExtract(const DecodedInstruction& instruction,
                                                   bool signedField,
                                                   bool scalar)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        IL::ValueId offset;
        IL::ValueId width;
        const bool uniform = scalar;
        if (scalar)
        {
            const auto control = ReadBits(instruction.operands[2], instruction.pc);
            if (!control)
                return std::unexpected(control.error());
            offset = MaskShiftCount(*control, 31, true);
            width = m_builder.Emit(m_module.types.U32(),
                                   IL::Op::Binary,
                                   static_cast<std::uint16_t>(IL::BinaryOp::And),
                                   {m_builder.Emit(m_module.types.U32(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                                   {*control, m_builder.ConstantU32(16)},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   true),
                                    m_builder.ConstantU32(0x7F)},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   true);
        }
        else
        {
            const auto offsetValue = ReadBits(instruction.operands[2], instruction.pc);
            if (!offsetValue)
                return std::unexpected(offsetValue.error());
            offset = MaskShiftCount(*offsetValue, 31, uniform);

            const auto widthValue = ReadBits(instruction.operands[3], instruction.pc);
            if (!widthValue)
                return std::unexpected(widthValue.error());
            width = m_builder.Emit(m_module.types.U32(),
                                   IL::Op::Binary,
                                   static_cast<std::uint16_t>(IL::BinaryOp::And),
                                   {*widthValue, m_builder.ConstantU32(0x7F)},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   uniform);
        }

        const IL::ValueId mask = BuildBitMask(width, uniform);
        IL::ValueId value = m_builder.Emit(m_module.types.U32(),
                                           IL::Op::Binary,
                                           static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                           {*source, offset},
                                           {},
                                           IL::NoPayload,
                                           instruction.pc,
                                           static_cast<std::uint16_t>(instruction.op),
                                           uniform);
        value = m_builder.Emit(m_module.types.U32(),
                               IL::Op::Binary,
                               static_cast<std::uint16_t>(IL::BinaryOp::And),
                               {value, mask},
                               {},
                               IL::NoPayload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op),
                               uniform);

        if (signedField)
        {
            const IL::ValueId amount = m_builder.Emit(m_module.types.U32(),
                                                      IL::Op::Binary,
                                                      static_cast<std::uint16_t>(IL::BinaryOp::ISub),
                                                      {m_builder.ConstantU32(32), width},
                                                      {},
                                                      IL::NoPayload,
                                                      instruction.pc,
                                                      static_cast<std::uint16_t>(instruction.op),
                                                      uniform);
            const IL::ValueId shift = MaskShiftCount(amount, 31, uniform);
            IL::ValueId shifted = m_builder.Emit(m_module.types.U32(),
                                                 IL::Op::Binary,
                                                 static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                 {value, shift},
                                                 {},
                                                 IL::NoPayload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op),
                                                 uniform);
            shifted = m_builder.Emit(m_module.types.I32(),
                                     IL::Op::Binary,
                                     static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                     {Bitcast(m_module.types.I32(), shifted), shift},
                                     {},
                                     IL::NoPayload,
                                     instruction.pc,
                                     static_cast<std::uint16_t>(instruction.op),
                                     uniform);
            value = Bitcast(m_module.types.U32(), shifted);
        }

        if (scalar)
        {
            const auto scc = WriteSccIfNonZero(value, instruction.pc, true);
            if (!scc)
                return scc;
        }
        return WriteDest(instruction.operands[0], value, instruction.pc);
    }

    // V_BFI_B32: mask from src0, inserted bits from src1, base from src2.
    [[nodiscard]] Result<void> LiftBitFieldInsert(const DecodedInstruction& instruction)
    {
        const auto mask = ReadBits(instruction.operands[1], instruction.pc);
        if (!mask)
            return std::unexpected(mask.error());
        const auto value = ReadBits(instruction.operands[2], instruction.pc);
        if (!value)
            return std::unexpected(value.error());
        const auto base = ReadBits(instruction.operands[3], instruction.pc);
        if (!base)
            return std::unexpected(base.error());

        const IL::ValueId cleared = m_builder.Emit(m_module.types.U32(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                   {*base,
                                                    m_builder.UnaryValue(IL::UnaryOp::Not,
                                                                         m_module.types.U32(),
                                                                         *mask,
                                                                         false)},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   false);
        const IL::ValueId inserted = m_builder.Emit(m_module.types.U32(),
                                                    IL::Op::Binary,
                                                    static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                    {*value, *mask},
                                                    {},
                                                    IL::NoPayload,
                                                    instruction.pc,
                                                    static_cast<std::uint16_t>(instruction.op),
                                                    false);
        const IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                                  {cleared, inserted},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  false);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // V_ALIGNBIT_B32 / V_ALIGNBYTE_B32: take a 32-bit window from the 64-bit
    // value {src0, src1}, starting at src2 bits or bytes.
    [[nodiscard]] Result<void> LiftAlign(const DecodedInstruction& instruction, bool bytes)
    {
        const auto high = ReadBits(instruction.operands[1], instruction.pc);
        if (!high)
            return std::unexpected(high.error());
        const auto low = ReadBits(instruction.operands[2], instruction.pc);
        if (!low)
            return std::unexpected(low.error());
        const auto shift = ReadBits(instruction.operands[3], instruction.pc);
        if (!shift)
            return std::unexpected(shift.error());

        IL::ValueId amount = MaskShiftCount(*shift, bytes ? 7u : 31u, false);
        if (bytes)
        {
            amount = m_builder.Emit(m_module.types.U32(),
                                    IL::Op::Binary,
                                    static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                    {amount, m_builder.ConstantU32(3)},
                                    {},
                                    IL::NoPayload,
                                    instruction.pc,
                                    static_cast<std::uint16_t>(instruction.op),
                                    false);
        }

        const IL::ValueId high64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *high, false);
        const IL::ValueId low64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *low, false);
        IL::ValueId value = m_builder.Emit(m_module.types.U64(),
                                           IL::Op::Binary,
                                           static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                           {high64, m_builder.ConstantU64(32)},
                                           {},
                                           IL::NoPayload,
                                           instruction.pc,
                                           static_cast<std::uint16_t>(instruction.op),
                                           false);
        value = m_builder.Emit(m_module.types.U64(),
                               IL::Op::Binary,
                               static_cast<std::uint16_t>(IL::BinaryOp::Or),
                               {value, low64},
                               {},
                               IL::NoPayload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op),
                               false);
        value = m_builder.Emit(m_module.types.U64(),
                               IL::Op::Binary,
                               static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                               {value, m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, amount, false)},
                               {},
                               IL::NoPayload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op),
                               false);
        const IL::ValueId result = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, false);
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
        const auto a = ReadFloatSource(instruction, 1);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadFloatSource(instruction, 2);
        if (!b)
            return std::unexpected(b.error());
        const auto c = ReadFloatSource(instruction, 3);
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
        result = ApplyOutputModifiers(instruction, result, false);
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
        {
            const auto exec = ReadExec(instruction.pc);
            if (!exec)
                return std::unexpected(exec.error());
            IL::ValueId condition = *exec;
            if (instruction.op == Op::S_CbranchExecz)
                condition = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), condition, true);
            return SetCondBranch(block, condition, *target, instruction.pc + 4, instruction.pc, true);
        }
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
        if ((instruction.abs != 0 || instruction.neg != 0 || instruction.omod != 0 || instruction.clamp) &&
            !SupportsFloatModifiers(instruction.op))
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-019",
                                             std::string("unsupported modifiers on ") +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        }

        switch (instruction.op)
        {
        case Op::S_MovB64:
        case Op::S_NotB64:
        case Op::S_AndB64:
        case Op::S_OrB64:
        case Op::S_XorB64:
        case Op::S_CselectB64:
            return LiftScalar64(instruction);
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
            return LiftCarry(instruction, false);        case Op::S_SubU32:
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
        case Op::S_AbsDiffI32:
            return LiftAbsDiff(instruction);
        case Op::S_BfmB32:
            return LiftBitMask(instruction, true);
        case Op::S_BfeU32:
            return LiftBitFieldExtract(instruction, false, true);
        case Op::S_BfeI32:
            return LiftBitFieldExtract(instruction, true, true);
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
        case Op::V_SubrevNcU32:
        {
            const auto left = ReadBits(instruction.operands[1], instruction.pc);
            if (!left)
                return std::unexpected(left.error());
            const auto right = ReadBits(instruction.operands[2], instruction.pc);
            if (!right)
                return std::unexpected(right.error());
            IL::ValueId result = m_builder.Emit(m_module.types.U32(),
                                                IL::Op::Binary,
                                                static_cast<std::uint16_t>(IL::BinaryOp::ISub),
                                                {*right, *left},
                                                {},
                                                IL::NoPayload,
                                                instruction.pc,
                                                static_cast<std::uint16_t>(instruction.op),
                                                false);
            return WriteDest(instruction.operands[0], result, instruction.pc);
        }
        case Op::V_SubrevF32:
            return LiftF32Reverse(instruction);
        case Op::V_MulDx9ZeroF32:
            return LiftMulDx9Zero(instruction);
        case Op::V_MulI32I24:
            return LiftMul24(instruction, true, false);
        case Op::V_MulHiI32I24:
            return LiftMul24(instruction, true, true);
        case Op::V_MulU32U24:
            return LiftMul24(instruction, false, false);
        case Op::V_MulHiU32U24:
            return LiftMul24(instruction, false, true);
        case Op::V_XnorB32:
            return LiftXnor(instruction);
        case Op::V_LshlrevB64:
            return LiftLshlrevB64(instruction);
        case Op::V_AddCoCiU32:
            return LiftVectorCarry(instruction, false, false);
        case Op::V_SubCoCiU32:
            return LiftVectorCarry(instruction, true, false);
        case Op::V_SubrevCoCiU32:
            return LiftVectorCarry(instruction, true, true);
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
            return LiftVectorCompare(instruction, IL::CompareOp::ILt, VectorPredicate::Compare, false, true);
        case Op::V_CmpEqI32:
            return LiftVectorCompare(instruction, IL::CompareOp::IEq, VectorPredicate::Compare, false, true);
        case Op::V_CmpLeI32:
            return LiftVectorCompare(instruction, IL::CompareOp::ILe, VectorPredicate::Compare, false, true);
        case Op::V_CmpGtI32:
            return LiftVectorCompare(instruction, IL::CompareOp::IGt, VectorPredicate::Compare, false, true);
        case Op::V_CmpNeI32:
            return LiftVectorCompare(instruction, IL::CompareOp::INe, VectorPredicate::Compare, false, true);
        case Op::V_CmpGeI32:
            return LiftVectorCompare(instruction, IL::CompareOp::IGe, VectorPredicate::Compare, false, true);
        case Op::V_CmpFalseI32:
            return LiftVectorCompare(instruction, IL::CompareOp::IEq, VectorPredicate::False, false, true);
        case Op::V_CmpTrueI32:
            return LiftVectorCompare(instruction, IL::CompareOp::IEq, VectorPredicate::True, false, true);
        case Op::V_CmpLtU32:
            return LiftVectorCompare(instruction, IL::CompareOp::ULt, VectorPredicate::Compare, false, false);
        case Op::V_CmpEqU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UEq, VectorPredicate::Compare, false, false);
        case Op::V_CmpLeU32:
            return LiftVectorCompare(instruction, IL::CompareOp::ULe, VectorPredicate::Compare, false, false);
        case Op::V_CmpGtU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UGt, VectorPredicate::Compare, false, false);
        case Op::V_CmpNeU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UNe, VectorPredicate::Compare, false, false);
        case Op::V_CmpGeU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UGe, VectorPredicate::Compare, false, false);
        case Op::V_CmpFalseU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UEq, VectorPredicate::False, false, false);
        case Op::V_CmpTrueU32:
            return LiftVectorCompare(instruction, IL::CompareOp::UEq, VectorPredicate::True, false, false);
        case Op::V_CmpLtF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdLt, VectorPredicate::Compare, true, false);
        case Op::V_CmpEqF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdEq, VectorPredicate::Compare, true, false);
        case Op::V_CmpLeF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdLe, VectorPredicate::Compare, true, false);
        case Op::V_CmpGtF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdGt, VectorPredicate::Compare, true, false);
        case Op::V_CmpLgF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdNe, VectorPredicate::Compare, true, false);
        case Op::V_CmpGeF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdGe, VectorPredicate::Compare, true, false);
        case Op::V_CmpFalseF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdEq, VectorPredicate::False, true, false);
        case Op::V_CmpTrueF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdEq, VectorPredicate::True, true, false);
        case Op::V_CmpOrdF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdEq, VectorPredicate::Ordered, true, false);
        case Op::V_CmpUnordF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdEq, VectorPredicate::Unordered, true, false);
        case Op::V_CmpNgeF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FUnordLt, VectorPredicate::Compare, true, false);
        case Op::V_CmpNlgF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FUnordEq, VectorPredicate::Compare, true, false);
        case Op::V_CmpNgtF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FUnordLe, VectorPredicate::Compare, true, false);
        case Op::V_CmpNleF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FUnordGt, VectorPredicate::Compare, true, false);
        case Op::V_CmpNeqF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FOrdNe, VectorPredicate::Compare, true, false);
        case Op::V_CmpNltF32:
            return LiftVectorCompare(instruction, IL::CompareOp::FUnordGe, VectorPredicate::Compare, true, false);
        case Op::V_CndmaskB32:
            return LiftCndmask(instruction);
        case Op::V_Nop:
            return {};
        case Op::V_ReadfirstlaneB32:
        {
            const auto value = ReadBits(instruction.operands[1], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            return WriteDest(instruction.operands[0], *value, instruction.pc);
        }
        case Op::V_CvtNearestI32F32:
            return LiftConvertRound(instruction, IL::UnaryOp::RoundEven);
        case Op::V_CvtFloorI32F32:
            return LiftConvertRound(instruction, IL::UnaryOp::Floor);
        case Op::V_CvtF32Ubyte0:
            return LiftByteToFloat(instruction, 0);
        case Op::V_CvtF32Ubyte1:
            return LiftByteToFloat(instruction, 1);
        case Op::V_CvtF32Ubyte2:
            return LiftByteToFloat(instruction, 2);
        case Op::V_CvtF32Ubyte3:
            return LiftByteToFloat(instruction, 3);
        case Op::V_FractF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Fract);
        case Op::V_TruncF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Trunc);
        case Op::V_CeilF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Ceil);
        case Op::V_RndneF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::RoundEven);
        case Op::V_FloorF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Floor);
        case Op::V_SinF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Sin);
        case Op::V_CosF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Cos);
        case Op::V_BfrevB32:
            return LiftUnaryBits(instruction, IL::UnaryOp::BitReverse, false);
        case Op::V_ClsI32:
            return LiftCls(instruction);
        case Op::V_CvtI32I16:
            return LiftConvert16(instruction, true);
        case Op::V_CvtU32U16:
            return LiftConvert16(instruction, false);
        case Op::V_Add3U32:
            return LiftIntegerTernary(instruction, IntegerTernary::Add3);
        case Op::V_AddLshlU32:
            return LiftIntegerTernary(instruction, IntegerTernary::AddLshl);
        case Op::V_AndOrB32:
            return LiftIntegerTernary(instruction, IntegerTernary::AndOr);
        case Op::V_Or3B32:
            return LiftIntegerTernary(instruction, IntegerTernary::Or3);
        case Op::V_Xor3B32:
            return LiftIntegerTernary(instruction, IntegerTernary::Xor3);
        case Op::V_LshlAddU32:
            return LiftIntegerTernary(instruction, IntegerTernary::LshlAdd);
        case Op::V_LshlOrB32:
            return LiftIntegerTernary(instruction, IntegerTernary::LshlOr);
        case Op::V_XadU32:
            return LiftIntegerTernary(instruction, IntegerTernary::Xad);
        case Op::V_Min3I32:
            return LiftIntegerTernary(instruction, IntegerTernary::Min3Signed);
        case Op::V_Min3U32:
            return LiftIntegerTernary(instruction, IntegerTernary::Min3Unsigned);
        case Op::V_Med3I32:
            return LiftIntegerTernary(instruction, IntegerTernary::Med3Signed);
        case Op::V_Med3U32:
            return LiftIntegerTernary(instruction, IntegerTernary::Med3Unsigned);
        case Op::V_Max3I32:
            return LiftIntegerTernary(instruction, IntegerTernary::Max3Signed);
        case Op::V_Max3U32:
            return LiftIntegerTernary(instruction, IntegerTernary::Max3Unsigned);
        case Op::V_SadU32:
            return LiftIntegerTernary(instruction, IntegerTernary::Sad);
        case Op::V_MulHiI32:
            return LiftMulHi32(instruction, true);
        case Op::V_MulHiU32:
            return LiftMulHi32(instruction, false);
        case Op::V_MadI32I24:
            return LiftMad24(instruction, true);
        case Op::V_MadU32U24:
            return LiftMad24(instruction, false);
        case Op::V_LdexpF32:
            return LiftLdexp(instruction);
        case Op::V_CvtF16F32:
        case Op::V_CvtF32F16:
        case Op::V_CvtF16U16:
        case Op::V_CvtF16I16:
        case Op::V_CvtU16F16:
        case Op::V_CvtI16F16:
            return LiftF16Convert(instruction);
        case Op::V_CvtNormI16F16:
            return LiftF16Norm(instruction, true);
        case Op::V_CvtNormU16F16:
            return LiftF16Norm(instruction, false);
        case Op::V_RcpF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Rcp);
        case Op::V_RsqF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Rsq);
        case Op::V_SqrtF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Sqrt);
        case Op::V_ExpF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Exp2);
        case Op::V_LogF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Log2);
        case Op::V_FloorF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Floor);
        case Op::V_CeilF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Ceil);
        case Op::V_TruncF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Trunc);
        case Op::V_RndneF16:
            return LiftF16Unary(instruction, IL::UnaryOp::RoundEven);
        case Op::V_FractF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Fract);
        case Op::V_SinF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Sin);
        case Op::V_CosF16:
            return LiftF16Unary(instruction, IL::UnaryOp::Cos);
        case Op::V_MovB16:
            return LiftBit16(instruction, false);
        case Op::V_NotB16:
            return LiftBit16(instruction, true);
        case Op::V_AddF16:
            return LiftF16Binary(instruction, IL::BinaryOp::FAdd);
        case Op::V_SubF16:
            return LiftF16Binary(instruction, IL::BinaryOp::FSub);
        case Op::V_SubrevF16:
            return LiftF16Binary(instruction, IL::BinaryOp::FSub, true);
        case Op::V_MulF16:
            return LiftF16Binary(instruction, IL::BinaryOp::FMul);
        case Op::V_MinF16:
            return LiftF16Binary(instruction, IL::BinaryOp::FMin);
        case Op::V_MaxF16:
            return LiftF16Binary(instruction, IL::BinaryOp::FMax);
        case Op::V_FmaF16:
            return LiftF16Fma(instruction, false);
        case Op::V_FmacF16:
            return LiftF16Fma(instruction, true);
        case Op::V_LdexpF16:
            return LiftF16Ldexp(instruction);
        case Op::V_Min3F16:
            return LiftF16Ternary(instruction, IL::TernaryOp::FMin3);
        case Op::V_Max3F16:
            return LiftF16Ternary(instruction, IL::TernaryOp::FMax3);
        case Op::V_CvtI32F64:
        case Op::V_CvtF64I32:
        case Op::V_CvtF32F64:
        case Op::V_CvtF64F32:
        case Op::V_CvtU32F64:
        case Op::V_CvtF64U32:
            return LiftF64Convert(instruction);
        case Op::V_TruncF64:
            return LiftF64Unary(instruction, IL::UnaryOp::Trunc);
        case Op::V_CeilF64:
            return LiftF64Unary(instruction, IL::UnaryOp::Ceil);
        case Op::V_RndneF64:
            return LiftF64Unary(instruction, IL::UnaryOp::RoundEven);
        case Op::V_FloorF64:
            return LiftF64Unary(instruction, IL::UnaryOp::Floor);
        case Op::V_RcpF64:
            return LiftF64Unary(instruction, IL::UnaryOp::Rcp);
        case Op::V_RsqF64:
            return LiftF64Unary(instruction, IL::UnaryOp::Rsq);
        case Op::V_SqrtF64:
            return LiftF64Unary(instruction, IL::UnaryOp::Sqrt);
        case Op::V_FractF64:
            return LiftF64Unary(instruction, IL::UnaryOp::Fract);
        case Op::V_AddF64:
            return LiftF64Binary(instruction, IL::BinaryOp::FAdd);
        case Op::V_MulF64:
            return LiftF64Binary(instruction, IL::BinaryOp::FMul);
        case Op::V_MinF64:
            return LiftF64Binary(instruction, IL::BinaryOp::FMin);
        case Op::V_MaxF64:
            return LiftF64Binary(instruction, IL::BinaryOp::FMax);
        case Op::V_FmaF64:
            return LiftF64Fma(instruction);
        case Op::V_LdexpF64:
            return LiftF64Ldexp(instruction);
        case Op::V_BfeU32:
            return LiftBitFieldExtract(instruction, false, false);        case Op::V_BfeI32:
            return LiftBitFieldExtract(instruction, true, false);
        case Op::V_BfiB32:
            return LiftBitFieldInsert(instruction);
        case Op::V_BfmB32:
            return LiftBitMask(instruction, false);
        case Op::V_AlignbitB32:
            return LiftAlign(instruction, false);
        case Op::V_AlignbyteB32:
            return LiftAlign(instruction, true);
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
    IL::ValueId m_execSlot = IL::InvalidValue;
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
