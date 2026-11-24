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
    S_SextI32I8,
    S_SextI32I16,
    S_BrevB64,
    S_Bcnt0I32B32,
    S_Bcnt0I32B64,
    S_Bcnt1I32B64,
    S_Bitset0B32,
    S_Bitset1B32,
    S_Bitset0B64,
    S_Bitset1B64,
    S_LshlB64,
    S_LshrB64,
    S_AshrI64,
    S_BfmB64,
    S_BfeI64,
    S_BfeU64,
    S_MulHiI32,
    S_MulHiU32,
    S_Ff0I32B32,
    S_Ff1I32B64,
    S_FlbitI32B64,
    S_FlbitI32,
    S_FlbitI32I64,
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
    V_Nop,
    V_ReadfirstlaneB32,
    V_CvtNearestI32F32,
    V_CvtFloorI32F32,
    V_CvtF32Ubyte0,
    V_CvtF32Ubyte1,
    V_CvtF32Ubyte2,
    V_CvtF32Ubyte3,
    V_FractF32,
    V_TruncF32,
    V_CeilF32,
    V_RndneF32,
    V_FloorF32,
    V_SinF32,
    V_CosF32,
    V_BfrevB32,
    V_ClsI32,
    V_CvtI32I16,
    V_CvtU32U16,
    V_SubrevF32,
    V_MulDx9ZeroF32,
    V_MulI32I24,
    V_MulHiI32I24,
    V_MulU32U24,
    V_MulHiU32U24,
    V_XnorB32,
    V_LshlrevB64,
    V_AddCoCiU32,
    V_SubCoCiU32,
    V_SubrevCoCiU32,
    V_SubrevNcU32,
    V_Add3U32,
    V_AddLshlU32,
    V_AndOrB32,
    V_Or3B32,
    V_Xor3B32,
    V_LshlAddU32,
    V_LshlOrB32,
    V_XadU32,
    V_MulHiI32,
    V_MulHiU32,
    V_Min3I32,
    V_Min3U32,
    V_Med3I32,
    V_Med3U32,
    V_Max3I32,
    V_Max3U32,
    V_MadI32I24,
    V_MadU32U24,
    V_SadU32,
    V_LdexpF32,
    V_CvtF16F32,
    V_CvtF32F16,
    V_CvtF16U16,
    V_CvtF16I16,
    V_CvtU16F16,
    V_CvtI16F16,
    V_CvtNormI16F16,
    V_CvtNormU16F16,
    V_RcpF16,
    V_RsqF16,
    V_SqrtF16,
    V_ExpF16,
    V_LogF16,
    V_FloorF16,
    V_CeilF16,
    V_TruncF16,
    V_RndneF16,
    V_FractF16,
    V_SinF16,
    V_CosF16,
    V_MovB16,
    V_NotB16,
    V_AddF16,
    V_SubF16,
    V_SubrevF16,
    V_MulF16,
    V_FmacF16,
    V_MinF16,
    V_MaxF16,
    V_LdexpF16,
    V_Min3F16,
    V_Max3F16,
    V_FmaF16,
    V_CvtI32F64,
    V_CvtF64I32,
    V_CvtF32F64,
    V_CvtF64F32,
    V_CvtU32F64,
    V_CvtF64U32,
    V_TruncF64,
    V_CeilF64,
    V_RndneF64,
    V_FloorF64,
    V_RcpF64,
    V_RsqF64,
    V_SqrtF64,
    V_FractF64,
    V_AddF64,
    V_MulF64,
    V_MinF64,
    V_MaxF64,
    V_FmaF64,
    V_LdexpF64,
    V_PkMadI16,
    V_PkMulLoU16,
    V_PkAddI16,
    V_PkSubI16,
    V_PkLshlrevB16,
    V_PkLshrrevB16,
    V_PkAshrrevI16,
    V_PkMaxI16,
    V_PkMinI16,
    V_PkMadU16,
    V_PkAddU16,
    V_PkSubU16,
    V_PkMaxU16,
    V_PkMinU16,
    V_PkFmaF16,
    V_PkAddF16,
    V_PkMulF16,
    V_PkMinF16,
    V_PkMaxF16,
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
    V_CmpFalseF32,
    V_CmpOrdF32,
    V_CmpUnordF32,
    V_CmpNgeF32,
    V_CmpNlgF32,
    V_CmpNgtF32,
    V_CmpNleF32,
    V_CmpNeqF32,
    V_CmpNltF32,
    V_CmpTrueF32,
    V_CmpFalseI32,
    V_CmpTrueI32,
    V_CmpFalseU32,
    V_CmpTrueU32,
    V_CmpFalseF16,
    V_CmpLtF16,
    V_CmpEqF16,
    V_CmpLeF16,
    V_CmpGtF16,
    V_CmpLgF16,
    V_CmpGeF16,
    V_CmpOrdF16,
    V_CmpUnordF16,
    V_CmpNgeF16,
    V_CmpNlgF16,
    V_CmpNgtF16,
    V_CmpNleF16,
    V_CmpNeqF16,
    V_CmpNltF16,
    V_CmpTrueF16,
    V_CmpFalseF64,
    V_CmpLtF64,
    V_CmpEqF64,
    V_CmpLeF64,
    V_CmpGtF64,
    V_CmpLgF64,
    V_CmpGeF64,
    V_CmpOrdF64,
    V_CmpUnordF64,
    V_CmpNgeF64,
    V_CmpNlgF64,
    V_CmpNgtF64,
    V_CmpNleF64,
    V_CmpNeqF64,
    V_CmpNltF64,
    V_CmpTrueF64,
    V_CmpLtI16,
    V_CmpEqI16,
    V_CmpLeI16,
    V_CmpGtI16,
    V_CmpNeI16,
    V_CmpGeI16,
    V_CmpLtU16,
    V_CmpEqU16,
    V_CmpLeU16,
    V_CmpGtU16,
    V_CmpNeU16,
    V_CmpGeU16,
    V_CmpFalseI64,
    V_CmpLtI64,
    V_CmpEqI64,
    V_CmpLeI64,
    V_CmpGtI64,
    V_CmpNeI64,
    V_CmpGeI64,
    V_CmpTrueI64,
    V_CmpFalseU64,
    V_CmpLtU64,
    V_CmpEqU64,
    V_CmpLeU64,
    V_CmpGtU64,
    V_CmpNeU64,
    V_CmpGeU64,
    V_CmpTrueU64,
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
    Vop1Sdst,
    Vop3Sdst,
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
    bool execMask = false; // V_CMPX updates EXEC in addition to VCC
    std::uint8_t opSel = 0;    // VOP3P source-half selectors
    std::uint8_t opSelHi = 0;  // VOP3P high selector bits
    std::uint8_t negHi = 0;    // VOP3P high-half negate bits

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
