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
#include "SR/Frontend/Arch/Gfx/GfxBufferFormat.hpp"

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
    Lifter(IL::Module& module,
           IL::Function& function,
           IL::FunctionBuilder& builder,
           Arch arch,
           const LiftOptions& options)
        : m_module(module)
        , m_function(function)
        , m_builder(builder)
        , m_arch(arch)
        , m_options(options)
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

        // A vertex program receives its vertex, primitive and instance ids in
        // v0, v2 and v3. Attributes arrive through the fetch stage, which the
        // front end does not model yet.
        if (m_module.stage == IL::Stage::Vertex)
        {
            const auto seed = [this](IL::Builtin builtin, std::uint32_t vgpr) {
                const IL::TypeId u32 = m_module.types.U32();
                (void)EnsureSignature(IL::Space::Input, builtin, 0, u32);
                const IL::PayloadId payload =
                    StageIoPayload(IL::Space::Input, builtin, 0, 0, IL::Interpolation::None);
                const IL::ValueId value = m_builder.Emit(u32,
                                                         IL::Op::Intrinsic,
                                                         static_cast<std::uint16_t>(IL::IntrinsicId::StageInput),
                                                         {},
                                                         {},
                                                         payload,
                                                         0,
                                                         0,
                                                         true);
                m_builder.Store(Slot(m_vgprSlots, vgpr, IL::RegisterClass::Vgpr), value, 0);
            };

            seed(IL::Builtin::VertexIndex, 0);
            seed(IL::Builtin::InstanceIndex, 3);
        }

        // User data arrives in the first scalar registers, in root order. A
        // fixture or the container's root layout supplies it as constants until
        // the descriptor model can read it from memory.
        for (std::size_t index = 0; index < m_options.userData.size() && index <= kLastSgpr; ++index)
        {
            IL::ValueId value = IL::InvalidValue;
            if (m_options.userDataFromPush)
            {
                IL::UserDataDesc payload;
                payload.index = static_cast<std::uint32_t>(index);
                const IL::PayloadId payloadId = m_module.payloads.Add(payload);
                value = m_builder.Emit(m_module.types.U32(),
                                       IL::Op::Intrinsic,
                                       static_cast<std::uint16_t>(IL::IntrinsicId::UserData),
                                       {},
                                       {},
                                       payloadId,
                                       0,
                                       0,
                                       true);
            }
            else
            {
                value = m_builder.ConstantU32(m_options.userData[index]);
            }

            m_builder.Store(Slot(m_sgprSlots, static_cast<std::uint32_t>(index), IL::RegisterClass::Sgpr), value, 0);
        }

        // A compute program reads its thread id from the builtin; the workgroup
        // ids and the user data follow in the scalar registers, which the
        // descriptor model will supply.
        if (m_module.stage == IL::Stage::Compute)
        {
            const IL::TypeId u32 = m_module.types.U32();
            const IL::SpecialRegId ids[3] = {IL::SpecialRegId::ThreadIdX, IL::SpecialRegId::ThreadIdY,
                                             IL::SpecialRegId::ThreadIdZ};
            for (std::uint32_t component = 0; component < 3; ++component)
            {
                IL::SpecialRegPayload payload;
                payload.id = ids[component];
                const IL::PayloadId payloadId = m_module.payloads.Add(payload);
                const IL::ValueId value = m_builder.Emit(u32,
                                                         IL::Op::Intrinsic,
                                                         static_cast<std::uint16_t>(IL::IntrinsicId::SpecialReg),
                                                         {},
                                                         {},
                                                         payloadId,
                                                         0,
                                                         0,
                                                         false);
                m_builder.Store(Slot(m_vgprSlots, component, IL::RegisterClass::Vgpr), value, 0);
            }
        }

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

    // Which staged vertex the next message publishes.
    [[nodiscard]] IL::ValueId GeometryEmittedSlot()
    {
        if (m_geometryEmitted == IL::InvalidValue)
        {
            const IL::TypeId pointer = m_module.types.Pointer(IL::Space::Private, m_module.types.U32());
            m_geometryEmitted = m_builder.Alloca(pointer, m_entry);
            m_builder.Store(m_geometryEmitted, m_builder.ConstantU32(0), 0);
        }
        return m_geometryEmitted;
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

    // One scalar register's 32-bit contents. The slot exists because the user
    // data seeding created it; an unseeded register reads undef.
    [[nodiscard]] Result<IL::ValueId> ReadSgprBits(std::uint32_t index, std::uint32_t pc)
    {
        if (index > kLastSgpr)
            return std::unexpected(LiftError("SR-GFX-LIFT-083", "scalar register is out of range", pc));
        return m_builder.Load(m_module.types.U32(), Slot(m_sgprSlots, index, IL::RegisterClass::Sgpr), pc, true);
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
        // From here on a memory access may be masked for some lanes.
        m_execMayBeFalse = true;
        return {};
    }

    // The source's EXEC mask becomes the guard of the access just emitted.
    // The EXEC mask as of this point, or InvalidValue when no mask can be
    // false. It is read before the access so the value exists when the access
    // executes.
    [[nodiscard]] IL::ValueId CurrentGuard(std::uint32_t pc)
    {
        if (!m_execMayBeFalse)
            return IL::InvalidValue;
        const auto exec = ReadExec(pc);
        return exec ? *exec : IL::InvalidValue;
    }

    // Guards the most recent access with a mask captured before it was emitted.
    void ApplyGuard(std::uint32_t pc, IL::ValueId guard)
    {
        if (guard == IL::InvalidValue)
            return;

        IL::Block& block = m_builder.GetBlock(m_builder.CurrentBlock());

        constexpr std::size_t none = static_cast<std::size_t>(-1);
        std::size_t target = none;
        for (std::size_t i = block.body.size(); i-- > 0;)
        {
            const IL::Instruction& candidate = block.body[i];
            IL::Effect effect = IL::EffectsOf(candidate.op);
            if (candidate.op == IL::Op::Intrinsic)
                effect = IL::GetIntrinsicSpec(static_cast<IL::IntrinsicId>(candidate.subop)).effects;

            if (IL::HasEffect(effect, IL::Effect::WriteMemory) || IL::HasEffect(effect, IL::Effect::Atomic))
            {
                target = i;
                break;
            }
        }

        if (target == none)
            return;

        block.body[target].guard = guard;
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

    [[nodiscard]] Result<void> WriteSgprIndex(std::uint32_t index, IL::ValueId value, std::uint32_t pc)
    {
        if (index > kLastSgpr)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-071",
                                             "write to special scalar register " + std::to_string(index),
                                             pc));
        }
        m_builder.Store(Slot(m_sgprSlots, index, IL::RegisterClass::Sgpr), value, pc);
        return {};
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

        // A 64-bit write to a special register opens the wave or sets a mask.
        // The model keeps one boolean per invocation, so the pair collapses to
        // "does this lane have a bit".
        if (index == 126 || index == 106)
        {
            const IL::ValueId nonzero =
                m_builder.CompareValue(IL::CompareOp::UNe, value, m_builder.ConstantU64(0), true);
            return index == 126 ? WriteExec(nonzero, pc) : WriteVcc(nonzero, pc);
        }

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
        case Op::S_AndNot1B64:
        case Op::S_OrNot1B64:
        case Op::S_NandB64:
        case Op::S_NorB64:
        case Op::S_XnorB64:
        {
            const auto left = ReadPairSource(instruction.operands[1], instruction.pc);
            if (!left)
                return std::unexpected(left.error());
            const auto right = ReadPairSource(instruction.operands[2], instruction.pc);
            if (!right)
                return std::unexpected(right.error());
            const bool inverted = instruction.op == Op::S_AndNot1B64 ||
                                  instruction.op == Op::S_OrNot1B64 || instruction.op == Op::S_NandB64 ||
                                  instruction.op == Op::S_NorB64 || instruction.op == Op::S_XnorB64;
            const bool invertSecond = instruction.op == Op::S_AndNot1B64 ||
                                      instruction.op == Op::S_OrNot1B64;
            IL::BinaryOp subop = IL::BinaryOp::And;
            switch (instruction.op)
            {
            case Op::S_AndB64:
            case Op::S_AndNot1B64:
            case Op::S_NandB64:
                subop = IL::BinaryOp::And;
                break;
            case Op::S_OrB64:
            case Op::S_OrNot1B64:
            case Op::S_NorB64:
                subop = IL::BinaryOp::Or;
                break;
            default:
                subop = IL::BinaryOp::Xor;
                break;
            }
            const IL::ValueId operand =
                invertSecond
                    ? m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U64(), *right, true)
                    : *right;
            IL::ValueId combined = m_builder.Emit(m_module.types.U64(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(subop),
                                                  {*left, operand},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  true);
            if (inverted)
                combined = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U64(), combined, true);
            value = combined;
            break;
        }
        case Op::S_AddNcU64:
        case Op::S_SubNcU64:
        case Op::S_MulU64:
        {
            const auto left = ReadPairSource(instruction.operands[1], instruction.pc);
            if (!left)
                return std::unexpected(left.error());
            const auto right = ReadPairSource(instruction.operands[2], instruction.pc);
            if (!right)
                return std::unexpected(right.error());
            const IL::BinaryOp subop = instruction.op == Op::S_AddNcU64 ? IL::BinaryOp::IAdd
                                       : instruction.op == Op::S_SubNcU64 ? IL::BinaryOp::ISub
                                                                         : IL::BinaryOp::IMul;
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

    enum class CompareValueKind
    {
        F16,
        F32,
        F64,
        I16,
        U16,
        I32,
        U32,
        I64,
        U64,
    };

    [[nodiscard]] IL::ValueId SignExtend16Value(IL::ValueId value)
    {
        IL::ValueId shifted = m_builder.Emit(m_module.types.U32(),
                                             IL::Op::Binary,
                                             static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                             {value, m_builder.ConstantU32(16)},
                                             {},
                                             IL::NoPayload,
                                             0,
                                             0,
                                             false);
        shifted = m_builder.Emit(m_module.types.I32(),
                                 IL::Op::Binary,
                                 static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                 {Bitcast(m_module.types.I32(), shifted), m_builder.ConstantU32(16)},
                                 {},
                                 IL::NoPayload,
                                 0,
                                 0,
                                 false);
        return Bitcast(m_module.types.U32(), shifted);
    }

    [[nodiscard]] Result<IL::ValueId> ReadCompareValue(const DecodedInstruction& instruction,
                                                       CompareValueKind kind,
                                                       std::uint8_t index)
    {
        switch (kind)
        {
        case CompareValueKind::F16:
            return ReadF16(instruction.operands[index], instruction.pc);
        case CompareValueKind::F32:
            return ReadFloatSource(instruction, index);
        case CompareValueKind::F64:
            return ReadF64(instruction.operands[index], instruction.pc);
        case CompareValueKind::I16:
        {
            const auto value = ReadBits(instruction.operands[index], instruction.pc);
            if (!value)
                return value;
            return Bitcast(m_module.types.I32(), SignExtend16Value(*value));
        }
        case CompareValueKind::U16:
        {
            const auto value = ReadBits(instruction.operands[index], instruction.pc);
            if (!value)
                return value;
            return m_builder.Emit(m_module.types.U32(),
                                  IL::Op::Binary,
                                  static_cast<std::uint16_t>(IL::BinaryOp::And),
                                  {*value, m_builder.ConstantU32(0xFFFF)},
                                  {},
                                  IL::NoPayload,
                                  instruction.pc,
                                  static_cast<std::uint16_t>(instruction.op),
                                  false);
        }
        case CompareValueKind::I32:
            return ReadSigned(instruction.operands[index], instruction.pc);
        case CompareValueKind::U32:
            return ReadBits(instruction.operands[index], instruction.pc);
        case CompareValueKind::I64:
        {
            if (instruction.operands[index].kind != OperandKind::Vgpr &&
                instruction.operands[index].kind != OperandKind::Vsrc)
                return std::unexpected(LiftError("SR-GFX-LIFT-053", "i64 compare source is not a VGPR pair",
                                                 instruction.pc));
            const auto bits = ReadVgpr64(instruction.operands[index].value, instruction.pc);
            if (!bits)
                return bits;
            return Bitcast(m_module.types.I64(), *bits);
        }
        case CompareValueKind::U64:
        {
            if (instruction.operands[index].kind != OperandKind::Vgpr &&
                instruction.operands[index].kind != OperandKind::Vsrc)
                return std::unexpected(LiftError("SR-GFX-LIFT-054", "u64 compare source is not a VGPR pair",
                                                 instruction.pc));
            return ReadVgpr64(instruction.operands[index].value, instruction.pc);
        }
        }
        return std::unexpected(LiftError("SR-GFX-LIFT-055", "unknown compare kind", instruction.pc));
    }

    [[nodiscard]] Result<IL::ValueId> CompareCondition(const DecodedInstruction& instruction,
                                                       CompareValueKind kind,
                                                       IL::CompareOp compare,
                                                       VectorPredicate predicate,
                                                       std::uint8_t firstIndex = 1,
                                                       std::uint8_t secondIndex = 2)
    {
        if (predicate == VectorPredicate::False || predicate == VectorPredicate::True)
            return m_builder.ConstantBool(predicate == VectorPredicate::True);

        const auto left = ReadCompareValue(instruction, kind, firstIndex);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadCompareValue(instruction, kind, secondIndex);
        if (!right)
            return std::unexpected(right.error());

        if (predicate == VectorPredicate::Ordered || predicate == VectorPredicate::Unordered)
        {
            const IL::ValueId leftOrdered =
                m_builder.CompareValue(IL::CompareOp::FOrdEq, *left, *left, false);
            const IL::ValueId rightOrdered =
                m_builder.CompareValue(IL::CompareOp::FOrdEq, *right, *right, false);
            IL::ValueId condition = m_builder.Emit(m_module.types.Bool(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                   {leftOrdered, rightOrdered},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   false);
            if (predicate == VectorPredicate::Unordered)
                condition =
                    m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), condition, false);
            return condition;
        }

        return m_builder.CompareValue(compare, *left, *right, false);
    }

    [[nodiscard]] Result<void> LiftVectorCompareTyped(const DecodedInstruction& instruction,
                                                      CompareValueKind kind,
                                                      IL::CompareOp compare,
                                                      VectorPredicate predicate)
    {
        const auto condition = CompareCondition(instruction, kind, compare, predicate);
        if (!condition)
            return std::unexpected(condition.error());

        // The VOP3 compare form takes an explicit SGPR pair and writes the wave
        // ballot there: the predicate is per lane, the mask is per wave.
        if (instruction.encoding == Encoding::Vop3)
        {
            // A cmpx writes EXEC, not the named destination.
            const Operand* destination = instruction.execMask ? nullptr : FindOperand(instruction, OperandKind::Sdst);
            if (destination != nullptr)
            {
                const IL::ValueId ballot = m_builder.Emit(m_module.types.U64(),
                                                          IL::Op::Intrinsic,
                                                          static_cast<std::uint16_t>(IL::IntrinsicId::WaveBallot),
                                                          {*condition},
                                                          {},
                                                          IL::NoPayload,
                                                          instruction.pc,
                                                          static_cast<std::uint16_t>(instruction.op),
                                                          true);
                return WriteSgpr64(*destination, ballot, instruction.pc);
            }
        }

        const auto vcc = WriteVcc(*condition, instruction.pc);
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
                                                       {*exec, *condition},
                                                       {},
                                                       IL::NoPayload,
                                                       instruction.pc,
                                                       static_cast<std::uint16_t>(instruction.op),
                                                       false);
            return WriteExec(updated, instruction.pc);
        }
        return {};
    }

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

        // The VOP3 compare form takes an explicit SGPR destination and writes
        // the wave ballot there: the predicate is per lane, the mask is per
        // wave. A 64-lane wave needs the pair.
        if (instruction.encoding == Encoding::Vop3)
        {
            // A cmpx writes EXEC, not the named destination.
            const Operand* scalarDestination =
                instruction.execMask ? nullptr : FindOperand(instruction, OperandKind::Sdst);
            if (scalarDestination != nullptr)
            {
                const IL::ValueId ballot = m_builder.Emit(m_module.types.U64(),
                                                          IL::Op::Intrinsic,
                                                          static_cast<std::uint16_t>(IL::IntrinsicId::WaveBallot),
                                                          {condition},
                                                          {},
                                                          IL::NoPayload,
                                                          instruction.pc,
                                                          static_cast<std::uint16_t>(instruction.op),
                                                          true);
                if (m_options.wave64)
                    return WriteSgpr64(*scalarDestination, ballot, instruction.pc);

                const IL::ValueId low =
                    m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, ballot, true);
                return WriteDest(*scalarDestination, low, instruction.pc);
            }
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
    [[nodiscard]] IL::TypeId BF16Type() { return m_module.types.Float(IL::FloatFormat::BF16); }
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

    enum class PackedKind
    {
        AddI16,
        SubI16,
        MulLoU16,
        MadI16,
        MinI16,
        MaxI16,
        AddU16,
        SubU16,
        MadU16,
        MinU16,
        MaxU16,
        LshlrevB16,
        LshrrevB16,
        AshrrevI16,
        AddF16,
        MulF16,
        FmaF16,
        MinF16,
        MaxF16,
    };

    [[nodiscard]] IL::ValueId PackLanes(IL::ValueId low, IL::ValueId high)
    {
        const IL::ValueId highShifted = m_builder.Emit(m_module.types.U32(),
                                                       IL::Op::Binary,
                                                       static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                       {high, m_builder.ConstantU32(16)},
                                                       {},
                                                       IL::NoPayload,
                                                       0,
                                                       0,
                                                       false);
        return m_builder.Emit(m_module.types.U32(),
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(IL::BinaryOp::Or),
                              {highShifted, low},
                              {},
                              IL::NoPayload,
                              0,
                              0,
                              false);
    }

    [[nodiscard]] IL::ValueId F16Lane(IL::ValueId bits)
    {
        const IL::ValueId low = m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, bits, false);
        return Bitcast(F16Type(), low);
    }

    [[nodiscard]] IL::ValueId F16LaneBits(IL::ValueId value)
    {
        return m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                      Bitcast(U16Type(), value), false);
    }

    [[nodiscard]] Result<void> LiftPacked(const DecodedInstruction& instruction, PackedKind kind)
    {
        if (instruction.neg != 0 || instruction.negHi != 0 || instruction.clamp)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-056",
                                             "packed negate/clamp modifiers are not implemented yet",
                                             instruction.pc));
        }

        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());

        const bool ternary = kind == PackedKind::MadI16 || kind == PackedKind::MadU16 ||
                             kind == PackedKind::FmaF16;
        IL::ValueId third = IL::InvalidValue;
        if (ternary)
        {
            const auto value = ReadBits(instruction.operands[3], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            third = *value;
        }

        const auto lane = [&](IL::ValueId value, std::uint32_t shift) {
            if (shift == 0)
                return m_builder.Emit(m_module.types.U32(),
                                      IL::Op::Binary,
                                      static_cast<std::uint16_t>(IL::BinaryOp::And),
                                      {value, m_builder.ConstantU32(0xFFFF)},
                                      {},
                                      IL::NoPayload,
                                      instruction.pc,
                                      static_cast<std::uint16_t>(instruction.op),
                                      false);
            return m_builder.Emit(m_module.types.U32(),
                                  IL::Op::Binary,
                                  static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                  {value, m_builder.ConstantU32(shift)},
                                  {},
                                  IL::NoPayload,
                                  instruction.pc,
                                  static_cast<std::uint16_t>(instruction.op),
                                  false);
        };

        const auto compute = [&](IL::ValueId a, IL::ValueId b, IL::ValueId c) -> IL::ValueId {
            const auto mask = [&](IL::ValueId value) {
                return m_builder.Emit(m_module.types.U32(),
                                      IL::Op::Binary,
                                      static_cast<std::uint16_t>(IL::BinaryOp::And),
                                      {value, m_builder.ConstantU32(0xFFFF)},
                                      {},
                                      IL::NoPayload,
                                      instruction.pc,
                                      static_cast<std::uint16_t>(instruction.op),
                                      false);
            };
            const auto binary = [&](IL::BinaryOp subop, IL::ValueId left, IL::ValueId right) {
                return mask(m_builder.Emit(m_module.types.U32(),
                                           IL::Op::Binary,
                                           static_cast<std::uint16_t>(subop),
                                           {left, right},
                                           {},
                                           IL::NoPayload,
                                           instruction.pc,
                                           static_cast<std::uint16_t>(instruction.op),
                                           false));
            };

            switch (kind)
            {
            case PackedKind::AddI16:
            case PackedKind::AddU16:
                return binary(IL::BinaryOp::IAdd, a, b);
            case PackedKind::SubI16:
            case PackedKind::SubU16:
                return binary(IL::BinaryOp::ISub, a, b);
            case PackedKind::MulLoU16:
                return binary(IL::BinaryOp::IMul, a, b);
            case PackedKind::MadI16:
            case PackedKind::MadU16:
            {
                const IL::ValueId product = binary(IL::BinaryOp::IMul, a, b);
                return binary(IL::BinaryOp::IAdd, product, c);
            }
            case PackedKind::MinI16:
                return mask(SelectMin(SignExtend16Value(a), SignExtend16Value(b), true));
            case PackedKind::MaxI16:
                return mask(SelectMax(SignExtend16Value(a), SignExtend16Value(b), true));
            case PackedKind::MinU16:
                return mask(SelectMin(mask(a), mask(b), false));
            case PackedKind::MaxU16:
                return mask(SelectMax(mask(a), mask(b), false));
            case PackedKind::LshlrevB16:
                return binary(IL::BinaryOp::Shl, b, MaskShiftCount(a, 15, false));
            case PackedKind::LshrrevB16:
                return binary(IL::BinaryOp::LShr, b, MaskShiftCount(a, 15, false));
            case PackedKind::AshrrevI16:
            {
                const IL::ValueId shifted = m_builder.Emit(m_module.types.I32(),
                                                           IL::Op::Binary,
                                                           static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                                           {Bitcast(m_module.types.I32(), SignExtend16Value(b)),
                                                            MaskShiftCount(a, 15, false)},
                                                           {},
                                                           IL::NoPayload,
                                                           instruction.pc,
                                                           static_cast<std::uint16_t>(instruction.op),
                                                           false);
                return mask(Bitcast(m_module.types.U32(), shifted));
            }
            case PackedKind::AddF16:
            case PackedKind::MulF16:
            case PackedKind::MinF16:
            case PackedKind::MaxF16:
            {
                IL::BinaryOp subop = IL::BinaryOp::FAdd;
                if (kind == PackedKind::MulF16)
                    subop = IL::BinaryOp::FMul;
                else if (kind == PackedKind::MinF16)
                    subop = IL::BinaryOp::FMin;
                else if (kind == PackedKind::MaxF16)
                    subop = IL::BinaryOp::FMax;
                const IL::ValueId result = m_builder.Emit(F16Type(),
                                                          IL::Op::Binary,
                                                          static_cast<std::uint16_t>(subop),
                                                          {F16Lane(a), F16Lane(b)},
                                                          {},
                                                          IL::NoPayload,
                                                          instruction.pc,
                                                          static_cast<std::uint16_t>(instruction.op),
                                                          false);
                return F16LaneBits(result);
            }
            case PackedKind::FmaF16:
            {
                const IL::ValueId result = m_builder.Emit(F16Type(),
                                                          IL::Op::Ternary,
                                                          static_cast<std::uint16_t>(IL::TernaryOp::Fma),
                                                          {F16Lane(a), F16Lane(b), F16Lane(c)},
                                                          {},
                                                          IL::NoPayload,
                                                          instruction.pc,
                                                          static_cast<std::uint16_t>(instruction.op),
                                                          false);
                return F16LaneBits(result);
            }
            }
            return IL::InvalidValue;
        };

        const IL::ValueId low = compute(lane(*first, 0), lane(*second, 0), ternary ? lane(third, 0) : 0);
        const IL::ValueId high =
            compute(lane(*first, 16), lane(*second, 16), ternary ? lane(third, 16) : 0);
        return WriteDest(instruction.operands[0], PackLanes(low, high), instruction.pc);
    }

    [[nodiscard]] IL::ValueId SignExtend8Value(IL::ValueId value)
    {
        IL::ValueId shifted = m_builder.Emit(m_module.types.U32(),
                                             IL::Op::Binary,
                                             static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                             {value, m_builder.ConstantU32(24)},
                                             {},
                                             IL::NoPayload,
                                             0,
                                             0,
                                             false);
        shifted = m_builder.Emit(m_module.types.I32(),
                                 IL::Op::Binary,
                                 static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                 {Bitcast(m_module.types.I32(), shifted), m_builder.ConstantU32(24)},
                                 {},
                                 IL::NoPayload,
                                 0,
                                 0,
                                 false);
        return Bitcast(m_module.types.U32(), shifted);
    }

    [[nodiscard]] Result<void> LiftSext(const DecodedInstruction& instruction, std::uint32_t bits)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const IL::ValueId result = bits == 8 ? SignExtend8Value(*source) : SignExtend16Value(*source);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] IL::ValueId PopCount64(IL::ValueId value)
    {
        // OpBitCount is 32-bit only in Vulkan SPIR-V: count both halves.
        const IL::ValueId low = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, true);
        const IL::ValueId high64 = m_builder.Emit(m_module.types.U64(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                                  {value, m_builder.ConstantU64(32)},
                                                  {}, IL::NoPayload, 0, 0, true);
        const IL::ValueId high = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, true);
        const IL::ValueId lowCount = m_builder.UnaryValue(IL::UnaryOp::PopCount, m_module.types.U32(), low, true);
        const IL::ValueId highCount = m_builder.UnaryValue(IL::UnaryOp::PopCount, m_module.types.U32(), high, true);
        return m_builder.Emit(m_module.types.U32(),
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                              {lowCount, highCount},
                              {}, IL::NoPayload, 0, 0, true);
    }

    [[nodiscard]] Result<void> LiftBitCount64(const DecodedInstruction& instruction, bool ones)
    {
        const auto value = ReadPairSource(instruction.operands[1], instruction.pc);
        if (!value)
            return std::unexpected(value.error());
        IL::ValueId count = PopCount64(*value);
        if (!ones)
        {
            count = m_builder.Emit(m_module.types.U32(),
                                   IL::Op::Binary,
                                   static_cast<std::uint16_t>(IL::BinaryOp::ISub),
                                   {m_builder.ConstantU32(64), count},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   true);
        }
        const auto scc = WriteSccIfNonZero(count, instruction.pc, true);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], count, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftBitset(const DecodedInstruction& instruction, bool wide, bool set)
    {
        const std::uint32_t width = wide ? 64 : 32;
        if (wide)
        {
            const std::uint32_t index = instruction.operands[0].value;
            const auto base = ReadSgpr64(index, instruction.pc);
            if (!base)
                return std::unexpected(base.error());
            const auto offset = ReadBits(instruction.operands[1], instruction.pc);
            if (!offset)
                return std::unexpected(offset.error());
            const IL::ValueId shift = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt,
                                                             MaskShiftCount(*offset, width - 1, true), true);
            const IL::ValueId mask = m_builder.Emit(m_module.types.U64(),
                                                    IL::Op::Binary,
                                                    static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                    {m_builder.ConstantU64(1), shift},
                                                    {},
                                                    IL::NoPayload,
                                                    instruction.pc,
                                                    static_cast<std::uint16_t>(instruction.op),
                                                    true);
            IL::ValueId result = IL::InvalidValue;
            if (set)
            {
                result = m_builder.Emit(m_module.types.U64(),
                                        IL::Op::Binary,
                                        static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                        {*base, mask},
                                        {},
                                        IL::NoPayload,
                                        instruction.pc,
                                        static_cast<std::uint16_t>(instruction.op),
                                        true);
            }
            else
            {
                const IL::ValueId inverted =
                    m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U64(), mask, true);
                result = m_builder.Emit(m_module.types.U64(),
                                        IL::Op::Binary,
                                        static_cast<std::uint16_t>(IL::BinaryOp::And),
                                        {*base, inverted},
                                        {},
                                        IL::NoPayload,
                                        instruction.pc,
                                        static_cast<std::uint16_t>(instruction.op),
                                        true);
            }
            return WriteSgpr64(instruction.operands[0], result, instruction.pc);
        }

        const std::uint32_t index = instruction.operands[0].value;
        const auto base = ReadSgpr(index, instruction.pc);
        if (!base)
            return std::unexpected(base.error());
        const auto offset = ReadBits(instruction.operands[1], instruction.pc);
        if (!offset)
            return std::unexpected(offset.error());
        const IL::ValueId mask = m_builder.Emit(m_module.types.U32(),
                                                IL::Op::Binary,
                                                static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                {m_builder.ConstantU32(1),
                                                 MaskShiftCount(*offset, width - 1, true)},
                                                {},
                                                IL::NoPayload,
                                                instruction.pc,
                                                static_cast<std::uint16_t>(instruction.op),
                                                true);
        IL::ValueId result = IL::InvalidValue;
        if (set)
        {
            result = m_builder.Emit(m_module.types.U32(),
                                    IL::Op::Binary,
                                    static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                    {*base, mask},
                                    {},
                                    IL::NoPayload,
                                    instruction.pc,
                                    static_cast<std::uint16_t>(instruction.op),
                                    true);
        }
        else
        {
            const IL::ValueId inverted =
                m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U32(), mask, true);
            result = m_builder.Emit(m_module.types.U32(),
                                    IL::Op::Binary,
                                    static_cast<std::uint16_t>(IL::BinaryOp::And),
                                    {*base, inverted},
                                    {},
                                    IL::NoPayload,
                                    instruction.pc,
                                    static_cast<std::uint16_t>(instruction.op),
                                    true);
        }
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftShift64(const DecodedInstruction& instruction, IL::BinaryOp subop)
    {
        const auto value = ReadPairSource(instruction.operands[1], instruction.pc);
        if (!value)
            return std::unexpected(value.error());
        const auto count = ReadBits(instruction.operands[2], instruction.pc);
        if (!count)
            return std::unexpected(count.error());
        const IL::ValueId shift = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt,
                                                         MaskShiftCount(*count, 63, true), true);
        IL::ValueId result = m_builder.Emit(subop == IL::BinaryOp::AShr ? m_module.types.I64()
                                                                        : m_module.types.U64(),
                                            IL::Op::Binary,
                                            static_cast<std::uint16_t>(subop),
                                            {subop == IL::BinaryOp::AShr ? Bitcast(m_module.types.I64(), *value)
                                                                         : *value,
                                             shift},
                                            {},
                                            IL::NoPayload,
                                            instruction.pc,
                                            static_cast<std::uint16_t>(instruction.op),
                                            true);
        const IL::ValueId bits = subop == IL::BinaryOp::AShr ? Bitcast(m_module.types.U64(), result) : result;
        const IL::ValueId nonzero = m_builder.CompareValue(IL::CompareOp::UNe, bits,
                                                           m_builder.ConstantU64(0), true);
        const auto scc = WriteScc(nonzero, instruction.pc);
        if (!scc)
            return scc;
        return WriteSgpr64(instruction.operands[0], bits, instruction.pc);
    }

    [[nodiscard]] IL::ValueId BuildBitMask64(IL::ValueId width)
    {
        const IL::ValueId one = m_builder.ConstantU64(1);
        const IL::ValueId width64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, width, true);
        IL::ValueId shifted = m_builder.Emit(m_module.types.U64(),
                                             IL::Op::Binary,
                                             static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                             {one, width64},
                                             {},
                                             IL::NoPayload,
                                             0,
                                             0,
                                             true);
        shifted = m_builder.Emit(m_module.types.U64(),
                                 IL::Op::Binary,
                                 static_cast<std::uint16_t>(IL::BinaryOp::ISub),
                                 {shifted, one},
                                 {},
                                 IL::NoPayload,
                                 0,
                                 0,
                                 true);
        return shifted;
    }

    [[nodiscard]] Result<void> LiftBfm64(const DecodedInstruction& instruction)
    {
        const auto offset = ReadBits(instruction.operands[1], instruction.pc);
        if (!offset)
            return std::unexpected(offset.error());
        const auto size = ReadBits(instruction.operands[2], instruction.pc);
        if (!size)
            return std::unexpected(size.error());
        const IL::ValueId mask = BuildBitMask64(*size);
        const IL::ValueId shift = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt,
                                                         MaskShiftCount(*offset, 63, true), true);
        const IL::ValueId result = m_builder.Emit(m_module.types.U64(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                  {mask, shift},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  true);
        return WriteSgpr64(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftBfe64(const DecodedInstruction& instruction, bool isSigned)
    {
        const auto value = ReadPairSource(instruction.operands[1], instruction.pc);
        if (!value)
            return std::unexpected(value.error());
        const auto control = ReadBits(instruction.operands[2], instruction.pc);
        if (!control)
            return std::unexpected(control.error());
        const IL::ValueId offset = MaskShiftCount(*control, 63, true);
        IL::ValueId width = m_builder.Emit(m_module.types.U32(),
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
        const IL::ValueId shift = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt,
                                                         offset, true);
        IL::ValueId extracted = m_builder.Emit(m_module.types.U64(),
                                               IL::Op::Binary,
                                               static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                               {*value, shift},
                                               {},
                                               IL::NoPayload,
                                               instruction.pc,
                                               static_cast<std::uint16_t>(instruction.op),
                                               true);
        extracted = m_builder.Emit(m_module.types.U64(),
                                   IL::Op::Binary,
                                   static_cast<std::uint16_t>(IL::BinaryOp::And),
                                   {extracted, BuildBitMask64(width)},
                                   {},
                                   IL::NoPayload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op),
                                   true);
        if (isSigned)
        {
            const IL::ValueId amount = m_builder.Emit(m_module.types.U32(),
                                                      IL::Op::Binary,
                                                      static_cast<std::uint16_t>(IL::BinaryOp::ISub),
                                                      {m_builder.ConstantU32(64), width},
                                                      {},
                                                      IL::NoPayload,
                                                      instruction.pc,
                                                      static_cast<std::uint16_t>(instruction.op),
                                                      true);
            const IL::ValueId shiftAmount =
                m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt,
                                       MaskShiftCount(amount, 63, true), true);
            IL::ValueId shifted = m_builder.Emit(m_module.types.U64(),
                                                 IL::Op::Binary,
                                                 static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                 {extracted, shiftAmount},
                                                 {},
                                                 IL::NoPayload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op),
                                                 true);
            shifted = m_builder.Emit(m_module.types.I64(),
                                     IL::Op::Binary,
                                     static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                     {Bitcast(m_module.types.I64(), shifted), shiftAmount},
                                     {},
                                     IL::NoPayload,
                                     instruction.pc,
                                     static_cast<std::uint16_t>(instruction.op),
                                     true);
            extracted = Bitcast(m_module.types.U64(), shifted);
        }
        const IL::ValueId nonzero = m_builder.CompareValue(IL::CompareOp::UNe, extracted,
                                                           m_builder.ConstantU64(0), true);
        const auto scc = WriteScc(nonzero, instruction.pc);
        if (!scc)
            return scc;
        return WriteSgpr64(instruction.operands[0], extracted, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMulHiScalar(const DecodedInstruction& instruction, bool isSigned)
    {
        const auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());
        const IL::ConvertOp extend = isSigned ? IL::ConvertOp::SExt : IL::ConvertOp::ZExt;
        const IL::TypeId wide = isSigned ? m_module.types.I64() : m_module.types.U64();
        IL::ValueId a64 = m_builder.ConvertValue(wide, extend, *left, true);
        IL::ValueId b64 = m_builder.ConvertValue(wide, extend, *right, true);
        const IL::ValueId product = m_builder.Emit(m_module.types.U64(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::IMul),
                                                   {a64, b64},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   true);
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
                                                   true);
        const IL::ValueId result = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc,
                                                          shifted, true);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] IL::ValueId Clz64Value(IL::ValueId value)
    {
        const IL::ValueId low = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, true);
        const IL::ValueId high64 = m_builder.Emit(m_module.types.U64(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                                  {value, m_builder.ConstantU64(32)},
                                                  {}, IL::NoPayload, 0, 0, true);
        const IL::ValueId high = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, true);
        const IL::ValueId highZero = m_builder.CompareValue(IL::CompareOp::UEq, high,
                                                            m_builder.ConstantU32(0), true);
        const IL::ValueId highCount = m_builder.UnaryValue(IL::UnaryOp::Clz, m_module.types.U32(), high, true);
        const IL::ValueId lowCount = m_builder.UnaryValue(IL::UnaryOp::Clz, m_module.types.U32(), low, true);
        const IL::ValueId extended = m_builder.Emit(m_module.types.U32(),
                                                    IL::Op::Binary,
                                                    static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                                    {m_builder.ConstantU32(32), lowCount},
                                                    {}, IL::NoPayload, 0, 0, true);
        return m_builder.SelectValue(m_module.types.U32(), highZero, extended, highCount, true);
    }

    [[nodiscard]] IL::ValueId Ctz64Value(IL::ValueId value)
    {
        const IL::ValueId low = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, true);
        const IL::ValueId high64 = m_builder.Emit(m_module.types.U64(),
                                                  IL::Op::Binary,
                                                  static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                                  {value, m_builder.ConstantU64(32)},
                                                  {}, IL::NoPayload, 0, 0, true);
        const IL::ValueId high = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, true);
        const IL::ValueId lowZero = m_builder.CompareValue(IL::CompareOp::UEq, low,
                                                           m_builder.ConstantU32(0), true);
        const IL::ValueId lowCount = m_builder.UnaryValue(IL::UnaryOp::Ctz, m_module.types.U32(), low, true);
        const IL::ValueId highCount = m_builder.UnaryValue(IL::UnaryOp::Ctz, m_module.types.U32(), high, true);
        const IL::ValueId extended = m_builder.Emit(m_module.types.U32(),
                                                    IL::Op::Binary,
                                                    static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                                    {m_builder.ConstantU32(32), highCount},
                                                    {}, IL::NoPayload, 0, 0, true);
        return m_builder.SelectValue(m_module.types.U32(), lowZero, extended, lowCount, true);
    }

    [[nodiscard]] Result<void> LiftBitScan64(const DecodedInstruction& instruction, bool leading, bool ones)
    {
        const auto value = ReadPairSource(instruction.operands[1], instruction.pc);
        if (!value)
            return std::unexpected(value.error());
        IL::ValueId input = *value;
        if (ones)
            input = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U64(), input, true);
        const IL::ValueId zero = m_builder.CompareValue(IL::CompareOp::UEq, input,
                                                        m_builder.ConstantU64(0), true);
        const IL::ValueId count = leading ? Clz64Value(input) : Ctz64Value(input);
        const IL::ValueId minusOne = m_builder.ConstantU32(0xFFFFFFFFu);
        const IL::ValueId result = m_builder.SelectValue(m_module.types.U32(), zero, minusOne, count, true);
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftCls64(const DecodedInstruction& instruction)
    {
        const auto value = ReadPairSource(instruction.operands[1], instruction.pc);
        if (!value)
            return std::unexpected(value.error());
        const IL::ValueId sign = m_builder.Emit(m_module.types.U64(),
                                                IL::Op::Binary,
                                                static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                                {Bitcast(m_module.types.I64(), *value),
                                                 m_builder.ConstantU32(63)},
                                                {}, IL::NoPayload, instruction.pc,
                                                static_cast<std::uint16_t>(instruction.op), true);
        const IL::ValueId different = m_builder.Emit(m_module.types.U64(),
                                                     IL::Op::Binary,
                                                     static_cast<std::uint16_t>(IL::BinaryOp::Xor),
                                                     {*value, Bitcast(m_module.types.U64(), sign)},
                                                     {}, IL::NoPayload, instruction.pc,
                                                     static_cast<std::uint16_t>(instruction.op), true);
        const IL::ValueId allSame = m_builder.CompareValue(IL::CompareOp::UEq, different,
                                                           m_builder.ConstantU64(0), true);
        const IL::ValueId count = Clz64Value(different);
        const IL::ValueId minusOne = m_builder.ConstantU32(0xFFFFFFFFu);
        const IL::ValueId result =
            m_builder.SelectValue(m_module.types.U32(), allSame, minusOne, count, true);
        return WriteDest(instruction.operands[0], result, instruction.pc);
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

    // MUBUF/MTBUF: a resource descriptor in SGPRs, a VGPR address and an SGPR
    // or inline offset. The descriptor is read from the user data slots, which
    // the host fills; the result is a buffer device address the emitter turns
    // into a physical pointer.
    // MTBUF: a typed buffer read or write. The format decides the conversion
    // the hardware applies, so only the formats whose element is a raw 32-bit
    // value are translated; anything else names its format and refuses.
    [[nodiscard]] Result<void> LiftMtbuf(const DecodedInstruction& instruction)
    {
        const bool isStore = instruction.op == Op::TbufferStoreFormatX || instruction.op == Op::TbufferStoreFormatXy ||
                             instruction.op == Op::TbufferStoreFormatXyz ||
                             instruction.op == Op::TbufferStoreFormatXyzw;

        const std::uint32_t components = instruction.op == Op::TbufferLoadFormatX ||
                                                 instruction.op == Op::TbufferStoreFormatX
                                             ? 1
                                             : instruction.op == Op::TbufferLoadFormatXy ||
                                                       instruction.op == Op::TbufferStoreFormatXy
                                                   ? 2
                                                   : instruction.op == Op::TbufferLoadFormatXyz ||
                                                             instruction.op == Op::TbufferStoreFormatXyz
                                                         ? 3
                                                         : 4;

        // The instruction carries (data, number); the measured table names the
        // layout, and a pair it does not name is refused rather than guessed.
        const std::uint32_t unified =
            UnifiedBufferFormat(instruction.bufferDataFormat, instruction.bufferNumberFormat);
        const auto format = BufferFormatOf(instruction.bufferDataFormat, instruction.bufferNumberFormat);
        if (!format)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-094",
                                             "typed buffer format " + std::to_string(unified) +
                                                 " (data " + std::to_string(instruction.bufferDataFormat) +
                                                 ", number " + std::to_string(instruction.bufferNumberFormat) +
                                                 ") names no layout in this backend",
                                             instruction.pc));
        }
        if (format->components < components)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-094",
                                             "typed buffer format " + std::to_string(unified) + " has " +
                                                 std::to_string(format->components) + " components, the "
                                                 "instruction reads " + std::to_string(components),
                                             instruction.pc));
        }
        // A store writes the components back in the format's own layout: only
        // the formats whose components are 32 bits wide need no conversion.
        if (isStore)
        {
            for (std::uint32_t i = 0; i < format->components; ++i)
            {
                if (format->bits[i] != 32u)
                {
                    return std::unexpected(LiftError("SR-GFX-LIFT-094",
                                                     "typed buffer store of format " +
                                                         std::to_string(unified) +
                                                         " is not converted yet",
                                                     instruction.pc));
                }
            }
        }

        if (instruction.operandCount < 3)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-095", "typed buffer access has too few operands", instruction.pc));

        const std::uint32_t srsrc = instruction.bufferSrsrc;
        if (static_cast<std::size_t>(srsrc) + 1 >= m_options.userData.size())
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-096",
                                             "typed buffer descriptor s" + std::to_string(srsrc) +
                                                 " is not in the user data",
                                             instruction.pc));
        }

        const auto low = ReadSgprBits(srsrc, instruction.pc);
        if (!low)
            return std::unexpected(low.error());
        const auto high = ReadSgprBits(srsrc + 1, instruction.pc);
        if (!high)
            return std::unexpected(high.error());

        const IL::ValueId low64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *low, true);
        const IL::ValueId highMasked = m_builder.Emit(m_module.types.U32(),
                                                      IL::Op::Binary,
                                                      static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                      {*high, m_builder.ConstantU32(0xFFFFu)},
                                                      {},
                                                      IL::NoPayload,
                                                      instruction.pc,
                                                      static_cast<std::uint16_t>(instruction.op),
                                                      true);
        const IL::ValueId high64 =
            m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, highMasked, true);
        const IL::ValueId highShifted = m_builder.Emit(m_module.types.U64(),
                                                       IL::Op::Binary,
                                                       static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                       {high64, m_builder.ConstantU64(32)},
                                                       {},
                                                       IL::NoPayload,
                                                       instruction.pc,
                                                       static_cast<std::uint16_t>(instruction.op),
                                                       true);
        const IL::ValueId base = m_builder.Emit(m_module.types.U64(),
                                                IL::Op::Binary,
                                                static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                                {low64, highShifted},
                                                {},
                                                IL::NoPayload,
                                                instruction.pc,
                                                static_cast<std::uint16_t>(instruction.op),
                                                true);

        const auto vaddr = ReadBits(instruction.operands[1], instruction.pc);
        if (!vaddr)
            return std::unexpected(vaddr.error());

        // The element offset is scaled by the descriptor stride; a byte-addressed
        // fixture sets stride 1, so the index is the byte offset.
        const auto soffset = ReadBits(instruction.operands[2], instruction.pc);
        if (!soffset)
            return std::unexpected(soffset.error());

        IL::ValueId address = m_builder.Emit(m_module.types.U64(),
                                             IL::Op::Binary,
                                             static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                             {base, m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt,
                                                                           *vaddr, false)},
                                             {},
                                             IL::NoPayload,
                                             instruction.pc,
                                             static_cast<std::uint16_t>(instruction.op),
                                             false);
        address = m_builder.Emit(m_module.types.U64(),
                                 IL::Op::Binary,
                                 static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                 {address,
                                  m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *soffset, true)},
                                 {},
                                 IL::NoPayload,
                                 instruction.pc,
                                 static_cast<std::uint16_t>(instruction.op),
                                 false);
        if (instruction.bufferOffset != 0)
        {
            address = m_builder.Emit(m_module.types.U64(),
                                     IL::Op::Binary,
                                     static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                     {address, m_builder.ConstantU64(instruction.bufferOffset)},
                                     {},
                                     IL::NoPayload,
                                     instruction.pc,
                                     static_cast<std::uint16_t>(instruction.op),
                                     false);
        }

        const Operand& data = instruction.operands[0];
        for (std::uint32_t component = 0; component < components; ++component)
        {
            const std::uint32_t bitOffset = format->bitOffsets[component];
            const std::uint32_t width = format->bits[component];
            const std::uint32_t dwordIndex = bitOffset / 32u;
            const std::uint32_t shift = bitOffset % 32u;

            IL::ValueId element = address;
            if (dwordIndex != 0)
            {
                element = m_builder.Emit(m_module.types.U64(),
                                         IL::Op::Binary,
                                         static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                         {address, m_builder.ConstantU64(4ull * dwordIndex)},
                                         {},
                                         IL::NoPayload,
                                         instruction.pc,
                                         static_cast<std::uint16_t>(instruction.op),
                                         false);
            }

            Operand slot = data;
            slot.value += component;
            if (isStore)
            {
                const auto value = ReadBits(slot, instruction.pc);
                if (!value)
                    return std::unexpected(value.error());
                const IL::ValueId guard = CurrentGuard(instruction.pc);
                m_builder.IntrinsicVoid(IL::IntrinsicId::DeviceStore, {element, *value}, IL::NoPayload,
                                        instruction.pc);
                ApplyGuard(instruction.pc, guard);
                continue;
            }

            const IL::ValueId raw = m_builder.IntrinsicValue(m_module.types.U32(),
                                                             IL::IntrinsicId::DeviceLoad,
                                                             {element},
                                                             IL::NoPayload,
                                                             instruction.pc,
                                                             false);
            IL::ValueId shifted = raw;
            if (shift != 0)
                shifted = Bin(instruction, IL::BinaryOp::LShr, raw, m_builder.ConstantU32(shift));
            IL::ValueId bits = shifted;
            if (width < 32u)
                bits = Bin(instruction, IL::BinaryOp::And, shifted, m_builder.ConstantU32((1u << width) - 1u));

            const auto converted = ConvertBufferComponent(instruction, *format, bits, width);
            if (!converted)
                return std::unexpected(converted.error());
            if (const auto written = WriteDest(slot, *converted, instruction.pc); !written)
                return written;
        }

        return {};
    }

    // One format component's bits become the value the destination register
    // holds: the integer itself for the integer classes, the f32 bit pattern
    // for the float classes and their normalised forms.
    [[nodiscard]] Result<IL::ValueId> ConvertBufferComponent(const DecodedInstruction& instruction,
                                                             const BufferFormatInfo& format,
                                                             IL::ValueId bits,
                                                             std::uint32_t width)
    {
        const IL::TypeId u32 = m_module.types.U32();
        const IL::TypeId f32 = m_module.types.F32();
        const std::uint32_t signShift = 32u - width;

        const auto signExtend = [&]() -> IL::ValueId {
            if (width >= 32u)
                return bits;
            const IL::ValueId lifted =
                Bin(instruction, IL::BinaryOp::Shl, bits, m_builder.ConstantU32(signShift));
            return Bin(instruction, IL::BinaryOp::AShr, lifted, m_builder.ConstantU32(signShift));
        };
        const auto asBits = [&](IL::ValueId value) -> IL::ValueId {
            return m_builder.Emit(u32,
                                  IL::Op::Bitcast,
                                  {},
                                  {value},
                                  {},
                                  IL::NoPayload,
                                  instruction.pc,
                                  static_cast<std::uint16_t>(instruction.op));
        };

        switch (format.numeric)
        {
        case BufferFormatClass::UInt:
            return bits;
        case BufferFormatClass::SInt:
            return signExtend();
        case BufferFormatClass::Float:
        {
            if (width == 32u)
                return bits;
            if (width != 16u)
                return std::unexpected(LiftError("SR-GFX-LIFT-094",
                                                 "float buffer component of " + std::to_string(width) +
                                                     " bits is not modelled",
                                                 instruction.pc));
            const IL::TypeId u16 = m_module.types.Int(16, false);
            const IL::TypeId f16 = m_module.types.Float(IL::FloatFormat::F16);
            const IL::ValueId narrowed = m_builder.ConvertValue(u16, IL::ConvertOp::Trunc, bits, false);
            const IL::ValueId half =
                m_builder.Emit(f16, IL::Op::Bitcast, {}, {narrowed}, {}, IL::NoPayload, instruction.pc,
                               static_cast<std::uint16_t>(instruction.op));
            return asBits(m_builder.ConvertValue(f32, IL::ConvertOp::FPExt, half, false));
        }
        case BufferFormatClass::UNorm:
        case BufferFormatClass::UScaled:
        case BufferFormatClass::SNorm:
        case BufferFormatClass::SScaled:
        {
            const bool signedValue = format.numeric == BufferFormatClass::SNorm ||
                                     format.numeric == BufferFormatClass::SScaled;
            const bool normalised = format.numeric == BufferFormatClass::UNorm ||
                                    format.numeric == BufferFormatClass::SNorm;
            IL::ValueId value = m_builder.ConvertValue(f32,
                                                       signedValue ? IL::ConvertOp::SIToFP
                                                                   : IL::ConvertOp::UIToFP,
                                                       signedValue ? signExtend() : bits,
                                                       false);
            if (normalised)
            {
                const float scale = signedValue
                                        ? 1.0f / static_cast<float>((1u << (width - 1u)) - 1u)
                                        : 1.0f / static_cast<float>((1u << width) - 1u);
                value = Bin(instruction, IL::BinaryOp::FMul, value, m_builder.ConstantF32(scale), f32);
                if (signedValue)
                {
                    value = Bin(instruction, IL::BinaryOp::FMax, value, m_builder.ConstantF32(-1.0f), f32);
                    value = Bin(instruction, IL::BinaryOp::FMin, value, m_builder.ConstantF32(1.0f), f32);
                }
            }
            return asBits(value);
        }
        }
        return std::unexpected(
            LiftError("SR-GFX-LIFT-094", "buffer component class is not modelled", instruction.pc));
    }

    // An image access: the T# in the scalar registers names the surface, the
    // VGPR pair holds the coordinates and DMASK selects the components. The
    // host binds the image the descriptor resolved to, so the translation
    // carries a descriptor-set binding rather than re-deriving the tiling.
    [[nodiscard]] Result<void> LiftImageAccess(const DecodedInstruction& instruction)
    {
        const auto atomicKind = [&instruction]() -> std::optional<IL::AtomicKind> {
            switch (instruction.op)
            {
            case Op::ImageAtomicSwap:
                return IL::AtomicKind::Exchange;
            case Op::ImageAtomicCmpswap:
                return IL::AtomicKind::CompareExchange;
            case Op::ImageAtomicAdd:
                return IL::AtomicKind::Add;
            case Op::ImageAtomicSub:
                return IL::AtomicKind::Sub;
            case Op::ImageAtomicSmin:
                return IL::AtomicKind::SMin;
            case Op::ImageAtomicUmin:
                return IL::AtomicKind::UMin;
            case Op::ImageAtomicSmax:
                return IL::AtomicKind::SMax;
            case Op::ImageAtomicUmax:
                return IL::AtomicKind::UMax;
            case Op::ImageAtomicAnd:
                return IL::AtomicKind::And;
            case Op::ImageAtomicOr:
                return IL::AtomicKind::Or;
            case Op::ImageAtomicXor:
                return IL::AtomicKind::Xor;
            case Op::ImageAtomicInc:
                return IL::AtomicKind::Increment;
            case Op::ImageAtomicDec:
                return IL::AtomicKind::Decrement;
            default:
                return std::nullopt;
            }
        }();

        if (instruction.op == Op::ImageLoadMip)
        {
            // A storage image read cannot name a level: the view selects it and
            // OpImageRead has no Lod operand. The binding model gives each image
            // one level, so the level is not something this path can honour.
            return std::unexpected(LiftError("SR-GFX-LIFT-069",
                                             "image_load_mip cannot select a level from a storage image "
                                             "binding; the view names the level",
                                             instruction.pc));
        }

        if (instruction.op != Op::ImageLoad &&
            instruction.op != Op::ImageStore && instruction.op != Op::ImageSample &&
            instruction.op != Op::ImageSampleL && instruction.op != Op::ImageSampleD &&
            instruction.op != Op::ImageGetResinfo &&
            instruction.op != Op::ImageGetLod && instruction.op != Op::ImageGather4 && !atomicKind)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-090",
                                             std::string("image access is not translated yet: ") +
                                                 std::string(ToString(instruction.op)) +
                                                 " T#=s" + std::to_string(instruction.imageSrsrc) +
                                                 " S#=s" + std::to_string(instruction.imageSsamp),
                                             instruction.pc));
        }

        const std::uint32_t srsrc = instruction.imageSrsrc;
        if (static_cast<std::size_t>(srsrc) + 1 >= m_options.userData.size())
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-091",
                                             "image descriptor s" + std::to_string(srsrc) +
                                                 " is not in the user data",
                                             instruction.pc));
        }

        // T#: 48-bit base address in dwords 0 and 1, extent in dword 2.
        const auto low = ReadSgprBits(srsrc, instruction.pc);
        if (!low)
            return std::unexpected(low.error());
        const auto high = ReadSgprBits(srsrc + 1, instruction.pc);
        if (!high)
            return std::unexpected(high.error());
        const auto extent = ReadSgprBits(srsrc + 2, instruction.pc);
        if (!extent)
            return std::unexpected(extent.error());

        // Bindings for the test ABI: storage images start at 1, sampled images
        // at 8 and samplers at 16, so a set can hold one of each kind.
        if (instruction.op == Op::ImageGetLod)
        {
            // The level is computed from derivatives, which a compute stage
            // may not use without a derivative group the target profile does
            // not enable.
            return std::unexpected(LiftError("SR-GFX-LIFT-067",
                                             "image_get_lod needs a derivative group, which the target "
                                             "profile does not enable",
                                             instruction.pc));
        }

        // resinfo and atomics use the surface directly; gather4 goes through
        // the sampler.
        const bool sampled = instruction.op == Op::ImageSample || instruction.op == Op::ImageSampleL ||
                             instruction.op == Op::ImageSampleD || instruction.op == Op::ImageGather4;
        std::uint32_t binding = 0;
        for (std::size_t i = 0; i < m_imageBindings.size(); ++i)
        {
            if (m_imageBindings[i].srsrc == srsrc && m_imageBindings[i].sampled == sampled)
                binding = m_imageBindings[i].binding;
        }
        if (binding == 0)
        {
            const std::uint32_t kindCount = static_cast<std::uint32_t>(std::count_if(
                m_imageBindings.begin(), m_imageBindings.end(),
                [sampled](const ImageBinding& entry) { return entry.sampled == sampled; }));
            binding = (sampled ? 8u : 1u) + kindCount;
            m_imageBindings.push_back(ImageBinding{srsrc, binding, sampled});
        }

        const std::uint32_t samplerBinding = sampled ? 16 + (binding - 8) : 0;

        // A null descriptor is a common way to say "no surface": treat the
        // access as reading zero rather than faulting.
        const IL::ValueId addressLow = *low;
        (void)addressLow;
        const IL::ValueId addressHigh = *high;
        (void)addressHigh;
        const IL::ValueId extentValue = *extent;
        (void)extentValue;

        const Operand* vaddrOperand = instruction.operandCount > 1 ? &instruction.operands[1] : nullptr;
        if (vaddrOperand == nullptr)
            return std::unexpected(LiftError("SR-GFX-LIFT-092", "image access has no address", instruction.pc));

        // 2D coordinates: x in the address VGPR, y in the next one.
        const Operand xOperand = *vaddrOperand;
        Operand yOperand = xOperand;
        ++yOperand.value;
        const auto x = ReadBits(xOperand, instruction.pc);
        if (!x)
            return std::unexpected(x.error());
        const auto y = ReadBits(yOperand, instruction.pc);
        if (!y)
            return std::unexpected(y.error());

        const Operand* dataOperand = instruction.operandCount > 0 ? &instruction.operands[0] : nullptr;
        if (dataOperand == nullptr)
            return std::unexpected(LiftError("SR-GFX-LIFT-093", "image access has no data register", instruction.pc));

        if (atomicKind)
        {
            // Image atomics are one operation on component zero. VDATA0 holds
            // the value, or the comparison value for a compare-exchange, with
            // the replacement in VDATA1.
            IL::ImageAccessDesc access;
            access.set = 0;
            access.binding = binding;
            access.samplerBinding = 0;
            access.component = 0;
            access.dimensions = 2;
            access.sampled = false;
            if (m_options.imageFormat == 0)
                return std::unexpected(LiftError("SR-GFX-LIFT-068",
                                                 "an image atomic needs the surface format "
                                                 "(--image-format r32ui); the descriptor format is "
                                                 "not decoded yet",
                                                 instruction.pc));
            access.format = m_options.imageFormat;
            access.atomic = true;
            access.atomicKind = *atomicKind;
            const IL::PayloadId payload = m_module.payloads.Add(access);

            std::vector<IL::ValueId> operands{*x, *y};
            const auto first = ReadBits(*dataOperand, instruction.pc);
            if (!first)
                return std::unexpected(first.error());
            operands.push_back(*first);
            if (*atomicKind == IL::AtomicKind::CompareExchange)
            {
                Operand replacement = *dataOperand;
                ++replacement.value;
                const auto second = ReadBits(replacement, instruction.pc);
                if (!second)
                    return std::unexpected(second.error());
                operands.push_back(*second);
            }

            const IL::ValueId value = m_builder.IntrinsicValue(m_module.types.U32(),
                                                               IL::IntrinsicId::ImageAtomic,
                                                               operands,
                                                               payload,
                                                               instruction.pc,
                                                               false);
            if (const auto written = WriteDest(*dataOperand, value, instruction.pc); !written)
                return written;
            return {};
        }

        // A gather4 writes four neighbouring texels regardless of the mask; the
        // mask names the channel that is gathered.
        std::uint32_t gatherChannel = 0;
        if (instruction.op == Op::ImageGather4)
        {
            std::uint32_t mask = instruction.imageDmask & 0xFu;
            while (mask != 0 && (mask & 1u) == 0)
            {
                mask >>= 1;
                ++gatherChannel;
            }
        }

        for (std::uint32_t component = 0; component < 4; ++component)
        {
            if (instruction.op != Op::ImageGather4 && (instruction.imageDmask & (1u << component)) == 0)
                continue;

            IL::ImageAccessDesc access;
            access.set = 0;
            access.binding = binding;
            access.samplerBinding = samplerBinding;
            access.component = static_cast<std::uint8_t>(component);
            access.dimensions = 2;
            access.sampled = sampled;
            access.channel = static_cast<std::uint8_t>(gatherChannel);
            const IL::PayloadId payload = m_module.payloads.Add(access);

            if (instruction.op == Op::ImageLoad ||
                instruction.op == Op::ImageSample || instruction.op == Op::ImageSampleL ||
                instruction.op == Op::ImageSampleD)
            {
                std::vector<IL::ValueId> coordinates{*x, *y};
                if (instruction.op == Op::ImageSampleD)
                {
                    // The gradient pair follows the coordinate: dx.xy then
                    // dy.xy in the address registers.
                    for (std::uint32_t index = 2; index < 6; ++index)
                    {
                        Operand component = *vaddrOperand;
                        component.value += index;
                        const auto value = ReadBits(component, instruction.pc);
                        if (!value)
                            return std::unexpected(value.error());
                        coordinates.push_back(*value);
                    }
                }
                if (instruction.op == Op::ImageSampleL)
                {
                    // The level is the third address dword.
                    Operand level = *vaddrOperand;
                    level.value += 2;
                    const auto levelValue = ReadBits(level, instruction.pc);
                    if (!levelValue)
                        return std::unexpected(levelValue.error());
                    coordinates.push_back(*levelValue);
                }
                const IL::ValueId value = m_builder.IntrinsicValue(
                    m_module.types.U32(),
                    sampled ? IL::IntrinsicId::ImageSample : IL::IntrinsicId::ImageLoad,
                                                                   coordinates,
                                                                   payload,
                                                                   instruction.pc,
                                                                   false);
                Operand destination = *dataOperand;
                destination.value += component;
                if (const auto written = WriteDest(destination, value, instruction.pc); !written)
                    return written;
            }
            else if (instruction.op == Op::ImageGetResinfo)
            {
                const IL::ValueId value = m_builder.IntrinsicValue(m_module.types.U32(),
                                                                   IL::IntrinsicId::ImageQuerySize,
                                                                   {},
                                                                   payload,
                                                                   instruction.pc,
                                                                   false);
                Operand destination = *dataOperand;
                destination.value += component;
                if (const auto written = WriteDest(destination, value, instruction.pc); !written)
                    return written;
            }
            else if (instruction.op == Op::ImageGetLod || instruction.op == Op::ImageGather4)
            {
                const IL::ValueId value = m_builder.IntrinsicValue(
                    m_module.types.U32(),
                    instruction.op == Op::ImageGetLod ? IL::IntrinsicId::ImageQueryLod
                                                      : IL::IntrinsicId::ImageGather,
                    {*x, *y},
                    payload,
                    instruction.pc,
                    false);
                Operand destination = *dataOperand;
                destination.value += component;
                if (const auto written = WriteDest(destination, value, instruction.pc); !written)
                    return written;
            }
        }

        if (instruction.op == Op::ImageStore)
        {
            // One write replaces the whole texel: all four data registers.
            std::vector<IL::ValueId> values;
            values.push_back(*x);
            values.push_back(*y);
            for (std::uint32_t component = 0; component < 4; ++component)
            {
                Operand source = *dataOperand;
                source.value += component;
                const auto value = ReadBits(source, instruction.pc);
                if (!value)
                    return std::unexpected(value.error());
                values.push_back(*value);
            }

            IL::ImageAccessDesc access;
            access.set = 0;
            access.binding = binding;
            access.samplerBinding = samplerBinding;
            access.component = 0;
            access.dimensions = 2;
            access.sampled = false;
            const IL::PayloadId payload = m_module.payloads.Add(access);

            const IL::ValueId guard = CurrentGuard(instruction.pc);
            m_builder.IntrinsicVoid(IL::IntrinsicId::ImageStore, values, payload, instruction.pc);
            ApplyGuard(instruction.pc, guard);
        }

        return {};
    }

    [[nodiscard]] Result<void> LiftMubuf(const DecodedInstruction& instruction)
    {
        if (instruction.operandCount < 3)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-080", "buffer instruction has too few operands", instruction.pc));

        // Which access shape the opcode names: a run of dwords, or one narrow
        // element read out of a dword.
        struct Shape
        {
            std::uint32_t dwords = 0;
            std::uint32_t elementBytes = 0; // 1 or 2 for a narrow load
            bool sign = false;
            bool store = false;
        };

        Shape shape;
        switch (instruction.op)
        {
        case Op::BufferLoadB32:
            shape.dwords = 1;
            break;
        case Op::BufferLoadB64:
            shape.dwords = 2;
            break;
        case Op::BufferLoadB96:
            shape.dwords = 3;
            break;
        case Op::BufferLoadB128:
            shape.dwords = 4;
            break;
        case Op::BufferLoadU8:
            shape.elementBytes = 1;
            break;
        case Op::BufferLoadI8:
            shape.elementBytes = 1;
            shape.sign = true;
            break;
        case Op::BufferLoadU16:
            shape.elementBytes = 2;
            break;
        case Op::BufferStoreB8:
            shape.elementBytes = 1;
            shape.store = true;
            break;
        case Op::BufferStoreB16:
            shape.elementBytes = 2;
            shape.store = true;
            break;
        case Op::BufferLoadI16:
            shape.elementBytes = 2;
            shape.sign = true;
            break;
        case Op::BufferStoreB32:
            shape.dwords = 1;
            shape.store = true;
            break;
        case Op::BufferStoreB64:
            shape.dwords = 2;
            shape.store = true;
            break;
        case Op::BufferStoreB96:
            shape.dwords = 3;
            shape.store = true;
            break;
        case Op::BufferStoreB128:
            shape.dwords = 4;
            shape.store = true;
            break;
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-081",
                                             std::string("buffer access is not modeled yet: ") +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        }

        const std::uint32_t srsrc = instruction.bufferSrsrc;
        if (static_cast<std::size_t>(srsrc) + 1 >= m_options.userData.size())
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-082",
                                             "buffer descriptor s" + std::to_string(srsrc) +
                                                 " is not in the user data",
                                             instruction.pc));
        }

        // Descriptor: 48-bit base address in dwords 0 and 1, stride in dword 1.
        const auto descriptor = [this](std::uint32_t index) -> Result<IL::ValueId> {
            return ReadSgprBits(index, 0);
        };

        const auto low = descriptor(srsrc);
        if (!low)
            return std::unexpected(low.error());
        const auto high = descriptor(srsrc + 1);
        if (!high)
            return std::unexpected(high.error());

        const IL::ValueId low64 = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *low, true);
        const IL::ValueId highMasked = m_builder.Emit(m_module.types.U32(),
                                                      IL::Op::Binary,
                                                      static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                      {*high, m_builder.ConstantU32(0xFFFFu)},
                                                      {},
                                                      IL::NoPayload,
                                                      instruction.pc,
                                                      static_cast<std::uint16_t>(instruction.op),
                                                      true);
        const IL::ValueId high64 =
            m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, highMasked, true);
        const IL::ValueId highShifted = m_builder.Emit(m_module.types.U64(),
                                                       IL::Op::Binary,
                                                       static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                       {high64, m_builder.ConstantU64(32)},
                                                       {},
                                                       IL::NoPayload,
                                                       instruction.pc,
                                                       static_cast<std::uint16_t>(instruction.op),
                                                       true);
        const IL::ValueId base = m_builder.Emit(m_module.types.U64(),
                                                IL::Op::Binary,
                                                static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                                {low64, highShifted},
                                                {},
                                                IL::NoPayload,
                                                instruction.pc,
                                                static_cast<std::uint16_t>(instruction.op),
                                                true);

        // Address = base + vaddr (+ soffset). A structured descriptor scales the
        // index by its stride; the fixtures use a byte-addressed buffer.
        const auto vaddr = ReadBits(instruction.operands[1], instruction.pc);
        if (!vaddr)
            return std::unexpected(vaddr.error());
        const IL::ValueId vaddr64 =
            m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *vaddr, false);

        const auto soffset = ReadBits(instruction.operands[2], instruction.pc);
        if (!soffset)
            return std::unexpected(soffset.error());
        const IL::ValueId soffset64 =
            m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, *soffset, true);

        IL::ValueId sum = m_builder.Emit(m_module.types.U64(),
                                         IL::Op::Binary,
                                         static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                         {base, vaddr64},
                                         {},
                                         IL::NoPayload,
                                         instruction.pc,
                                         static_cast<std::uint16_t>(instruction.op),
                                         false);
        sum = m_builder.Emit(m_module.types.U64(),
                             IL::Op::Binary,
                             static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                             {sum, soffset64},
                             {},
                             IL::NoPayload,
                             instruction.pc,
                             static_cast<std::uint16_t>(instruction.op),
                             false);
        if (instruction.bufferOffset != 0)
        {
            sum = m_builder.Emit(m_module.types.U64(),
                                 IL::Op::Binary,
                                 static_cast<std::uint16_t>(IL::BinaryOp::IAdd),
                                 {sum, m_builder.ConstantU64(instruction.bufferOffset)},
                                 {},
                                 IL::NoPayload,
                                 instruction.pc,
                                 static_cast<std::uint16_t>(instruction.op),
                                 false);
        }

        if (shape.store && shape.dwords != 0)
        {
            const auto first = ReadBits(instruction.operands[0], instruction.pc);
            if (!first)
                return std::unexpected(first.error());

            const IL::ValueId guard = CurrentGuard(instruction.pc);
            for (std::uint32_t dword = 0; dword < shape.dwords; ++dword)
            {
                IL::ValueId value = *first;
                if (dword != 0)
                {
                    Operand next = instruction.operands[0];
                    next.value += dword;
                    const auto component = ReadBits(next, instruction.pc);
                    if (!component)
                        return std::unexpected(component.error());
                    value = *component;
                }
                const IL::ValueId address =
                    dword == 0 ? sum
                               : Bin(instruction, IL::BinaryOp::IAdd, sum, m_builder.ConstantU64(4 * dword),
                                     m_module.types.U64());
                m_builder.IntrinsicVoid(IL::IntrinsicId::DeviceStore, {address, value}, IL::NoPayload,
                                        instruction.pc);
            }
            ApplyGuard(instruction.pc, guard);
            return {};
        }

        if (shape.elementBytes != 0 && shape.store)
        {
            // A narrow store merges the element into the dword that holds it.
            const IL::ValueId aligned = Bin(instruction,
                                            IL::BinaryOp::And,
                                            sum,
                                            m_builder.ConstantU64(~static_cast<std::uint64_t>(3u)),
                                            m_module.types.U64());
            const IL::ValueId offset = Bin(instruction,
                                           IL::BinaryOp::And,
                                           sum,
                                           m_builder.ConstantU64(3u),
                                           m_module.types.U64());
            const IL::ValueId offset32 =
                m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, offset, false);
            const IL::ValueId shift =
                Bin(instruction, IL::BinaryOp::Shl, offset32, m_builder.ConstantU32(3));
            const IL::ValueId elementMask =
                m_builder.ConstantU32(shape.elementBytes == 1 ? 0xFFu : 0xFFFFu);

            const auto source = ReadBits(instruction.operands[0], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId bits = Bin(instruction, IL::BinaryOp::And, *source, elementMask);
            const IL::ValueId shifted = Bin(instruction, IL::BinaryOp::Shl, bits, shift);
            const IL::ValueId keepMask = Bin(instruction, IL::BinaryOp::Shl, elementMask, shift);
            const IL::ValueId inverted =
                Bin(instruction, IL::BinaryOp::Xor, keepMask, m_builder.ConstantU32(0xFFFFFFFFu));
            const IL::ValueId old = m_builder.IntrinsicValue(m_module.types.U32(),
                                                             IL::IntrinsicId::DeviceLoad,
                                                             {aligned},
                                                             IL::NoPayload,
                                                             instruction.pc,
                                                             false);
            const IL::ValueId kept = Bin(instruction, IL::BinaryOp::And, old, inverted);
            const IL::ValueId merged = Bin(instruction, IL::BinaryOp::Or, kept, shifted);

            const IL::ValueId guard = CurrentGuard(instruction.pc);
            m_builder.IntrinsicVoid(IL::IntrinsicId::DeviceStore, {aligned, merged}, IL::NoPayload,
                                    instruction.pc);
            ApplyGuard(instruction.pc, guard);
            return {};
        }

        if (shape.elementBytes != 0)
        {
            // A narrow element lives inside one dword: load the aligned dword,
            // shift the element into place and extend it.
            const std::uint32_t alignMask = shape.elementBytes == 1 ? 3u : 3u;
            const IL::ValueId aligned = Bin(instruction,
                                            IL::BinaryOp::And,
                                            sum,
                                            m_builder.ConstantU64(~static_cast<std::uint64_t>(alignMask)),
                                            m_module.types.U64());
            const IL::ValueId dword = m_builder.IntrinsicValue(m_module.types.U32(),
                                                               IL::IntrinsicId::DeviceLoad,
                                                               {aligned},
                                                               IL::NoPayload,
                                                               instruction.pc,
                                                               false);
            const IL::ValueId offset = Bin(instruction,
                                           IL::BinaryOp::And,
                                           sum,
                                           m_builder.ConstantU64(alignMask),
                                           m_module.types.U64());
            const IL::ValueId offset32 =
                m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, offset, false);
            const IL::ValueId shift =
                Bin(instruction, IL::BinaryOp::Shl, offset32, m_builder.ConstantU32(3));
            IL::ValueId value = Bin(instruction, IL::BinaryOp::LShr, dword, shift);
            if (shape.elementBytes == 1)
            {
                value = Bin(instruction, IL::BinaryOp::And, value, m_builder.ConstantU32(0xFFu));
                if (shape.sign)
                    value = Bin(instruction, IL::BinaryOp::Shl, value, m_builder.ConstantU32(24));
            }
            else
            {
                value = Bin(instruction, IL::BinaryOp::And, value, m_builder.ConstantU32(0xFFFFu));
                if (shape.sign)
                    value = Bin(instruction, IL::BinaryOp::Shl, value, m_builder.ConstantU32(16));
            }
            // The element was shifted into the top of the dword above, so the
            // arithmetic shift that sign-extends it moves it back down.
            if (shape.sign)
                value = Bin(instruction,
                            IL::BinaryOp::AShr,
                            value,
                            m_builder.ConstantU32((4 - shape.elementBytes) * 8));
            return WriteDest(instruction.operands[0], value, instruction.pc);
        }

        for (std::uint32_t dword = 0; dword < shape.dwords; ++dword)
        {
            const IL::ValueId address =
                dword == 0 ? sum
                           : Bin(instruction, IL::BinaryOp::IAdd, sum, m_builder.ConstantU64(4 * dword),
                                 m_module.types.U64());
            const IL::ValueId result = m_builder.IntrinsicValue(m_module.types.U32(),
                                                                IL::IntrinsicId::DeviceLoad,
                                                                {address},
                                                                IL::NoPayload,
                                                                instruction.pc,
                                                                false);
            Operand destination = instruction.operands[0];
            destination.value += dword;
            if (const auto written = WriteDest(destination, result, instruction.pc); !written)
                return written;
        }
        return {};
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

    // Scalar memory: the base is an SGPR pair whose low word is the byte
    // address in the test ABI, plus the encoded immediate offset.
    [[nodiscard]] Result<IL::ValueId> ScalarByteAddress(const DecodedInstruction& instruction)
    {
        if (instruction.operandCount < 2)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-069", "scalar load has no base register", instruction.pc));
        const auto base = ReadBits(instruction.operands[1], instruction.pc);
        if (!base)
            return std::unexpected(base.error());
        if (instruction.operandCount < 3)
            return *base;
        const std::uint32_t immediate = instruction.operands[2].value;
        if (immediate == 0)
            return *base;
        return Bin(instruction, IL::BinaryOp::IAdd, *base, m_builder.ConstantU32(immediate));
    }

    [[nodiscard]] Result<void> LiftScalarMemory(const DecodedInstruction& instruction)
    {
        switch (instruction.op)
        {
        case Op::S_Gl1Inv:
        case Op::S_DcacheInv:
        case Op::S_AtcProbe:
        case Op::S_AtcProbeBuffer:
        case Op::S_PrefetchInst:
        case Op::S_PrefetchInstPcRel:
        case Op::S_PrefetchData:
        case Op::S_BufferPrefetchData:
        case Op::S_PrefetchDataPcRel:
            // Cache maintenance and prefetch leave the register file unchanged.
            return {};
        default:
            break;
        }

        std::uint32_t bytes = 0;
        bool singleByte = false;
        bool singleHalf = false;
        bool isSigned = false;
        switch (instruction.op)
        {
        case Op::S_LoadB32:
        case Op::S_BufferLoadB32:
            bytes = 4;
            break;
        case Op::S_LoadB64:
        case Op::S_BufferLoadB64:
            bytes = 8;
            break;
        case Op::S_LoadB96:
        case Op::S_BufferLoadB96:
            bytes = 12;
            break;
        case Op::S_LoadB128:
        case Op::S_BufferLoadB128:
            bytes = 16;
            break;
        case Op::S_LoadB256:
        case Op::S_BufferLoadB256:
            bytes = 32;
            break;
        case Op::S_LoadB512:
        case Op::S_BufferLoadB512:
            bytes = 64;
            break;
        case Op::S_LoadI8:
        case Op::S_BufferLoadI8:
            singleByte = true;
            isSigned = true;
            break;
        case Op::S_LoadU8:
        case Op::S_BufferLoadU8:
            singleByte = true;
            break;
        case Op::S_LoadI16:
        case Op::S_BufferLoadI16:
            singleHalf = true;
            isSigned = true;
            break;
        case Op::S_LoadU16:
        case Op::S_BufferLoadU16:
            singleHalf = true;
            break;
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-070",
                                             std::string("unsupported scalar memory instruction: ") +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        }

        const auto byteAddress = ScalarByteAddress(instruction);
        if (!byteAddress)
            return std::unexpected(byteAddress.error());
        const auto index = DsIndexFromBytes(instruction, *byteAddress);
        if (!index)
            return std::unexpected(index.error());

        const std::uint32_t destination = instruction.operands[0].value;

        if (singleByte || singleHalf)
        {
            const std::uint32_t fieldMask = singleByte ? 0xFFu : 0xFFFFu;
            const IL::ValueId shift = DsLaneShift(instruction, *byteAddress, singleByte ? 3u : 2u);
            const IL::ValueId field =
                Bin(instruction, IL::BinaryOp::And,
                    Bin(instruction, IL::BinaryOp::LShr, BufferDword(instruction, *index), shift),
                    m_builder.ConstantU32(fieldMask));
            IL::ValueId value = field;
            if (isSigned)
                value = singleByte ? SignExtend8Value(field) : SignExtend16Value(field);
            return WriteSgprIndex(instruction.operands[0].value, value, instruction.pc);
        }

        const std::uint32_t count = bytes / 4;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const IL::ValueId value = BufferDword(instruction, DsDwordAt(instruction, *index, i));
            const auto status = WriteSgprIndex(destination + i, value, instruction.pc);
            if (!status)
                return status;
        }
        return {};
    }

    // ---- Flat, global and scratch memory ----
    //
    // All three address spaces map onto the test ABI storage buffer: the
    // shader's byte address is divided by four to index dwords. The encoding
    // selects the address space, which decides how the base is formed; with no
    // descriptor model yet the base is the VGPR address plus the immediate
    // offset for every space.

#include "GfxMemoryTables.inc"

    [[nodiscard]] static const MemOpInfo* MemoryOpInfo(Op op)
    {
        for (const auto& entry : kMemoryOps)
        {
            if (entry.first == op)
                return &entry.second;
        }
        return nullptr;
    }

    [[nodiscard]] Result<IL::ValueId> MemoryByteAddress(const DecodedInstruction& instruction)
    {
        const Operand* address = FindOperand(instruction, OperandKind::GlobalVaddr);
        if (address == nullptr)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-062", "memory instruction has no address operand", instruction.pc));

        const auto base = ReadVgpr(address->value, instruction.pc);
        if (!base)
            return std::unexpected(base.error());

        IL::ValueId byteAddress = *base;
        if (instruction.bufferOffen)
        {
            const Operand* soffset = FindOperand(instruction, OperandKind::Sgpr);
            if (soffset != nullptr)
            {
                const auto value = ReadSgpr(soffset->value, instruction.pc);
                if (!value)
                    return std::unexpected(value.error());
                byteAddress = Bin(instruction, IL::BinaryOp::IAdd, byteAddress, *value);
            }
        }

        const Operand* offset = FindOperand(instruction, OperandKind::GlobalIoffset);
        if (offset == nullptr || offset->value == 0)
            return byteAddress;
        return Bin(instruction, IL::BinaryOp::IAdd, byteAddress, m_builder.ConstantU32(offset->value));
    }

    [[nodiscard]] IL::ValueId BufferDword(const DecodedInstruction& instruction, IL::ValueId index)
    {
        const IL::PayloadId payload = m_module.payloads.Add(IL::TestBufferDesc{0, 0});
        return m_builder.IntrinsicValue(m_module.types.U32(),
                                        IL::IntrinsicId::TestBufferLoad,
                                        {index},
                                        payload,
                                        instruction.pc,
                                        true);
    }

    void BufferPutDword(const DecodedInstruction& instruction, IL::ValueId index, IL::ValueId value)
    {
        const IL::PayloadId payload = m_module.payloads.Add(IL::TestBufferDesc{0, 0});
        const IL::ValueId guard = CurrentGuard(instruction.pc);
        m_builder.IntrinsicVoid(IL::IntrinsicId::TestBufferStore, {index, value}, payload, instruction.pc);
        ApplyGuard(instruction.pc, guard);
    }

    [[nodiscard]] std::uint32_t MemoryDestination(const DecodedInstruction& instruction) const
    {
        const Operand* destination = FindOperand(instruction, OperandKind::GlobalVdst);
        return destination == nullptr ? 0u : destination->value;
    }

    void MemoryWriteVgpr(const DecodedInstruction& instruction, std::uint32_t index, IL::ValueId value)
    {
        Operand destination;
        destination.kind = OperandKind::Vgpr;
        destination.value = index;
        (void)WriteDest(destination, value, instruction.pc);
    }

    void MemoryWritePair(const DecodedInstruction& instruction, std::uint32_t index, IL::ValueId value)
    {
        const IL::ValueId low =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, false);
        const IL::ValueId high64 =
            Bin(instruction, IL::BinaryOp::LShr, value, m_builder.ConstantU64(32), m_module.types.U64());
        const IL::ValueId high =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, false);
        MemoryWriteVgpr(instruction, index, low);
        MemoryWriteVgpr(instruction, index + 1, high);
    }

    [[nodiscard]] IL::ValueId BufferQword(const DecodedInstruction& instruction, IL::ValueId index)
    {
        const IL::ValueId low = BufferDword(instruction, index);
        const IL::ValueId high = BufferDword(instruction, DsDwordAt(instruction, index, 1));
        const IL::ValueId high64 =
            m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, high, false);
        const IL::ValueId shifted =
            Bin(instruction, IL::BinaryOp::Shl, high64, m_builder.ConstantU64(32), m_module.types.U64());
        const IL::ValueId low64 =
            m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, low, false);
        return Bin(instruction, IL::BinaryOp::Or, low64, shifted, m_module.types.U64());
    }

    void BufferPutQword(const DecodedInstruction& instruction, IL::ValueId index, IL::ValueId value)
    {
        const IL::ValueId low =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, false);
        const IL::ValueId high64 =
            Bin(instruction, IL::BinaryOp::LShr, value, m_builder.ConstantU64(32), m_module.types.U64());
        const IL::ValueId high =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, false);
        BufferPutDword(instruction, index, low);
        BufferPutDword(instruction, DsDwordAt(instruction, index, 1), high);
    }

    [[nodiscard]] Result<void> LiftMemoryLoad(const DecodedInstruction& instruction,
                                              const MemOpInfo& info,
                                              IL::ValueId index,
                                              IL::ValueId byteAddress)
    {
        const std::uint32_t destination = MemoryDestination(instruction);

        const bool singleByte = info.bytes == 1;
        const bool singleHalf = info.bytes == 2;
        if (singleByte || singleHalf)
        {
            const std::uint32_t fieldMask = singleByte ? 0xFFu : 0xFFFFu;
            const IL::ValueId shift = DsLaneShift(instruction, byteAddress, singleByte ? 3u : 2u);
            const IL::ValueId field =
                Bin(instruction, IL::BinaryOp::And,
                    Bin(instruction, IL::BinaryOp::LShr, BufferDword(instruction, index), shift),
                    m_builder.ConstantU32(fieldMask));

            const bool signedValue = info.kind == MemKind::LoadI8 || info.kind == MemKind::LoadI16 ||
                                     info.kind == MemKind::LoadD16I8 || info.kind == MemKind::LoadD16HiI8;
            IL::ValueId value = field;
            if (signedValue)
                value = singleByte ? SignExtend8Value(field) : SignExtend16Value(field);

            const bool lowHalf = info.kind == MemKind::LoadD16U8 || info.kind == MemKind::LoadD16I8 ||
                                 info.kind == MemKind::LoadD16B16;
            const bool highHalf = info.kind == MemKind::LoadD16HiU8 || info.kind == MemKind::LoadD16HiI8 ||
                                  info.kind == MemKind::LoadD16HiB16;
            if (lowHalf || highHalf)
            {
                const auto current = ReadVgpr(destination, instruction.pc);
                if (!current)
                    return std::unexpected(current.error());
                const IL::ValueId merged =
                    highHalf
                        ? Bin(instruction, IL::BinaryOp::Or,
                              Bin(instruction, IL::BinaryOp::And, *current,
                                  m_builder.ConstantU32(0xFFFF)),
                              Bin(instruction, IL::BinaryOp::Shl,
                                  Bin(instruction, IL::BinaryOp::And, value,
                                      m_builder.ConstantU32(fieldMask)),
                                  m_builder.ConstantU32(16)))
                        : Bin(instruction, IL::BinaryOp::Or,
                              Bin(instruction, IL::BinaryOp::And, *current,
                                  m_builder.ConstantU32(0xFFFF0000)),
                              Bin(instruction, IL::BinaryOp::And, value,
                                  m_builder.ConstantU32(fieldMask)));
                MemoryWriteVgpr(instruction, destination, merged);
                return {};
            }

            MemoryWriteVgpr(instruction, destination, value);
            return {};
        }

        const std::uint32_t count = info.bytes / 4;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            MemoryWriteVgpr(instruction, destination + i,
                            BufferDword(instruction, DsDwordAt(instruction, index, i)));
        }
        return {};
    }

    [[nodiscard]] Result<void> LiftMemoryStore(const DecodedInstruction& instruction,
                                               const MemOpInfo& info,
                                               IL::ValueId index,
                                               IL::ValueId byteAddress)
    {
        const Operand* source = FindOperand(instruction, OperandKind::GlobalVsrc);
        if (source == nullptr)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-063", "memory store has no data operand", instruction.pc));

        const bool singleByte = info.bytes == 1;
        const bool singleHalf = info.bytes == 2;
        if (singleByte || singleHalf)
        {
            const std::uint32_t fieldMask = singleByte ? 0xFFu : 0xFFFFu;
            const IL::ValueId shift = DsLaneShift(instruction, byteAddress, singleByte ? 3u : 2u);
            const auto data = ReadVgpr(source->value, instruction.pc);
            if (!data)
                return std::unexpected(data.error());

            const bool highHalf = info.kind == MemKind::StoreD16HiB8 ||
                                  info.kind == MemKind::StoreD16HiB16;
            IL::ValueId value = *data;
            if (highHalf)
                value = Bin(instruction, IL::BinaryOp::LShr, value, m_builder.ConstantU32(16));
            const IL::ValueId field =
                Bin(instruction, IL::BinaryOp::Shl,
                    Bin(instruction, IL::BinaryOp::And, value, m_builder.ConstantU32(fieldMask)), shift);
            const IL::ValueId mask =
                Bin(instruction, IL::BinaryOp::Shl, m_builder.ConstantU32(fieldMask), shift);
            const IL::ValueId old = BufferDword(instruction, index);
            const IL::ValueId cleared =
                Bin(instruction, IL::BinaryOp::And, old,
                    m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U32(), mask, false));
            BufferPutDword(instruction, index, Bin(instruction, IL::BinaryOp::Or, cleared, field));
            return {};
        }

        const std::uint32_t count = info.bytes / 4;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto value = ReadVgpr(source->value + i, instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            BufferPutDword(instruction, DsDwordAt(instruction, index, i), *value);
        }
        return {};
    }

    [[nodiscard]] Result<void> LiftMemoryAtomic(const DecodedInstruction& instruction,
                                                const MemOpInfo& info,
                                                IL::ValueId index)
    {
        const std::uint32_t destination = MemoryDestination(instruction);
        const Operand* source = FindOperand(instruction, OperandKind::GlobalVsrc);
        if (source == nullptr)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-064", "memory atomic has no data operand", instruction.pc));

        const MemKind kind = info.kind;
        const bool wide = info.bytes == 8;
        const IL::TypeId uintType = wide ? m_module.types.U64() : m_module.types.U32();
        const IL::TypeId floatType = wide ? F64Type() : m_module.types.F32();

        // cmpswap keeps the comparison value in the low half of the source
        // pair and the replacement value in the high half; the destination
        // receives the value that was in memory.
        const bool compareSwap = kind == MemKind::AtomicCmpswapB32 ||
                                 kind == MemKind::AtomicCmpswapB64;

        const auto first = wide ? ReadVgpr64(source->value, instruction.pc)
                                : ReadVgpr(source->value, instruction.pc);
        if (!first)
            return std::unexpected(first.error());

        IL::ValueId replacement = IL::InvalidValue;
        if (compareSwap)
        {
            const std::uint32_t base = source->value + (wide ? 2u : 1u);
            const auto value = wide ? ReadVgpr64(base, instruction.pc) : ReadVgpr(base, instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            replacement = *value;
        }

        const IL::ValueId old = wide ? BufferQword(instruction, index) : BufferDword(instruction, index);
        const IL::ValueId zero = wide ? m_builder.ConstantU64(0) : m_builder.ConstantU32(0);
        const IL::ValueId one = wide ? m_builder.ConstantU64(1) : m_builder.ConstantU32(1);

        IL::ValueId result = old;
        switch (kind)
        {
        case MemKind::AtomicSwapB32:
        case MemKind::AtomicSwapB64:
            result = *first;
            break;
        case MemKind::AtomicCmpswapB32:
        case MemKind::AtomicCmpswapB64:
        {
            const IL::ValueId equal = m_builder.CompareValue(IL::CompareOp::UEq, old, *first, false);
            result = m_builder.SelectValue(uintType, equal, replacement, old, false);
            break;
        }
        case MemKind::AtomicAddU32:
        case MemKind::AtomicAddU64:
        case MemKind::AtomicOrderedAddB64:
            result = Bin(instruction, IL::BinaryOp::IAdd, old, *first, uintType);
            break;
        case MemKind::AtomicSubU32:
        case MemKind::AtomicSubU64:
            result = Bin(instruction, IL::BinaryOp::ISub, old, *first, uintType);
            break;
        case MemKind::AtomicSubClampU32:
        {
            const IL::ValueId large = m_builder.CompareValue(IL::CompareOp::UGe, old, *first, false);
            result = m_builder.SelectValue(uintType, large, zero,
                                           Bin(instruction, IL::BinaryOp::ISub, *first, old, uintType),
                                           false);
            break;
        }
        case MemKind::AtomicMinI32:
        case MemKind::AtomicMinI64:
            result = m_builder.SelectValue(uintType,
                                           m_builder.CompareValue(IL::CompareOp::ILt, old, *first, false),
                                           old, *first, false);
            break;
        case MemKind::AtomicMaxI32:
        case MemKind::AtomicMaxI64:
            result = m_builder.SelectValue(uintType,
                                           m_builder.CompareValue(IL::CompareOp::IGt, old, *first, false),
                                           old, *first, false);
            break;
        case MemKind::AtomicMinU32:
        case MemKind::AtomicMinU64:
            result = m_builder.SelectValue(uintType,
                                           m_builder.CompareValue(IL::CompareOp::ULt, old, *first, false),
                                           old, *first, false);
            break;
        case MemKind::AtomicMaxU32:
        case MemKind::AtomicMaxU64:
            result = m_builder.SelectValue(uintType,
                                           m_builder.CompareValue(IL::CompareOp::UGt, old, *first, false),
                                           old, *first, false);
            break;
        case MemKind::AtomicAndB32:
        case MemKind::AtomicAndB64:
            result = Bin(instruction, IL::BinaryOp::And, old, *first, uintType);
            break;
        case MemKind::AtomicOrB32:
        case MemKind::AtomicOrB64:
            result = Bin(instruction, IL::BinaryOp::Or, old, *first, uintType);
            break;
        case MemKind::AtomicXorB32:
        case MemKind::AtomicXorB64:
            result = Bin(instruction, IL::BinaryOp::Xor, old, *first, uintType);
            break;
        case MemKind::AtomicIncU32:
        case MemKind::AtomicIncU64:
        {
            const IL::ValueId exceeded = m_builder.CompareValue(IL::CompareOp::UGe, old, *first, false);
            result = m_builder.SelectValue(uintType, exceeded, zero,
                                           Bin(instruction, IL::BinaryOp::IAdd, old, one, uintType),
                                           false);
            break;
        }
        case MemKind::AtomicDecU32:
        case MemKind::AtomicDecU64:
        {
            const IL::ValueId atZero = m_builder.CompareValue(IL::CompareOp::UEq, old, zero, false);
            result = m_builder.SelectValue(uintType, atZero, *first,
                                           Bin(instruction, IL::BinaryOp::ISub, old, one, uintType),
                                           false);
            break;
        }
        case MemKind::AtomicCondSubU32:
        {
            const IL::ValueId large = m_builder.CompareValue(IL::CompareOp::UGe, old, *first, false);
            result = m_builder.SelectValue(uintType, large,
                                           Bin(instruction, IL::BinaryOp::ISub, old, *first, uintType),
                                           old, false);
            break;
        }
        case MemKind::AtomicMinNumF32:
            result = Bin(instruction, IL::BinaryOp::FMin, old, *first, floatType);
            break;
        case MemKind::AtomicMaxNumF32:
            result = Bin(instruction, IL::BinaryOp::FMax, old, *first, floatType);
            break;
        case MemKind::AtomicAddF32:
            result = Bin(instruction, IL::BinaryOp::FAdd, old, *first, floatType);
            break;
        default:
            return std::unexpected(
                LiftError("SR-GFX-LIFT-066", "unsupported memory atomic kind", instruction.pc));
        }

        if (wide)
        {
            BufferPutQword(instruction, index, result);
            MemoryWritePair(instruction, destination, old);
        }
        else
        {
            BufferPutDword(instruction, index, result);
            MemoryWriteVgpr(instruction, destination, old);
        }
        return {};
    }

    [[nodiscard]] Result<void> LiftFlatMemory(const DecodedInstruction& instruction)
    {
        const MemOpInfo* info = MemoryOpInfo(instruction.op);
        if (info == nullptr)
        {
            return std::unexpected(LiftError("SR-GFX-LIFT-067",
                                             std::string("unsupported memory instruction: ") +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        }

        switch (info->kind)
        {
        case MemKind::Inv:
        case MemKind::Wb:
        case MemKind::Wbinv:
            // Cache maintenance does not change memory contents.
            return {};
        case MemKind::LoadAddtidB32:
        case MemKind::StoreAddtidB32:
        case MemKind::LoadBlock:
        case MemKind::StoreBlock:
        case MemKind::LoadTrB64:
        case MemKind::LoadTrB128:
        case MemKind::AtomicPkAddF16:
        case MemKind::AtomicPkAddBf16:
            return std::unexpected(LiftError("SR-GFX-LIFT-068",
                                             std::string("unsupported memory instruction: ") +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        default:
            break;
        }

        const auto byteAddress = MemoryByteAddress(instruction);
        if (!byteAddress)
            return std::unexpected(byteAddress.error());
        const auto index = DsIndexFromBytes(instruction, *byteAddress);
        if (!index)
            return std::unexpected(index.error());

        switch (info->kind)
        {
        case MemKind::LoadU8:
        case MemKind::LoadI8:
        case MemKind::LoadU16:
        case MemKind::LoadI16:
        case MemKind::LoadB32:
        case MemKind::LoadB64:
        case MemKind::LoadB96:
        case MemKind::LoadB128:
        case MemKind::LoadD16U8:
        case MemKind::LoadD16I8:
        case MemKind::LoadD16B16:
        case MemKind::LoadD16HiU8:
        case MemKind::LoadD16HiI8:
        case MemKind::LoadD16HiB16:
            return LiftMemoryLoad(instruction, *info, *index, *byteAddress);
        case MemKind::StoreB8:
        case MemKind::StoreB16:
        case MemKind::StoreB32:
        case MemKind::StoreB64:
        case MemKind::StoreB96:
        case MemKind::StoreB128:
        case MemKind::StoreD16HiB8:
        case MemKind::StoreD16HiB16:
            return LiftMemoryStore(instruction, *info, *index, *byteAddress);
        default:
            return LiftMemoryAtomic(instruction, *info, *index);
        }
    }

    // ---- Data-share (LDS) ----
    //
    // LDS addresses are byte offsets formed from a VGPR plus an immediate. The
    // IL addresses workgroup-scope memory by dword index, so byte accesses split
    // into a dword index and a byte lane. A single invocation is modelled, so
    // read-modify-write atomics need no cross-lane arbitration.

    [[nodiscard]] Operand DsOperand(const DecodedInstruction& instruction, OperandKind kind) const
    {
        if (const Operand* found = FindOperand(instruction, kind))
            return *found;
        return Operand{};
    }

    [[nodiscard]] IL::ValueId Bin(const DecodedInstruction& instruction,
                                  IL::BinaryOp op,
                                  IL::ValueId left,
                                  IL::ValueId right,
                                  IL::TypeId type = IL::InvalidType)
    {
        return m_builder.Emit(type == IL::InvalidType ? m_module.types.U32() : type,
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(op),
                              {left, right},
                              {},
                              IL::NoPayload,
                              instruction.pc,
                              static_cast<std::uint16_t>(instruction.op),
                              false);
    }

    [[nodiscard]] Result<IL::ValueId> DsByteAddress(const DecodedInstruction& instruction,
                                                    std::uint32_t immediate)
    {
        const Operand address = DsOperand(instruction, OperandKind::DsAddr);
        if (address.kind == OperandKind::None)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-057", "DS instruction has no address operand", instruction.pc));

        const auto base = ReadVgpr(address.value, instruction.pc);
        if (!base)
            return std::unexpected(base.error());
        if (immediate == 0)
            return *base;
        return Bin(instruction, IL::BinaryOp::IAdd, *base, m_builder.ConstantU32(immediate));
    }

    [[nodiscard]] Result<IL::ValueId> DsIndexFromBytes(const DecodedInstruction& instruction,
                                                       IL::ValueId bytes)
    {
        return Bin(instruction, IL::BinaryOp::LShr, bytes, m_builder.ConstantU32(2));
    }

    [[nodiscard]] Result<IL::ValueId> DsIndex(const DecodedInstruction& instruction,
                                              std::uint32_t immediate)
    {
        const auto bytes = DsByteAddress(instruction, immediate);
        if (!bytes)
            return std::unexpected(bytes.error());
        return DsIndexFromBytes(instruction, *bytes);
    }

    [[nodiscard]] IL::ValueId DsLaneShift(const DecodedInstruction& instruction, IL::ValueId bytes,
                                          std::uint32_t mask)
    {
        const IL::ValueId lane = Bin(instruction, IL::BinaryOp::And, bytes, m_builder.ConstantU32(mask));
        return Bin(instruction, IL::BinaryOp::Shl, lane, m_builder.ConstantU32(3));
    }

    [[nodiscard]] IL::ValueId DsDword(const DecodedInstruction& instruction, IL::ValueId index)
    {
        return m_builder.IntrinsicValue(m_module.types.U32(),
                                        IL::IntrinsicId::LocalLoad,
                                        {index},
                                        IL::NoPayload,
                                        instruction.pc,
                                        false);
    }

    void DsPutDword(const DecodedInstruction& instruction, IL::ValueId index, IL::ValueId value)
    {
        const IL::ValueId guard = CurrentGuard(instruction.pc);
        m_builder.IntrinsicVoid(IL::IntrinsicId::LocalStore, {index, value}, IL::NoPayload, instruction.pc);
        ApplyGuard(instruction.pc, guard);
    }

    [[nodiscard]] IL::ValueId DsDwordAt(const DecodedInstruction& instruction,
                                        IL::ValueId index,
                                        std::uint32_t delta)
    {
        if (delta == 0)
            return index;
        return Bin(instruction, IL::BinaryOp::IAdd, index, m_builder.ConstantU32(delta));
    }

    [[nodiscard]] IL::ValueId DsLoad64(const DecodedInstruction& instruction, IL::ValueId index)
    {
        const IL::ValueId low = DsDword(instruction, index);
        const IL::ValueId high = DsDword(instruction, DsDwordAt(instruction, index, 1));
        const IL::ValueId high64 =
            m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, high, false);
        const IL::ValueId shifted =
            Bin(instruction, IL::BinaryOp::Shl, high64, m_builder.ConstantU64(32), m_module.types.U64());
        const IL::ValueId low64 =
            m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, low, false);
        return Bin(instruction, IL::BinaryOp::Or, low64, shifted, m_module.types.U64());
    }

    void DsStore64(const DecodedInstruction& instruction, IL::ValueId index, IL::ValueId value)
    {
        const IL::ValueId low =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, false);
        const IL::ValueId high64 =
            Bin(instruction, IL::BinaryOp::LShr, value, m_builder.ConstantU64(32), m_module.types.U64());
        const IL::ValueId high =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, false);
        DsPutDword(instruction, index, low);
        DsPutDword(instruction, DsDwordAt(instruction, index, 1), high);
    }

    void DsWriteVgpr(const DecodedInstruction& instruction, std::uint32_t index, IL::ValueId value)
    {
        Operand destination;
        destination.kind = OperandKind::Vgpr;
        destination.value = index;
        (void)WriteDest(destination, value, instruction.pc);
    }

    void DsWriteVgprPair(const DecodedInstruction& instruction, std::uint32_t index, IL::ValueId value)
    {
        const IL::ValueId low =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, value, false);
        const IL::ValueId high64 =
            Bin(instruction, IL::BinaryOp::LShr, value, m_builder.ConstantU64(32), m_module.types.U64());
        const IL::ValueId high =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, false);
        DsWriteVgpr(instruction, index, low);
        DsWriteVgpr(instruction, index + 1, high);
    }

    [[nodiscard]] Result<void> DsWriteDest(const DecodedInstruction& instruction, IL::ValueId value)
    {
        const Operand destination = DsOperand(instruction, OperandKind::DsDest);
        if (destination.kind == OperandKind::None)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-058", "DS instruction has no destination", instruction.pc));
        DsWriteVgpr(instruction, destination.value, value);
        return {};
    }

    [[nodiscard]] Result<IL::ValueId> DsData(const DecodedInstruction& instruction,
                                             OperandKind kind,
                                             bool wide)
    {
        const Operand operand = DsOperand(instruction, kind);
        if (operand.kind == OperandKind::None)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-059", "DS instruction is missing a data operand", instruction.pc));
        if (wide)
            return ReadVgpr64(operand.value, instruction.pc);
        return ReadVgpr(operand.value, instruction.pc);
    }

    [[nodiscard]] Result<IL::ValueId> DsLoadValue(const DecodedInstruction& instruction,
                                                  std::uint32_t immediate,
                                                  bool wide)
    {
        const auto index = DsIndex(instruction, immediate);
        if (!index)
            return std::unexpected(index.error());
        return wide ? DsLoad64(instruction, *index) : DsDword(instruction, *index);
    }

    [[nodiscard]] Result<void> DsStoreValue(const DecodedInstruction& instruction,
                                            std::uint32_t immediate,
                                            IL::ValueId value,
                                            bool wide)
    {
        const auto index = DsIndex(instruction, immediate);
        if (!index)
            return std::unexpected(index.error());
        if (wide)
            DsStore64(instruction, *index, value);
        else
            DsPutDword(instruction, *index, value);
        return {};
    }

    // A geometry input vertex is an array element: the ring address names the
    // vertex and the immediate names one dword of its exported outputs. The
    // order is position first, then one four-component location per output
    // register, so dword d is register d / 4 and component d % 4.
    [[nodiscard]] Result<void> LiftGeometryDsRead(const DecodedInstruction& instruction,
                                                 std::uint32_t firstDword,
                                                 std::uint32_t dwords)
    {
        const Operand address = DsOperand(instruction, OperandKind::DsAddr);
        if (address.kind == OperandKind::None)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-057", "DS instruction has no address operand", instruction.pc));

        const auto base = ReadVgpr(address.value, instruction.pc);
        if (!base)
            return std::unexpected(base.error());

        // The ring address is vertex * 256 bytes.
        const IL::ValueId vertex =
            Bin(instruction, IL::BinaryOp::LShr, *base, m_builder.ConstantU32(8));
        const IL::TypeId f32 = m_module.types.F32();
        const IL::TypeId vec4 = m_module.types.Vector(f32, 4);
        const Operand destination = DsOperand(instruction, OperandKind::DsDest);
        if (destination.kind == OperandKind::None)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-058", "DS instruction has no destination", instruction.pc));

        for (std::uint32_t i = 0; i < dwords; ++i)
        {
            const std::uint32_t dword = (firstDword + i) % 64;
            const std::uint32_t reg = dword / 4;
            const std::uint32_t component = dword % 4;
            const IL::Builtin builtin = reg == 0 ? IL::Builtin::Position : IL::Builtin::None;
            const std::uint32_t location = reg == 0 ? 0 : reg - 1;

            (void)EnsureSignature(IL::Space::Input, builtin, location, vec4);
            const IL::PayloadId payload =
                StageIoPayload(IL::Space::Input, builtin, location, component, IL::Interpolation::None);
            const IL::ValueId value =
                m_builder.Emit(f32,
                               IL::Op::Intrinsic,
                               static_cast<std::uint16_t>(IL::IntrinsicId::StageInput),
                               {vertex},
                               {},
                               payload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op));
            const IL::ValueId bits =
                m_builder.Emit(m_module.types.U32(),
                               IL::Op::Bitcast,
                               {},
                               {value},
                               {},
                               IL::NoPayload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op));
            DsWriteVgpr(instruction, destination.value + i, bits);
        }

        return {};
    }

    // Publishes the staged vertex `emitted`: copy each staged register to the
    // output variables, emit, and move to the next slot.
    [[nodiscard]] Result<void> EmitStagedGeometryVertex(const DecodedInstruction& instruction,
                                                        bool endPrimitive)
    {
        const std::uint32_t registers = m_options.geometryOutputRegisters;
        if (registers == 0)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-065",
                          "a staged geometry emit needs the output register count (--gs-output-registers)",
                          instruction.pc));

        const IL::TypeId f32 = m_module.types.F32();
        const IL::TypeId vec4 = m_module.types.Vector(f32, 4);
        const IL::ValueId emitted = m_builder.Load(m_module.types.U32(), GeometryEmittedSlot(), instruction.pc);

        for (std::uint32_t reg = 0; reg < registers; ++reg)
        {
            const IL::Builtin builtin = reg == 0 ? IL::Builtin::Position : IL::Builtin::None;
            const std::uint32_t location = reg == 0 ? 0 : reg - 1;
            (void)EnsureSignature(IL::Space::Output, builtin, location, vec4);

            for (std::uint32_t component = 0; component < 4; ++component)
            {
                const IL::ValueId index = StagedIndex(instruction, emitted, reg, component, registers);
                const IL::ValueId value =
                    m_builder.IntrinsicValue(f32,
                                             IL::IntrinsicId::GeometryStageLoad,
                                             {index},
                                             IL::NoPayload,
                                             instruction.pc,
                                             false);
                const IL::PayloadId payload = StageIoPayload(IL::Space::Output,
                                                             builtin,
                                                             location,
                                                             component,
                                                             IL::Interpolation::None);
                m_builder.EmitVoid(IL::Op::Intrinsic,
                                   static_cast<std::uint16_t>(IL::IntrinsicId::StageOutput),
                                   {value},
                                   {},
                                   payload,
                                   instruction.pc,
                                   static_cast<std::uint16_t>(instruction.op));
            }
        }

        m_primitiveOpen = true;
        m_builder.EmitVoid(IL::Op::Intrinsic,
                           static_cast<std::uint16_t>(IL::IntrinsicId::EmitVertex),
                           {},
                           {},
                           IL::NoPayload,
                           instruction.pc,
                           static_cast<std::uint16_t>(instruction.op));
        if (endPrimitive)
        {
            const auto primitive = EndPrimitive(instruction.pc, static_cast<std::uint16_t>(instruction.op));
            if (!primitive)
                return std::unexpected(primitive.error());
        }

        m_builder.Store(GeometryEmittedSlot(),
                        Bin(instruction, IL::BinaryOp::IAdd, emitted, m_builder.ConstantU32(1)),
                        instruction.pc);
        return {};
    }

    // One staged component: emitted slot * one vertex + register * 4 + component.
    [[nodiscard]] IL::ValueId StagedIndex(const DecodedInstruction& instruction,
                                          IL::ValueId emitted,
                                          std::uint32_t reg,
                                          std::uint32_t component,
                                          std::uint32_t registers)
    {
        const IL::ValueId base = Bin(instruction,
                                     IL::BinaryOp::IMul,
                                     emitted,
                                     m_builder.ConstantU32(4 * registers));
        return Bin(instruction,
                   IL::BinaryOp::IAdd,
                   base,
                   m_builder.ConstantU32(reg * 4 + component));
    }

    // A staged store names its dword with the immediate offset; the slot is
    // the dword divided by one vertex, so the address register is not part of
    // the staging index.
    [[nodiscard]] Result<void> StoreStagedGeometryDword(const DecodedInstruction& instruction,
                                                        std::uint32_t byteOffset,
                                                        IL::ValueId value)
    {
        const std::uint32_t registers = m_options.geometryOutputRegisters;
        if (registers == 0)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-065",
                          "a staged geometry store needs the output register count (--gs-output-registers)",
                          instruction.pc));
        if (byteOffset % 4 != 0)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-062", "geometry LDS store is not dword aligned", instruction.pc));

        const std::uint32_t dword = byteOffset / 4;
        const std::uint32_t slot = dword / (4 * registers);
        if (slot >= m_options.geometryOutputVertices)
            return std::unexpected(LiftError("SR-GFX-LIFT-066",
                                             "geometry staged vertex " + std::to_string(slot) +
                                                 " is outside the vertex budget",
                                             instruction.pc));

        // The staging area holds f32 components, so the dword's bits become a
        // float and the emit reads them back the same way.
        const IL::ValueId component =
            m_builder.Emit(m_module.types.F32(),
                           IL::Op::Bitcast,
                           {},
                           {value},
                           {},
                           IL::NoPayload,
                           instruction.pc,
                           static_cast<std::uint16_t>(instruction.op));
        const IL::ValueId index = m_builder.ConstantU32(dword);
        m_builder.IntrinsicVoid(IL::IntrinsicId::GeometryStageStore,
                                {index, component},
                                IL::NoPayload,
                                instruction.pc);
        return {};
    }

    [[nodiscard]] Result<void> LiftGeometryDs(const DecodedInstruction& instruction)
    {
        switch (instruction.op)
        {
        case Op::DsStoreB32:
        {
            const auto value = DsData(instruction, OperandKind::DsData0, false);
            if (!value)
                return std::unexpected(value.error());
            return StoreStagedGeometryDword(instruction, instruction.dsOffset0, *value);
        }
        case Op::DsStoreB64:
        {
            const auto value = DsData(instruction, OperandKind::DsData0, true);
            if (!value)
                return std::unexpected(value.error());
            const IL::ValueId low =
                m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, *value, false);
            const IL::ValueId high64 =
                Bin(instruction, IL::BinaryOp::LShr, *value, m_builder.ConstantU64(32), m_module.types.U64());
            const IL::ValueId high =
                m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, false);
            const auto first = StoreStagedGeometryDword(instruction, instruction.dsOffset0, low);
            if (!first)
                return first;
            return StoreStagedGeometryDword(instruction, instruction.dsOffset0 + 4, high);
        }
        case Op::DsStore2addrB32:
        {
            const auto first = DsData(instruction, OperandKind::DsData0, false);
            if (!first)
                return std::unexpected(first.error());
            const auto second = DsData(instruction, OperandKind::DsData1, false);
            if (!second)
                return std::unexpected(second.error());
            const auto status = StoreStagedGeometryDword(instruction, instruction.dsOffset0, *first);
            if (!status)
                return status;
            return StoreStagedGeometryDword(instruction, instruction.dsOffset1, *second);
        }
        case Op::DsStore2addrB64:
        case Op::DsStore2addrStride64B32:
        case Op::DsStore2addrStride64B64:
            return std::unexpected(LiftError("SR-GFX-LIFT-061",
                                             "unsupported geometry LDS store " +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        case Op::DsLoadB32:
        case Op::DsLoadB64:
            break;
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-061",
                                             "unsupported geometry LDS access " +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        }

        if (instruction.dsOffset0 % 4 != 0)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-062", "geometry LDS read is not dword aligned", instruction.pc));

        return LiftGeometryDsRead(instruction,
                                  instruction.dsOffset0 / 4,
                                  instruction.op == Op::DsLoadB64 ? 2u : 1u);
    }

    [[nodiscard]] Result<void> LiftDsLoadStore(const DecodedInstruction& instruction)
    {
        if (m_module.stage == IL::Stage::Geometry)
            return LiftGeometryDs(instruction);

        const bool wide = instruction.op == Op::DsLoadB64 || instruction.op == Op::DsStoreB64 ||
                          instruction.op == Op::DsLoad2addrB64 ||
                          instruction.op == Op::DsStore2addrB64 ||
                          instruction.op == Op::DsLoad2addrStride64B64 ||
                          instruction.op == Op::DsStore2addrStride64B64;

        const bool store = instruction.op == Op::DsStoreB32 || instruction.op == Op::DsStoreB64 ||
                           instruction.op == Op::DsStore2addrB32 || instruction.op == Op::DsStore2addrB64 ||
                           instruction.op == Op::DsStore2addrStride64B32 ||
                           instruction.op == Op::DsStore2addrStride64B64;

        const bool second = instruction.op == Op::DsLoad2addrB32 || instruction.op == Op::DsLoad2addrB64 ||
                            instruction.op == Op::DsLoad2addrStride64B32 ||
                            instruction.op == Op::DsLoad2addrStride64B64 ||
                            instruction.op == Op::DsStore2addrB32 ||
                            instruction.op == Op::DsStore2addrB64 ||
                            instruction.op == Op::DsStore2addrStride64B32 ||
                            instruction.op == Op::DsStore2addrStride64B64;

        const bool strided = instruction.op == Op::DsLoad2addrStride64B32 ||
                             instruction.op == Op::DsLoad2addrStride64B64 ||
                             instruction.op == Op::DsStore2addrStride64B32 ||
                             instruction.op == Op::DsStore2addrStride64B64;

        // The stride-64 forms scale both immediates by 256 bytes.
        const std::uint32_t first = strided ? instruction.dsOffset0 * 256u : instruction.dsOffset0;
        const std::uint32_t secondOffset =
            strided ? instruction.dsOffset1 * 256u : instruction.dsOffset1;

        if (store)
        {
            const auto firstValue = DsData(instruction, OperandKind::DsData0, wide);
            if (!firstValue)
                return std::unexpected(firstValue.error());
            const auto status = DsStoreValue(instruction, first, *firstValue, wide);
            if (!status)
                return status;
            if (second)
            {
                const auto value = DsData(instruction, OperandKind::DsData1, wide);
                if (!value)
                    return std::unexpected(value.error());
                return DsStoreValue(instruction, secondOffset, *value, wide);
            }
            return {};
        }

        const auto value = DsLoadValue(instruction, first, wide);
        if (!value)
            return std::unexpected(value.error());
        if (wide)
        {
            const Operand destination = DsOperand(instruction, OperandKind::DsDest);
            if (destination.kind == OperandKind::None)
                return std::unexpected(
                    LiftError("SR-GFX-LIFT-058", "DS instruction has no destination", instruction.pc));
            DsWriteVgprPair(instruction, destination.value, *value);
            if (second)
            {
                const auto other = DsLoadValue(instruction, secondOffset, true);
                if (!other)
                    return std::unexpected(other.error());
                DsWriteVgprPair(instruction, destination.value + 2, *other);
            }
            return {};
        }

        const auto status = DsWriteDest(instruction, *value);
        if (!status)
            return status;
        if (second)
        {
            const auto other = DsLoadValue(instruction, secondOffset, false);
            if (!other)
                return std::unexpected(other.error());
            DsWriteVgpr(instruction, DsOperand(instruction, OperandKind::DsDest).value + 1, *other);
        }
        return {};
    }

    [[nodiscard]] Result<void> LiftDsBlock(const DecodedInstruction& instruction, std::uint32_t count)
    {
        const bool store = instruction.op == Op::DsStoreB96 || instruction.op == Op::DsStoreB128;
        const auto bytes = DsByteAddress(instruction, instruction.dsOffset0);
        if (!bytes)
            return std::unexpected(bytes.error());
        const auto index = DsIndexFromBytes(instruction, *bytes);
        if (!index)
            return std::unexpected(index.error());

        if (store)
        {
            const std::uint32_t base = DsOperand(instruction, OperandKind::DsData0).value;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const auto value = ReadVgpr(base + i, instruction.pc);
                if (!value)
                    return std::unexpected(value.error());
                DsPutDword(instruction, DsDwordAt(instruction, *index, i), *value);
            }
            return {};
        }

        const std::uint32_t base = DsOperand(instruction, OperandKind::DsDest).value;
        for (std::uint32_t i = 0; i < count; ++i)
            DsWriteVgpr(instruction, base + i, DsDword(instruction, DsDwordAt(instruction, *index, i)));
        return {};
    }

    enum class DsAtomicKind
    {
        Add,
        Sub,
        Rsub,
        Inc,
        Dec,
        MinI,
        MaxI,
        MinU,
        MaxU,
        And,
        Or,
        Xor,
        Mskor,
        Exchange,
        CompareStore,
        AddFloat,
        MinNum,
        MaxNum,
        CondSub,
        SubClamp,
    };

    // v_mbcnt_lo_u32_b32 dst, src0, src1: the number of set bits of src0 below
    // this lane, plus src1. With src0 all ones it is the lane id.
    [[nodiscard]] Result<void> LiftMbcnt(const DecodedInstruction& instruction)
    {
        if (instruction.operandCount < 3)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-072", "mbcnt has too few operands", instruction.pc));

        const auto maskSource = ReadBits(instruction.operands[1], instruction.pc);
        if (!maskSource)
            return std::unexpected(maskSource.error());
        const auto carry = ReadBits(instruction.operands[2], instruction.pc);
        if (!carry)
            return std::unexpected(carry.error());

        IL::SpecialRegPayload payload;
        payload.id = IL::SpecialRegId::SubgroupLocalInvocationId;
        const IL::PayloadId payloadId = m_module.payloads.Add(payload);
        const IL::ValueId lane = m_builder.Emit(m_module.types.U32(),
                                                IL::Op::Intrinsic,
                                                static_cast<std::uint16_t>(IL::IntrinsicId::SpecialReg),
                                                {},
                                                {},
                                                payloadId,
                                                instruction.pc,
                                                static_cast<std::uint16_t>(instruction.op),
                                                false);

        // bits below this lane: (1 << lane) - 1
        const IL::ValueId one = m_builder.ConstantU32(1);
        const IL::ValueId shift = Bin(instruction, IL::BinaryOp::Shl, one, lane);
        const IL::ValueId below = Bin(instruction, IL::BinaryOp::ISub, shift, one);
        const IL::ValueId selected = Bin(instruction, IL::BinaryOp::And, *maskSource, below);
        const IL::ValueId count = m_builder.Emit(m_module.types.U32(),
                                                 IL::Op::Unary,
                                                 static_cast<std::uint16_t>(IL::UnaryOp::PopCount),
                                                 {selected},
                                                 {},
                                                 IL::NoPayload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op));
        const IL::ValueId total = Bin(instruction, IL::BinaryOp::IAdd, count, *carry);
        return WriteDest(instruction.operands[0], total, instruction.pc);
    }

    // ds_swizzle_b32: a cross-lane permute named by the instruction's immediate.
    // The two measured encodings are the quadratic permute (bit 15 set: four
    // two-bit selectors, one per lane of the quad) and the bitmask permute (bit
    // 15 clear: each set bit swaps the lane pairs that differ in it). The FFT
    // form (0xFFFF) is refused.
    [[nodiscard]] Result<void> LiftDsSwizzle(const DecodedInstruction& instruction)
    {
        const std::uint32_t control = instruction.dsOffset0 | (instruction.dsOffset1 << 8);
        if (control == 0xFFFFu)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-074", "the FFT swizzle form is not modelled", instruction.pc));

        // The swizzle has no address register: its one data operand sits where
        // the DS address normally is.
        const Operand dataOperand = DsOperand(instruction, OperandKind::DsAddr);
        if (dataOperand.kind == OperandKind::None)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-075", "swizzle has no data operand", instruction.pc));
        const auto data = ReadVgpr(dataOperand.value, instruction.pc);
        if (!data)
            return std::unexpected(data.error());

        IL::SpecialRegPayload payload;
        payload.id = IL::SpecialRegId::SubgroupLocalInvocationId;
        const IL::PayloadId payloadId = m_module.payloads.Add(payload);
        const IL::ValueId lane = m_builder.Emit(m_module.types.U32(),
                                                IL::Op::Intrinsic,
                                                static_cast<std::uint16_t>(IL::IntrinsicId::SpecialReg),
                                                {},
                                                {},
                                                payloadId,
                                                instruction.pc,
                                                static_cast<std::uint16_t>(instruction.op),
                                                false);

        IL::ValueId index = lane;
        if ((control & 0x8000u) != 0)
        {
            const IL::ValueId quad = Bin(instruction, IL::BinaryOp::And, lane, m_builder.ConstantU32(3));
            const IL::ValueId shift = Bin(instruction, IL::BinaryOp::Shl, quad, m_builder.ConstantU32(1));
            const IL::ValueId selected =
                Bin(instruction, IL::BinaryOp::LShr, m_builder.ConstantU32(control), shift);
            const IL::ValueId selector =
                Bin(instruction, IL::BinaryOp::And, selected, m_builder.ConstantU32(3));
            const IL::ValueId base =
                Bin(instruction, IL::BinaryOp::And, lane, m_builder.ConstantU32(~3u));
            index = Bin(instruction, IL::BinaryOp::Or, base, selector);
        }
        else
        {
            index = Bin(instruction, IL::BinaryOp::Xor, lane, m_builder.ConstantU32(control & 0x1Fu));
        }

        IL::ShufflePayload shuffle;
        shuffle.mode = IL::ShuffleMode::Index;
        const IL::PayloadId shuffleId = m_module.payloads.Add(shuffle);
        const IL::ValueId value = m_builder.IntrinsicValue(m_module.types.U32(),
                                                           IL::IntrinsicId::WaveShuffle,
                                                           {*data, index},
                                                           shuffleId,
                                                           instruction.pc,
                                                           false);
        return DsWriteDest(instruction, value);
    }

    // ds_bpermute_b32: the value written by the lane named in the byte address
    // (address / 4) is the result. It is a cross-lane gather, so it maps to the
    // shuffle intrinsic rather than to LDS: a single invocation cannot see the
    // other lanes' stores.
    [[nodiscard]] Result<void> LiftDsBPermute(const DecodedInstruction& instruction)
    {
        if (instruction.operandCount < 3)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-071", "bpermute has too few operands", instruction.pc));

        const auto data = DsData(instruction, OperandKind::DsData0, false);
        if (!data)
            return std::unexpected(data.error());
        const Operand address = DsOperand(instruction, OperandKind::DsAddr);
        if (address.kind == OperandKind::None)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-073", "bpermute has no address operand", instruction.pc));
        const auto addressValue = ReadVgpr(address.value, instruction.pc);
        if (!addressValue)
            return std::unexpected(addressValue.error());
        const IL::ValueId lane =
            Bin(instruction, IL::BinaryOp::LShr, *addressValue, m_builder.ConstantU32(2));

        IL::ShufflePayload payload;
        payload.mode = IL::ShuffleMode::Index;
        const IL::PayloadId payloadId = m_module.payloads.Add(payload);
        const IL::ValueId value = m_builder.IntrinsicValue(m_module.types.U32(),
                                                           IL::IntrinsicId::WaveShuffle,
                                                           {*data, lane},
                                                           payloadId,
                                                           instruction.pc,
                                                           false);
        return DsWriteDest(instruction, value);
    }

    // ds_read_addtid_b32 / ds_write_addtid_b32: the address is the encoded
    // offset plus the invocation's thread id, so each lane reaches its own LDS
    // slot at the same offset.
    [[nodiscard]] Result<void> LiftDsAddTid(const DecodedInstruction& instruction)
    {
        const auto bytes = DsByteAddress(instruction, instruction.dsOffset0);
        if (!bytes)
            return std::unexpected(bytes.error());

        // The thread id of a compute invocation is the x component of the
        // local invocation id; the emitter supports that form.
        IL::SpecialRegPayload payload;
        payload.id = IL::SpecialRegId::ThreadIdX;
        const IL::PayloadId payloadId = m_module.payloads.Add(payload);
        const IL::ValueId tid = m_builder.Emit(m_module.types.U32(),
                                               IL::Op::Intrinsic,
                                               static_cast<std::uint16_t>(IL::IntrinsicId::SpecialReg),
                                               {},
                                               {},
                                               payloadId,
                                               instruction.pc,
                                               static_cast<std::uint16_t>(instruction.op),
                                               false);
        const IL::ValueId tidBytes =
            Bin(instruction, IL::BinaryOp::Shl, tid, m_builder.ConstantU32(2));
        const IL::ValueId address = Bin(instruction, IL::BinaryOp::IAdd, *bytes, tidBytes);

        const auto index = DsIndexFromBytes(instruction, address);
        if (!index)
            return std::unexpected(index.error());

        if (instruction.op == Op::DsStoreAddtidB32)
        {
            const auto value = DsData(instruction, OperandKind::DsData0, false);
            if (!value)
                return std::unexpected(value.error());
            DsPutDword(instruction, *index, *value);
            return {};
        }

        const IL::ValueId value = DsDword(instruction, *index);
        return DsWriteDest(instruction, value);
    }

    // ds_append / ds_consume: return the counter and step it. The plain forms
    // wrap through zero; the bounded forms take their bound from M0.
    [[nodiscard]] Result<void> LiftDsCounter(const DecodedInstruction& instruction, bool increment)
    {
        if (instruction.dsOffset0 % 4 != 0)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-070", "counter address is not dword aligned", instruction.pc));
        const IL::ValueId index = m_builder.ConstantU32(instruction.dsOffset0 / 4);

        const IL::ValueId old = DsDword(instruction, index);
        const IL::ValueId next = Bin(instruction,
                                     increment ? IL::BinaryOp::IAdd : IL::BinaryOp::ISub,
                                     old,
                                     m_builder.ConstantU32(1));
        const IL::ValueId guard = CurrentGuard(instruction.pc);
        m_builder.IntrinsicVoid(IL::IntrinsicId::LocalStore, {index, next}, IL::NoPayload, instruction.pc);
        ApplyGuard(instruction.pc, guard);
        return DsWriteDest(instruction, old);
    }

    [[nodiscard]] Result<void> LiftDsAtomic(const DecodedInstruction& instruction,
                                            DsAtomicKind kind,
                                            bool wide,
                                            bool rtn)
    {
        const auto index = DsIndex(instruction, instruction.dsOffset0);
        if (!index)
            return std::unexpected(index.error());

        const auto data0 = DsData(instruction, OperandKind::DsData0, wide);
        if (!data0)
            return std::unexpected(data0.error());

        const bool needsSecond = kind == DsAtomicKind::Mskor || kind == DsAtomicKind::CompareStore;
        IL::ValueId data1 = IL::InvalidValue;
        if (needsSecond)
        {
            const auto value = DsData(instruction, OperandKind::DsData1, wide);
            if (!value)
                return std::unexpected(value.error());
            data1 = *value;
        }

        const IL::TypeId uintType = wide ? m_module.types.U64() : m_module.types.U32();
        const IL::TypeId intType = wide ? m_module.types.I64() : m_module.types.I32();
        const IL::TypeId floatType = wide ? F64Type() : m_module.types.F32();

        const IL::ValueId old = wide ? DsLoad64(instruction, *index) : DsDword(instruction, *index);
        const IL::ValueId zero = wide ? m_builder.ConstantU64(0) : m_builder.ConstantU32(0);
        const IL::ValueId one = wide ? m_builder.ConstantU64(1) : m_builder.ConstantU32(1);

        IL::ValueId result = old;
        switch (kind)
        {
        case DsAtomicKind::Add:
            result = Bin(instruction, IL::BinaryOp::IAdd, old, *data0, uintType);
            break;
        case DsAtomicKind::Sub:
            result = Bin(instruction, IL::BinaryOp::ISub, old, *data0, uintType);
            break;
        case DsAtomicKind::Rsub:
            result = Bin(instruction, IL::BinaryOp::ISub, *data0, old, uintType);
            break;
        case DsAtomicKind::Inc:
        {
            const IL::ValueId exceeded = m_builder.CompareValue(IL::CompareOp::UGe, old, *data0, false);
            const IL::ValueId incremented = Bin(instruction, IL::BinaryOp::IAdd, old, one, uintType);
            result = m_builder.SelectValue(uintType, exceeded, zero, incremented, false);
            break;
        }
        case DsAtomicKind::Dec:
        {
            const IL::ValueId atZero = m_builder.CompareValue(IL::CompareOp::UEq, old, zero, false);
            const IL::ValueId decremented = Bin(instruction, IL::BinaryOp::ISub, old, one, uintType);
            result = m_builder.SelectValue(uintType, atZero, *data0, decremented, false);
            break;
        }
        case DsAtomicKind::MinI:
            result = m_builder.SelectValue(uintType,
                                           m_builder.CompareValue(IL::CompareOp::ILt, old, *data0, false),
                                           old,
                                           *data0,
                                           false);
            break;
        case DsAtomicKind::MaxI:
            result = m_builder.SelectValue(uintType,
                                           m_builder.CompareValue(IL::CompareOp::IGt, old, *data0, false),
                                           old,
                                           *data0,
                                           false);
            break;
        case DsAtomicKind::MinU:
            result = m_builder.SelectValue(uintType,
                                           m_builder.CompareValue(IL::CompareOp::ULt, old, *data0, false),
                                           old,
                                           *data0,
                                           false);
            break;
        case DsAtomicKind::MaxU:
            result = m_builder.SelectValue(uintType,
                                           m_builder.CompareValue(IL::CompareOp::UGt, old, *data0, false),
                                           old,
                                           *data0,
                                           false);
            break;
        case DsAtomicKind::And:
            result = Bin(instruction, IL::BinaryOp::And, old, *data0, uintType);
            break;
        case DsAtomicKind::Or:
            result = Bin(instruction, IL::BinaryOp::Or, old, *data0, uintType);
            break;
        case DsAtomicKind::Xor:
            result = Bin(instruction, IL::BinaryOp::Xor, old, *data0, uintType);
            break;
        case DsAtomicKind::Mskor:
        {
            const IL::ValueId masked = Bin(instruction, IL::BinaryOp::And, old, *data0, uintType);
            result = Bin(instruction, IL::BinaryOp::Or, masked, data1, uintType);
            break;
        }
        case DsAtomicKind::Exchange:
            result = *data0;
            break;
        case DsAtomicKind::CompareStore:
        {
            const IL::ValueId equal = m_builder.CompareValue(IL::CompareOp::UEq, old, *data0, false);
            result = m_builder.SelectValue(uintType, equal, data1, old, false);
            break;
        }
        case DsAtomicKind::AddFloat:
            result = Bin(instruction, IL::BinaryOp::FAdd, old, *data0, floatType);
            break;
        case DsAtomicKind::MinNum:
            result = Bin(instruction, IL::BinaryOp::FMin, old, *data0, floatType);
            break;
        case DsAtomicKind::MaxNum:
            result = Bin(instruction, IL::BinaryOp::FMax, old, *data0, floatType);
            break;
        case DsAtomicKind::CondSub:
        {
            const IL::ValueId large = m_builder.CompareValue(IL::CompareOp::UGe, old, *data0, false);
            const IL::ValueId difference = Bin(instruction, IL::BinaryOp::ISub, old, *data0, uintType);
            result = m_builder.SelectValue(uintType, large, difference, old, false);
            break;
        }
        case DsAtomicKind::SubClamp:
        {
            const IL::ValueId large = m_builder.CompareValue(IL::CompareOp::UGe, old, *data0, false);
            const IL::ValueId difference = Bin(instruction, IL::BinaryOp::ISub, *data0, old, uintType);
            result = m_builder.SelectValue(uintType, large, zero, difference, false);
            break;
        }
        }

        if (wide)
            DsStore64(instruction, *index, result);
        else
            DsPutDword(instruction, *index, result);

        (void)intType;
        if (rtn)
        {
            if (!wide)
                return DsWriteDest(instruction, old);
            DsWriteVgprPair(instruction, DsOperand(instruction, OperandKind::DsDest).value, old);
            return {};
        }
        return {};
    }

    [[nodiscard]] Result<void> LiftDsPkAdd(const DecodedInstruction& instruction, bool bfloat, bool rtn)
    {
        const auto index = DsIndex(instruction, instruction.dsOffset0);
        if (!index)
            return std::unexpected(index.error());
        const auto data = DsData(instruction, OperandKind::DsData0, false);
        if (!data)
            return std::unexpected(data.error());

        const IL::ValueId old = DsDword(instruction, *index);
        const IL::TypeId halfType = bfloat ? BF16Type() : F16Type();

        const auto laneAdd = [&](std::uint32_t shift) -> IL::ValueId {
            const auto lane = [&](IL::ValueId value) {
                const IL::ValueId bits = shift == 0
                                             ? value
                                             : Bin(instruction, IL::BinaryOp::LShr, value,
                                                   m_builder.ConstantU32(shift));
                const IL::ValueId low =
                    m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, bits, false);
                return Bitcast(halfType, low);
            };
            const IL::ValueId sum = Bin(instruction, IL::BinaryOp::FAdd, lane(old), lane(*data), halfType);
            const IL::ValueId bits = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                                            Bitcast(U16Type(), sum), false);
            return shift == 0
                       ? bits
                       : Bin(instruction, IL::BinaryOp::Shl, bits, m_builder.ConstantU32(shift));
        };

        const IL::ValueId low = laneAdd(0);
        const IL::ValueId high = laneAdd(16);
        const IL::ValueId merged = Bin(instruction, IL::BinaryOp::Or,
                                       Bin(instruction, IL::BinaryOp::And, low, m_builder.ConstantU32(0xFFFF)),
                                       high);
        DsPutDword(instruction, *index, merged);
        if (rtn)
            return DsWriteDest(instruction, old);
        return {};
    }

    [[nodiscard]] Result<void> LiftDsByteAccess(const DecodedInstruction& instruction)
    {
        const bool store = instruction.op == Op::DsStoreB8 || instruction.op == Op::DsStoreB16 ||
                           instruction.op == Op::DsStoreB8D16Hi ||
                           instruction.op == Op::DsStoreB16D16Hi;
        const std::uint32_t width =
            (instruction.op == Op::DsStoreB8 || instruction.op == Op::DsLoadU8 ||
             instruction.op == Op::DsLoadI8 || instruction.op == Op::DsStoreB8D16Hi ||
             instruction.op == Op::DsLoadU8D16 || instruction.op == Op::DsLoadU8D16Hi ||
             instruction.op == Op::DsLoadI8D16 || instruction.op == Op::DsLoadI8D16Hi)
                ? 8u
                : 16u;
        const bool highHalf = instruction.op == Op::DsStoreB8D16Hi ||
                              instruction.op == Op::DsStoreB16D16Hi ||
                              instruction.op == Op::DsLoadU8D16Hi || instruction.op == Op::DsLoadI8D16Hi ||
                              instruction.op == Op::DsLoadU16D16Hi;

        const auto bytes = DsByteAddress(instruction, instruction.dsOffset0);
        if (!bytes)
            return std::unexpected(bytes.error());
        const auto index = DsIndexFromBytes(instruction, *bytes);
        if (!index)
            return std::unexpected(index.error());

        const IL::ValueId shift =
            DsLaneShift(instruction, *bytes, width == 8 ? 3u : 2u);
        const std::uint32_t fieldMask = width == 8 ? 0xFFu : 0xFFFFu;
        const IL::ValueId mask =
            Bin(instruction, IL::BinaryOp::Shl, m_builder.ConstantU32(fieldMask), shift);

        if (store)
        {
            const auto data = DsData(instruction, OperandKind::DsData0, false);
            if (!data)
                return std::unexpected(data.error());
            IL::ValueId value = *data;
            if (highHalf)
                value = Bin(instruction, IL::BinaryOp::LShr, value, m_builder.ConstantU32(16));
            const IL::ValueId field =
                Bin(instruction, IL::BinaryOp::Shl,
                    Bin(instruction, IL::BinaryOp::And, value, m_builder.ConstantU32(fieldMask)), shift);
            const IL::ValueId old = DsDword(instruction, *index);
            const IL::ValueId cleared =
                Bin(instruction, IL::BinaryOp::And, old,
                    m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U32(), mask, false));
            DsPutDword(instruction, *index, Bin(instruction, IL::BinaryOp::Or, cleared, field));
            return {};
        }

        const IL::ValueId old = DsDword(instruction, *index);
        const IL::ValueId field =
            Bin(instruction, IL::BinaryOp::And,
                Bin(instruction, IL::BinaryOp::LShr, old, shift), m_builder.ConstantU32(fieldMask));

        IL::ValueId value = field;
        if (instruction.op == Op::DsLoadI8 || instruction.op == Op::DsLoadI8D16 ||
            instruction.op == Op::DsLoadI8D16Hi)
        {
            value = SignExtend8Value(field);
        }
        else if (instruction.op == Op::DsLoadI16)
        {
            value = SignExtend16Value(field);
        }

        if (highHalf)
        {
            // The D16_HI forms replace only the high half of the destination.
            const std::uint32_t destination = DsOperand(instruction, OperandKind::DsDest).value;
            const auto current = ReadVgpr(destination, instruction.pc);
            if (!current)
                return std::unexpected(current.error());
            const IL::ValueId narrowed =
                width == 8 ? value
                           : Bin(instruction, IL::BinaryOp::And, value, m_builder.ConstantU32(0xFFFF));
            const IL::ValueId merged =
                Bin(instruction, IL::BinaryOp::Or,
                    Bin(instruction, IL::BinaryOp::And, *current, m_builder.ConstantU32(0xFFFF)),
                    Bin(instruction, IL::BinaryOp::Shl, narrowed, m_builder.ConstantU32(16)));
            DsWriteVgpr(instruction, destination, merged);
            return {};
        }

        if (instruction.op == Op::DsLoadU8D16 || instruction.op == Op::DsLoadI8D16 ||
            instruction.op == Op::DsLoadU16D16 || instruction.op == Op::DsLoadU8D16Hi ||
            instruction.op == Op::DsLoadI8D16Hi || instruction.op == Op::DsLoadU16D16Hi)
        {
            const std::uint32_t destination = DsOperand(instruction, OperandKind::DsDest).value;
            const auto current = ReadVgpr(destination, instruction.pc);
            if (!current)
                return std::unexpected(current.error());
            const IL::ValueId merged =
                Bin(instruction, IL::BinaryOp::Or,
                    Bin(instruction, IL::BinaryOp::And, *current, m_builder.ConstantU32(0xFFFF0000)),
                    Bin(instruction, IL::BinaryOp::And, value, m_builder.ConstantU32(0xFFFF)));
            DsWriteVgpr(instruction, destination, merged);
            return {};
        }

        return DsWriteDest(instruction, value);
    }

    [[nodiscard]] Result<void> LiftDs(const DecodedInstruction& instruction)
    {
        if (instruction.dsGds)
        {
            return std::unexpected(
                LiftError("SR-GFX-LIFT-060", "GDS addressing is not implemented", instruction.pc));
        }

        switch (instruction.op)
        {
        case Op::DsNop:
            return {};
        case Op::DsLoadB32:
        case Op::DsLoadB64:
        case Op::DsLoad2addrB32:
        case Op::DsLoad2addrB64:
        case Op::DsLoad2addrStride64B32:
        case Op::DsLoad2addrStride64B64:
        case Op::DsStoreB32:
        case Op::DsStoreB64:
        case Op::DsStore2addrB32:
        case Op::DsStore2addrB64:
        case Op::DsStore2addrStride64B32:
        case Op::DsStore2addrStride64B64:
            return LiftDsLoadStore(instruction);
        case Op::DsLoadB96:
            return LiftDsBlock(instruction, 3);
        case Op::DsLoadB128:
            return LiftDsBlock(instruction, 4);
        case Op::DsStoreB96:
            return LiftDsBlock(instruction, 3);
        case Op::DsStoreB128:
            return LiftDsBlock(instruction, 4);
        case Op::DsLoadU8:
        case Op::DsLoadI8:
        case Op::DsLoadU16:
        case Op::DsLoadI16:
        case Op::DsLoadU8D16:
        case Op::DsLoadU8D16Hi:
        case Op::DsLoadI8D16:
        case Op::DsLoadI8D16Hi:
        case Op::DsLoadU16D16:
        case Op::DsLoadU16D16Hi:
        case Op::DsStoreB8:
        case Op::DsStoreB16:
        case Op::DsStoreB8D16Hi:
        case Op::DsStoreB16D16Hi:
            return LiftDsByteAccess(instruction);

        case Op::DsBpermuteB32:
            return LiftDsBPermute(instruction);
        case Op::DsSwizzleB32:
            return LiftDsSwizzle(instruction);
        case Op::DsLoadAddtidB32:
        case Op::DsStoreAddtidB32:
            return LiftDsAddTid(instruction);
        case Op::DsAppend:
            return LiftDsCounter(instruction, true);
        case Op::DsConsume:
            return LiftDsCounter(instruction, false);
        case Op::DsAddU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Add, false, false);
        case Op::DsAddRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Add, false, true);
        case Op::DsSubU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Sub, false, false);
        case Op::DsSubRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Sub, false, true);
        case Op::DsRsubU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Rsub, false, false);
        case Op::DsRsubRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Rsub, false, true);
        case Op::DsIncU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Inc, false, false);
        case Op::DsIncRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Inc, false, true);
        case Op::DsDecU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Dec, false, false);
        case Op::DsDecRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::Dec, false, true);
        case Op::DsMinI32:
            return LiftDsAtomic(instruction, DsAtomicKind::MinI, false, false);
        case Op::DsMinRtnI32:
            return LiftDsAtomic(instruction, DsAtomicKind::MinI, false, true);
        case Op::DsMaxI32:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxI, false, false);
        case Op::DsMaxRtnI32:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxI, false, true);
        case Op::DsMinU32:
            return LiftDsAtomic(instruction, DsAtomicKind::MinU, false, false);
        case Op::DsMinRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::MinU, false, true);
        case Op::DsMaxU32:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxU, false, false);
        case Op::DsMaxRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxU, false, true);
        case Op::DsAndB32:
            return LiftDsAtomic(instruction, DsAtomicKind::And, false, false);
        case Op::DsAndRtnB32:
            return LiftDsAtomic(instruction, DsAtomicKind::And, false, true);
        case Op::DsOrB32:
            return LiftDsAtomic(instruction, DsAtomicKind::Or, false, false);
        case Op::DsOrRtnB32:
            return LiftDsAtomic(instruction, DsAtomicKind::Or, false, true);
        case Op::DsXorB32:
            return LiftDsAtomic(instruction, DsAtomicKind::Xor, false, false);
        case Op::DsXorRtnB32:
            return LiftDsAtomic(instruction, DsAtomicKind::Xor, false, true);
        case Op::DsMskorB32:
            return LiftDsAtomic(instruction, DsAtomicKind::Mskor, false, false);
        case Op::DsMskorRtnB32:
            return LiftDsAtomic(instruction, DsAtomicKind::Mskor, false, true);
        case Op::DsStorexchgRtnB32:
        case Op::DsStorexchg2addrRtnB32:
        case Op::DsStorexchg2addrStride64RtnB32:
            return LiftDsAtomic(instruction, DsAtomicKind::Exchange, false, true);
        case Op::DsCmpstoreB32:
            return LiftDsAtomic(instruction, DsAtomicKind::CompareStore, false, false);
        case Op::DsCmpstoreRtnB32:
            return LiftDsAtomic(instruction, DsAtomicKind::CompareStore, false, true);
        case Op::DsAddF32:
            return LiftDsAtomic(instruction, DsAtomicKind::AddFloat, false, false);
        case Op::DsAddRtnF32:
            return LiftDsAtomic(instruction, DsAtomicKind::AddFloat, false, true);
        case Op::DsMinNumF32:
            return LiftDsAtomic(instruction, DsAtomicKind::MinNum, false, false);
        case Op::DsMinNumRtnF32:
            return LiftDsAtomic(instruction, DsAtomicKind::MinNum, false, true);
        case Op::DsMaxNumF32:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxNum, false, false);
        case Op::DsMaxNumRtnF32:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxNum, false, true);
        case Op::DsCondSubU32:
            return LiftDsAtomic(instruction, DsAtomicKind::CondSub, false, false);
        case Op::DsCondSubRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::CondSub, false, true);
        case Op::DsSubClampU32:
            return LiftDsAtomic(instruction, DsAtomicKind::SubClamp, false, false);
        case Op::DsSubClampRtnU32:
            return LiftDsAtomic(instruction, DsAtomicKind::SubClamp, false, true);

        case Op::DsAddU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Add, true, false);
        case Op::DsAddRtnU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Add, true, true);
        case Op::DsSubU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Sub, true, false);
        case Op::DsSubRtnU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Sub, true, true);
        case Op::DsRsubU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Rsub, true, false);
        case Op::DsRsubRtnU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Rsub, true, true);
        case Op::DsIncU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Inc, true, false);
        case Op::DsIncRtnU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Inc, true, true);
        case Op::DsDecU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Dec, true, false);
        case Op::DsDecRtnU64:
            return LiftDsAtomic(instruction, DsAtomicKind::Dec, true, true);
        case Op::DsMinI64:
            return LiftDsAtomic(instruction, DsAtomicKind::MinI, true, false);
        case Op::DsMinRtnI64:
            return LiftDsAtomic(instruction, DsAtomicKind::MinI, true, true);
        case Op::DsMaxI64:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxI, true, false);
        case Op::DsMaxRtnI64:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxI, true, true);
        case Op::DsMinU64:
            return LiftDsAtomic(instruction, DsAtomicKind::MinU, true, false);
        case Op::DsMinRtnU64:
            return LiftDsAtomic(instruction, DsAtomicKind::MinU, true, true);
        case Op::DsMaxU64:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxU, true, false);
        case Op::DsMaxRtnU64:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxU, true, true);
        case Op::DsAndB64:
            return LiftDsAtomic(instruction, DsAtomicKind::And, true, false);
        case Op::DsAndRtnB64:
            return LiftDsAtomic(instruction, DsAtomicKind::And, true, true);
        case Op::DsOrB64:
            return LiftDsAtomic(instruction, DsAtomicKind::Or, true, false);
        case Op::DsOrRtnB64:
            return LiftDsAtomic(instruction, DsAtomicKind::Or, true, true);
        case Op::DsXorB64:
            return LiftDsAtomic(instruction, DsAtomicKind::Xor, true, false);
        case Op::DsXorRtnB64:
            return LiftDsAtomic(instruction, DsAtomicKind::Xor, true, true);
        case Op::DsMskorB64:
            return LiftDsAtomic(instruction, DsAtomicKind::Mskor, true, false);
        case Op::DsMskorRtnB64:
            return LiftDsAtomic(instruction, DsAtomicKind::Mskor, true, true);
        case Op::DsStorexchgRtnB64:
        case Op::DsStorexchg2addrRtnB64:
        case Op::DsStorexchg2addrStride64RtnB64:
            return LiftDsAtomic(instruction, DsAtomicKind::Exchange, true, true);
        case Op::DsCmpstoreB64:
            return LiftDsAtomic(instruction, DsAtomicKind::CompareStore, true, false);
        case Op::DsCmpstoreRtnB64:
            return LiftDsAtomic(instruction, DsAtomicKind::CompareStore, true, true);
        case Op::DsMinNumF64:
            return LiftDsAtomic(instruction, DsAtomicKind::MinNum, true, false);
        case Op::DsMinNumRtnF64:
            return LiftDsAtomic(instruction, DsAtomicKind::MinNum, true, true);
        case Op::DsMaxNumF64:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxNum, true, false);
        case Op::DsMaxNumRtnF64:
            return LiftDsAtomic(instruction, DsAtomicKind::MaxNum, true, true);

        case Op::DsPkAddF16:
            return LiftDsPkAdd(instruction, false, false);
        case Op::DsPkAddRtnF16:
            return LiftDsPkAdd(instruction, false, true);
        case Op::DsPkAddBf16:
            return LiftDsPkAdd(instruction, true, false);
        case Op::DsPkAddRtnBf16:
            return LiftDsPkAdd(instruction, true, true);

        default:
            break;
        }

        return std::unexpected(LiftError("SR-GFX-LIFT-061",
                                         std::string("unsupported data-share instruction: ") +
                                             std::string(ToString(instruction.op)),
                                         instruction.pc));
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

    // ---- Scalar compares, float arithmetic and conversions ----

    [[nodiscard]] Result<IL::ValueId> ReadScalarHalf(const Operand& operand, std::uint32_t pc)
    {
        const auto bits = ReadBits(operand, pc);
        if (!bits)
            return std::unexpected(bits.error());
        const IL::ValueId low = m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, *bits, false);
        return Bitcast(F16Type(), low);
    }

    [[nodiscard]] Result<IL::ValueId> ReadScalarFloat(const DecodedInstruction& instruction,
                                                      std::uint8_t index,
                                                      bool half)
    {
        if (half)
            return ReadScalarHalf(instruction.operands[index], instruction.pc);
        return ReadFloatSource(instruction, index);
    }

    [[nodiscard]] IL::ValueId FloatBits(IL::ValueId value, bool half)
    {
        if (half)
        {
            const IL::ValueId bits = Bitcast(U16Type(), value);
            return m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt, bits, false);
        }
        return Bitcast(m_module.types.U32(), value);
    }

    [[nodiscard]] Result<void> LiftScalarFloatBinary(const DecodedInstruction& instruction,
                                                     IL::BinaryOp op,
                                                     bool half)
    {
        const auto left = ReadScalarFloat(instruction, 1, half);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadScalarFloat(instruction, 2, half);
        if (!right)
            return std::unexpected(right.error());
        const IL::TypeId type = half ? F16Type() : m_module.types.F32();
        const IL::ValueId result = Bin(instruction, op, *left, *right, type);
        return WriteDest(instruction.operands[0], FloatBits(result, half), instruction.pc);
    }

    // IEEE minimum/maximum propagate NaN; the min/max number forms drop it.
    [[nodiscard]] Result<void> LiftScalarFloatMinimum(const DecodedInstruction& instruction,
                                                      bool half,
                                                      bool maximum)
    {
        const auto left = ReadScalarFloat(instruction, 1, half);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadScalarFloat(instruction, 2, half);
        if (!right)
            return std::unexpected(right.error());
        const IL::TypeId type = half ? F16Type() : m_module.types.F32();
        const IL::ValueId orderedLeft = m_builder.CompareValue(IL::CompareOp::FOrdEq, *left, *left, true);
        const IL::ValueId orderedRight = m_builder.CompareValue(IL::CompareOp::FOrdEq, *right, *right, true);
        const IL::ValueId ordered = m_builder.Emit(m_module.types.Bool(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                   {orderedLeft, orderedRight},
                                                   {},
                                                   IL::NoPayload,
                                                   instruction.pc,
                                                   static_cast<std::uint16_t>(instruction.op),
                                                   true);
        const IL::ValueId selected =
            Bin(instruction, maximum ? IL::BinaryOp::FMax : IL::BinaryOp::FMin, *left, *right, type);
        const IL::ValueId nan = half ? Bitcast(F16Type(), m_builder.ConstantU32(0x7E00))
                                     : m_builder.ConstantF32(std::numeric_limits<float>::quiet_NaN());
        const IL::ValueId result =
            m_builder.SelectValue(type, ordered, selected, nan, true);
        return WriteDest(instruction.operands[0], FloatBits(result, half), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftScalarFmac(const DecodedInstruction& instruction, bool half)
    {
        const IL::TypeId type = half ? F16Type() : m_module.types.F32();
        // The accumulator is the destination register itself.
        const auto current = ReadSgpr(instruction.operands[0].value, instruction.pc);
        if (!current)
            return std::unexpected(current.error());
        const IL::ValueId accumulator =
            half ? Bitcast(F16Type(), m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, *current, true))
                 : Bitcast(m_module.types.F32(), *current);
        const auto left = ReadScalarFloat(instruction, 1, half);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadScalarFloat(instruction, 2, half);
        if (!right)
            return std::unexpected(right.error());
        const IL::ValueId result = m_builder.Emit(type,
                                                  IL::Op::Ternary,
                                                  static_cast<std::uint16_t>(IL::TernaryOp::Fma),
                                                  {*left, *right, accumulator},
                                                  {},
                                                  IL::NoPayload,
                                                  instruction.pc,
                                                  static_cast<std::uint16_t>(instruction.op),
                                                  true);
        return WriteDest(instruction.operands[0], FloatBits(result, half), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftScalarUnaryFloat(const DecodedInstruction& instruction,
                                                    IL::UnaryOp op,
                                                    bool half)
    {
        const auto source = ReadScalarFloat(instruction, 1, half);
        if (!source)
            return std::unexpected(source.error());
        const IL::TypeId type = half ? F16Type() : m_module.types.F32();
        const IL::ValueId result = m_builder.UnaryValue(op, type, *source, false);
        return WriteDest(instruction.operands[0], FloatBits(result, half), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftScalarConvert(const DecodedInstruction& instruction)
    {
        const IL::TypeId f32 = m_module.types.F32();
        switch (instruction.op)
        {
        case Op::S_CvtF32I32:
        case Op::S_CvtF32U32:
        {
            const bool isSigned = instruction.op == Op::S_CvtF32I32;
            const auto source = isSigned ? ReadSigned(instruction.operands[1], instruction.pc)
                                         : ReadBits(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId result = m_builder.ConvertValue(
                f32, isSigned ? IL::ConvertOp::SIToFP : IL::ConvertOp::UIToFP, *source, true);
            return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result),
                             instruction.pc);
        }
        case Op::S_CvtI32F32:
        case Op::S_CvtU32F32:
        {
            const bool isSigned = instruction.op == Op::S_CvtI32F32;
            const auto source = ReadFloatSource(instruction, 1);
            if (!source)
                return std::unexpected(source.error());
            const IL::TypeId target = isSigned ? m_module.types.I32() : m_module.types.U32();
            const IL::ValueId result = m_builder.ConvertValue(
                target, isSigned ? IL::ConvertOp::FPToSI : IL::ConvertOp::FPToUI, *source, true);
            return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result),
                             instruction.pc);
        }
        case Op::S_CvtF16F32:
        {
            const auto source = ReadFloatSource(instruction, 1);
            if (!source)
                return std::unexpected(source.error());
            const IL::ValueId half = m_builder.ConvertValue(F16Type(), IL::ConvertOp::FPTrunc,
                                                            *source, true);
            return WriteDest(instruction.operands[0], FloatBits(half, true), instruction.pc);
        }
        case Op::S_CvtF32F16:
        case Op::S_CvtHiF32F16:
        {
            const auto bits = ReadBits(instruction.operands[1], instruction.pc);
            if (!bits)
                return std::unexpected(bits.error());
            IL::ValueId halfBits = *bits;
            if (instruction.op == Op::S_CvtHiF32F16)
                halfBits = Bin(instruction, IL::BinaryOp::LShr, halfBits, m_builder.ConstantU32(16));
            const IL::ValueId low =
                m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, halfBits, false);
            const IL::ValueId half = Bitcast(F16Type(), low);
            const IL::ValueId result = m_builder.ConvertValue(f32, IL::ConvertOp::FPExt, half, true);
            return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result),
                             instruction.pc);
        }
        default:
            break;
        }
        return std::unexpected(
            LiftError("SR-GFX-LIFT-072", "unsupported scalar conversion", instruction.pc));
    }

    [[nodiscard]] Result<void> LiftScalarPack(const DecodedInstruction& instruction)
    {
        // PACK_xy packs a half from each source: x selects src0's half and y
        // selects src1's half, where l is the low half and h is the high half.
        const bool secondHigh = instruction.op == Op::S_PackLhB32B16 ||
                                instruction.op == Op::S_PackHhB32B16;
        const bool firstHigh = instruction.op == Op::S_PackHhB32B16 ||
                               instruction.op == Op::S_PackHlB32B16;
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());

        const auto halfOf = [&](IL::ValueId value, bool high) {
            if (!high)
                return Bin(instruction, IL::BinaryOp::And, value, m_builder.ConstantU32(0xFFFF));
            return Bin(instruction, IL::BinaryOp::LShr, value, m_builder.ConstantU32(16));
        };
        const IL::ValueId low = halfOf(*first, firstHigh);
        const IL::ValueId high = halfOf(*second, secondHigh);
        const IL::ValueId packed = Bin(instruction, IL::BinaryOp::Or, low,
                                       Bin(instruction, IL::BinaryOp::Shl, high,
                                           m_builder.ConstantU32(16)));
        return WriteDest(instruction.operands[0], packed, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftScalarLshlAdd(const DecodedInstruction& instruction,
                                                 std::uint32_t shift)
    {
        const auto left = ReadBits(instruction.operands[1], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadBits(instruction.operands[2], instruction.pc);
        if (!right)
            return std::unexpected(right.error());
        const IL::ValueId shifted =
            Bin(instruction, IL::BinaryOp::Shl, *left, m_builder.ConstantU32(shift));
        const IL::ValueId sum = Bin(instruction, IL::BinaryOp::IAdd, shifted, *right);
        const IL::ValueId overflow = m_builder.CompareValue(IL::CompareOp::ULt, sum, shifted, true);
        const auto scc = WriteScc(overflow, instruction.pc);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], sum, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftScalarCmov(const DecodedInstruction& instruction)
    {
        const auto condition = ReadScc(instruction.pc);
        if (!condition)
            return std::unexpected(condition.error());
        const bool wide = instruction.op == Op::S_CmovB64;
        if (wide)
        {
            const auto source = ReadPairSource(instruction.operands[1], instruction.pc);
            if (!source)
                return std::unexpected(source.error());
            const auto current = ReadSgpr64(instruction.operands[0].value, instruction.pc);
            if (!current)
                return std::unexpected(current.error());
            const IL::ValueId selected =
                m_builder.SelectValue(m_module.types.U64(), *condition, *source, *current, true);
            return WriteSgpr64(instruction.operands[0], selected, instruction.pc);
        }
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const auto current = ReadSgpr(instruction.operands[0].value, instruction.pc);
        if (!current)
            return std::unexpected(current.error());
        const IL::ValueId selected =
            m_builder.SelectValue(m_module.types.U32(), *condition, *source, *current, true);
        return WriteDest(instruction.operands[0], selected, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftBitreplicate(const DecodedInstruction& instruction)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        // Spread each bit of a 32-bit value to even positions, then duplicate
        // it into the following odd position.
        IL::ValueId spread = m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt,
                                                    *source, true);
        const std::uint64_t masks[] = {0x0000FFFF0000FFFFull, 0x00FF00FF00FF00FFull,
                                       0x0F0F0F0F0F0F0F0Full, 0x3333333333333333ull,
                                       0x5555555555555555ull};
        const std::uint32_t shifts[] = {16, 8, 4, 2, 1};
        for (std::uint32_t i = 0; i < 5; ++i)
        {
            const IL::ValueId shifted = Bin(instruction, IL::BinaryOp::Shl, spread,
                                            m_builder.ConstantU64(shifts[i]), m_module.types.U64());
            const IL::ValueId combined =
                Bin(instruction, IL::BinaryOp::Or, spread, shifted, m_module.types.U64());
            spread = Bin(instruction, IL::BinaryOp::And, combined, m_builder.ConstantU64(masks[i]),
                         m_module.types.U64());
        }
        const IL::ValueId doubled =
            Bin(instruction, IL::BinaryOp::Or, spread,
                Bin(instruction, IL::BinaryOp::Shl, spread, m_builder.ConstantU64(1),
                    m_module.types.U64()),
                m_module.types.U64());
        return WriteSgpr64(instruction.operands[0], doubled, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftQuadmask(const DecodedInstruction& instruction)
    {
        const bool wide = instruction.op == Op::S_QuadmaskB64;
        const auto source = wide ? ReadPairSource(instruction.operands[1], instruction.pc)
                                 : ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        const IL::TypeId valueType = wide ? m_module.types.U64() : m_module.types.U32();
        IL::ValueId any = *source;
        for (std::uint32_t i = 1; i < 4; ++i)
        {
            const IL::ValueId shifted =
                wide ? Bin(instruction, IL::BinaryOp::LShr, any, m_builder.ConstantU64(i),
                           m_module.types.U64())
                     : Bin(instruction, IL::BinaryOp::LShr, any, m_builder.ConstantU32(i));
            any = Bin(instruction, IL::BinaryOp::Or, any, shifted, valueType);
        }

        IL::ValueId mask = m_builder.ConstantU32(0);
        const std::uint32_t groups = wide ? 16u : 8u;
        for (std::uint32_t i = 0; i < groups; ++i)
        {
            const std::uint32_t bit = 3u * i;
            const IL::ValueId groupShifted =
                wide ? Bin(instruction, IL::BinaryOp::LShr, any, m_builder.ConstantU64(bit),
                           m_module.types.U64())
                     : Bin(instruction, IL::BinaryOp::LShr, any, m_builder.ConstantU32(bit));
            const IL::ValueId groupBit =
                wide ? m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc,
                                              groupShifted, true)
                     : groupShifted;
            const IL::ValueId single =
                Bin(instruction, IL::BinaryOp::And, groupBit,
                    m_builder.ConstantU32(1u << i));
            mask = Bin(instruction, IL::BinaryOp::Or, mask, single);
        }

        const IL::ValueId nonzero = m_builder.CompareValue(IL::CompareOp::UNe, mask,
                                                           m_builder.ConstantU32(0), true);
        const auto scc = WriteScc(nonzero, instruction.pc);
        if (!scc)
            return scc;
        return WriteDest(instruction.operands[0], mask, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftScalarFloatCompare(const DecodedInstruction& instruction,
                                                      bool half,
                                                      IL::CompareOp compare,
                                                      VectorPredicate predicate)
    {
        const auto condition = CompareCondition(
            instruction, half ? CompareValueKind::F16 : CompareValueKind::F32, compare, predicate, 0, 1);
        if (!condition)
            return std::unexpected(condition.error());
        return WriteScc(*condition, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftScalarBitCompare(const DecodedInstruction& instruction)
    {
        const bool wide = instruction.op == Op::S_Bitcmp0B64 || instruction.op == Op::S_Bitcmp1B64;
        const bool set = instruction.op == Op::S_Bitcmp1B32 || instruction.op == Op::S_Bitcmp1B64;

        const auto index = ReadBits(instruction.operands[1], instruction.pc);
        if (!index)
            return std::unexpected(index.error());

        IL::ValueId bit = IL::InvalidValue;
        if (wide)
        {
            const auto value = ReadPairSource(instruction.operands[0], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            const IL::ValueId shift = m_builder.ConvertValue(
                m_module.types.U64(), IL::ConvertOp::ZExt, MaskShiftCount(*index, 63, true), true);
            const IL::ValueId shifted =
                Bin(instruction, IL::BinaryOp::LShr, *value, shift, m_module.types.U64());
            const IL::ValueId masked =
                Bin(instruction, IL::BinaryOp::And, shifted, m_builder.ConstantU64(1),
                    m_module.types.U64());
            bit = m_builder.CompareValue(IL::CompareOp::UNe, masked, m_builder.ConstantU64(0), true);
        }
        else
        {
            const auto value = ReadBits(instruction.operands[0], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            const IL::ValueId shifted =
                Bin(instruction, IL::BinaryOp::LShr, *value, MaskShiftCount(*index, 31, true));
            const IL::ValueId masked =
                Bin(instruction, IL::BinaryOp::And, shifted, m_builder.ConstantU32(1));
            bit = m_builder.CompareValue(IL::CompareOp::UNe, masked, m_builder.ConstantU32(0), true);
        }

        if (!set)
            bit = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), bit, true);
        return WriteScc(bit, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftScalarU64Compare(const DecodedInstruction& instruction)
    {
        const auto left = ReadPairSource(instruction.operands[0], instruction.pc);
        if (!left)
            return std::unexpected(left.error());
        const auto right = ReadPairSource(instruction.operands[1], instruction.pc);
        if (!right)
            return std::unexpected(right.error());
        const IL::CompareOp op =
            instruction.op == Op::S_CmpEqU64 ? IL::CompareOp::UEq : IL::CompareOp::UNe;
        const IL::ValueId condition = m_builder.CompareValue(op, *left, *right, true);
        return WriteScc(condition, instruction.pc);
    }

    // Scalar control instructions: waits, cache hints and mode registers have
    // no effect on the register file in this model; the wave control forms are
    // diagnosed because a single-invocation interpreter cannot honour them.
    [[nodiscard]] Result<void> LiftScalarControl(const DecodedInstruction& instruction)
    {
        switch (instruction.op)
        {
        case Op::S_Sendmsg:
        {
            if (m_module.stage != IL::Stage::Geometry)
                return {};

            // bits[3:0] message, bits[6:4] GS op, bits[9:8] stream.
            const std::uint32_t message = instruction.operands[0].value;
            const std::uint32_t id = message & 0xF;
            const std::uint32_t gsOp = (message >> 4) & 0x7;
            const std::uint32_t stream = (message >> 8) & 0x3;

            if (id == 3) // MSG_GS_DONE
                return {};
            if (id == 9) // MSG_GS_ALLOC_REQ: the allocation is wave state
                return {};

            if (id == 2) // MSG_GS
            {
                if (stream != 0)
                    return std::unexpected(
                        LiftError("SR-GFX-LIFT-063", "geometry streams are not modeled", instruction.pc));
                if (gsOp == 2 || gsOp == 3) // GS_OP_EMIT, GS_OP_EMIT_CUT
                    return EmitStagedGeometryVertex(instruction, gsOp == 3);
                if (gsOp == 1) // GS_OP_CUT
                {
                    m_primitiveOpen = true;
                    return EndPrimitive(instruction.pc, static_cast<std::uint16_t>(instruction.op));
                }
                return {};
            }

            return std::unexpected(LiftError("SR-GFX-LIFT-064",
                                             "unmodeled geometry message " + std::to_string(message),
                                             instruction.pc));
        }
        case Op::S_Sendmsghalt:
        case Op::S_Sethalt:
        case Op::S_Setkill:
        case Op::S_Sleep:
            // Wave-level control has no effect on the single-invocation model.
            return {};
        case Op::S_Trap:
        {
            IL::Terminator terminator;
            terminator.kind = IL::TerminatorKind::Unreachable;
            terminator.sourcePc = instruction.pc;
            m_builder.Terminate(m_builder.CurrentBlock(), std::move(terminator));
            return {};
        }
        case Op::S_Nop:
        case Op::S_Clause:
        case Op::S_DelayAlu:
        case Op::S_WaitAlu:
        case Op::S_Waitcnt:
        case Op::S_WaitIdle:
        case Op::S_WaitEvent:
        case Op::S_RoundMode:
        case Op::S_DenormMode:
        case Op::S_BarrierWait:
        case Op::S_Wakeup:
        case Op::S_Setprio:
        case Op::S_Incperflevel:
        case Op::S_Decperflevel:
        case Op::S_Ttracedata:
        case Op::S_TtracedataImm:
        case Op::S_IcacheInv:
        case Op::S_WaitLoadcnt:
        case Op::S_WaitStorecnt:
        case Op::S_WaitSamplecnt:
        case Op::S_WaitBvhcnt:
        case Op::S_WaitExpcnt:
        case Op::S_WaitDscnt:
        case Op::S_WaitKmcnt:
        case Op::S_WaitLoadcntDscnt:
        case Op::S_WaitStorecntDscnt:
            return {};
        case Op::S_CodeEnd:
        case Op::S_EndpgmSaved:
        {
            IL::Terminator terminator;
            terminator.kind = IL::TerminatorKind::Ret;
            terminator.sourcePc = instruction.pc;
            m_builder.Terminate(m_builder.CurrentBlock(), std::move(terminator));
            return {};
        }
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-073",
                                             std::string("unsupported scalar control instruction: ") +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        }
    }

    [[nodiscard]] Result<void> LiftScalarImmediate(const DecodedInstruction& instruction)
    {
        const std::uint32_t immediate =
            static_cast<std::uint32_t>(static_cast<std::int32_t>(
                static_cast<std::int16_t>(instruction.operands[1].value & 0xFFFFu)));
        switch (instruction.op)
        {
        case Op::S_CmovkI32:
        {
            const auto condition = ReadScc(instruction.pc);
            if (!condition)
                return std::unexpected(condition.error());
            const auto current = ReadSgpr(instruction.operands[0].value, instruction.pc);
            if (!current)
                return std::unexpected(current.error());
            const IL::ValueId selected = m_builder.SelectValue(
                m_module.types.U32(), *condition, m_builder.ConstantU32(immediate), *current, true);
            return WriteDest(instruction.operands[0], selected, instruction.pc);
        }
        case Op::S_AddkCoI32:
        {
            const auto current = ReadSgpr(instruction.operands[0].value, instruction.pc);
            if (!current)
                return std::unexpected(current.error());
            const IL::ValueId sum =
                Bin(instruction, IL::BinaryOp::IAdd, *current, m_builder.ConstantU32(immediate));
            const IL::ValueId carry = m_builder.CompareValue(IL::CompareOp::ULt, sum, *current, true);
            const auto scc = WriteScc(carry, instruction.pc);
            if (!scc)
                return scc;
            return WriteDest(instruction.operands[0], sum, instruction.pc);
        }
        default:
            return std::unexpected(LiftError("SR-GFX-LIFT-074",
                                             std::string("unsupported immediate scalar instruction: ") +
                                                 std::string(ToString(instruction.op)),
                                             instruction.pc));
        }
    }

    // Floating-point classification: bit 0 is a signaling NaN, bit 1 quiet
    // NaN, 2..5 are -inf, -normal, -subnormal, -zero, and 6..9 the positive
    // mirror. The mask is built from the raw exponent and mantissa fields.
    [[nodiscard]] Result<IL::ValueId> BuildClassMask(const DecodedInstruction& instruction,
                                                     IL::ValueId bits,
                                                     CompareValueKind kind)
    {
        const bool wide = kind == CompareValueKind::F64;
        const std::uint32_t exponentBits =
            kind == CompareValueKind::F16 ? 5u : (wide ? 11u : 8u);
        const std::uint32_t mantissaBits =
            kind == CompareValueKind::F16 ? 10u : (wide ? 52u : 23u);
        const std::uint32_t exponentMax = (1u << exponentBits) - 1u;
        const std::uint32_t signShift = exponentBits + mantissaBits;

        const IL::TypeId type = wide ? m_module.types.U64() : m_module.types.U32();
        const auto constant = [&](std::uint64_t value) {
            return wide ? m_builder.ConstantU64(value) : m_builder.ConstantU32(static_cast<std::uint32_t>(value));
        };
        const auto binary = [&](IL::BinaryOp op, IL::ValueId left, IL::ValueId right) {
            return Bin(instruction, op, left, right, type);
        };
        const auto boolean = [&](IL::BinaryOp op, IL::ValueId left, IL::ValueId right) {
            return m_builder.Emit(m_module.types.Bool(),
                                  IL::Op::Binary,
                                  static_cast<std::uint16_t>(op),
                                  {left, right},
                                  {},
                                  IL::NoPayload,
                                  instruction.pc,
                                  static_cast<std::uint16_t>(instruction.op),
                                  false);
        };
        const auto negate = [&](IL::ValueId value) {
            return m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), value, false);
        };
        const auto equal = [&](IL::ValueId left, IL::ValueId right) {
            return m_builder.CompareValue(IL::CompareOp::UEq, left, right, false);
        };

        const IL::ValueId sign = binary(IL::BinaryOp::LShr, bits, constant(signShift));
        const IL::ValueId negative = m_builder.CompareValue(
            IL::CompareOp::UNe, sign, constant(0), false);
        const IL::ValueId exponent =
            binary(IL::BinaryOp::And, binary(IL::BinaryOp::LShr, bits, constant(mantissaBits)),
                   constant(exponentMax));
        const IL::ValueId mantissa =
            binary(IL::BinaryOp::And, bits, constant((std::uint64_t{1} << mantissaBits) - 1));
        const IL::ValueId mantissaZero = equal(mantissa, constant(0));
        const IL::ValueId exponentMaxed = equal(exponent, constant(exponentMax));
        const IL::ValueId exponentZero = equal(exponent, constant(0));

        const IL::ValueId isNaN = boolean(IL::BinaryOp::And, exponentMaxed, negate(mantissaZero));
        const IL::ValueId isInf = boolean(IL::BinaryOp::And, exponentMaxed, mantissaZero);
        const IL::ValueId isZero = boolean(IL::BinaryOp::And, exponentZero, mantissaZero);
        const IL::ValueId isSubnormal =
            boolean(IL::BinaryOp::And, exponentZero, negate(mantissaZero));
        const IL::ValueId isNormal = boolean(
            IL::BinaryOp::And, negate(exponentMaxed), negate(exponentZero));

        const IL::ValueId quietBit = binary(
            IL::BinaryOp::And,
            binary(IL::BinaryOp::LShr, mantissa, constant(mantissaBits - 1u)), constant(1));
        const IL::ValueId quiet = m_builder.CompareValue(
            IL::CompareOp::UNe, quietBit, constant(0), false);
        const IL::ValueId signaling = boolean(IL::BinaryOp::And, isNaN, negate(quiet));
        const IL::ValueId quietNaN = boolean(IL::BinaryOp::And, isNaN, quiet);

        const auto withSign = [&](IL::ValueId value, IL::ValueId isNegative) {
            return boolean(IL::BinaryOp::And, value, isNegative);
        };

        const IL::ValueId classes[] = {
            signaling,
            quietNaN,
            withSign(isInf, negative),
            withSign(isNormal, negative),
            withSign(isSubnormal, negative),
            withSign(isZero, negative),
            withSign(isZero, negate(negative)),
            withSign(isSubnormal, negate(negative)),
            withSign(isNormal, negate(negative)),
            withSign(isInf, negate(negative)),
        };

        IL::ValueId mask = constant(0);
        for (std::uint32_t bit = 0; bit < 10; ++bit)
        {
            const IL::ValueId selected = m_builder.SelectValue(
                type, classes[bit], constant(std::uint64_t{1} << bit), mask, false);
            mask = selected;
        }
        return mask;
    }

    [[nodiscard]] Result<void> LiftClassCompare(const DecodedInstruction& instruction,
                                                CompareValueKind kind,
                                                bool execMask)
    {
        const std::uint8_t bitsIndex = 1;
        const std::uint8_t maskIndex = 2;

        IL::ValueId bits = IL::InvalidValue;
        if (kind == CompareValueKind::F16)
        {
            const auto raw = ReadBits(instruction.operands[bitsIndex], instruction.pc);
            if (!raw)
                return std::unexpected(raw.error());
            bits = Bin(instruction, IL::BinaryOp::And, *raw, m_builder.ConstantU32(0xFFFF));
        }
        else if (kind == CompareValueKind::F32)
        {
            const auto raw = ReadBits(instruction.operands[bitsIndex], instruction.pc);
            if (!raw)
                return std::unexpected(raw.error());
            bits = *raw;
        }
        else
        {
            if (instruction.operands[bitsIndex].kind != OperandKind::Vgpr &&
                instruction.operands[bitsIndex].kind != OperandKind::Vsrc)
                return std::unexpected(
                    LiftError("SR-GFX-LIFT-075", "f64 class source is not a VGPR pair", instruction.pc));
            const auto raw = ReadVgpr64(instruction.operands[bitsIndex].value, instruction.pc);
            if (!raw)
                return std::unexpected(raw.error());
            bits = *raw;
        }

        const auto classMask = BuildClassMask(instruction, bits, kind);
        if (!classMask)
            return std::unexpected(classMask.error());

        const auto wanted = ReadBits(instruction.operands[maskIndex], instruction.pc);
        if (!wanted)
            return std::unexpected(wanted.error());

        const IL::TypeId type = kind == CompareValueKind::F64 ? m_module.types.U64()
                                                              : m_module.types.U32();
        const IL::ValueId matched =
            Bin(instruction, IL::BinaryOp::And, *classMask, *wanted, type);
        const IL::ValueId condition = m_builder.CompareValue(
            IL::CompareOp::UNe, matched,
            kind == CompareValueKind::F64 ? m_builder.ConstantU64(0) : m_builder.ConstantU32(0),
            false);

        if (instruction.encoding == Encoding::Vop3)
        {
            const Operand* destination = FindOperand(instruction, OperandKind::Sdst);
            if (destination != nullptr)
            {
                const IL::ValueId mask =
                    m_builder.SelectValue(m_module.types.U32(), condition,
                                          m_builder.ConstantU32(0xFFFFFFFFu),
                                          m_builder.ConstantU32(0), true);
                return WriteDest(*destination, mask, instruction.pc);
            }
        }

        const auto vcc = WriteVcc(condition, instruction.pc);
        if (!vcc)
            return vcc;
        if (execMask)
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

    [[nodiscard]] Result<void> LiftSwap(const DecodedInstruction& instruction)
    {
        const Operand source = instruction.operands[1];
        if (source.kind != OperandKind::Vgpr && source.kind != OperandKind::Vsrc)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-076", "swap source is not a vector register", instruction.pc));
        const std::uint32_t destination = instruction.operands[0].value;

        const auto destinationValue = ReadVgpr(destination, instruction.pc);
        if (!destinationValue)
            return std::unexpected(destinationValue.error());
        const auto sourceValue = ReadVgpr(source.value, instruction.pc);
        if (!sourceValue)
            return std::unexpected(sourceValue.error());

        IL::ValueId newDestination = *sourceValue;
        IL::ValueId newSource = *destinationValue;
        if (instruction.op == Op::V_SwapB16)
        {
            const IL::ValueId lowMask = m_builder.ConstantU32(0xFFFF);
            const IL::ValueId highMask = m_builder.ConstantU32(0xFFFF0000);
            newDestination = Bin(instruction, IL::BinaryOp::Or,
                                 Bin(instruction, IL::BinaryOp::And, *destinationValue, highMask),
                                 Bin(instruction, IL::BinaryOp::And, *sourceValue, lowMask));
            newSource = Bin(instruction, IL::BinaryOp::Or,
                            Bin(instruction, IL::BinaryOp::And, *sourceValue, highMask),
                            Bin(instruction, IL::BinaryOp::And, *destinationValue, lowMask));
        }

        Operand destinationOperand;
        destinationOperand.kind = OperandKind::Vgpr;
        destinationOperand.value = destination;
        const auto first = WriteDest(destinationOperand, newDestination, instruction.pc);
        if (!first)
            return first;
        Operand sourceOperand;
        sourceOperand.kind = OperandKind::Vgpr;
        sourceOperand.value = source.value;
        return WriteDest(sourceOperand, newSource, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftSatPkU8(const DecodedInstruction& instruction)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        const auto saturate = [&](std::uint32_t shift) {
            const IL::ValueId lane =
                shift == 0 ? Bin(instruction, IL::BinaryOp::And, *source, m_builder.ConstantU32(0xFFFF))
                           : Bin(instruction, IL::BinaryOp::LShr, *source, m_builder.ConstantU32(shift));
            const IL::ValueId signedLane = SignExtend16Value(
                Bin(instruction, IL::BinaryOp::And, lane, m_builder.ConstantU32(0xFFFF)));
            const IL::ValueId clamped = m_builder.SelectValue(
                m_module.types.U32(),
                m_builder.CompareValue(IL::CompareOp::IGt, Bitcast(m_module.types.I32(), signedLane),
                                       m_builder.ConstantI32(255), false),
                m_builder.ConstantU32(255),
                m_builder.SelectValue(
                    m_module.types.U32(),
                    m_builder.CompareValue(IL::CompareOp::ILt, Bitcast(m_module.types.I32(), signedLane),
                                           m_builder.ConstantI32(0), false),
                    m_builder.ConstantU32(0), signedLane, false),
                false);
            return shift == 0 ? clamped
                              : Bin(instruction, IL::BinaryOp::Shl, clamped, m_builder.ConstantU32(8));
        };

        const IL::ValueId packed = Bin(instruction, IL::BinaryOp::Or, saturate(0), saturate(16));
        return WriteDest(instruction.operands[0], packed, instruction.pc);
    }

    // ---- Remaining VOP3 forms ----

    [[nodiscard]] Result<IL::ValueId> ReadLow16(const Operand& operand, std::uint32_t pc)
    {
        const auto value = ReadBits(operand, pc);
        if (!value)
            return std::unexpected(value.error());
        return m_builder.Emit(m_module.types.U32(),
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(IL::BinaryOp::And),
                              {*value, m_builder.ConstantU32(0xFFFF)},
                              {},
                              IL::NoPayload,
                              pc,
                              0,
                              false);
    }

    [[nodiscard]] IL::ValueId MaskLow16(IL::ValueId value, std::uint32_t pc)
    {
        return m_builder.Emit(m_module.types.U32(),
                              IL::Op::Binary,
                              static_cast<std::uint16_t>(IL::BinaryOp::And),
                              {value, m_builder.ConstantU32(0xFFFF)},
                              {},
                              IL::NoPayload,
                              pc,
                              0,
                              false);
    }

    [[nodiscard]] IL::ValueId SignExtendLow16(IL::ValueId value)
    {
        const IL::ValueId shifted = m_builder.Emit(m_module.types.U32(),
                                                   IL::Op::Binary,
                                                   static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                   {value, m_builder.ConstantU32(16)},
                                                   {},
                                                   IL::NoPayload,
                                                   0,
                                                   0,
                                                   false);
        return Bitcast(m_module.types.U32(),
                       m_builder.Emit(m_module.types.I32(),
                                      IL::Op::Binary,
                                      static_cast<std::uint16_t>(IL::BinaryOp::AShr),
                                      {Bitcast(m_module.types.I32(), shifted),
                                       m_builder.ConstantU32(16)},
                                      {},
                                      IL::NoPayload,
                                      0,
                                      0,
                                      false));
    }

    // 16-bit integer arithmetic keeps the operands in the low halves of the
    // source registers and writes the zero-extended 16-bit result.
    [[nodiscard]] Result<void> LiftU16Binary(const DecodedInstruction& instruction,
                                             IL::BinaryOp subop,
                                             bool isSigned)
    {
        const auto first = ReadLow16(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadLow16(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());

        IL::ValueId left = *first;
        IL::ValueId right = *second;
        if (isSigned)
        {
            left = SignExtendLow16(left);
            right = SignExtendLow16(right);
        }
        const IL::ValueId result = Bin(instruction, subop, left, right);
        return WriteDest(instruction.operands[0], MaskLow16(result, instruction.pc), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftU16MinMax(const DecodedInstruction& instruction, bool minimum,
                                             bool isSigned)
    {
        const auto first = ReadLow16(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadLow16(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());

        IL::ValueId left = *first;
        IL::ValueId right = *second;
        if (isSigned)
        {
            left = SignExtendLow16(left);
            right = SignExtendLow16(right);
        }
        const IL::CompareOp less = isSigned ? (minimum ? IL::CompareOp::ILt : IL::CompareOp::IGt)
                                           : (minimum ? IL::CompareOp::ULt : IL::CompareOp::UGt);
        const IL::ValueId condition = m_builder.CompareValue(less, left, right, false);
        const IL::ValueId selected =
            m_builder.SelectValue(m_module.types.U32(), condition, left, right, false);
        return WriteDest(instruction.operands[0], MaskLow16(selected, instruction.pc),
                         instruction.pc);
    }

    [[nodiscard]] Result<void> LiftU16Shift(const DecodedInstruction& instruction, bool left,
                                            bool arithmetic)
    {
        const auto count = ReadLow16(instruction.operands[1], instruction.pc);
        if (!count)
            return std::unexpected(count.error());
        const auto value = ReadLow16(instruction.operands[2], instruction.pc);
        if (!value)
            return std::unexpected(value.error());

        IL::ValueId input = *value;
        if (arithmetic)
            input = SignExtendLow16(input);
        const IL::ValueId shift = MaskShiftCount(*count, 15, false);
        const IL::BinaryOp subop = left ? IL::BinaryOp::Shl
                                        : (arithmetic ? IL::BinaryOp::AShr : IL::BinaryOp::LShr);
        const IL::ValueId result =
            Bin(instruction, subop, input, shift, arithmetic ? m_module.types.I32() : IL::InvalidType);
        return WriteDest(instruction.operands[0], MaskLow16(result, instruction.pc), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMad16(const DecodedInstruction& instruction, bool isSigned,
                                         bool wideResult)
    {
        const auto a = ReadLow16(instruction.operands[1], instruction.pc);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadLow16(instruction.operands[2], instruction.pc);
        if (!b)
            return std::unexpected(b.error());
        const auto c = wideResult ? ReadBits(instruction.operands[3], instruction.pc)
                                  : ReadLow16(instruction.operands[3], instruction.pc);
        if (!c)
            return std::unexpected(c.error());

        IL::ValueId left = *a;
        IL::ValueId right = *b;
        if (isSigned)
        {
            left = SignExtendLow16(left);
            right = SignExtendLow16(right);
        }
        const IL::ValueId product = Bin(instruction, IL::BinaryOp::IMul, left, right);
        const IL::ValueId sum = Bin(instruction, IL::BinaryOp::IAdd, product, *c);
        return WriteDest(instruction.operands[0],
                         wideResult ? sum : MaskLow16(sum, instruction.pc), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMinMax3(const DecodedInstruction& instruction, bool innerMin,
                                           bool outerMin, bool median, bool isFloat,
                                           bool isSigned)
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

        const auto compare = [&](bool minimum, IL::ValueId left, IL::ValueId right) {
            if (isFloat)
            {
                return m_builder.CompareValue(
                    minimum ? IL::CompareOp::FOrdLt : IL::CompareOp::FOrdGt,
                    Bitcast(m_module.types.F32(), left), Bitcast(m_module.types.F32(), right),
                    false);
            }
            return m_builder.CompareValue(
                minimum ? (isSigned ? IL::CompareOp::ILt : IL::CompareOp::ULt)
                        : (isSigned ? IL::CompareOp::IGt : IL::CompareOp::UGt),
                left, right, false);
        };
        const auto choose = [&](bool minimum, IL::ValueId left, IL::ValueId right) {
            return m_builder.SelectValue(m_module.types.U32(), compare(minimum, left, right), left,
                                         right, false);
        };

        const IL::ValueId a = *first;
        const IL::ValueId b = *second;
        const IL::ValueId c = *third;

        IL::ValueId result = a;
        if (median)
        {
            const IL::ValueId low = choose(true, a, b);
            const IL::ValueId high = choose(false, a, b);
            result = choose(true, choose(false, low, c), high);
        }
        else
        {
            const IL::ValueId inner = choose(innerMin, a, b);
            result = choose(outerMin, inner, c);
        }
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // Rounds a float to an integer using round-to-nearest-even, then clamps.
    [[nodiscard]] IL::ValueId RoundToInteger(const DecodedInstruction& instruction,
                                             IL::ValueId value,
                                             std::int64_t low,
                                             std::int64_t high)
    {
        const IL::TypeId f32 = m_module.types.F32();
        const IL::ValueId absValue = m_builder.UnaryValue(IL::UnaryOp::FAbs, f32, value, false);
        const IL::ValueId truncated =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::FPToUI, absValue, false);
        const IL::ValueId truncatedFloat =
            m_builder.ConvertValue(f32, IL::ConvertOp::UIToFP, truncated, false);
        const IL::ValueId fraction =
            Bin(instruction, IL::BinaryOp::FSub, absValue, truncatedFloat, f32);
        const IL::ValueId half = m_builder.ConstantF32(0.5f);
        const IL::ValueId aboveHalf =
            m_builder.CompareValue(IL::CompareOp::FOrdGt, fraction, half, false);
        const IL::ValueId atHalf =
            m_builder.CompareValue(IL::CompareOp::FOrdEq, fraction, half, false);
        const IL::ValueId odd = m_builder.CompareValue(
            IL::CompareOp::UNe,
            Bin(instruction, IL::BinaryOp::And, truncated, m_builder.ConstantU32(1)),
            m_builder.ConstantU32(0), false);
        const IL::ValueId roundUp =
            m_builder.Emit(m_module.types.Bool(),
                           IL::Op::Binary,
                           static_cast<std::uint16_t>(IL::BinaryOp::Or),
                           {aboveHalf,
                            m_builder.Emit(m_module.types.Bool(),
                                           IL::Op::Binary,
                                           static_cast<std::uint16_t>(IL::BinaryOp::And),
                                           {atHalf, odd},
                                           {},
                                           IL::NoPayload,
                                           0,
                                           0,
                                           false)},
                           {},
                           IL::NoPayload,
                           0,
                           0,
                           false);
        const IL::ValueId rounded =
            m_builder.SelectValue(m_module.types.U32(), roundUp,
                                  Bin(instruction, IL::BinaryOp::IAdd, truncated,
                                      m_builder.ConstantU32(1)),
                                  truncated, false);
        const IL::ValueId signedValue = Bitcast(m_module.types.I32(), rounded);
        const IL::ValueId negative =
            m_builder.CompareValue(IL::CompareOp::FOrdLt, value, m_builder.ConstantF32(0.0f), false);
        const IL::ValueId signedRounded = m_builder.SelectValue(
            m_module.types.I32(), negative,
            Bin(instruction, IL::BinaryOp::ISub, m_builder.ConstantU32(0), rounded,
                m_module.types.I32()),
            signedValue, false);
        const IL::ValueId clampedLow = m_builder.SelectValue(
            m_module.types.I32(),
            m_builder.CompareValue(IL::CompareOp::ILt, signedRounded,
                                   m_builder.ConstantI32(static_cast<std::int32_t>(low)), false),
            m_builder.ConstantI32(static_cast<std::int32_t>(low)), signedRounded, false);
        (void)high;
        return clampedLow;
    }

    enum class PkConvert
    {
        I16F32,
        U16F32,
        I16I32,
        U16U32,
        NormI16F32,
        NormU16F32,
        NormI16F16,
        NormU16F16,
        RtzF16F32,
    };

    [[nodiscard]] Result<void> LiftPkConvert(const DecodedInstruction& instruction, PkConvert kind)
    {
        const bool halfSource = kind == PkConvert::NormI16F16 || kind == PkConvert::NormU16F16;

        const auto lane = [&](std::uint8_t index) -> Result<IL::ValueId> {
            const Operand& operand = instruction.operands[index];
            switch (kind)
            {
            case PkConvert::I16I32:
                return ReadSigned(operand, instruction.pc);
            case PkConvert::U16U32:
                return ReadBits(operand, instruction.pc);
            case PkConvert::RtzF16F32:
            case PkConvert::I16F32:
            case PkConvert::U16F32:
            case PkConvert::NormI16F32:
            case PkConvert::NormU16F32:
                return ReadFloatSource(instruction, index);
            default:
                break;
            }
            const auto bits = ReadBits(operand, instruction.pc);
            if (!bits)
                return std::unexpected(bits.error());
            const IL::ValueId low =
                m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, *bits, false);
            return Bitcast(F16Type(), low);
        };

        const auto first = lane(1);
        if (!first)
            return std::unexpected(first.error());
        const auto second = lane(2);
        if (!second)
            return std::unexpected(second.error());

        const auto convert = [&](IL::ValueId value) -> IL::ValueId {
            switch (kind)
            {
            case PkConvert::I16I32:
            {
                const IL::ValueId clamped = m_builder.SelectValue(
                    m_module.types.I32(),
                    m_builder.CompareValue(IL::CompareOp::IGt, value,
                                           m_builder.ConstantI32(32767), false),
                    m_builder.ConstantI32(32767),
                    m_builder.SelectValue(
                        m_module.types.I32(),
                        m_builder.CompareValue(IL::CompareOp::ILt, value,
                                               m_builder.ConstantI32(-32768), false),
                        m_builder.ConstantI32(-32768), value, false),
                    false);
                return MaskLow16(Bitcast(m_module.types.U32(), clamped), instruction.pc);
            }
            case PkConvert::U16U32:
            {
                const IL::ValueId clamped = m_builder.SelectValue(
                    m_module.types.U32(),
                    m_builder.CompareValue(IL::CompareOp::UGt, value,
                                           m_builder.ConstantU32(0xFFFF), false),
                    m_builder.ConstantU32(0xFFFF), value, false);
                return MaskLow16(clamped, instruction.pc);
            }
            case PkConvert::I16F32:
            case PkConvert::U16F32:
            {
                const bool isSigned = kind == PkConvert::I16F32;
                const IL::ValueId rounded = RoundToInteger(
                    instruction, value, isSigned ? -32768 : 0, isSigned ? 32767 : 65535);
                const IL::ValueId clamped = m_builder.SelectValue(
                    m_module.types.I32(),
                    m_builder.CompareValue(
                        IL::CompareOp::IGt, rounded,
                        m_builder.ConstantI32(isSigned ? 32767 : 65535), false),
                    m_builder.ConstantI32(isSigned ? 32767 : 65535),
                    m_builder.SelectValue(
                        m_module.types.I32(),
                        m_builder.CompareValue(IL::CompareOp::ILt, rounded,
                                               m_builder.ConstantI32(isSigned ? -32768 : 0), false),
                        m_builder.ConstantI32(isSigned ? -32768 : 0), rounded, false),
                    false);
                return MaskLow16(Bitcast(m_module.types.U32(), clamped), instruction.pc);
            }
            case PkConvert::NormI16F32:
            case PkConvert::NormU16F32:
            case PkConvert::NormI16F16:
            case PkConvert::NormU16F16:
            {
                const bool isSigned =
                    kind == PkConvert::NormI16F32 || kind == PkConvert::NormI16F16;
                const IL::TypeId type = halfSource ? F16Type() : m_module.types.F32();
                const float multiplier = isSigned ? 32767.0f : 65535.0f;
                const IL::ValueId scale = halfSource
                                              ? Bitcast(F16Type(), m_builder.ConstantU32(0x3C00))
                                              : m_builder.ConstantF32(1.0f);
                IL::ValueId clamped = value;
                const IL::ValueId upper = halfSource
                                              ? Bitcast(F16Type(), m_builder.ConstantU32(0x3C00))
                                              : m_builder.ConstantF32(1.0f);
                const IL::ValueId lower = isSigned
                                              ? (halfSource ? Bitcast(F16Type(), m_builder.ConstantU32(0xBC00))
                                                            : m_builder.ConstantF32(-1.0f))
                                              : (halfSource ? Bitcast(F16Type(), m_builder.ConstantU32(0))
                                                            : m_builder.ConstantF32(0.0f));
                clamped = m_builder.SelectValue(
                    type,
                    m_builder.CompareValue(IL::CompareOp::FOrdGt, clamped, upper, false),
                    upper, clamped, false);
                clamped = m_builder.SelectValue(
                    type,
                    m_builder.CompareValue(IL::CompareOp::FOrdLt, clamped, lower, false),
                    lower, clamped, false);
                const IL::ValueId widened =
                    halfSource ? m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::FPExt,
                                                        clamped, false)
                               : clamped;
                (void)scale;
                const IL::ValueId scaled =
                    Bin(instruction, IL::BinaryOp::FMul, widened,
                        m_builder.ConstantF32(multiplier), m_module.types.F32());
                const IL::ValueId rounded = RoundToInteger(
                    instruction, scaled, isSigned ? -32768 : 0, isSigned ? 32767 : 65535);
                const IL::ValueId clampedInt = m_builder.SelectValue(
                    m_module.types.I32(),
                    m_builder.CompareValue(IL::CompareOp::IGt, rounded,
                                           m_builder.ConstantI32(isSigned ? 32767 : 65535), false),
                    m_builder.ConstantI32(isSigned ? 32767 : 65535),
                    m_builder.SelectValue(
                        m_module.types.I32(),
                        m_builder.CompareValue(IL::CompareOp::ILt, rounded,
                                               m_builder.ConstantI32(isSigned ? -32768 : 0), false),
                        m_builder.ConstantI32(isSigned ? -32768 : 0), rounded, false),
                    false);
                return MaskLow16(Bitcast(m_module.types.U32(), clampedInt), instruction.pc);
            }
            case PkConvert::RtzF16F32:
            {
                const IL::ValueId rounded =
                    m_builder.ConvertValue(F16Type(), IL::ConvertOp::FPTrunc, value, false);
                const IL::ValueId roundedWidened =
                    m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::FPExt, rounded, false);
                const IL::ValueId absSource =
                    m_builder.UnaryValue(IL::UnaryOp::FAbs, m_module.types.F32(), value, false);
                const IL::ValueId absRounded =
                    m_builder.UnaryValue(IL::UnaryOp::FAbs, m_module.types.F32(), roundedWidened, false);
                const IL::ValueId tooLarge = m_builder.CompareValue(
                    IL::CompareOp::FOrdGt, absRounded, absSource, false);
                const IL::ValueId bits =
                    m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                           Bitcast(U16Type(), rounded), false);
                const IL::ValueId magnitude =
                    Bin(instruction, IL::BinaryOp::And, bits, m_builder.ConstantU32(0x7FFF));
                const IL::ValueId stepped = m_builder.SelectValue(
                    m_module.types.U32(), tooLarge,
                    Bin(instruction, IL::BinaryOp::ISub, bits, m_builder.ConstantU32(1)), bits, false);
                (void)magnitude;
                return MaskLow16(stepped, instruction.pc);
            }
            }
            return IL::InvalidValue;
        };

        const IL::ValueId low = convert(*first);
        const IL::ValueId high = convert(*second);
        const IL::ValueId packed = Bin(instruction, IL::BinaryOp::Or, low,
                                       Bin(instruction, IL::BinaryOp::Shl, high,
                                           m_builder.ConstantU32(16)));
        return WriteDest(instruction.operands[0], packed, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftPackB32F16(const DecodedInstruction& instruction)
    {
        const auto first = ReadF16(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadF16(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const IL::ValueId low =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                   Bitcast(U16Type(), *first), false);
        const IL::ValueId high =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                   Bitcast(U16Type(), *second), false);
        return WriteDest(instruction.operands[0],
                         Bin(instruction, IL::BinaryOp::Or, low,
                             Bin(instruction, IL::BinaryOp::Shl, high,
                                 m_builder.ConstantU32(16))),
                         instruction.pc);
    }

    [[nodiscard]] Result<void> LiftSadU8(const DecodedInstruction& instruction, bool high,
                                         bool masked)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto addend = ReadBits(instruction.operands[3], instruction.pc);
        if (!addend)
            return std::unexpected(addend.error());

        IL::ValueId sum = m_builder.ConstantU32(0);
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            const IL::ValueId shift = m_builder.ConstantU32(8 * i);
            const IL::ValueId a =
                Bin(instruction, IL::BinaryOp::And,
                    Bin(instruction, IL::BinaryOp::LShr, *first, shift), m_builder.ConstantU32(0xFF));
            const IL::ValueId b =
                Bin(instruction, IL::BinaryOp::And,
                    Bin(instruction, IL::BinaryOp::LShr, *second, shift), m_builder.ConstantU32(0xFF));
            const IL::ValueId difference = m_builder.SelectValue(
                m_module.types.U32(),
                m_builder.CompareValue(IL::CompareOp::UGe, a, b, false),
                Bin(instruction, IL::BinaryOp::ISub, a, b), Bin(instruction, IL::BinaryOp::ISub, b, a),
                false);
            if (masked)
            {
                const IL::ValueId included = m_builder.CompareValue(
                    IL::CompareOp::UNe, b, m_builder.ConstantU32(0), false);
                sum = Bin(instruction, IL::BinaryOp::IAdd, sum,
                          m_builder.SelectValue(m_module.types.U32(), included, difference,
                                                m_builder.ConstantU32(0), false));
            }
            else
            {
                sum = Bin(instruction, IL::BinaryOp::IAdd, sum, difference);
            }
        }
        IL::ValueId shifted = sum;
        if (high)
            shifted = Bin(instruction, IL::BinaryOp::Shl, sum, m_builder.ConstantU32(16));
        return WriteDest(instruction.operands[0],
                         Bin(instruction, IL::BinaryOp::IAdd, shifted, *addend), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftLerpU8(const DecodedInstruction& instruction)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto control = ReadBits(instruction.operands[3], instruction.pc);
        if (!control)
            return std::unexpected(control.error());

        IL::ValueId result = m_builder.ConstantU32(0);
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            const IL::ValueId shift = m_builder.ConstantU32(8 * i);
            const auto byteAt = [&](IL::ValueId value) {
                return Bin(instruction, IL::BinaryOp::And,
                           Bin(instruction, IL::BinaryOp::LShr, value, shift),
                           m_builder.ConstantU32(0xFF));
            };
            const IL::ValueId round =
                Bin(instruction, IL::BinaryOp::And, byteAt(*control), m_builder.ConstantU32(1));
            const IL::ValueId total = Bin(
                instruction, IL::BinaryOp::IAdd,
                Bin(instruction, IL::BinaryOp::IAdd, byteAt(*first), byteAt(*second)), round);
            const IL::ValueId averaged =
                Bin(instruction, IL::BinaryOp::LShr, total, m_builder.ConstantU32(1));
            result = Bin(instruction, IL::BinaryOp::Or, result,
                         Bin(instruction, IL::BinaryOp::Shl, averaged, shift));
        }
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftCube(const DecodedInstruction& instruction, std::uint8_t mode)
    {
        const auto x = ReadFloat(instruction.operands[1], instruction.pc);
        if (!x)
            return std::unexpected(x.error());
        const auto y = ReadFloat(instruction.operands[2], instruction.pc);
        if (!y)
            return std::unexpected(y.error());
        const auto z = ReadFloat(instruction.operands[3], instruction.pc);
        if (!z)
            return std::unexpected(z.error());

        const IL::TypeId f32 = m_module.types.F32();
        const auto abs = [&](IL::ValueId value) {
            return m_builder.UnaryValue(IL::UnaryOp::FAbs, f32, value, false);
        };
        const auto greaterOrEqual = [&](IL::ValueId left, IL::ValueId right) {
            return m_builder.CompareValue(IL::CompareOp::FOrdGe, left, right, false);
        };
        const auto greater = [&](IL::ValueId left, IL::ValueId right) {
            return m_builder.CompareValue(IL::CompareOp::FOrdGt, left, right, false);
        };
        const auto negate = [&](IL::ValueId value) {
            return m_builder.UnaryValue(IL::UnaryOp::Neg, f32, value, false);
        };

        const IL::ValueId ax = abs(*x);
        const IL::ValueId ay = abs(*y);
        const IL::ValueId az = abs(*z);

        const IL::ValueId xMajor = m_builder.Emit(
            m_module.types.Bool(), IL::Op::Binary, static_cast<std::uint16_t>(IL::BinaryOp::And),
            {greaterOrEqual(ax, ay), greaterOrEqual(ax, az)}, {}, IL::NoPayload, 0, 0, false);
        const IL::ValueId yMajor = greaterOrEqual(ay, az);
        const IL::ValueId xPositive = greater(*x, m_builder.ConstantF32(0.0f));
        const IL::ValueId yPositive = greater(*y, m_builder.ConstantF32(0.0f));
        const IL::ValueId zPositive = greater(*z, m_builder.ConstantF32(0.0f));

        const IL::ValueId major = m_builder.SelectValue(
            f32, xMajor, *x,
            m_builder.SelectValue(f32, yMajor, *y, *z, false), false);

        if (mode == 0)
        {
            const IL::ValueId id = m_builder.SelectValue(
                m_module.types.U32(), xMajor,
                m_builder.SelectValue(m_module.types.U32(), xPositive, m_builder.ConstantU32(0),
                                      m_builder.ConstantU32(1), false),
                m_builder.SelectValue(
                    m_module.types.U32(), yMajor,
                    m_builder.SelectValue(m_module.types.U32(), yPositive, m_builder.ConstantU32(2),
                                          m_builder.ConstantU32(3), false),
                    m_builder.SelectValue(m_module.types.U32(), zPositive,
                                          m_builder.ConstantU32(4), m_builder.ConstantU32(5), false),
                    false),
                false);
            return WriteDest(instruction.operands[0], id, instruction.pc);
        }
        if (mode == 1)
        {
            const IL::ValueId doubled = Bin(instruction, IL::BinaryOp::FMul, major,
                                            m_builder.ConstantF32(2.0f), f32);
            return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), doubled),
                             instruction.pc);
        }

        // S and T are the two components that follow the major axis, with the
        // sign convention of the classic cubemap lookup.
        const IL::ValueId sc = m_builder.SelectValue(
            f32, xMajor,
            m_builder.SelectValue(f32,
                                  m_builder.Emit(m_module.types.Bool(), IL::Op::Binary,
                                                 static_cast<std::uint16_t>(IL::BinaryOp::And),
                                                 {xMajor, xPositive}, {}, IL::NoPayload, 0, 0,
                                                 false),
                                  negate(*z), *z, false),
            m_builder.SelectValue(f32, yMajor, *x,
                                  m_builder.SelectValue(f32, zPositive, *x, negate(*x), false), false),
            false);
        const IL::ValueId tc = m_builder.SelectValue(
            f32, xMajor, negate(*y),
            m_builder.SelectValue(f32, yMajor,
                                  m_builder.SelectValue(f32, yPositive, *z, negate(*z), false),
                                  negate(*y), false),
            false);
        const IL::ValueId value = mode == 2 ? sc : tc;
        return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), value),
                         instruction.pc);
    }

    [[nodiscard]] Result<void> LiftDivFixup(const DecodedInstruction& instruction)
    {
        const IL::TypeId f32 = m_module.types.F32();
        const auto quotient = ReadFloatSource(instruction, 1);
        if (!quotient)
            return std::unexpected(quotient.error());
        const auto denominator = ReadFloatSource(instruction, 2);
        if (!denominator)
            return std::unexpected(denominator.error());
        const auto numerator = ReadFloatSource(instruction, 3);
        if (!numerator)
            return std::unexpected(numerator.error());

        const IL::ValueId zero = m_builder.ConstantF32(0.0f);
        const IL::ValueId nan = m_builder.ConstantF32(std::numeric_limits<float>::quiet_NaN());
        const IL::ValueId inf = m_builder.ConstantF32(std::numeric_limits<float>::infinity());
        const auto isZero = [&](IL::ValueId value) {
            return m_builder.CompareValue(IL::CompareOp::FOrdEq, value, zero, false);
        };
        const auto isInf = [&](IL::ValueId value) {
            return m_builder.CompareValue(IL::CompareOp::FOrdEq, value, inf, false);
        };
        const auto isNan = [&](IL::ValueId value) {
            return m_builder.UnaryValue(
                IL::UnaryOp::Not,
                m_module.types.Bool(),
                m_builder.CompareValue(IL::CompareOp::FOrdEq, value, value, false), false);
        };
        const auto negative = [&](IL::ValueId value) {
            return m_builder.CompareValue(IL::CompareOp::FOrdLt, value, zero, false);
        };
        const auto make = [&](IL::ValueId magnitude, IL::ValueId negativeSource) {
            const IL::ValueId negativeMagnitude =
                m_builder.UnaryValue(IL::UnaryOp::Neg, f32, magnitude, false);
            return m_builder.SelectValue(f32, negative(negativeSource), negativeMagnitude, magnitude,
                                         false);
        };
        const auto both = [&](IL::ValueId left, IL::ValueId right) {
            return m_builder.Emit(m_module.types.Bool(), IL::Op::Binary,
                                  static_cast<std::uint16_t>(IL::BinaryOp::And), {left, right}, {},
                                  IL::NoPayload, 0, 0, false);
        };
        const auto either = [&](IL::ValueId left, IL::ValueId right) {
            return m_builder.Emit(m_module.types.Bool(), IL::Op::Binary,
                                  static_cast<std::uint16_t>(IL::BinaryOp::Or), {left, right}, {},
                                  IL::NoPayload, 0, 0, false);
        };

        IL::ValueId result = *quotient;
        // denominator zero: 0/0 is NaN, otherwise the quotient is an infinity
        // with the sign of the numerator.
        const IL::ValueId denominatorZero = isZero(*denominator);
        const IL::ValueId zeroOverZero = both(denominatorZero, isZero(*numerator));
        const IL::ValueId divideByZero = both(denominatorZero, isZero(*numerator) == 0
                                                                  ? m_builder.ConstantBool(true)
                                                                  : m_builder.ConstantBool(true));
        (void)divideByZero;
        result = m_builder.SelectValue(f32, denominatorZero,
                                       m_builder.SelectValue(f32, isZero(*numerator), nan,
                                                             make(inf, *numerator), false),
                                       result, false);
        // infinite inputs
        const IL::ValueId infiniteDenominator = isInf(*denominator);
        const IL::ValueId infiniteNumerator = isInf(*numerator);
        result = m_builder.SelectValue(
            f32, infiniteDenominator,
            m_builder.SelectValue(f32, infiniteNumerator, nan, make(zero, *quotient), false),
            result, false);
        result = m_builder.SelectValue(f32, infiniteNumerator, make(inf, *quotient), result, false);
        // NaN propagates.
        result = m_builder.SelectValue(f32, either(isNan(*denominator), isNan(*numerator)), nan,
                                       result, false);
        (void)zeroOverZero;
        return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result),
                         instruction.pc);
    }

    [[nodiscard]] Result<void> LiftMulLit(const DecodedInstruction& instruction)
    {
        const IL::TypeId f32 = m_module.types.F32();
        const auto first = ReadFloatSource(instruction, 1);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadFloatSource(instruction, 2);
        if (!second)
            return std::unexpected(second.error());
        const IL::ValueId zero = m_builder.ConstantF32(0.0f);
        const IL::ValueId product = Bin(instruction, IL::BinaryOp::FMul, *first, *second, f32);
        const IL::ValueId eitherZero = m_builder.Emit(
            m_module.types.Bool(), IL::Op::Binary, static_cast<std::uint16_t>(IL::BinaryOp::Or),
            {m_builder.CompareValue(IL::CompareOp::FOrdEq, *first, zero, false),
             m_builder.CompareValue(IL::CompareOp::FOrdEq, *second, zero, false)},
            {}, IL::NoPayload, 0, 0, false);
        const IL::ValueId result = m_builder.SelectValue(f32, eitherZero, zero, product, false);
        return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result),
                         instruction.pc);
    }

    [[nodiscard]] Result<void> LiftFmaDx9(const DecodedInstruction& instruction)
    {
        const IL::TypeId f32 = m_module.types.F32();
        const auto a = ReadFloatSource(instruction, 1);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadFloatSource(instruction, 2);
        if (!b)
            return std::unexpected(b.error());
        const auto c = ReadFloatSource(instruction, 3);
        if (!c)
            return std::unexpected(c.error());
        const IL::ValueId zero = m_builder.ConstantF32(0.0f);
        const IL::ValueId fused = m_builder.Emit(f32, IL::Op::Ternary,
                                                 static_cast<std::uint16_t>(IL::TernaryOp::Fma),
                                                 {*a, *b, *c}, {}, IL::NoPayload, 0, 0, false);
        const IL::ValueId eitherZero = m_builder.Emit(
            m_module.types.Bool(), IL::Op::Binary, static_cast<std::uint16_t>(IL::BinaryOp::Or),
            {m_builder.CompareValue(IL::CompareOp::FOrdEq, *a, zero, false),
             m_builder.CompareValue(IL::CompareOp::FOrdEq, *b, zero, false)},
            {}, IL::NoPayload, 0, 0, false);
        const IL::ValueId result = m_builder.SelectValue(f32, eitherZero, *c, fused, false);
        return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result),
                         instruction.pc);
    }

    [[nodiscard]] Result<void> LiftDivFmas(const DecodedInstruction& instruction)
    {
        const IL::TypeId f32 = m_module.types.F32();
        const auto a = ReadFloatSource(instruction, 1);
        if (!a)
            return std::unexpected(a.error());
        const auto b = ReadFloatSource(instruction, 2);
        if (!b)
            return std::unexpected(b.error());
        const auto c = ReadFloatSource(instruction, 3);
        if (!c)
            return std::unexpected(c.error());
        IL::ValueId fused = m_builder.Emit(f32, IL::Op::Ternary,
                                           static_cast<std::uint16_t>(IL::TernaryOp::Fma),
                                           {*a, *b, *c}, {}, IL::NoPayload, 0, 0, false);
        const auto vcc = ReadVcc(instruction.pc);
        if (!vcc)
            return std::unexpected(vcc.error());
        const IL::ValueId scaled =
            Bin(instruction, IL::BinaryOp::FMul, fused,
                m_builder.ConstantF32(18446744073709551616.0f), f32);
        const IL::ValueId result = m_builder.SelectValue(f32, *vcc, scaled, fused, false);
        return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), result),
                         instruction.pc);
    }

    // V_PK_FMAC_F16: destination = source0 * destination + destination, per
    // half of the packed pair.
    [[nodiscard]] Result<void> LiftPackedFmac(const DecodedInstruction& instruction)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());
        const auto current = ReadVgpr(instruction.operands[0].value, instruction.pc);
        if (!current)
            return std::unexpected(current.error());

        const auto lane = [&](std::uint32_t shift) {
            const auto extract = [&](IL::ValueId value) {
                const IL::ValueId shifted =
                    shift == 0 ? value
                               : Bin(instruction, IL::BinaryOp::LShr, value,
                                     m_builder.ConstantU32(shift));
                const IL::ValueId low =
                    m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, shifted, false);
                return Bitcast(F16Type(), low);
            };
            const IL::ValueId accumulator = extract(*current);
            const IL::ValueId product =
                Bin(instruction, IL::BinaryOp::FMul, extract(*source), accumulator, F16Type());
            const IL::ValueId sum =
                Bin(instruction, IL::BinaryOp::FAdd, product, accumulator, F16Type());
            const IL::ValueId bits = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                                            Bitcast(U16Type(), sum), false);
            return shift == 0
                       ? bits
                       : Bin(instruction, IL::BinaryOp::Shl, bits, m_builder.ConstantU32(16));
        };

        const IL::ValueId result = Bin(instruction, IL::BinaryOp::Or, lane(0), lane(16));
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // ---- VOP3P dot products and mixed-precision FMA ----

    [[nodiscard]] IL::ValueId HalfLaneAsFloat(const DecodedInstruction& instruction,
                                              IL::ValueId packed,
                                              std::uint32_t shift,
                                              bool bfloat)
    {
        const IL::ValueId shifted =
            shift == 0 ? packed
                       : Bin(instruction, IL::BinaryOp::LShr, packed, m_builder.ConstantU32(shift));
        const IL::ValueId low =
            m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, shifted, false);
        if (bfloat)
        {
            // bfloat16 widens to f32 by placing its bits in the high half.
            const IL::ValueId widened =
                m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt, low, false);
            return Bitcast(m_module.types.F32(),
                           Bin(instruction, IL::BinaryOp::Shl, widened, m_builder.ConstantU32(16)));
        }
        return m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::FPExt,
                                      Bitcast(F16Type(), low), false);
    }

    [[nodiscard]] Result<void> LiftDot2F32(const DecodedInstruction& instruction, bool bfloat)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto accumulator = ReadFloatSource(instruction, 3);
        if (!accumulator)
            return std::unexpected(accumulator.error());

        const IL::TypeId f32 = m_module.types.F32();
        IL::ValueId sum = *accumulator;
        for (std::uint32_t lane = 0; lane < 2; ++lane)
        {
            const std::uint32_t shift = 16 * lane;
            const IL::ValueId product =
                Bin(instruction, IL::BinaryOp::FMul,
                    HalfLaneAsFloat(instruction, *first, shift, bfloat),
                    HalfLaneAsFloat(instruction, *second, shift, bfloat), f32);
            sum = Bin(instruction, IL::BinaryOp::FAdd, sum, product, f32);
        }
        return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), sum),
                         instruction.pc);
    }

    [[nodiscard]] Result<void> LiftDot2F16(const DecodedInstruction& instruction, bool bfloat)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto accumulator = ReadBits(instruction.operands[3], instruction.pc);
        if (!accumulator)
            return std::unexpected(accumulator.error());

        const IL::TypeId type = F16Type();
        const auto lane = [&](IL::ValueId value, std::uint32_t shift) {
            const IL::ValueId bits =
                shift == 0 ? value
                           : Bin(instruction, IL::BinaryOp::LShr, value,
                                 m_builder.ConstantU32(shift));
            const IL::ValueId low =
                m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, bits, false);
            if (bfloat)
                return Bitcast(type, m_builder.ConstantU32(0));
            return Bitcast(type, low);
        };
        IL::ValueId sum = lane(*accumulator, 0);
        for (std::uint32_t i = 0; i < 2; ++i)
        {
            const IL::ValueId product =
                Bin(instruction, IL::BinaryOp::FMul, lane(*first, 16 * i), lane(*second, 16 * i),
                    type);
            sum = Bin(instruction, IL::BinaryOp::FAdd, sum, product, type);
        }
        if (bfloat)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-080", "packed bfloat dot is not implemented",
                          instruction.pc));
        const IL::ValueId bits = m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                                        Bitcast(U16Type(), sum), false);
        return WriteDest(instruction.operands[0], bits, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftDotInteger(const DecodedInstruction& instruction,
                                              std::uint32_t elements,
                                              std::uint32_t elementBits,
                                              bool signedFirst,
                                              bool signedSecond)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto accumulator = ReadBits(instruction.operands[3], instruction.pc);
        if (!accumulator)
            return std::unexpected(accumulator.error());

        const std::uint32_t mask = (1u << elementBits) - 1u;
        IL::ValueId sum = *accumulator;
        for (std::uint32_t i = 0; i < elements; ++i)
        {
            const IL::ValueId shift = m_builder.ConstantU32(elementBits * i);
            const IL::ValueId lhs =
                Bin(instruction, IL::BinaryOp::And,
                    Bin(instruction, IL::BinaryOp::LShr, *first, shift), m_builder.ConstantU32(mask));
            const IL::ValueId rhs =
                Bin(instruction, IL::BinaryOp::And,
                    Bin(instruction, IL::BinaryOp::LShr, *second, shift),
                    m_builder.ConstantU32(mask));

            const auto widen = [&](IL::ValueId value, bool isSigned) {
                if (elementBits == 8)
                    return isSigned ? SignExtend8Value(value) : value;
                const IL::ValueId low = Bin(instruction, IL::BinaryOp::And, value,
                                            m_builder.ConstantU32(0xF));
                if (!isSigned)
                    return low;
                const IL::ValueId signedLow = Bitcast(m_module.types.I32(), low);
                const IL::ValueId negative = Bin(
                    instruction, IL::BinaryOp::And, signedLow, m_builder.ConstantI32(0x8));
                const IL::ValueId extended =
                    Bitcast(m_module.types.U32(),
                            Bin(instruction, IL::BinaryOp::Or, signedLow,
                                m_builder.ConstantI32(-16)));
                return m_builder.SelectValue(
                    m_module.types.U32(),
                    m_builder.CompareValue(IL::CompareOp::INe, negative, m_builder.ConstantI32(0),
                                           false),
                    extended, low, false);
            };

            const IL::ValueId product = Bin(instruction, IL::BinaryOp::IMul,
                                            widen(lhs, signedFirst), widen(rhs, signedSecond));
            sum = Bin(instruction, IL::BinaryOp::IAdd, sum, product);
        }
        return WriteDest(instruction.operands[0], sum, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftFmaMix(const DecodedInstruction& instruction, std::uint8_t mode)
    {
        const auto firstBits = ReadBits(instruction.operands[1], instruction.pc);
        if (!firstBits)
            return std::unexpected(firstBits.error());
        const auto secondBits = ReadBits(instruction.operands[2], instruction.pc);
        if (!secondBits)
            return std::unexpected(secondBits.error());
        const auto thirdBits = ReadBits(instruction.operands[3], instruction.pc);
        if (!thirdBits)
            return std::unexpected(thirdBits.error());

        // opSel picks the half used for each of the two half-precision sources.
        const std::uint32_t firstShift = ((instruction.opSel >> 0) & 1u) * 16u;
        const std::uint32_t secondShift = ((instruction.opSel >> 1) & 1u) * 16u;

        const IL::TypeId f32 = m_module.types.F32();
        if (mode == 0)
        {
            const IL::ValueId product =
                Bin(instruction, IL::BinaryOp::FMul,
                    HalfLaneAsFloat(instruction, *firstBits, firstShift, false),
                    HalfLaneAsFloat(instruction, *secondBits, secondShift, false), f32);
            const IL::ValueId sum =
                Bin(instruction, IL::BinaryOp::FAdd, product, Bitcast(f32, *thirdBits), f32);
            return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), sum),
                             instruction.pc);
        }

        const bool high = mode == 2;
        const IL::ValueId accumulator =
            high ? Bin(instruction, IL::BinaryOp::LShr, *thirdBits, m_builder.ConstantU32(16))
                 : *thirdBits;
        const IL::ValueId accumulatorLow =
            m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, accumulator, false);
        const IL::ValueId accumulatorHalf = Bitcast(F16Type(), accumulatorLow);
        const IL::ValueId product =
            Bin(instruction, IL::BinaryOp::FMul,
                HalfLaneAsFloatToHalf(instruction, *firstBits, firstShift),
                HalfLaneAsFloatToHalf(instruction, *secondBits, secondShift), F16Type());
        const IL::ValueId sum =
            Bin(instruction, IL::BinaryOp::FAdd, product, accumulatorHalf, F16Type());
        const IL::ValueId resultBits =
            m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                   Bitcast(U16Type(), sum), false);

        const auto current = ReadVgpr(instruction.operands[0].value, instruction.pc);
        if (!current)
            return std::unexpected(current.error());
        const IL::ValueId merged =
            high ? Bin(instruction, IL::BinaryOp::Or,
                       Bin(instruction, IL::BinaryOp::And, *current, m_builder.ConstantU32(0xFFFF)),
                       Bin(instruction, IL::BinaryOp::Shl, resultBits, m_builder.ConstantU32(16)))
                 : Bin(instruction, IL::BinaryOp::Or,
                       Bin(instruction, IL::BinaryOp::And, *current,
                           m_builder.ConstantU32(0xFFFF0000)),
                       resultBits);
        return WriteDest(instruction.operands[0], merged, instruction.pc);
    }

    [[nodiscard]] IL::ValueId HalfLaneAsFloatToHalf(const DecodedInstruction& instruction,
                                                    IL::ValueId packed,
                                                    std::uint32_t shift)
    {
        const IL::ValueId shifted =
            shift == 0 ? packed
                       : Bin(instruction, IL::BinaryOp::LShr, packed, m_builder.ConstantU32(shift));
        const IL::ValueId low =
            m_builder.ConvertValue(U16Type(), IL::ConvertOp::Trunc, shifted, false);
        return Bitcast(F16Type(), low);
    }

    // 8-bit float formats: fp8 is E4M3 and bf8 is E5M2. Decoding to f32 needs
    // no rounding, so it is built from the exponent and mantissa fields.
    [[nodiscard]] IL::ValueId DecodeFp8(const DecodedInstruction& instruction, IL::ValueId byte,
                                        bool bf8)
    {
        const std::uint32_t mantissaBits = bf8 ? 2u : 3u;
        const std::uint32_t exponentBits = bf8 ? 5u : 4u;
        const std::uint32_t bias = bf8 ? 15u : 7u;
        const std::uint32_t exponentMax = (1u << exponentBits) - 1u;
        const std::uint32_t mantissaMask = (1u << mantissaBits) - 1u;
        const IL::TypeId f32 = m_module.types.F32();

        const IL::ValueId sign = Bin(instruction, IL::BinaryOp::And,
                                     Bin(instruction, IL::BinaryOp::LShr, byte,
                                         m_builder.ConstantU32(7)),
                                     m_builder.ConstantU32(1));
        const IL::ValueId exponentBitsValue =
            Bin(instruction, IL::BinaryOp::And,
                Bin(instruction, IL::BinaryOp::LShr, byte, m_builder.ConstantU32(mantissaBits)),
                m_builder.ConstantU32(exponentMax));
        const IL::ValueId mantissa =
            Bin(instruction, IL::BinaryOp::And, byte, m_builder.ConstantU32(mantissaMask));

        const IL::ValueId isZeroExponent = m_builder.CompareValue(
            IL::CompareOp::UEq, exponentBitsValue, m_builder.ConstantU32(0), false);
        const IL::ValueId isMaxExponent = m_builder.CompareValue(
            IL::CompareOp::UEq, exponentBitsValue, m_builder.ConstantU32(exponentMax), false);
        const IL::ValueId mantissaZero = m_builder.CompareValue(
            IL::CompareOp::UEq, mantissa, m_builder.ConstantU32(0), false);

        // Unbiased exponent: subnormals use 1 - bias.
        const IL::ValueId unbiasedNormal = Bin(
            instruction, IL::BinaryOp::ISub, exponentBitsValue, m_builder.ConstantU32(bias));
        const IL::ValueId unbiased =
            m_builder.SelectValue(m_module.types.U32(), isZeroExponent,
                                  m_builder.ConstantU32(1u - bias), unbiasedNormal, false);
        const IL::ValueId biasedExponent =
            Bin(instruction, IL::BinaryOp::IAdd, unbiased, m_builder.ConstantU32(127));
        const IL::ValueId powerBits =
            Bin(instruction, IL::BinaryOp::Shl, biasedExponent, m_builder.ConstantU32(23));
        const IL::ValueId power = Bitcast(f32, powerBits);

        // Mantissa value: 1.m for normals, 0.m for subnormals.
        const IL::ValueId mantissaScale = m_builder.ConstantF32(
            mantissaBits == 3 ? 0.125f : 0.25f);
        const IL::ValueId fraction = Bin(
            instruction, IL::BinaryOp::FMul,
            m_builder.ConvertValue(f32, IL::ConvertOp::UIToFP, mantissa, false), mantissaScale, f32);
        const IL::ValueId one = m_builder.ConstantF32(1.0f);
        const IL::ValueId mantissaValue = m_builder.SelectValue(
            f32, isZeroExponent, fraction, Bin(instruction, IL::BinaryOp::FAdd, one, fraction, f32),
            false);
        IL::ValueId value = Bin(instruction, IL::BinaryOp::FMul, mantissaValue, power, f32);

        // Zero, infinity and NaN patterns.
        const IL::ValueId zero = m_builder.ConstantF32(0.0f);
        const IL::ValueId infinity = m_builder.ConstantF32(std::numeric_limits<float>::infinity());
        const IL::ValueId nan = m_builder.ConstantF32(std::numeric_limits<float>::quiet_NaN());
        const IL::ValueId zeroPattern = m_builder.Emit(
            m_module.types.Bool(), IL::Op::Binary, static_cast<std::uint16_t>(IL::BinaryOp::And),
            {isZeroExponent, mantissaZero}, {}, IL::NoPayload, 0, 0, false);
        value = m_builder.SelectValue(f32, zeroPattern, zero, value, false);
        if (bf8)
        {
            value = m_builder.SelectValue(f32, isMaxExponent, nan, value, false);
            const IL::ValueId infinite = m_builder.Emit(
                m_module.types.Bool(), IL::Op::Binary,
                static_cast<std::uint16_t>(IL::BinaryOp::And), {isMaxExponent, mantissaZero}, {},
                IL::NoPayload, 0, 0, false);
            value = m_builder.SelectValue(f32, infinite, infinity, value, false);
        }
        else
        {
            const IL::ValueId nanPattern = m_builder.Emit(
                m_module.types.Bool(), IL::Op::Binary,
                static_cast<std::uint16_t>(IL::BinaryOp::And),
                {isMaxExponent,
                 m_builder.CompareValue(IL::CompareOp::UEq, mantissa,
                                        m_builder.ConstantU32(mantissaMask), false)},
                {}, IL::NoPayload, 0, 0, false);
            value = m_builder.SelectValue(f32, nanPattern, nan, value, false);
        }

        const IL::ValueId negative = m_builder.CompareValue(
            IL::CompareOp::UNe, sign, m_builder.ConstantU32(0), false);
        const IL::ValueId negated = m_builder.UnaryValue(IL::UnaryOp::Neg, f32, value, false);
        return m_builder.SelectValue(f32, negative, negated, value, false);
    }

    [[nodiscard]] Result<void> LiftCvtFromFp8(const DecodedInstruction& instruction, bool bf8,
                                              bool packed)
    {
        const auto source = ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        const auto byteAt = [&](std::uint32_t shift) {
            return Bin(instruction, IL::BinaryOp::And,
                       Bin(instruction, IL::BinaryOp::LShr, *source, m_builder.ConstantU32(shift)),
                       m_builder.ConstantU32(0xFF));
        };

        if (!packed)
        {
            const IL::ValueId value = DecodeFp8(instruction, byteAt(0), bf8);
            return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), value),
                             instruction.pc);
        }

        // The packed form gives two results: f32 conversions of two bytes.
        const IL::ValueId low = DecodeFp8(instruction, byteAt(0), bf8);
        const IL::ValueId high = DecodeFp8(instruction, byteAt(8), bf8);
        const IL::ValueId destination = instruction.operands[0].value;
        IL::ValueId second = IL::InvalidValue;
        {
            const IL::ValueId lowBits = Bitcast(m_module.types.U32(), low);
            const IL::ValueId highBits = Bitcast(m_module.types.U32(), high);
            const auto firstStatus = WriteVgprValue(destination, lowBits, instruction.pc);
            if (!firstStatus)
                return firstStatus;
            second = highBits;
        }
        return WriteVgprValue(destination + 1, second, instruction.pc);
    }

    [[nodiscard]] Result<void> WriteVgprValue(std::uint32_t index, IL::ValueId value,
                                              std::uint32_t pc)
    {
        Operand destination;
        destination.kind = OperandKind::Vgpr;
        destination.value = index;
        return WriteDest(destination, value, pc);
    }

    [[nodiscard]] Result<void> LiftDot4Fp8(const DecodedInstruction& instruction, bool firstBf8,
                                           bool secondBf8)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto accumulator = ReadFloatSource(instruction, 3);
        if (!accumulator)
            return std::unexpected(accumulator.error());

        const IL::TypeId f32 = m_module.types.F32();
        IL::ValueId sum = *accumulator;
        for (std::uint32_t i = 0; i < 4; ++i)
        {
            const IL::ValueId shift = m_builder.ConstantU32(8 * i);
            const auto byteAt = [&](IL::ValueId value) {
                return Bin(instruction, IL::BinaryOp::And,
                           Bin(instruction, IL::BinaryOp::LShr, value, shift),
                           m_builder.ConstantU32(0xFF));
            };
            const IL::ValueId product =
                Bin(instruction, IL::BinaryOp::FMul, DecodeFp8(instruction, byteAt(*first), firstBf8),
                    DecodeFp8(instruction, byteAt(*second), secondBf8), f32);
            sum = Bin(instruction, IL::BinaryOp::FAdd, sum, product, f32);
        }
        return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), sum),
                         instruction.pc);
    }

    // VOP3 cndmask takes the mask as an explicit operand; only the first bit
    // matters for the single-invocation model.
    [[nodiscard]] Result<void> LiftVop3Cndmask(const DecodedInstruction& instruction, bool half)
    {
        const auto mask = ReadBits(instruction.operands[3], instruction.pc);
        if (!mask)
            return std::unexpected(mask.error());
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());

        const IL::ValueId condition = m_builder.CompareValue(
            IL::CompareOp::UNe,
            Bin(instruction, IL::BinaryOp::And, *mask, m_builder.ConstantU32(1)),
            m_builder.ConstantU32(0), false);

        if (!half)
        {
            const IL::ValueId selected =
                m_builder.SelectValue(m_module.types.U32(), condition, *first, *second, false);
            return WriteDest(instruction.operands[0], selected, instruction.pc);
        }
        const IL::ValueId low =
            m_builder.SelectValue(m_module.types.U32(), condition,
                                  Bin(instruction, IL::BinaryOp::And, *first,
                                      m_builder.ConstantU32(0xFFFF)),
                                  Bin(instruction, IL::BinaryOp::And, *second,
                                      m_builder.ConstantU32(0xFFFF)),
                                  false);
        const IL::ValueId high =
            m_builder.SelectValue(m_module.types.U32(), condition,
                                  Bin(instruction, IL::BinaryOp::And, *first,
                                      m_builder.ConstantU32(0xFFFF0000)),
                                  Bin(instruction, IL::BinaryOp::And, *second,
                                      m_builder.ConstantU32(0xFFFF0000)),
                                  false);
        return WriteDest(instruction.operands[0],
                         Bin(instruction, IL::BinaryOp::Or, low, high), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftCvtPkU8F32(const DecodedInstruction& instruction)
    {
        const auto value = ReadFloatSource(instruction, 1);
        if (!value)
            return std::unexpected(value.error());
        const auto select = ReadBits(instruction.operands[2], instruction.pc);
        if (!select)
            return std::unexpected(select.error());
        const auto base = ReadBits(instruction.operands[3], instruction.pc);
        if (!base)
            return std::unexpected(base.error());

        const IL::ValueId rounded = RoundToInteger(instruction, *value, 0, 255);
        const IL::ValueId shift = Bin(
            instruction, IL::BinaryOp::Shl,
            Bin(instruction, IL::BinaryOp::And, *select, m_builder.ConstantU32(3)),
            m_builder.ConstantU32(3));
        const IL::ValueId mask =
            Bin(instruction, IL::BinaryOp::Shl, m_builder.ConstantU32(0xFF), shift);
        const IL::ValueId cleared =
            Bin(instruction, IL::BinaryOp::And, *base,
                m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U32(), mask, false));
        const IL::ValueId field =
            Bin(instruction, IL::BinaryOp::Shl,
                Bin(instruction, IL::BinaryOp::And, rounded, m_builder.ConstantU32(0xFF)), shift);
        return WriteDest(instruction.operands[0],
                         Bin(instruction, IL::BinaryOp::Or, cleared, field), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftSadU16(const DecodedInstruction& instruction)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());
        const auto addend = ReadBits(instruction.operands[3], instruction.pc);
        if (!addend)
            return std::unexpected(addend.error());

        IL::ValueId sum = m_builder.ConstantU32(0);
        for (std::uint32_t i = 0; i < 2; ++i)
        {
            const IL::ValueId shift = m_builder.ConstantU32(16 * i);
            const auto halfAt = [&](IL::ValueId value) {
                return Bin(instruction, IL::BinaryOp::And,
                           Bin(instruction, IL::BinaryOp::LShr, value, shift),
                           m_builder.ConstantU32(0xFFFF));
            };
            const IL::ValueId a = halfAt(*first);
            const IL::ValueId b = halfAt(*second);
            const IL::ValueId difference = m_builder.SelectValue(
                m_module.types.U32(),
                m_builder.CompareValue(IL::CompareOp::UGe, a, b, false),
                Bin(instruction, IL::BinaryOp::ISub, a, b),
                Bin(instruction, IL::BinaryOp::ISub, b, a), false);
            sum = Bin(instruction, IL::BinaryOp::IAdd, sum, difference);
        }
        return WriteDest(instruction.operands[0],
                         Bin(instruction, IL::BinaryOp::IAdd, sum, *addend), instruction.pc);
    }

    [[nodiscard]] Result<void> LiftShift64Vector(const DecodedInstruction& instruction,
                                                bool arithmetic)
    {
        if (instruction.operands[1].kind != OperandKind::Vgpr ||
            instruction.operands[2].kind != OperandKind::Vgpr)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-081", "64-bit shift needs vector register pairs",
                          instruction.pc));
        const auto count = ReadVgpr(instruction.operands[1].value, instruction.pc);
        if (!count)
            return std::unexpected(count.error());
        const auto value = ReadVgpr64(instruction.operands[2].value, instruction.pc);
        if (!value)
            return std::unexpected(value.error());

        const IL::ValueId shift = m_builder.ConvertValue(
            m_module.types.U64(), IL::ConvertOp::ZExt, MaskShiftCount(*count, 63, false), false);
        const IL::TypeId type = arithmetic ? m_module.types.I64() : m_module.types.U64();
        const IL::ValueId shifted = m_builder.Emit(
            type, IL::Op::Binary,
            static_cast<std::uint16_t>(arithmetic ? IL::BinaryOp::AShr : IL::BinaryOp::LShr),
            {arithmetic ? Bitcast(m_module.types.I64(), *value) : *value, shift}, {}, IL::NoPayload,
            instruction.pc, static_cast<std::uint16_t>(instruction.op), false);
        return WriteVgpr64(instruction.operands[0], shifted, instruction.pc);
    }

    // frexp: x = m * 2^e with m in [0.5, 1). The mantissa keeps the sign and
    // rescales the exponent field to 126; subnormals are normalized first.
    [[nodiscard]] Result<void> LiftFrexp(const DecodedInstruction& instruction, bool mantissa,
                                        bool half)
    {
        const auto source = half ? ReadBits(instruction.operands[1], instruction.pc)
                                 : ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        IL::ValueId bits = *source;
        if (half)
            bits = Bin(instruction, IL::BinaryOp::And, bits, m_builder.ConstantU32(0xFFFF));

        const std::uint32_t mantissaBits = half ? 10u : 23u;
        const std::uint32_t exponentBias = half ? 15u : 127u;
        const std::uint32_t exponentMax = half ? 31u : 255u;
        const std::uint32_t mantissaMask = (1u << mantissaBits) - 1u;
        const std::uint32_t signShift = half ? 15u : 31u;
        const std::uint32_t targetExponent = exponentBias - 1u; // 0.5 * 2^e

        const IL::ValueId mantissaField = Bin(
            instruction, IL::BinaryOp::And, bits, m_builder.ConstantU32(mantissaMask));
        const IL::ValueId exponentField = Bin(
            instruction, IL::BinaryOp::And,
            Bin(instruction, IL::BinaryOp::LShr, bits, m_builder.ConstantU32(mantissaBits)),
            m_builder.ConstantU32(exponentMax));
        const IL::ValueId sign = Bin(instruction, IL::BinaryOp::And,
                                     Bin(instruction, IL::BinaryOp::LShr, bits,
                                         m_builder.ConstantU32(signShift)),
                                     m_builder.ConstantU32(1));

        const IL::ValueId exponentZero = m_builder.CompareValue(
            IL::CompareOp::UEq, exponentField, m_builder.ConstantU32(0), false);
        const IL::ValueId mantissaZero = m_builder.CompareValue(
            IL::CompareOp::UEq, mantissaField, m_builder.ConstantU32(0), false);
        const IL::ValueId isZero = m_builder.Emit(
            m_module.types.Bool(), IL::Op::Binary, static_cast<std::uint16_t>(IL::BinaryOp::And),
            {exponentZero, mantissaZero}, {}, IL::NoPayload, 0, 0, false);
        const IL::ValueId isSubnormal = m_builder.Emit(
            m_module.types.Bool(), IL::Op::Binary, static_cast<std::uint16_t>(IL::BinaryOp::And),
            {exponentZero,
             m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), mantissaZero, false)},
            {}, IL::NoPayload, 0, 0, false);
        const IL::ValueId exponentMaxed = m_builder.CompareValue(
            IL::CompareOp::UEq, exponentField, m_builder.ConstantU32(exponentMax), false);

        const IL::ValueId leadingZeroBits =
            m_builder.UnaryValue(IL::UnaryOp::Clz, m_module.types.U32(), mantissaField, false);
        // Clz counts over the full 32-bit register, so the mantissa's most
        // significant bit position is 31 - clz for either width.
        const IL::ValueId highestBit = Bin(
            instruction, IL::BinaryOp::ISub, m_builder.ConstantU32(31u), leadingZeroBits);
        const IL::ValueId normalizingShift = Bin(
            instruction, IL::BinaryOp::ISub, m_builder.ConstantU32(mantissaBits), highestBit);
        const IL::ValueId normalizedMantissa = Bin(
            instruction, IL::BinaryOp::And,
            Bin(instruction, IL::BinaryOp::Shl, mantissaField, normalizingShift),
            m_builder.ConstantU32(mantissaMask));

        const IL::ValueId normalBits =
            Bin(instruction, IL::BinaryOp::Or,
                Bin(instruction, IL::BinaryOp::Shl, sign, m_builder.ConstantU32(signShift)),
                Bin(instruction, IL::BinaryOp::Or,
                    Bin(instruction, IL::BinaryOp::Shl, m_builder.ConstantU32(targetExponent),
                        m_builder.ConstantU32(mantissaBits)),
                    mantissaField));
        const IL::ValueId subnormalBits =
            Bin(instruction, IL::BinaryOp::Or,
                Bin(instruction, IL::BinaryOp::Shl, sign, m_builder.ConstantU32(signShift)),
                Bin(instruction, IL::BinaryOp::Or,
                    Bin(instruction, IL::BinaryOp::Shl, m_builder.ConstantU32(targetExponent),
                        m_builder.ConstantU32(mantissaBits)),
                    normalizedMantissa));

        const IL::ValueId normalExponent = Bin(
            instruction, IL::BinaryOp::ISub, exponentField,
            m_builder.ConstantU32(exponentBias - 1u));
        const IL::ValueId subnormalExponent =
            Bin(instruction, IL::BinaryOp::IAdd, highestBit,
                m_builder.ConstantU32(2u - exponentBias - mantissaBits));

        IL::ValueId result = mantissa ? normalBits : normalExponent;
        result = m_builder.SelectValue(
            m_module.types.U32(), isSubnormal, mantissa ? subnormalBits : subnormalExponent,
            result, false);
        result = m_builder.SelectValue(m_module.types.U32(), isZero,
                                       m_builder.ConstantU32(0), result, false);
        if (mantissa)
        {
            // Non-finite inputs are returned unchanged.
            result = m_builder.SelectValue(m_module.types.U32(), exponentMaxed, bits, result,
                                           false);
        }
        else
        {
            result = m_builder.SelectValue(m_module.types.U32(), exponentMaxed,
                                           m_builder.ConstantU32(0), result, false);
        }
        return WriteDest(instruction.operands[0], result, instruction.pc);
    }

    // Save-exec forms: the destination receives the previous mask (all ones
    // while this lane is active) and EXEC takes the combined condition.
    [[nodiscard]] Result<void> LiftSaveExec(const DecodedInstruction& instruction, IL::BinaryOp op,
                                            bool invert, bool wide)
    {
        const auto source = wide ? ReadPairSource(instruction.operands[1], instruction.pc)
                                 : ReadBits(instruction.operands[1], instruction.pc);
        if (!source)
            return std::unexpected(source.error());

        const auto exec = ReadExec(instruction.pc);
        if (!exec)
            return std::unexpected(exec.error());

        const IL::ValueId maskNonZero = m_builder.CompareValue(
            IL::CompareOp::UNe, *source,
            wide ? m_builder.ConstantU64(0) : m_builder.ConstantU32(0), true);
        IL::ValueId condition = m_builder.Emit(m_module.types.Bool(), IL::Op::Binary,
                                              static_cast<std::uint16_t>(op), {*exec, maskNonZero},
                                              {}, IL::NoPayload, instruction.pc,
                                              static_cast<std::uint16_t>(instruction.op), true);
        if (invert)
            condition = m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.Bool(), condition, true);

        // The saved mask is the previous EXEC value for this lane.
        const IL::ValueId saved = m_builder.SelectValue(
            wide ? m_module.types.U64() : m_module.types.U32(), *exec,
            wide ? m_builder.ConstantU64(~std::uint64_t{0}) : m_builder.ConstantU32(0xFFFFFFFFu),
            wide ? m_builder.ConstantU64(0) : m_builder.ConstantU32(0), true);

        const auto status = wide ? WriteSgpr64(instruction.operands[0], saved, instruction.pc)
                                 : WriteDest(instruction.operands[0], saved, instruction.pc);
        if (!status)
            return status;
        return WriteExec(condition, instruction.pc);
    }

    [[nodiscard]] Result<void> LiftCvtPkRtzF16F32Scalar(const DecodedInstruction& instruction)
    {
        const auto first = ReadBits(instruction.operands[1], instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadBits(instruction.operands[2], instruction.pc);
        if (!second)
            return std::unexpected(second.error());

        const auto convert = [&](IL::ValueId bits) {
            const IL::ValueId value = Bitcast(m_module.types.F32(), bits);
            const IL::ValueId rounded =
                m_builder.ConvertValue(F16Type(), IL::ConvertOp::FPTrunc, value, false);
            const IL::ValueId widened =
                m_builder.ConvertValue(m_module.types.F32(), IL::ConvertOp::FPExt, rounded, false);
            const IL::ValueId absSource =
                m_builder.UnaryValue(IL::UnaryOp::FAbs, m_module.types.F32(), value, false);
            const IL::ValueId absRounded =
                m_builder.UnaryValue(IL::UnaryOp::FAbs, m_module.types.F32(), widened, false);
            const IL::ValueId tooLarge =
                m_builder.CompareValue(IL::CompareOp::FOrdGt, absRounded, absSource, false);
            const IL::ValueId halfBits =
                m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::ZExt,
                                       Bitcast(U16Type(), rounded), false);
            const IL::ValueId stepped = m_builder.SelectValue(
                m_module.types.U32(), tooLarge,
                Bin(instruction, IL::BinaryOp::ISub, halfBits, m_builder.ConstantU32(1)), halfBits,
                false);
            return Bin(instruction, IL::BinaryOp::And, stepped, m_builder.ConstantU32(0xFFFF));
        };

        const IL::ValueId low = convert(*first);
        const IL::ValueId high = convert(*second);
        return WriteDest(instruction.operands[0],
                         Bin(instruction, IL::BinaryOp::Or, low,
                             Bin(instruction, IL::BinaryOp::Shl, high,
                                 m_builder.ConstantU32(16))),
                         instruction.pc);
    }

    // f64 minimum/maximum propagate NaN and write a VGPR pair.
    [[nodiscard]] Result<void> LiftF64MinMax(const DecodedInstruction& instruction, bool maximum)
    {
        if (instruction.operands[1].kind != OperandKind::Vgpr ||
            instruction.operands[2].kind != OperandKind::Vgpr)
            return std::unexpected(
                LiftError("SR-GFX-LIFT-083", "f64 min/max needs vector register pairs",
                          instruction.pc));
        const auto first = ReadVgpr64(instruction.operands[1].value, instruction.pc);
        if (!first)
            return std::unexpected(first.error());
        const auto second = ReadVgpr64(instruction.operands[2].value, instruction.pc);
        if (!second)
            return std::unexpected(second.error());

        const IL::TypeId f64 = F64Type();
        const IL::ValueId left = Bitcast(f64, *first);
        const IL::ValueId right = Bitcast(f64, *second);
        const IL::ValueId orderedLeft =
            m_builder.CompareValue(IL::CompareOp::FOrdEq, left, left, false);
        const IL::ValueId orderedRight =
            m_builder.CompareValue(IL::CompareOp::FOrdEq, right, right, false);
        const IL::ValueId ordered = m_builder.Emit(
            m_module.types.Bool(), IL::Op::Binary, static_cast<std::uint16_t>(IL::BinaryOp::And),
            {orderedLeft, orderedRight}, {}, IL::NoPayload, instruction.pc,
            static_cast<std::uint16_t>(instruction.op), false);
        const IL::ValueId selected =
            Bin(instruction, maximum ? IL::BinaryOp::FMax : IL::BinaryOp::FMin, left, right, f64);
        const IL::ValueId nan =
            m_builder.ConstantF64(std::numeric_limits<double>::quiet_NaN());
        const IL::ValueId result = m_builder.SelectValue(f64, ordered, selected, nan, false);
        return WriteVgpr64(instruction.operands[0], Bitcast(m_module.types.U64(), result),
                           instruction.pc);
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

    // Stage IO. A signature row is one location (or builtin) with a vector
    // type; individual components are addressed by the payload.
    struct StageSlot
    {
        IL::Space space;
        IL::Builtin builtin;
        std::uint32_t location;
        IL::TypeId type;
        std::size_t declaration;
    };

    [[nodiscard]] IL::TypeId EnsureSignature(IL::Space space,
                                             IL::Builtin builtin,
                                             std::uint32_t location,
                                             IL::TypeId type,
                                             IL::Interpolation interpolation = IL::Interpolation::None)
    {
        std::vector<IL::SignatureDecl>& declarations = space == IL::Space::Input ? m_module.inputs : m_module.outputs;

        for (const StageSlot& slot : m_stageSlots)
        {
            if (slot.space == space && slot.builtin == builtin && slot.location == location)
            {
                // The first use decides the mode; a later interp instruction can
                // refine an input that was declared without one.
                if (interpolation != IL::Interpolation::None)
                    declarations[slot.declaration].interpolation = interpolation;
                return slot.type;
            }
        }

        IL::SignatureDecl decl;
        decl.space = space;
        decl.location = location;
        decl.type = type;
        decl.builtin = builtin;
        decl.interpolation = interpolation;

        declarations.push_back(decl);
        m_stageSlots.push_back(StageSlot{space, builtin, location, type, declarations.size() - 1});
        return type;
    }

    [[nodiscard]] IL::PayloadId StageIoPayload(IL::Space space,
                                               IL::Builtin builtin,
                                               std::uint32_t location,
                                               std::uint32_t component,
                                               IL::Interpolation interpolation)
    {
        IL::StageIoDesc desc;
        desc.space = space;
        desc.builtin = builtin;
        desc.location = location;
        desc.component = component;
        desc.interpolation = interpolation;
        return m_module.payloads.Add(desc);
    }

    // v_interp_p1/p2 read the attribute the fixed-function unit already
    // interpolated; the barycentric operands only feed the hardware path. The
    // interpolation mode belongs to the SPI input table, which a console
    // container carries and a bare code object does not, so perspective is
    // assumed and v_interp_mov is flat.
    [[nodiscard]] Result<void> LiftInterp(const DecodedInstruction& instruction)
    {
        if (m_module.stage != IL::Stage::Fragment)
            return std::unexpected(LiftError("SR-GFX-LIFT-040", "interpolation outside a fragment shader", instruction.pc));

        const IL::TypeId f32 = m_module.types.F32();
        const IL::Interpolation mode =
            instruction.op == Op::V_InterpMovF32 ? IL::Interpolation::Flat : IL::Interpolation::Perspective;
        (void)EnsureSignature(IL::Space::Input,
                              IL::Builtin::None,
                              instruction.attr,
                              m_module.types.Vector(f32, 4),
                              mode);
        const IL::PayloadId payload =
            StageIoPayload(IL::Space::Input, IL::Builtin::None, instruction.attr, instruction.attrChan, mode);

        const IL::ValueId value = m_builder.Emit(f32,
                                                 IL::Op::Intrinsic,
                                                 static_cast<std::uint16_t>(IL::IntrinsicId::StageInput),
                                                 {},
                                                 {},
                                                 payload,
                                                 instruction.pc,
                                                 static_cast<std::uint16_t>(instruction.op),
                                                 false);
        return WriteDest(instruction.operands[0], Bitcast(m_module.types.U32(), value), instruction.pc);
    }

    // exp writes up to four channels of one target: an MRT slot, the depth
    // output, a position component or a parameter.
    [[nodiscard]] Result<void> LiftExport(const DecodedInstruction& instruction)
    {
        if (instruction.exportCompressed)
            return std::unexpected(LiftError("SR-GFX-LIFT-041", "16-bit packed exports are not modeled", instruction.pc));

        const std::uint32_t target = instruction.exportTarget;
        const IL::TypeId f32 = m_module.types.F32();
        const IL::TypeId vec4 = m_module.types.Vector(f32, 4);

        if (target == 20)
        {
            // exp prim closes the primitive: the vertex written by the exports
            // above it is emitted, and the strip ends.
            if (m_module.stage != IL::Stage::Geometry)
                return std::unexpected(LiftError("SR-GFX-LIFT-056",
                                                 "primitive export outside a geometry stage",
                                                 instruction.pc));
            const auto emitted = EndVertex(instruction.pc);
            if (!emitted)
                return std::unexpected(emitted.error());
            const auto primitive = EndPrimitive(instruction.pc, static_cast<std::uint16_t>(instruction.op));
            if (!primitive)
                return std::unexpected(primitive.error());
            return {};
        }

        // pos0 begins a vertex: the exports since the previous pos0 describe
        // the vertex that is now complete, so it is emitted before this one is
        // written.
        if (target == 12 && (instruction.exportEnabled & 1u) != 0 && m_module.stage == IL::Stage::Geometry)
        {
            const auto emitted = EndVertex(instruction.pc);
            if (!emitted)
                return std::unexpected(emitted.error());
            m_pendingVertex = true;
        }

        for (std::uint32_t channel = 0; channel < 4; ++channel)
        {
            if ((instruction.exportEnabled & (1u << channel)) == 0)
                continue;

            IL::Space space = IL::Space::Output;
            IL::Builtin builtin = IL::Builtin::None;
            std::uint32_t location = 0;
            std::uint32_t component = channel;
            IL::TypeId type = vec4;

            if (target <= 7)
            {
                location = target;
            }
            else if (target == 8)
            {
                // exp mrtz: channel 0 is depth; stencil and sample masks are
                // separate state the IL does not carry yet.
                if (channel != 0)
                    continue;
                builtin = IL::Builtin::FragDepth;
                component = 0;
                type = f32;
            }
            else if (target == 9)
            {
                continue; // exp null
            }
            else if (target >= 12 && target <= 15)
            {
                // pos0..pos3 are the four components: an export group names the
                // first slot, and the enabled channels follow it.
                builtin = IL::Builtin::Position;
                component = (target - 12) + channel;
                if (component > 3)
                {
                    return std::unexpected(LiftError("SR-GFX-LIFT-045",
                                                     "position export outside the four components",
                                                     instruction.pc));
                }
            }
            else if (target >= 32 && target <= 63)
            {
                // exp param<n>: parameters start at 32 in the target field.
                location = target - 32;
            }
            else
            {
                return std::unexpected(LiftError("SR-GFX-LIFT-042",
                                                 "export target " + std::to_string(target) + " is not modeled",
                                                 instruction.pc));
            }

            const auto value = ReadFloat(instruction.operands[channel], instruction.pc);
            if (!value)
                return std::unexpected(value.error());

            (void)EnsureSignature(space, builtin, location, type);
            const IL::PayloadId payload =
                StageIoPayload(space, builtin, location, component, IL::Interpolation::None);
            m_builder.EmitVoid(IL::Op::Intrinsic,
                               static_cast<std::uint16_t>(IL::IntrinsicId::StageOutput),
                               {*value},
                               {},
                               payload,
                               instruction.pc,
                               static_cast<std::uint16_t>(instruction.op));
        }

        return {};
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

        if (instruction.encoding == Encoding::Vintrp)
            return LiftInterp(instruction);
        if (instruction.encoding == Encoding::Exp)
            return LiftExport(instruction);
        if (instruction.encoding == Encoding::Ds)
            return LiftDs(instruction);
        if (instruction.encoding == Encoding::Mimg)
            return LiftImageAccess(instruction);
        if (instruction.encoding == Encoding::Mtbuf)
            return LiftMtbuf(instruction);
        if (instruction.encoding == Encoding::Mubuf)
            return LiftMubuf(instruction);
        if (instruction.encoding == Encoding::Flat || instruction.encoding == Encoding::Global ||
            instruction.encoding == Encoding::Scratch || instruction.encoding == Encoding::Buffer)
            return LiftFlatMemory(instruction);

        switch (instruction.op)
        {
        case Op::S_MovB64:
        case Op::S_NotB64:
        case Op::S_AndB64:
        case Op::S_OrB64:
        case Op::S_XorB64:
        case Op::S_CselectB64:
        case Op::S_AndNot1B64:
        case Op::S_OrNot1B64:
        case Op::S_NandB64:
        case Op::S_NorB64:
        case Op::S_XnorB64:
        case Op::S_AddNcU64:
        case Op::S_SubNcU64:
        case Op::S_MulU64:
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
        case Op::S_AbsDiffI32:
            return LiftAbsDiff(instruction);
        case Op::S_BfmB32:
            return LiftBitMask(instruction, true);
        case Op::S_SextI32I8:
            return LiftSext(instruction, 8);
        case Op::S_SextI32I16:
            return LiftSext(instruction, 16);
        case Op::S_BrevB64:
        {
            const auto value = ReadPairSource(instruction.operands[1], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            // OpBitReverse is 32-bit only in Vulkan SPIR-V, so reverse the halves
            // and swap them: bit i of a 64-bit reverse comes from bit 63 - i.
            const IL::ValueId low =
                m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, *value, true);
            const IL::ValueId high64 = m_builder.Emit(m_module.types.U64(),
                                                      IL::Op::Binary,
                                                      static_cast<std::uint16_t>(IL::BinaryOp::LShr),
                                                      {*value, m_builder.ConstantU64(32)},
                                                      {}, IL::NoPayload, instruction.pc,
                                                      static_cast<std::uint16_t>(instruction.op), true);
            const IL::ValueId high =
                m_builder.ConvertValue(m_module.types.U32(), IL::ConvertOp::Trunc, high64, true);
            const IL::ValueId lowReversed =
                m_builder.UnaryValue(IL::UnaryOp::BitReverse, m_module.types.U32(), low, true);
            const IL::ValueId highReversed =
                m_builder.UnaryValue(IL::UnaryOp::BitReverse, m_module.types.U32(), high, true);
            const IL::ValueId lowWidened =
                m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, lowReversed, true);
            const IL::ValueId highWidened =
                m_builder.ConvertValue(m_module.types.U64(), IL::ConvertOp::ZExt, highReversed, true);
            const IL::ValueId shifted = m_builder.Emit(m_module.types.U64(),
                                                       IL::Op::Binary,
                                                       static_cast<std::uint16_t>(IL::BinaryOp::Shl),
                                                       {lowWidened, m_builder.ConstantU64(32)},
                                                       {}, IL::NoPayload, instruction.pc,
                                                       static_cast<std::uint16_t>(instruction.op), true);
            const IL::ValueId result = m_builder.Emit(m_module.types.U64(),
                                                      IL::Op::Binary,
                                                      static_cast<std::uint16_t>(IL::BinaryOp::Or),
                                                      {shifted, highWidened},
                                                      {}, IL::NoPayload, instruction.pc,
                                                      static_cast<std::uint16_t>(instruction.op), true);
            return WriteSgpr64(instruction.operands[0], result, instruction.pc);
        }
        case Op::S_Bcnt0I32B32:
        {
            const auto value = ReadBits(instruction.operands[1], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            const IL::ValueId ones =
                m_builder.UnaryValue(IL::UnaryOp::PopCount, m_module.types.U32(), *value, true);
            const IL::ValueId zeros = m_builder.Emit(m_module.types.U32(),
                                                     IL::Op::Binary,
                                                     static_cast<std::uint16_t>(IL::BinaryOp::ISub),
                                                     {m_builder.ConstantU32(32), ones},
                                                     {},
                                                     IL::NoPayload,
                                                     instruction.pc,
                                                     static_cast<std::uint16_t>(instruction.op),
                                                     true);
            const auto scc = WriteSccIfNonZero(zeros, instruction.pc, true);
            if (!scc)
                return scc;
            return WriteDest(instruction.operands[0], zeros, instruction.pc);
        }
        case Op::S_Bcnt0I32B64:
            return LiftBitCount64(instruction, false);
        case Op::S_Bcnt1I32B64:
            return LiftBitCount64(instruction, true);
        case Op::S_Bitset0B32:
            return LiftBitset(instruction, false, false);
        case Op::S_Bitset1B32:
            return LiftBitset(instruction, false, true);
        case Op::S_Bitset0B64:
            return LiftBitset(instruction, true, false);
        case Op::S_Bitset1B64:
            return LiftBitset(instruction, true, true);
        case Op::S_LshlB64:
            return LiftShift64(instruction, IL::BinaryOp::Shl);
        case Op::S_LshrB64:
            return LiftShift64(instruction, IL::BinaryOp::LShr);
        case Op::S_AshrI64:
            return LiftShift64(instruction, IL::BinaryOp::AShr);
        case Op::S_BfmB64:
            return LiftBfm64(instruction);
        case Op::S_BfeI64:
            return LiftBfe64(instruction, true);
        case Op::S_BfeU64:
            return LiftBfe64(instruction, false);
        case Op::S_MulHiI32:
            return LiftMulHiScalar(instruction, true);
        case Op::S_MulHiU32:
            return LiftMulHiScalar(instruction, false);
        case Op::S_Ff0I32B32:
        {
            const auto value = ReadBits(instruction.operands[1], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            const IL::ValueId inverted =
                m_builder.UnaryValue(IL::UnaryOp::Not, m_module.types.U32(), *value, true);
            const IL::ValueId zero = m_builder.CompareValue(IL::CompareOp::UEq, inverted,
                                                            m_builder.ConstantU32(0), true);
            const IL::ValueId count =
                m_builder.UnaryValue(IL::UnaryOp::Ctz, m_module.types.U32(), inverted, true);
            const IL::ValueId result =
                m_builder.SelectValue(m_module.types.U32(), zero,
                                      m_builder.ConstantU32(0xFFFFFFFFu), count, true);
            return WriteDest(instruction.operands[0], result, instruction.pc);
        }
        case Op::S_Ff1I32B64:
            return LiftBitScan64(instruction, false, false);
        case Op::S_FlbitI32B64:
            return LiftBitScan64(instruction, true, false);
        case Op::S_FlbitI32:
            return LiftCls(instruction);
        case Op::S_FlbitI32I64:
            return LiftCls64(instruction);
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
            if (instruction.encoding == Encoding::Vop3 && instruction.operandCount >= 4)
                return LiftVop3Cndmask(instruction, false);
            return LiftCndmask(instruction);
        case Op::V_Nop:
            return {};
        case Op::V_ReadfirstlaneB32:
        {
            // Broadcast the first active lane's value to the whole wave.
            const auto value = ReadBits(instruction.operands[1], instruction.pc);
            if (!value)
                return std::unexpected(value.error());
            const IL::ValueId broadcast = m_builder.Emit(m_module.types.U32(),
                                                         IL::Op::Intrinsic,
                                                         static_cast<std::uint16_t>(IL::IntrinsicId::WaveBroadcastFirst),
                                                         {*value},
                                                         {},
                                                         IL::NoPayload,
                                                         instruction.pc,
                                                         static_cast<std::uint16_t>(instruction.op),
                                                         true);
            return WriteDest(instruction.operands[0], broadcast, instruction.pc);
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
        case Op::V_CmpLtF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdLt, VectorPredicate::Compare);
        case Op::V_CmpEqF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdEq, VectorPredicate::Compare);
        case Op::V_CmpLeF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdLe, VectorPredicate::Compare);
        case Op::V_CmpGtF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdGt, VectorPredicate::Compare);
        case Op::V_CmpLgF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdNe, VectorPredicate::Compare);
        case Op::V_CmpGeF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdGe, VectorPredicate::Compare);
        case Op::V_CmpFalseF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdEq, VectorPredicate::False);
        case Op::V_CmpTrueF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdEq, VectorPredicate::True);
        case Op::V_CmpOrdF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdEq, VectorPredicate::Ordered);
        case Op::V_CmpUnordF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdEq, VectorPredicate::Unordered);
        case Op::V_CmpNgeF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FUnordLt, VectorPredicate::Compare);
        case Op::V_CmpNlgF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FUnordEq, VectorPredicate::Compare);
        case Op::V_CmpNgtF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FUnordLe, VectorPredicate::Compare);
        case Op::V_CmpNleF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FUnordGt, VectorPredicate::Compare);
        case Op::V_CmpNeqF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FOrdNe, VectorPredicate::Compare);
        case Op::V_CmpNltF16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F16, IL::CompareOp::FUnordGe, VectorPredicate::Compare);
        case Op::V_CmpLtF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdLt, VectorPredicate::Compare);
        case Op::V_CmpEqF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdEq, VectorPredicate::Compare);
        case Op::V_CmpLeF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdLe, VectorPredicate::Compare);
        case Op::V_CmpGtF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdGt, VectorPredicate::Compare);
        case Op::V_CmpLgF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdNe, VectorPredicate::Compare);
        case Op::V_CmpGeF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdGe, VectorPredicate::Compare);
        case Op::V_CmpFalseF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdEq, VectorPredicate::False);
        case Op::V_CmpTrueF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdEq, VectorPredicate::True);
        case Op::V_CmpOrdF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdEq, VectorPredicate::Ordered);
        case Op::V_CmpUnordF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdEq, VectorPredicate::Unordered);
        case Op::V_CmpNgeF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FUnordLt, VectorPredicate::Compare);
        case Op::V_CmpNlgF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FUnordEq, VectorPredicate::Compare);
        case Op::V_CmpNgtF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FUnordLe, VectorPredicate::Compare);
        case Op::V_CmpNleF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FUnordGt, VectorPredicate::Compare);
        case Op::V_CmpNeqF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FOrdNe, VectorPredicate::Compare);
        case Op::V_CmpNltF64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::F64, IL::CompareOp::FUnordGe, VectorPredicate::Compare);
        case Op::V_CmpLtI16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I16, IL::CompareOp::ILt, VectorPredicate::Compare);
        case Op::V_CmpEqI16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I16, IL::CompareOp::IEq, VectorPredicate::Compare);
        case Op::V_CmpLeI16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I16, IL::CompareOp::ILe, VectorPredicate::Compare);
        case Op::V_CmpGtI16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I16, IL::CompareOp::IGt, VectorPredicate::Compare);
        case Op::V_CmpNeI16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I16, IL::CompareOp::INe, VectorPredicate::Compare);
        case Op::V_CmpGeI16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I16, IL::CompareOp::IGe, VectorPredicate::Compare);
        case Op::V_CmpLtI64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I64, IL::CompareOp::ILt, VectorPredicate::Compare);
        case Op::V_CmpEqI64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I64, IL::CompareOp::IEq, VectorPredicate::Compare);
        case Op::V_CmpLeI64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I64, IL::CompareOp::ILe, VectorPredicate::Compare);
        case Op::V_CmpGtI64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I64, IL::CompareOp::IGt, VectorPredicate::Compare);
        case Op::V_CmpNeI64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I64, IL::CompareOp::INe, VectorPredicate::Compare);
        case Op::V_CmpGeI64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I64, IL::CompareOp::IGe, VectorPredicate::Compare);
        case Op::V_CmpFalseI64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I64, IL::CompareOp::IEq, VectorPredicate::False);
        case Op::V_CmpTrueI64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::I64, IL::CompareOp::IEq, VectorPredicate::True);
        case Op::V_CmpLtU16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U16, IL::CompareOp::ULt, VectorPredicate::Compare);
        case Op::V_CmpEqU16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U16, IL::CompareOp::UEq, VectorPredicate::Compare);
        case Op::V_CmpLeU16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U16, IL::CompareOp::ULe, VectorPredicate::Compare);
        case Op::V_CmpGtU16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U16, IL::CompareOp::UGt, VectorPredicate::Compare);
        case Op::V_CmpNeU16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U16, IL::CompareOp::UNe, VectorPredicate::Compare);
        case Op::V_CmpGeU16:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U16, IL::CompareOp::UGe, VectorPredicate::Compare);
        case Op::V_CmpLtU64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U64, IL::CompareOp::ULt, VectorPredicate::Compare);
        case Op::V_CmpEqU64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U64, IL::CompareOp::UEq, VectorPredicate::Compare);
        case Op::V_CmpLeU64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U64, IL::CompareOp::ULe, VectorPredicate::Compare);
        case Op::V_CmpGtU64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U64, IL::CompareOp::UGt, VectorPredicate::Compare);
        case Op::V_CmpNeU64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U64, IL::CompareOp::UNe, VectorPredicate::Compare);
        case Op::V_CmpGeU64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U64, IL::CompareOp::UGe, VectorPredicate::Compare);
        case Op::V_CmpFalseU64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U64, IL::CompareOp::UEq, VectorPredicate::False);
        case Op::V_CmpTrueU64:
            return LiftVectorCompareTyped(instruction, CompareValueKind::U64, IL::CompareOp::UEq, VectorPredicate::True);
        case Op::V_PkAddI16:
            return LiftPacked(instruction, PackedKind::AddI16);
        case Op::V_PkSubI16:
            return LiftPacked(instruction, PackedKind::SubI16);
        case Op::V_PkMulLoU16:
            return LiftPacked(instruction, PackedKind::MulLoU16);
        case Op::V_PkMadI16:
            return LiftPacked(instruction, PackedKind::MadI16);
        case Op::V_PkMinI16:
            return LiftPacked(instruction, PackedKind::MinI16);
        case Op::V_PkMaxI16:
            return LiftPacked(instruction, PackedKind::MaxI16);
        case Op::V_PkAddU16:
            return LiftPacked(instruction, PackedKind::AddU16);
        case Op::V_PkSubU16:
            return LiftPacked(instruction, PackedKind::SubU16);
        case Op::V_PkMadU16:
            return LiftPacked(instruction, PackedKind::MadU16);
        case Op::V_PkMinU16:
            return LiftPacked(instruction, PackedKind::MinU16);
        case Op::V_PkMaxU16:
            return LiftPacked(instruction, PackedKind::MaxU16);
        case Op::V_PkLshlrevB16:
            return LiftPacked(instruction, PackedKind::LshlrevB16);
        case Op::V_PkLshrrevB16:
            return LiftPacked(instruction, PackedKind::LshrrevB16);
        case Op::V_PkAshrrevI16:
            return LiftPacked(instruction, PackedKind::AshrrevI16);
        case Op::V_PkAddF16:
            return LiftPacked(instruction, PackedKind::AddF16);
        case Op::V_PkMulF16:
            return LiftPacked(instruction, PackedKind::MulF16);
        case Op::V_PkFmaF16:
            return LiftPacked(instruction, PackedKind::FmaF16);
        case Op::V_PkMinF16:
            return LiftPacked(instruction, PackedKind::MinF16);
        case Op::V_PkMaxF16:
            return LiftPacked(instruction, PackedKind::MaxF16);
        case Op::V_BfeU32:
            return LiftBitFieldExtract(instruction, false, false);
        case Op::V_BfeI32:
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
        case Op::S_CmpEqU64:
        case Op::S_CmpLgU64:
            return LiftScalarU64Compare(instruction);
        case Op::S_Bitcmp0B32:
        case Op::S_Bitcmp1B32:
        case Op::S_Bitcmp0B64:
        case Op::S_Bitcmp1B64:
            return LiftScalarBitCompare(instruction);
        case Op::S_CmpLtF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdLt, VectorPredicate::Compare);
        case Op::S_CmpEqF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdEq, VectorPredicate::Compare);
        case Op::S_CmpLeF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdLe, VectorPredicate::Compare);
        case Op::S_CmpGtF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdGt, VectorPredicate::Compare);
        case Op::S_CmpLgF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdNe, VectorPredicate::Compare);
        case Op::S_CmpGeF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdGe, VectorPredicate::Compare);
        case Op::S_CmpOF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdEq, VectorPredicate::Ordered);
        case Op::S_CmpUF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdEq, VectorPredicate::Unordered);
        case Op::S_CmpNgeF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FUnordLt, VectorPredicate::Compare);
        case Op::S_CmpNlgF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FUnordEq, VectorPredicate::Compare);
        case Op::S_CmpNgtF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FUnordLe, VectorPredicate::Compare);
        case Op::S_CmpNleF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FUnordGt, VectorPredicate::Compare);
        case Op::S_CmpNeqF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FOrdNe, VectorPredicate::Compare);
        case Op::S_CmpNltF32:
            return LiftScalarFloatCompare(instruction, false, IL::CompareOp::FUnordGe, VectorPredicate::Compare);
        case Op::S_CmpLtF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdLt, VectorPredicate::Compare);
        case Op::S_CmpEqF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdEq, VectorPredicate::Compare);
        case Op::S_CmpLeF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdLe, VectorPredicate::Compare);
        case Op::S_CmpGtF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdGt, VectorPredicate::Compare);
        case Op::S_CmpLgF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdNe, VectorPredicate::Compare);
        case Op::S_CmpGeF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdGe, VectorPredicate::Compare);
        case Op::S_CmpOF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdEq, VectorPredicate::Ordered);
        case Op::S_CmpUF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdEq, VectorPredicate::Unordered);
        case Op::S_CmpNgeF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FUnordLt, VectorPredicate::Compare);
        case Op::S_CmpNlgF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FUnordEq, VectorPredicate::Compare);
        case Op::S_CmpNgtF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FUnordLe, VectorPredicate::Compare);
        case Op::S_CmpNleF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FUnordGt, VectorPredicate::Compare);
        case Op::S_CmpNeqF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FOrdNe, VectorPredicate::Compare);
        case Op::S_CmpNltF16:
            return LiftScalarFloatCompare(instruction, true, IL::CompareOp::FUnordGe, VectorPredicate::Compare);
        case Op::S_AddF32:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FAdd, false);
        case Op::S_SubF32:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FSub, false);
        case Op::S_MulF32:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FMul, false);
        case Op::S_MinNumF32:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FMin, false);
        case Op::S_MaxNumF32:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FMax, false);
        case Op::S_AddF16:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FAdd, true);
        case Op::S_SubF16:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FSub, true);
        case Op::S_MulF16:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FMul, true);
        case Op::S_MinNumF16:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FMin, true);
        case Op::S_MaxNumF16:
            return LiftScalarFloatBinary(instruction, IL::BinaryOp::FMax, true);
        case Op::S_MinimumF32:
            return LiftScalarFloatMinimum(instruction, false, false);
        case Op::S_MaximumF32:
            return LiftScalarFloatMinimum(instruction, false, true);
        case Op::S_MinimumF16:
            return LiftScalarFloatMinimum(instruction, true, false);
        case Op::S_MaximumF16:
            return LiftScalarFloatMinimum(instruction, true, true);
        case Op::S_FmacF32:
            return LiftScalarFmac(instruction, false);
        case Op::S_FmacF16:
            return LiftScalarFmac(instruction, true);
        case Op::S_PackLlB32B16:
        case Op::S_PackLhB32B16:
        case Op::S_PackHhB32B16:
        case Op::S_PackHlB32B16:
            return LiftScalarPack(instruction);
        case Op::S_Lshl1AddU32:
            return LiftScalarLshlAdd(instruction, 1);
        case Op::S_Lshl2AddU32:
            return LiftScalarLshlAdd(instruction, 2);
        case Op::S_Lshl3AddU32:
            return LiftScalarLshlAdd(instruction, 3);
        case Op::S_Lshl4AddU32:
            return LiftScalarLshlAdd(instruction, 4);
        case Op::S_CmovB32:
        case Op::S_CmovB64:
            return LiftScalarCmov(instruction);
        case Op::S_BitreplicateB64B32:
            return LiftBitreplicate(instruction);
        case Op::S_QuadmaskB32:
        case Op::S_QuadmaskB64:
            return LiftQuadmask(instruction);
        case Op::S_CeilF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Ceil, false);
        case Op::S_FloorF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Floor, false);
        case Op::S_TruncF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Trunc, false);
        case Op::S_RndneF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::RoundEven, false);
        case Op::S_CeilF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Ceil, true);
        case Op::S_FloorF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Floor, true);
        case Op::S_TruncF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Trunc, true);
        case Op::S_RndneF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::RoundEven, true);
        case Op::S_CvtF32I32:
        case Op::S_CvtF32U32:
        case Op::S_CvtI32F32:
        case Op::S_CvtU32F32:
        case Op::S_CvtF16F32:
        case Op::S_CvtF32F16:
        case Op::S_CvtHiF32F16:
            return LiftScalarConvert(instruction);
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
            const IL::ValueId guard = CurrentGuard(instruction.pc);
            m_builder.IntrinsicVoid(IL::IntrinsicId::TestBufferStore, {*index, *value}, payload, instruction.pc);
            ApplyGuard(instruction.pc, guard);
            return {};
        }
        case Op::S_LoadB32:
        case Op::S_LoadB64:
        case Op::S_LoadB96:
        case Op::S_LoadB128:
        case Op::S_LoadB256:
        case Op::S_LoadB512:
        case Op::S_LoadI8:
        case Op::S_LoadU8:
        case Op::S_LoadI16:
        case Op::S_LoadU16:
        case Op::S_BufferLoadB32:
        case Op::S_BufferLoadB64:
        case Op::S_BufferLoadB96:
        case Op::S_BufferLoadB128:
        case Op::S_BufferLoadB256:
        case Op::S_BufferLoadB512:
        case Op::S_BufferLoadI8:
        case Op::S_BufferLoadU8:
        case Op::S_BufferLoadI16:
        case Op::S_BufferLoadU16:
        case Op::S_DcacheInv:
        case Op::S_Gl1Inv:
        case Op::S_AtcProbe:
        case Op::S_AtcProbeBuffer:
        case Op::S_PrefetchInst:
        case Op::S_PrefetchInstPcRel:
        case Op::S_PrefetchData:
        case Op::S_BufferPrefetchData:
        case Op::S_PrefetchDataPcRel:
            return LiftScalarMemory(instruction);
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
            // The program end also ends the vertex and primitive a geometry
            // shader left open.
            if (m_module.stage == IL::Stage::Geometry)
            {
                const auto emitted = EndVertex(instruction.pc);
                if (!emitted)
                    return std::unexpected(emitted.error());
                const auto primitive = EndPrimitive(instruction.pc, static_cast<std::uint16_t>(instruction.op));
                if (!primitive)
                    return std::unexpected(primitive.error());
            }

            IL::Terminator terminator;
            terminator.kind = IL::TerminatorKind::Ret;
            terminator.sourcePc = instruction.pc;
            m_builder.Terminate(m_builder.CurrentBlock(), std::move(terminator));
            return {};
        }
        case Op::S_Barrier:
            return std::unexpected(LiftError("SR-GFX-LIFT-020", "s_barrier is not implemented yet", instruction.pc));
        case Op::S_Nop:
        case Op::S_Clause:
        case Op::S_DelayAlu:
        case Op::S_WaitAlu:
        case Op::S_Waitcnt:
        case Op::S_WaitIdle:
        case Op::S_WaitEvent:
        case Op::S_RoundMode:
        case Op::S_DenormMode:
        case Op::S_BarrierWait:
        case Op::S_Wakeup:
        case Op::S_Setprio:
        case Op::S_Incperflevel:
        case Op::S_Decperflevel:
        case Op::S_Ttracedata:
        case Op::S_TtracedataImm:
        case Op::S_IcacheInv:
        case Op::S_WaitLoadcnt:
        case Op::S_WaitStorecnt:
        case Op::S_WaitSamplecnt:
        case Op::S_WaitBvhcnt:
        case Op::S_WaitExpcnt:
        case Op::S_WaitDscnt:
        case Op::S_WaitKmcnt:
        case Op::S_WaitLoadcntDscnt:
        case Op::S_WaitStorecntDscnt:
        case Op::S_CodeEnd:
        case Op::S_EndpgmSaved:
        case Op::S_Sendmsg:
        case Op::S_Sendmsghalt:
        case Op::S_Sethalt:
        case Op::S_Setkill:
        case Op::S_Sleep:
            return LiftScalarControl(instruction);
        case Op::V_MbcntLoU32B32:
            return LiftMbcnt(instruction);
        case Op::V_CmpClassF16:
            return LiftClassCompare(instruction, CompareValueKind::F16, false);
        case Op::V_CmpClassF32:
            return LiftClassCompare(instruction, CompareValueKind::F32, false);
        case Op::V_CmpClassF64:
            return LiftClassCompare(instruction, CompareValueKind::F64, false);
        case Op::V_SwapB32:
        case Op::V_SwapB16:
            return LiftSwap(instruction);
        case Op::V_SatPkU8I16:
            return LiftSatPkU8(instruction);
        case Op::V_Pipeflush:
            return {};
        case Op::V_MinNumF16:
            return LiftF16Binary(instruction, IL::BinaryOp::FMin);
        case Op::V_MaxNumF16:
            return LiftF16Binary(instruction, IL::BinaryOp::FMax);
        case Op::V_SExpF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Exp2, false);
        case Op::V_SExpF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Exp2, true);
        case Op::V_SLogF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Log2, false);
        case Op::V_SLogF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Log2, true);
        case Op::V_SRcpF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Rcp, false);
        case Op::V_SRcpF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Rcp, true);
        case Op::V_SRsqF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Rsq, false);
        case Op::V_SRsqF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Rsq, true);
        case Op::V_SSqrtF32:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Sqrt, false);
        case Op::V_SSqrtF16:
            return LiftScalarUnaryFloat(instruction, IL::UnaryOp::Sqrt, true);
        case Op::V_RcpIflagF32:
            return LiftUnaryFloat(instruction, IL::UnaryOp::Rcp);
        case Op::V_Min3NumF16:
            return LiftMinMax3(instruction, true, true, false, true, false);
        case Op::V_Max3NumF16:
            return LiftMinMax3(instruction, false, false, false, true, false);
        case Op::V_Min3I16:
            return LiftMinMax3(instruction, true, true, false, false, true);
        case Op::V_Min3U16:
            return LiftMinMax3(instruction, true, true, false, false, false);
        case Op::V_Max3I16:
            return LiftMinMax3(instruction, false, false, false, false, true);
        case Op::V_Max3U16:
            return LiftMinMax3(instruction, false, false, false, false, false);
        case Op::V_Med3I16:
            return LiftMinMax3(instruction, false, false, true, false, true);
        case Op::V_Med3U16:
            return LiftMinMax3(instruction, false, false, true, false, false);
        case Op::V_Med3NumF32:
            return LiftMinMax3(instruction, false, false, true, true, false);
        case Op::V_Med3NumF16:
            return LiftMinMax3(instruction, false, false, true, true, false);
        case Op::V_Minimum3F32:
            return LiftMinMax3(instruction, true, true, false, true, false);
        case Op::V_Maximum3F32:
            return LiftMinMax3(instruction, false, false, false, true, false);
        case Op::V_Minimum3F16:
            return LiftMinMax3(instruction, true, true, false, true, false);
        case Op::V_Maximum3F16:
            return LiftMinMax3(instruction, false, false, false, true, false);
        case Op::V_MinmaxU32:
            return LiftMinMax3(instruction, true, false, false, false, false);
        case Op::V_MaxminU32:
            return LiftMinMax3(instruction, false, true, false, false, false);
        case Op::V_MinmaxI32:
            return LiftMinMax3(instruction, true, false, false, false, true);
        case Op::V_MaxminI32:
            return LiftMinMax3(instruction, false, true, false, false, true);
        case Op::V_MinmaxNumF32:
            return LiftMinMax3(instruction, true, false, false, true, false);
        case Op::V_MaxminNumF32:
            return LiftMinMax3(instruction, false, true, false, true, false);
        case Op::V_MinmaxNumF16:
            return LiftMinMax3(instruction, true, false, false, true, false);
        case Op::V_MaxminNumF16:
            return LiftMinMax3(instruction, false, true, false, true, false);
        case Op::V_MinimummaximumF32:
            return LiftMinMax3(instruction, true, false, false, true, false);
        case Op::V_MaximumminimumF32:
            return LiftMinMax3(instruction, false, true, false, true, false);
        case Op::V_MinimummaximumF16:
            return LiftMinMax3(instruction, true, false, false, true, false);
        case Op::V_MaximumminimumF16:
            return LiftMinMax3(instruction, false, true, false, true, false);
        case Op::V_AddNcU16:
            return LiftU16Binary(instruction, IL::BinaryOp::IAdd, false);
        case Op::V_SubNcU16:
            return LiftU16Binary(instruction, IL::BinaryOp::ISub, false);
        case Op::V_MulLoU16:
            return LiftU16Binary(instruction, IL::BinaryOp::IMul, false);
        case Op::V_AddNcI16:
            return LiftU16Binary(instruction, IL::BinaryOp::IAdd, true);
        case Op::V_SubNcI16:
            return LiftU16Binary(instruction, IL::BinaryOp::ISub, true);
        case Op::V_MinU16:
            return LiftU16MinMax(instruction, true, false);
        case Op::V_MaxU16:
            return LiftU16MinMax(instruction, false, false);
        case Op::V_MinI16:
            return LiftU16MinMax(instruction, true, true);
        case Op::V_MaxI16:
            return LiftU16MinMax(instruction, false, true);
        case Op::V_LshlrevB16:
            return LiftU16Shift(instruction, true, false);
        case Op::V_LshrrevB16:
            return LiftU16Shift(instruction, false, false);
        case Op::V_AshrrevI16:
            return LiftU16Shift(instruction, false, true);
        case Op::V_MadU16:
            return LiftMad16(instruction, false, false);
        case Op::V_MadI16:
            return LiftMad16(instruction, true, false);
        case Op::V_MadU32U16:
            return LiftMad16(instruction, false, true);
        case Op::V_MadI32I16:
            return LiftMad16(instruction, true, true);
        case Op::V_CvtPkI16F32:
            return LiftPkConvert(instruction, PkConvert::I16F32);
        case Op::V_CvtPkU16F32:
            return LiftPkConvert(instruction, PkConvert::U16F32);
        case Op::V_CvtPkI16I32:
            return LiftPkConvert(instruction, PkConvert::I16I32);
        case Op::V_CvtPkU16U32:
            return LiftPkConvert(instruction, PkConvert::U16U32);
        case Op::V_CvtPkNormI16F32:
            return LiftPkConvert(instruction, PkConvert::NormI16F32);
        case Op::V_CvtPkNormU16F32:
            return LiftPkConvert(instruction, PkConvert::NormU16F32);
        case Op::V_CvtPkNormI16F16:
            return LiftPkConvert(instruction, PkConvert::NormI16F16);
        case Op::V_CvtPkNormU16F16:
            return LiftPkConvert(instruction, PkConvert::NormU16F16);
        case Op::V_CvtPkRtzF16F32:
            return LiftPkConvert(instruction, PkConvert::RtzF16F32);
        case Op::V_MinimumF32:
            return LiftScalarFloatMinimum(instruction, false, false);
        case Op::V_MaximumF32:
            return LiftScalarFloatMinimum(instruction, false, true);
        case Op::V_MinimumF16:
            return LiftScalarFloatMinimum(instruction, true, false);
        case Op::V_MaximumF16:
            return LiftScalarFloatMinimum(instruction, true, true);
        case Op::V_PackB32F16:
            return LiftPackB32F16(instruction);
        case Op::V_PkFmacF16:
            return LiftPackedFmac(instruction);
        case Op::V_SadU8:
            return LiftSadU8(instruction, false, false);
        case Op::V_SadHiU8:
            return LiftSadU8(instruction, true, false);
        case Op::V_MsadU8:
            return LiftSadU8(instruction, false, true);
        case Op::V_LerpU8:
            return LiftLerpU8(instruction);
        case Op::V_CubeidF32:
            return LiftCube(instruction, 0);
        case Op::V_CubemaF32:
            return LiftCube(instruction, 1);
        case Op::V_CubescF32:
            return LiftCube(instruction, 2);
        case Op::V_CubetcF32:
            return LiftCube(instruction, 3);
        case Op::V_DivFixupF32:
            return LiftDivFixup(instruction);
        case Op::V_MullitF32:
            return LiftMulLit(instruction);
        case Op::V_FmaDx9ZeroF32:
            return LiftFmaDx9(instruction);
        case Op::V_DivFmasF32:
            return LiftDivFmas(instruction);
        case Op::V_Dot2F32F16:
            return LiftDot2F32(instruction, false);
        case Op::V_Dot2F32Bf16:
            return LiftDot2F32(instruction, true);
        case Op::V_Dot2F16F16:
            return LiftDot2F16(instruction, false);
        case Op::V_Dot2Bf16Bf16:
            return LiftDot2F16(instruction, true);
        case Op::V_Dot4I32Iu8:
            return LiftDotInteger(instruction, 4, 8, true, false);
        case Op::V_Dot4U32U8:
            return LiftDotInteger(instruction, 4, 8, false, false);
        case Op::V_Dot8I32Iu4:
            return LiftDotInteger(instruction, 8, 4, true, false);
        case Op::V_Dot8U32U4:
            return LiftDotInteger(instruction, 8, 4, false, false);
        case Op::V_FmaMixF32:
            return LiftFmaMix(instruction, 0);
        case Op::V_FmaMixloF16:
            return LiftFmaMix(instruction, 1);
        case Op::V_FmaMixhiF16:
            return LiftFmaMix(instruction, 2);
        case Op::V_CvtF32Fp8:
            return LiftCvtFromFp8(instruction, false, false);
        case Op::V_CvtF32Bf8:
            return LiftCvtFromFp8(instruction, true, false);
        case Op::V_CvtPkF32Fp8:
            return LiftCvtFromFp8(instruction, false, true);
        case Op::V_CvtPkF32Bf8:
            return LiftCvtFromFp8(instruction, true, true);
        case Op::V_Dot4F32Fp8Bf8:
            return LiftDot4Fp8(instruction, false, true);
        case Op::V_Dot4F32Bf8Fp8:
            return LiftDot4Fp8(instruction, true, false);
        case Op::V_Dot4F32Fp8Fp8:
            return LiftDot4Fp8(instruction, false, false);
        case Op::V_Dot4F32Bf8Bf8:
            return LiftDot4Fp8(instruction, true, true);
        case Op::V_CndmaskB16:
            return LiftVop3Cndmask(instruction, true);
        case Op::V_CvtPkU8F32:
            return LiftCvtPkU8F32(instruction);
        case Op::V_SadU16:
            return LiftSadU16(instruction);
        case Op::V_LshrrevB64:
            return LiftShift64Vector(instruction, false);
        case Op::V_AshrrevI64:
            return LiftShift64Vector(instruction, true);
        case Op::V_DivFixupF16:
            return std::unexpected(LiftError("SR-GFX-LIFT-082", "unsupported wide divide fixup", instruction.pc));
        case Op::V_DivFmasF64:
            return std::unexpected(LiftError("SR-GFX-LIFT-082", "unsupported wide divide fixup", instruction.pc));
        case Op::V_FrexpExpI32F32:
            return LiftFrexp(instruction, false, false);
        case Op::V_FrexpMantF32:
            return LiftFrexp(instruction, true, false);
        case Op::V_FrexpExpI16F16:
            return LiftFrexp(instruction, false, true);
        case Op::V_FrexpMantF16:
            return LiftFrexp(instruction, true, true);
        case Op::S_AndSaveexecB32:
            return LiftSaveExec(instruction, IL::BinaryOp::And, false, false);
        case Op::S_OrSaveexecB32:
            return LiftSaveExec(instruction, IL::BinaryOp::Or, false, false);
        case Op::S_XorSaveexecB32:
            return LiftSaveExec(instruction, IL::BinaryOp::Xor, false, false);
        case Op::S_NandSaveexecB32:
            return LiftSaveExec(instruction, IL::BinaryOp::And, true, false);
        case Op::S_NorSaveexecB32:
            return LiftSaveExec(instruction, IL::BinaryOp::Or, true, false);
        case Op::S_XnorSaveexecB32:
            return LiftSaveExec(instruction, IL::BinaryOp::Xor, true, false);
        case Op::S_AndSaveexecB64:
            return LiftSaveExec(instruction, IL::BinaryOp::And, false, true);
        case Op::S_OrSaveexecB64:
            return LiftSaveExec(instruction, IL::BinaryOp::Or, false, true);
        case Op::S_XorSaveexecB64:
            return LiftSaveExec(instruction, IL::BinaryOp::Xor, false, true);
        case Op::S_NandSaveexecB64:
            return LiftSaveExec(instruction, IL::BinaryOp::And, true, true);
        case Op::S_NorSaveexecB64:
            return LiftSaveExec(instruction, IL::BinaryOp::Or, true, true);
        case Op::S_XnorSaveexecB64:
            return LiftSaveExec(instruction, IL::BinaryOp::Xor, true, true);
        case Op::S_CvtPkRtzF16F32:
            return LiftCvtPkRtzF16F32Scalar(instruction);
        case Op::V_MinimumF64:
            return LiftF64MinMax(instruction, false);
        case Op::V_MaximumF64:
            return LiftF64MinMax(instruction, true);
        case Op::S_CmovkI32:
        case Op::S_AddkCoI32:
            return LiftScalarImmediate(instruction);
        case Op::Invalid:
            break;
        default:
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

            if (m_items[i].op == Op::S_Endpgm || m_items[i].op == Op::S_CodeEnd ||
                m_items[i].op == Op::S_EndpgmSaved || IsBranch(m_items[i].op))
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

    // Closes the vertex currently being exported: a geometry shader writes one
    // vertex's outputs and then asks for that vertex to be emitted.
    [[nodiscard]] Result<void> EndVertex(std::uint32_t pc)
    {
        if (!m_pendingVertex)
            return {};
        m_pendingVertex = false;
        m_primitiveOpen = true;
        m_builder.EmitVoid(IL::Op::Intrinsic,
                           static_cast<std::uint16_t>(IL::IntrinsicId::EmitVertex),
                           {},
                           {},
                           IL::NoPayload,
                           pc,
                           0);
        return {};
    }

    // Ends the strip, if one is open. Ending a primitive that was never
    // opened is not an error, it is simply already closed.
    [[nodiscard]] Result<void> EndPrimitive(std::uint32_t pc, std::uint16_t opcode)
    {
        if (!m_primitiveOpen)
            return {};
        m_primitiveOpen = false;
        m_builder.EmitVoid(IL::Op::Intrinsic,
                           static_cast<std::uint16_t>(IL::IntrinsicId::EndPrimitive),
                           {},
                           {},
                           IL::NoPayload,
                           pc,
                           opcode);
        return {};
    }

    IL::Module& m_module;
    IL::Function& m_function;
    IL::FunctionBuilder& m_builder;
    Arch m_arch;
    LiftOptions m_options;

    // A geometry vertex has been written and is waiting for its emit.
    bool m_pendingVertex = false;
    // The staged slot the next geometry message publishes.
    IL::ValueId m_geometryEmitted = IL::InvalidValue;
    // Emitted vertices that the current strip has not closed yet.
    bool m_primitiveOpen = false;

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
    std::vector<StageSlot> m_stageSlots;
    struct ImageBinding
    {
        std::uint32_t srsrc = 0;
        std::uint32_t binding = 0;
        bool sampled = false;
    };
    std::vector<ImageBinding> m_imageBindings;
    bool m_execMayBeFalse = false;
};

} // namespace

Result<IL::Module> Lift(const DecodedShader& shader, Arch arch, std::string_view entryPoint, LiftOptions options)
{
    if (shader.rawBytecode.empty())
        return std::unexpected(LiftError("SR-GFX-LIFT-030", "code object has no .text payload", 0));

    // The translation stage models one invocation: thread ids and memory. A
    // vertex or pixel program needs IO variables, interpolation and exports,
    // which the emitter does not have yet, so it is refused rather than lifted
    // as if it were compute.
    // Compute, vertex and fragment programs have an execution model. Merged
    // geometry stages need the ES/GS pairing and NGG needs the wave allocator,
    // so they are refused rather than lifted as something else.
    if (shader.stage != IL::Stage::Unknown && shader.stage != IL::Stage::Compute
        && shader.stage != IL::Stage::Vertex && shader.stage != IL::Stage::Fragment
        && shader.stage != IL::Stage::Geometry)
    {
        return std::unexpected(LiftError("SR-GFX-LIFT-032",
                                         "stage '" + std::string(IL::ToString(shader.stage))
                                             + "' has no execution model yet",
                                         0));
    }

    if (shader.rawBytecode.size() % 4 != 0)
        return std::unexpected(LiftError("SR-GFX-LIFT-031", "code object size is not a multiple of 4", 0));

    std::vector<std::uint32_t> words(shader.rawBytecode.size() / 4);
    std::memcpy(words.data(), shader.rawBytecode.data(), words.size() * sizeof(std::uint32_t));

    IL::Module module;
    module.stage = shader.stage == IL::Stage::Unknown ? IL::Stage::Compute : shader.stage;
    module.entryPoint = std::string(entryPoint);
    module.architecture = std::string(GetArchInfo(arch).name);
    module.executionModes.workgroupSize = {64, 1, 1};
    // A geometry stage's topology is pipeline state, not an instruction, so it
    // arrives with the options.
    module.executionModes.inputTopology = options.geometryInputTopology;
    module.executionModes.outputTopology = options.geometryOutputTopology;
    module.executionModes.outputVertices = options.geometryOutputVertices;
    module.executionModes.outputRegisters = options.geometryOutputRegisters;
    module.testAbi = options.testAbi;

    IL::Function& function = IL::CreateFunction(module, std::string(entryPoint), module.types.Void());
    function.isEntry = true;
    module.entryFunction = function.id;

    IL::FunctionBuilder builder(module, function);
    Lifter lifter(module, function, builder, arch, options);
    const auto status = lifter.Run(words);
    if (!status)
        return std::unexpected(status.error());

    return module;
}

} // namespace SR::Frontend::Gfx
