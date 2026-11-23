#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "SR/Common/Diagnostic.hpp"

namespace SR::Frontend::Gfx {

// AMD GPU generations share one encoding lineage. Each level inherits its
// parent's opcode table and applies a delta; only changed entries are listed.
enum class Arch : std::uint8_t
{
    Gfx10,   // RDNA1
    Gfx1013, // PS5 (RDNA2 derivative)
    Gfx103,  // RDNA2
    Gfx11,   // RDNA3
    Gfx115,  // RDNA3.5
    Gfx12,   // RDNA4
};

struct ArchInfo
{
    Arch arch = Arch::Gfx10;
    Arch parent = Arch::Gfx10;
    std::string_view name;        // "gfx1200"
    std::string_view family;      // "rdna4"
    // SMEM encodes the opcode in different bit positions across the lineage.
    std::uint8_t smemOpcodeOffset = 18;
    std::uint8_t smemOpcodeWidth = 8;
};

[[nodiscard]] const ArchInfo* FindArch(std::string_view name) noexcept;
[[nodiscard]] const ArchInfo& GetArchInfo(Arch arch) noexcept;
[[nodiscard]] bool Inherits(Arch arch, Arch ancestor) noexcept;

enum class Encoding : std::uint8_t
{
    Sop1,
    Sop2,
    SopC,
    SopP,
    SopK,
    Vop1,
    Vop2,
    Vop3,
    Vop3P,
    VopC,
    Smem,
    Ds,
    Global,
    Flat,
    Buffer,
    Scratch,
};

[[nodiscard]] std::string_view ToString(Encoding encoding) noexcept;

enum class Op : std::uint16_t
{
    Invalid,
    S_MovB32,
    S_AddU32,
    S_SubU32,
    S_AddCoU32,
    S_SubCoU32,
    S_AndB32,
    S_OrB32,
    S_XorB32,
    S_LshlB32,
    S_LshrB32,
    S_CselectB32,
    S_NotB32,
    S_BrevB32,
    S_Bcnt1I32B32,
    S_AbsI32,
    S_MulI32,
    S_MinI32,
    S_MaxI32,
    S_MinU32,
    S_MaxU32,
    S_NandB32,
    S_NorB32,
    S_XnorB32,
    S_AshrI32,
    S_MovkI32,
    S_AddI32,
    S_SubI32,
    S_AddcU32,
    S_SubbU32,
    S_AddkI32,
    S_MulkI32,
    S_Andn2B32,
    S_Orn2B32,
    S_AndNot1B32,
    S_OrNot1B32,
    S_Ff1I32B32,
    S_FlbitI32B32,
    S_AbsDiffI32,
    S_BfmB32,
    S_BfeI32,
    S_BfeU32,
    S_MovB64,
    S_NotB64,
    S_AndB64,
    S_OrB64,
    S_XorB64,
    S_CselectB64,
    S_CmpEqI32,
    S_CmpLgI32,
    S_CmpLtI32,
    S_CmpLeI32,
    S_CmpGtI32,
    S_CmpGeI32,
    S_CmpEqU32,
    S_CmpLgU32,
    S_CmpLtU32,
    S_CmpLeU32,
    S_CmpGtU32,
    S_CmpGeU32,
    S_Endpgm,
    S_Branch,
    S_CbranchScc0,
    S_CbranchScc1,
    S_CbranchVccz,
    S_CbranchVccnz,
    S_CbranchExecz,
    S_CbranchExecnz,
    S_Barrier,
    V_MovB32,
    V_AddF32,
    V_SubF32,
    V_MulF32,
    V_FmaF32,
    V_AndB32,
    V_OrB32,
    V_XorB32,
    V_CndmaskB32,
    V_NotB32,
    V_CvtF32I32,
    V_CvtF32U32,
    V_CvtI32F32,
    V_CvtU32F32,
    V_MinI32,
    V_MaxI32,
    V_MinU32,
    V_MaxU32,
    V_LshlrevB32,
    V_AshrrevI32,
    V_MulLoU32,
    V_FfbhU32,
    V_FfblB32,
    V_ClzI32U32,
    V_CtzI32B32,
    V_BcntU32B32,
    V_AddNcU32,
    V_SubNcU32,
    V_RcpF32,
    V_RsqF32,
    V_SqrtF32,
    V_ExpF32,
    V_LogF32,
    V_MinF32,
    V_MaxF32,
    V_Min3F32,
    V_Max3F32,
    V_MacF32,
    V_MadF32,
    V_LshrrevB32,
    V_CmpLtI32,
    V_CmpEqI32,
    V_CmpLeI32,
    V_CmpGtI32,
    V_CmpNeI32,
    V_CmpGeI32,
    V_CmpLtU32,
    V_CmpEqU32,
    V_CmpLeU32,
    V_CmpGtU32,
    V_CmpNeU32,
    V_CmpGeU32,
    V_CmpLtF32,
    V_CmpEqF32,
    V_CmpLeF32,
    V_CmpGtF32,
    V_CmpLgF32,
    V_CmpGeF32,
    V_BfeI32,
    V_BfeU32,
    V_BfiB32,
    V_BfmB32,
    V_AlignbitB32,
    V_AlignbyteB32,
    S_LoadB32,
    S_BufferLoadB32,
    GlobalLoadB32,
    GlobalStoreB32,
};

[[nodiscard]] std::string_view ToString(Op op) noexcept;
[[nodiscard]] bool IsVectorOp(Op op) noexcept;

enum class OperandKind : std::uint8_t
{
    None,
    Sgpr,
    Vgpr,
    Sdst,   // 7-bit scalar destination; may name EXEC/VCC
    Ssrc,   // 8-bit scalar source; may be an inline constant or literal
    Src,    // 9-bit vector source; may be an inline constant or literal
    Vsrc,   // 8-bit VGPR source
    Literal,
    Simm16,
    Imm32,
    Label,
    Vcc,
    Exec,
    // Memory operand layouts. The decoder normalises these to Sdst/Sgpr/Vgpr/
    // Imm32 so the lifter sees one consistent operand order.
    SmemSdst,
    SmemSbase,
    SmemOffset,
    SmemOffset24,
    SmemSoffset,
    FlatVdst,
    FlatVaddr,
    FlatSaddr,
    FlatData,
    FlatOffset12,
    FlatOffset13,
    GlobalVdst,
    GlobalVaddr,
    GlobalVsrc,
    GlobalIoffset,
};

struct Operand
{
    OperandKind kind = OperandKind::None;
    std::uint32_t value = 0;
    bool isInline = false; // value is an inline-constant index, not a register
};

struct DecodedInstruction
{
    Op op = Op::Invalid;
    Encoding encoding = Encoding::Sop1;
    Arch arch = Arch::Gfx10;
    std::uint32_t pc = 0;       // byte offset in the code object
    std::uint32_t wordCount = 0;
    std::uint32_t word0 = 0;
    std::uint32_t word1 = 0;

    // VOP3 modifiers, kept for diagnostics and lowering.
    std::uint8_t abs = 0;
    std::uint8_t neg = 0;
    std::uint8_t omod = 0;
    bool clamp = false;

    std::array<Operand, 4> operands{};
    std::uint8_t operandCount = 0;
};

[[nodiscard]] const Operand* FindOperand(const DecodedInstruction& instruction, OperandKind kind) noexcept;

// Decodes one instruction. `words` starts at the instruction. On success the
// decoder consumes `DecodedInstruction::wordCount` words.
[[nodiscard]] Result<DecodedInstruction> Decode(std::span<const std::uint32_t> words,
                                                Arch arch,
                                                std::uint32_t bytePc);

[[nodiscard]] std::uint32_t InlineInteger(std::uint32_t index) noexcept;
[[nodiscard]] std::optional<std::uint32_t> InlineFloatBits(std::uint32_t index) noexcept;

} // namespace SR::Frontend::Gfx
