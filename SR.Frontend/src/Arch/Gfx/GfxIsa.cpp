#include "SR/Frontend/Arch/Gfx/GfxIsa.hpp"

#include <algorithm>
#include <bit>
#include <string>
#include <vector>

namespace SR::Frontend::Gfx {
namespace {

struct Entry
{
    Encoding encoding;
    std::uint16_t opcode;
    Op op;
    std::string_view mnemonic;
    std::uint8_t operandCount;
    std::array<OperandKind, 4> operands;
    bool removed;
    bool execMask = false;
};

constexpr Entry Make(Encoding encoding,
                     std::uint16_t opcode,
                     Op op,
                     std::string_view mnemonic,
                     std::initializer_list<OperandKind> operands,
                     bool execMask = false)
{
    Entry entry{};
    entry.encoding = encoding;
    entry.opcode = opcode;
    entry.op = op;
    entry.mnemonic = mnemonic;
    entry.operandCount = static_cast<std::uint8_t>(operands.size());
    entry.execMask = execMask;
    std::size_t i = 0;
    for (const OperandKind kind : operands)
    {
        if (i < entry.operands.size())
            entry.operands[i++] = kind;
    }
    return entry;
}

constexpr Entry Remove(Encoding encoding, std::uint16_t opcode)
{
    Entry entry{};
    entry.encoding = encoding;
    entry.opcode = opcode;
    entry.op = Op::Invalid;
    entry.removed = true;
    return entry;
}

#include "GfxVectorCompareTables.inc"

// RDNA1 is the root of the lineage.
constexpr std::array kGfx10 = {
    Make(Encoding::Sop1, 3, Op::S_MovB32, "s_mov_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 4, Op::S_MovB64, "s_mov_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 8, Op::S_NotB64, "s_not_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 12, Op::S_BrevB64, "s_brev_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 13, Op::S_Bcnt0I32B32, "s_bcnt0_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 14, Op::S_Bcnt0I32B64, "s_bcnt0_i32_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 16, Op::S_Bcnt1I32B64, "s_bcnt1_i32_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 17, Op::S_Ff0I32B32, "s_ff0_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 20, Op::S_Ff1I32B64, "s_ff1_i32_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 22, Op::S_FlbitI32B64, "s_flbit_i32_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 23, Op::S_FlbitI32, "s_flbit_i32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 24, Op::S_FlbitI32I64, "s_flbit_i32_i64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 25, Op::S_SextI32I8, "s_sext_i32_i8", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 26, Op::S_SextI32I16, "s_sext_i32_i16", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 27, Op::S_Bitset0B32, "s_bitset0_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 28, Op::S_Bitset0B64, "s_bitset0_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 29, Op::S_Bitset1B32, "s_bitset1_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 30, Op::S_Bitset1B64, "s_bitset1_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 0, Op::S_AddU32, "s_add_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 1, Op::S_SubU32, "s_sub_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 14, Op::S_AndB32, "s_and_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 16, Op::S_OrB32, "s_or_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 18, Op::S_XorB32, "s_xor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 30, Op::S_LshlB32, "s_lshl_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 32, Op::S_LshrB32, "s_lshr_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 10, Op::S_CselectB32, "s_cselect_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 11, Op::S_CselectB64, "s_cselect_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 15, Op::S_AndB64, "s_and_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 17, Op::S_OrB64, "s_or_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 19, Op::S_XorB64, "s_xor_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 6, Op::S_MinI32, "s_min_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 7, Op::S_MinU32, "s_min_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 8, Op::S_MaxI32, "s_max_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 9, Op::S_MaxU32, "s_max_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 38, Op::S_MulI32, "s_mul_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 24, Op::S_NandB32, "s_nand_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 26, Op::S_NorB32, "s_nor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 28, Op::S_XnorB32, "s_xnor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 34, Op::S_AshrI32, "s_ashr_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 36, Op::S_BfmB32, "s_bfm_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 39, Op::S_BfeU32, "s_bfe_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 40, Op::S_BfeI32, "s_bfe_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 44, Op::S_AbsDiffI32, "s_absdiff_i32",
         {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 31, Op::S_LshlB64, "s_lshl_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 33, Op::S_LshrB64, "s_lshr_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 35, Op::S_AshrI64, "s_ashr_i64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 37, Op::S_BfmB64, "s_bfm_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 41, Op::S_BfeU64, "s_bfe_u64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 42, Op::S_BfeI64, "s_bfe_i64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 53, Op::S_MulHiU32, "s_mul_hi_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 54, Op::S_MulHiI32, "s_mul_hi_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),

    Make(Encoding::Sop1, 7, Op::S_NotB32, "s_not_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 11, Op::S_BrevB32, "s_brev_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 15, Op::S_Bcnt1I32B32, "s_bcnt1_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 52, Op::S_AbsI32, "s_abs_i32", {OperandKind::Sdst, OperandKind::Ssrc}),

    Make(Encoding::SopK, 0, Op::S_MovkI32, "s_movk_i32", {OperandKind::Sdst, OperandKind::Simm16}),
    Make(Encoding::SopK, 15, Op::S_AddkI32, "s_addk_i32", {OperandKind::Sdst, OperandKind::Simm16}),
    Make(Encoding::SopK, 16, Op::S_MulkI32, "s_mulk_i32", {OperandKind::Sdst, OperandKind::Simm16}),

    Make(Encoding::Sop2, 2, Op::S_AddI32, "s_add_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 3, Op::S_SubI32, "s_sub_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 4, Op::S_AddcU32, "s_addc_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 5, Op::S_SubbU32, "s_subb_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 20, Op::S_Andn2B32, "s_andn2_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 22, Op::S_Orn2B32, "s_orn2_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),

    Make(Encoding::Sop1, 19, Op::S_Ff1I32B32, "s_ff1_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 21, Op::S_FlbitI32B32, "s_flbit_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),

    Make(Encoding::SopC, 0, Op::S_CmpEqI32, "s_cmp_eq_i32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 1, Op::S_CmpLgI32, "s_cmp_lg_i32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 2, Op::S_CmpGtI32, "s_cmp_gt_i32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 3, Op::S_CmpGeI32, "s_cmp_ge_i32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 4, Op::S_CmpLtI32, "s_cmp_lt_i32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 5, Op::S_CmpLeI32, "s_cmp_le_i32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 6, Op::S_CmpEqU32, "s_cmp_eq_u32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 7, Op::S_CmpLgU32, "s_cmp_lg_u32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 8, Op::S_CmpGtU32, "s_cmp_gt_u32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 9, Op::S_CmpGeU32, "s_cmp_ge_u32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 10, Op::S_CmpLtU32, "s_cmp_lt_u32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 11, Op::S_CmpLeU32, "s_cmp_le_u32", {OperandKind::Ssrc, OperandKind::Ssrc}),

    Make(Encoding::SopP, 1, Op::S_Endpgm, "s_endpgm", {}),
    Make(Encoding::SopP, 2, Op::S_Branch, "s_branch", {OperandKind::Label}),
    Make(Encoding::SopP, 4, Op::S_CbranchScc0, "s_cbranch_scc0", {OperandKind::Label}),
    Make(Encoding::SopP, 5, Op::S_CbranchScc1, "s_cbranch_scc1", {OperandKind::Label}),
    Make(Encoding::SopP, 6, Op::S_CbranchVccz, "s_cbranch_vccz", {OperandKind::Label}),
    Make(Encoding::SopP, 7, Op::S_CbranchVccnz, "s_cbranch_vccnz", {OperandKind::Label}),
    Make(Encoding::SopP, 8, Op::S_CbranchExecz, "s_cbranch_execz", {OperandKind::Label}),
    Make(Encoding::SopP, 9, Op::S_CbranchExecnz, "s_cbranch_execnz", {OperandKind::Label}),
    Make(Encoding::SopP, 10, Op::S_Barrier, "s_barrier", {}),

    Make(Encoding::Vop1, 1, Op::V_MovB32, "v_mov_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 0, Op::V_Nop, "v_nop", {}),
    Make(Encoding::Vop1, 2, Op::V_ReadfirstlaneB32, "v_readfirstlane_b32",
         {OperandKind::Vop1Sdst, OperandKind::Src}),
    Make(Encoding::Vop1, 17, Op::V_CvtF32Ubyte0, "v_cvt_f32_ubyte0", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 18, Op::V_CvtF32Ubyte1, "v_cvt_f32_ubyte1", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 19, Op::V_CvtF32Ubyte2, "v_cvt_f32_ubyte2", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 20, Op::V_CvtF32Ubyte3, "v_cvt_f32_ubyte3", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 32, Op::V_FractF32, "v_fract_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 33, Op::V_TruncF32, "v_trunc_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 34, Op::V_CeilF32, "v_ceil_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 35, Op::V_RndneF32, "v_rndne_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 36, Op::V_FloorF32, "v_floor_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 53, Op::V_SinF32, "v_sin_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 54, Op::V_CosF32, "v_cos_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 56, Op::V_BfrevB32, "v_bfrev_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 57, Op::V_FfbhU32, "v_ffbh_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 58, Op::V_FfblB32, "v_ffbl_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 42, Op::V_RcpF32, "v_rcp_f32", {OperandKind::Vgpr, OperandKind::Src}),    Make(Encoding::Vop1, 46, Op::V_RsqF32, "v_rsq_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 51, Op::V_SqrtF32, "v_sqrt_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 37, Op::V_ExpF32, "v_exp_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 39, Op::V_LogF32, "v_log_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 55, Op::V_NotB32, "v_not_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 5, Op::V_CvtF32I32, "v_cvt_f32_i32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 6, Op::V_CvtF32U32, "v_cvt_f32_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 8, Op::V_CvtI32F32, "v_cvt_i32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 7, Op::V_CvtU32F32, "v_cvt_u32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop2, 3, Op::V_AddF32, "v_add_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 4, Op::V_SubF32, "v_sub_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 5, Op::V_SubrevF32, "v_subrev_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 9, Op::V_MulI32I24, "v_mul_i32_i24", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 10, Op::V_MulHiI32I24, "v_mul_hi_i32_i24", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 11, Op::V_MulU32U24, "v_mul_u32_u24", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 12, Op::V_MulHiU32U24, "v_mul_hi_u32_u24", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 30, Op::V_XnorB32, "v_xnor_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 39, Op::V_SubrevNcU32, "v_subrev_nc_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 40, Op::V_AddCoCiU32, "v_add_co_ci_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 41, Op::V_SubCoCiU32, "v_sub_co_ci_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 42, Op::V_SubrevCoCiU32, "v_subrev_co_ci_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 8, Op::V_MulF32, "v_mul_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 27, Op::V_AndB32, "v_and_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 28, Op::V_OrB32, "v_or_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 29, Op::V_XorB32, "v_xor_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 17, Op::V_MinI32, "v_min_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 18, Op::V_MaxI32, "v_max_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 19, Op::V_MinU32, "v_min_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 20, Op::V_MaxU32, "v_max_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 26, Op::V_LshlrevB32, "v_lshlrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 24, Op::V_AshrrevI32, "v_ashrrev_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 22, Op::V_LshrrevB32, "v_lshrrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 15, Op::V_MinF32, "v_min_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 16, Op::V_MaxF32, "v_max_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 31, Op::V_MacF32, "v_mac_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 43, Op::V_MacF32, "v_fmac_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 37, Op::V_AddNcU32, "v_add_nc_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 38, Op::V_SubNcU32, "v_sub_nc_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 1, Op::V_CndmaskB32, "v_cndmask_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),

    Make(Encoding::Vop3, 259, Op::V_AddF32, "v_add_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 260, Op::V_SubF32, "v_sub_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 261, Op::V_SubrevF32, "v_subrev_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 265, Op::V_MulI32I24, "v_mul_i32_i24", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 266, Op::V_MulHiI32I24, "v_mul_hi_i32_i24", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 267, Op::V_MulU32U24, "v_mul_u32_u24", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 268, Op::V_MulHiU32U24, "v_mul_hi_u32_u24", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 286, Op::V_XnorB32, "v_xnor_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 295, Op::V_SubrevNcU32, "v_subrev_nc_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 767, Op::V_LshlrevB64, "v_lshlrev_b64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 877, Op::V_Add3U32, "v_add3_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 839, Op::V_AddLshlU32, "v_add_lshl_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 881, Op::V_AndOrB32, "v_and_or_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 882, Op::V_Or3B32, "v_or3_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 376, Op::V_Xor3B32, "v_xor3_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 838, Op::V_LshlAddU32, "v_lshl_add_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 879, Op::V_LshlOrB32, "v_lshl_or_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 837, Op::V_XadU32, "v_xad_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 364, Op::V_MulHiI32, "v_mul_hi_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 362, Op::V_MulHiU32, "v_mul_hi_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 338, Op::V_Min3I32, "v_min3_i32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 339, Op::V_Min3U32, "v_min3_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 344, Op::V_Med3I32, "v_med3_i32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 345, Op::V_Med3U32, "v_med3_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 341, Op::V_Max3I32, "v_max3_i32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 342, Op::V_Max3U32, "v_max3_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 322, Op::V_MadI32I24, "v_mad_i32_i24",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 323, Op::V_MadU32U24, "v_mad_u32_u24",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 349, Op::V_SadU32, "v_sad_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 866, Op::V_LdexpF32, "v_ldexp_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 895, Op::V_AddNcU32, "v_add_nc_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 886, Op::V_SubNcU32, "v_sub_nc_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 264, Op::V_MulF32, "v_mul_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 331, Op::V_FmaF32, "v_fma_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 283, Op::V_AndB32, "v_and_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 284, Op::V_OrB32, "v_or_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 285, Op::V_XorB32, "v_xor_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 385, Op::V_MovB32, "v_mov_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 384, Op::V_Nop, "v_nop", {}),
    Make(Encoding::Vop3, 386, Op::V_ReadfirstlaneB32, "v_readfirstlane_b32",
         {OperandKind::Vop3Sdst, OperandKind::Src}),
    Make(Encoding::Vop3, 401, Op::V_CvtF32Ubyte0, "v_cvt_f32_ubyte0", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 402, Op::V_CvtF32Ubyte1, "v_cvt_f32_ubyte1", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 403, Op::V_CvtF32Ubyte2, "v_cvt_f32_ubyte2", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 404, Op::V_CvtF32Ubyte3, "v_cvt_f32_ubyte3", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 416, Op::V_FractF32, "v_fract_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 417, Op::V_TruncF32, "v_trunc_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 418, Op::V_CeilF32, "v_ceil_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 419, Op::V_RndneF32, "v_rndne_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 420, Op::V_FloorF32, "v_floor_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 437, Op::V_SinF32, "v_sin_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 438, Op::V_CosF32, "v_cos_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 440, Op::V_BfrevB32, "v_bfrev_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 439, Op::V_NotB32, "v_not_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 389, Op::V_CvtF32I32, "v_cvt_f32_i32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 390, Op::V_CvtF32U32, "v_cvt_f32_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 392, Op::V_CvtI32F32, "v_cvt_i32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 391, Op::V_CvtU32F32, "v_cvt_u32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 273, Op::V_MinI32, "v_min_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 274, Op::V_MaxI32, "v_max_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 275, Op::V_MinU32, "v_min_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 276, Op::V_MaxU32, "v_max_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 282, Op::V_LshlrevB32, "v_lshlrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 280, Op::V_AshrrevI32, "v_ashrrev_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 361, Op::V_MulLoU32, "v_mul_lo_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 441, Op::V_FfbhU32, "v_ffbh_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 442, Op::V_FfblB32, "v_ffbl_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 868, Op::V_BcntU32B32, "v_bcnt_u32_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 271, Op::V_MinF32, "v_min_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 272, Op::V_MaxF32, "v_max_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 287, Op::V_MacF32, "v_mac_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 299, Op::V_MacF32, "v_fmac_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 321, Op::V_MadF32, "v_mad_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 337, Op::V_Min3F32, "v_min3_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 340, Op::V_Max3F32, "v_max3_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 426, Op::V_RcpF32, "v_rcp_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 430, Op::V_RsqF32, "v_rsq_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 435, Op::V_SqrtF32, "v_sqrt_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 421, Op::V_ExpF32, "v_exp_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 423, Op::V_LogF32, "v_log_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 293, Op::V_AddNcU32, "v_add_nc_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 294, Op::V_SubNcU32, "v_sub_nc_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 278, Op::V_LshrrevB32, "v_lshrrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 328, Op::V_BfeU32, "v_bfe_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 329, Op::V_BfeI32, "v_bfe_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 330, Op::V_BfiB32, "v_bfi_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 867, Op::V_BfmB32, "v_bfm_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 334, Op::V_AlignbitB32, "v_alignbit_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 335, Op::V_AlignbyteB32, "v_alignbyte_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),

    Make(Encoding::VopC, 1, Op::V_CmpLtF32, "v_cmp_lt_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 2, Op::V_CmpEqF32, "v_cmp_eq_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 3, Op::V_CmpLeF32, "v_cmp_le_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 4, Op::V_CmpGtF32, "v_cmp_gt_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 5, Op::V_CmpLgF32, "v_cmp_lg_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 6, Op::V_CmpGeF32, "v_cmp_ge_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 129, Op::V_CmpLtI32, "v_cmp_lt_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 130, Op::V_CmpEqI32, "v_cmp_eq_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 131, Op::V_CmpLeI32, "v_cmp_le_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 132, Op::V_CmpGtI32, "v_cmp_gt_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 133, Op::V_CmpNeI32, "v_cmp_ne_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 134, Op::V_CmpGeI32, "v_cmp_ge_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 193, Op::V_CmpLtU32, "v_cmp_lt_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 194, Op::V_CmpEqU32, "v_cmp_eq_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 195, Op::V_CmpLeU32, "v_cmp_le_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 196, Op::V_CmpGtU32, "v_cmp_gt_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 197, Op::V_CmpNeU32, "v_cmp_ne_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 198, Op::V_CmpGeU32, "v_cmp_ge_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),

    Make(Encoding::Vop1, 10, Op::V_CvtF16F32, "v_cvt_f16_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 11, Op::V_CvtF32F16, "v_cvt_f32_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 80, Op::V_CvtF16U16, "v_cvt_f16_u16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 81, Op::V_CvtF16I16, "v_cvt_f16_i16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 82, Op::V_CvtU16F16, "v_cvt_u16_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 83, Op::V_CvtI16F16, "v_cvt_i16_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 84, Op::V_RcpF16, "v_rcp_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 85, Op::V_SqrtF16, "v_sqrt_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 86, Op::V_RsqF16, "v_rsq_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 87, Op::V_LogF16, "v_log_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 88, Op::V_ExpF16, "v_exp_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 91, Op::V_FloorF16, "v_floor_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 92, Op::V_CeilF16, "v_ceil_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 93, Op::V_TruncF16, "v_trunc_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 94, Op::V_RndneF16, "v_rndne_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 95, Op::V_FractF16, "v_fract_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 96, Op::V_SinF16, "v_sin_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 97, Op::V_CosF16, "v_cos_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 99, Op::V_CvtNormI16F16, "v_cvt_norm_i16_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 100, Op::V_CvtNormU16F16, "v_cvt_norm_u16_f16", {OperandKind::Vgpr, OperandKind::Src}),

    Make(Encoding::Vop2, 50, Op::V_AddF16, "v_add_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 51, Op::V_SubF16, "v_sub_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 52, Op::V_SubrevF16, "v_subrev_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 53, Op::V_MulF16, "v_mul_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 54, Op::V_FmacF16, "v_fmac_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 57, Op::V_MaxF16, "v_max_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 58, Op::V_MinF16, "v_min_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 59, Op::V_LdexpF16, "v_ldexp_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),

    Make(Encoding::Vop3, 306, Op::V_AddF16, "v_add_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 307, Op::V_SubF16, "v_sub_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 308, Op::V_SubrevF16, "v_subrev_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 309, Op::V_MulF16, "v_mul_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 310, Op::V_FmacF16, "v_fmac_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 313, Op::V_MaxF16, "v_max_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 314, Op::V_MinF16, "v_min_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 315, Op::V_LdexpF16, "v_ldexp_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 394, Op::V_CvtF16F32, "v_cvt_f16_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 395, Op::V_CvtF32F16, "v_cvt_f32_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 464, Op::V_CvtF16U16, "v_cvt_f16_u16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 465, Op::V_CvtF16I16, "v_cvt_f16_i16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 466, Op::V_CvtU16F16, "v_cvt_u16_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 467, Op::V_CvtI16F16, "v_cvt_i16_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 468, Op::V_RcpF16, "v_rcp_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 469, Op::V_SqrtF16, "v_sqrt_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 470, Op::V_RsqF16, "v_rsq_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 471, Op::V_LogF16, "v_log_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 472, Op::V_ExpF16, "v_exp_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 475, Op::V_FloorF16, "v_floor_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 476, Op::V_CeilF16, "v_ceil_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 477, Op::V_TruncF16, "v_trunc_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 478, Op::V_RndneF16, "v_rndne_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 479, Op::V_FractF16, "v_fract_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 480, Op::V_SinF16, "v_sin_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 481, Op::V_CosF16, "v_cos_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 483, Op::V_CvtNormI16F16, "v_cvt_norm_i16_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 484, Op::V_CvtNormU16F16, "v_cvt_norm_u16_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 843, Op::V_FmaF16, "v_fma_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 849, Op::V_Min3F16, "v_min3_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 852, Op::V_Max3F16, "v_max3_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop1, 3, Op::V_CvtI32F64, "v_cvt_i32_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 4, Op::V_CvtF64I32, "v_cvt_f64_i32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 15, Op::V_CvtF32F64, "v_cvt_f32_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 16, Op::V_CvtF64F32, "v_cvt_f64_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 21, Op::V_CvtU32F64, "v_cvt_u32_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 22, Op::V_CvtF64U32, "v_cvt_f64_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 23, Op::V_TruncF64, "v_trunc_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 24, Op::V_CeilF64, "v_ceil_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 25, Op::V_RndneF64, "v_rndne_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 26, Op::V_FloorF64, "v_floor_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 47, Op::V_RcpF64, "v_rcp_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 49, Op::V_RsqF64, "v_rsq_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 52, Op::V_SqrtF64, "v_sqrt_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 62, Op::V_FractF64, "v_fract_f64", {OperandKind::Vgpr, OperandKind::Src}),

    Make(Encoding::Vop3, 387, Op::V_CvtI32F64, "v_cvt_i32_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 388, Op::V_CvtF64I32, "v_cvt_f64_i32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 399, Op::V_CvtF32F64, "v_cvt_f32_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 400, Op::V_CvtF64F32, "v_cvt_f64_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 405, Op::V_CvtU32F64, "v_cvt_u32_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 406, Op::V_CvtF64U32, "v_cvt_f64_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 407, Op::V_TruncF64, "v_trunc_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 408, Op::V_CeilF64, "v_ceil_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 409, Op::V_RndneF64, "v_rndne_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 410, Op::V_FloorF64, "v_floor_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 431, Op::V_RcpF64, "v_rcp_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 433, Op::V_RsqF64, "v_rsq_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 436, Op::V_SqrtF64, "v_sqrt_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 446, Op::V_FractF64, "v_fract_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 356, Op::V_AddF64, "v_add_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 357, Op::V_MulF64, "v_mul_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 358, Op::V_MinF64, "v_min_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 359, Op::V_MaxF64, "v_max_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 332, Op::V_FmaF64, "v_fma_f64",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 360, Op::V_LdexpF64, "v_ldexp_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),

    Make(Encoding::Vop3P, 0, Op::V_PkMadI16, "v_pk_mad_i16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 1, Op::V_PkMulLoU16, "v_pk_mul_lo_u16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 2, Op::V_PkAddI16, "v_pk_add_i16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 3, Op::V_PkSubI16, "v_pk_sub_i16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 4, Op::V_PkLshlrevB16, "v_pk_lshlrev_b16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 5, Op::V_PkLshrrevB16, "v_pk_lshrrev_b16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 6, Op::V_PkAshrrevI16, "v_pk_ashrrev_i16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 7, Op::V_PkMaxI16, "v_pk_max_i16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 8, Op::V_PkMinI16, "v_pk_min_i16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 9, Op::V_PkMadU16, "v_pk_mad_u16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 10, Op::V_PkAddU16, "v_pk_add_u16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 11, Op::V_PkSubU16, "v_pk_sub_u16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 12, Op::V_PkMaxU16, "v_pk_max_u16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 13, Op::V_PkMinU16, "v_pk_min_u16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 14, Op::V_PkFmaF16, "v_pk_fma_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 15, Op::V_PkAddF16, "v_pk_add_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 16, Op::V_PkMulF16, "v_pk_mul_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 17, Op::V_PkMinF16, "v_pk_min_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 18, Op::V_PkMaxF16, "v_pk_max_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),

    Make(Encoding::Smem, 0, Op::S_LoadB32, "s_load_dword",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 8, Op::S_BufferLoadB32, "s_buffer_load_dword",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Flat, 12, Op::GlobalLoadB32, "global_load_dword",
         {OperandKind::FlatVdst, OperandKind::FlatVaddr, OperandKind::FlatOffset12}),
    Make(Encoding::Flat, 28, Op::GlobalStoreB32, "global_store_dword",
         {OperandKind::FlatVaddr, OperandKind::FlatData, OperandKind::FlatOffset12}),
};

// RDNA3 renumbered scalar opcodes and VOP3 FMA. Old opcodes are removed
// explicitly: the encoding space is reused, so inheritance alone would decode
// unrelated instructions as the old mnemonic.
constexpr std::array kGfx11 = {
    Remove(Encoding::Sop1, 3),
    Remove(Encoding::Sop1, 4),
    Remove(Encoding::Sop1, 7),
    Remove(Encoding::Sop1, 8),
    Remove(Encoding::Sop1, 11),
    Remove(Encoding::Sop1, 15),
    Remove(Encoding::Sop1, 19),
    Remove(Encoding::Sop1, 52),
    Remove(Encoding::Sop1, 12),
    Remove(Encoding::Sop1, 13),
    Remove(Encoding::Sop1, 14),
    Remove(Encoding::Sop1, 16),
    Remove(Encoding::Sop1, 17),
    Remove(Encoding::Sop1, 20),
    Remove(Encoding::Sop1, 22),
    Remove(Encoding::Sop1, 23),
    Remove(Encoding::Sop1, 24),
    Remove(Encoding::Sop1, 25),
    Remove(Encoding::Sop1, 26),
    Remove(Encoding::Sop1, 27),
    Remove(Encoding::Sop1, 28),
    Remove(Encoding::Sop1, 29),
    Remove(Encoding::Sop1, 30),
    Remove(Encoding::Sop1, 21),

    Remove(Encoding::Sop2, 14),
    Remove(Encoding::Sop2, 16),
    Remove(Encoding::Sop2, 18),
    Remove(Encoding::Sop2, 30),
    Remove(Encoding::Sop2, 32),
    Remove(Encoding::Sop2, 10),
    Remove(Encoding::Sop2, 6),
    Remove(Encoding::Sop2, 7),
    Remove(Encoding::Sop2, 8),
    Remove(Encoding::Sop2, 9),
    Remove(Encoding::Sop2, 38),
    Remove(Encoding::Sop2, 24),
    Remove(Encoding::Sop2, 26),
    Remove(Encoding::Sop2, 28),
    Remove(Encoding::Sop2, 34),
    Remove(Encoding::Sop2, 20),
    Remove(Encoding::Sop2, 22),
    Remove(Encoding::Sop2, 11),
    Remove(Encoding::Sop2, 15),
    Remove(Encoding::Sop2, 17),
    Remove(Encoding::Sop2, 31),
    Remove(Encoding::Sop2, 33),
    Remove(Encoding::Sop2, 35),
    Remove(Encoding::Sop2, 37),
    Remove(Encoding::Sop2, 41),
    Remove(Encoding::Sop2, 42),
    Remove(Encoding::Sop2, 53),
    Remove(Encoding::Sop2, 54),

    Remove(Encoding::SopP, 1),
    Remove(Encoding::SopP, 2),
    Remove(Encoding::SopP, 4),
    Remove(Encoding::SopP, 5),
    Remove(Encoding::SopP, 6),
    Remove(Encoding::SopP, 7),
    Remove(Encoding::SopP, 8),
    Remove(Encoding::SopP, 9),
    Remove(Encoding::SopP, 10),

    Remove(Encoding::Vop3, 331),
    Remove(Encoding::Vop3, 361),
    Remove(Encoding::Vop3, 280),
    Remove(Encoding::Vop3, 282),
    Remove(Encoding::Vop3, 287),
    Remove(Encoding::Vop3, 321),
    Remove(Encoding::Vop3, 337),
    Remove(Encoding::Vop3, 340),
    Remove(Encoding::Vop3, 278),
    Remove(Encoding::Vop3, 328),
    Remove(Encoding::Vop3, 329),
    Remove(Encoding::Vop3, 330),
    Remove(Encoding::Vop3, 867),
    Remove(Encoding::Vop3, 334),
    Remove(Encoding::Vop3, 335),
    Remove(Encoding::Vop3, 441),
    Remove(Encoding::Vop3, 442),
    Remove(Encoding::Vop3, 868),
    Remove(Encoding::Vop2, 24),
    Remove(Encoding::Vop2, 26),
    Remove(Encoding::Vop2, 31),
    Remove(Encoding::Vop2, 22),
    Remove(Encoding::Vop2, 40),
    Remove(Encoding::Vop2, 41),
    Remove(Encoding::Vop2, 42),
    Remove(Encoding::Vop3, 767),
    Remove(Encoding::Vop3, 877),
    Remove(Encoding::Vop3, 839),
    Remove(Encoding::Vop3, 881),
    Remove(Encoding::Vop3, 882),
    Remove(Encoding::Vop3, 376),
    Remove(Encoding::Vop3, 838),
    Remove(Encoding::Vop3, 879),
    Remove(Encoding::Vop3, 837),
    Remove(Encoding::Vop3, 364),
    Remove(Encoding::Vop3, 362),
    Remove(Encoding::Vop3, 338),
    Remove(Encoding::Vop3, 339),
    Remove(Encoding::Vop3, 344),
    Remove(Encoding::Vop3, 345),
    Remove(Encoding::Vop3, 341),
    Remove(Encoding::Vop3, 342),
    Remove(Encoding::Vop3, 322),
    Remove(Encoding::Vop3, 323),
    Remove(Encoding::Vop3, 349),
    Remove(Encoding::Vop3, 866),
    Remove(Encoding::Vop3, 895),
    Remove(Encoding::Vop3, 886),
    Remove(Encoding::Vop1, 57),
    Remove(Encoding::Vop1, 58),
    Remove(Encoding::Flat, 12),
    Remove(Encoding::VopC, 1),
    Remove(Encoding::VopC, 2),
    Remove(Encoding::VopC, 3),
    Remove(Encoding::VopC, 4),
    Remove(Encoding::VopC, 5),
    Remove(Encoding::VopC, 6),
    Remove(Encoding::VopC, 129),
    Remove(Encoding::VopC, 130),
    Remove(Encoding::VopC, 131),
    Remove(Encoding::VopC, 132),
    Remove(Encoding::VopC, 133),
    Remove(Encoding::VopC, 134),
    Remove(Encoding::VopC, 193),
    Remove(Encoding::VopC, 194),
    Remove(Encoding::VopC, 195),
    Remove(Encoding::VopC, 196),
    Remove(Encoding::VopC, 197),
    Remove(Encoding::VopC, 198),

    Make(Encoding::Sop1, 0, Op::S_MovB32, "s_mov_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 14, Op::S_SextI32I8, "s_sext_i32_i8", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 15, Op::S_SextI32I16, "s_sext_i32_i16", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 5, Op::S_BrevB64, "s_brev_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 22, Op::S_Bcnt0I32B32, "s_bcnt0_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 23, Op::S_Bcnt0I32B64, "s_bcnt0_i32_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 25, Op::S_Bcnt1I32B64, "s_bcnt1_i32_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 16, Op::S_Bitset0B32, "s_bitset0_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 17, Op::S_Bitset0B64, "s_bitset0_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 18, Op::S_Bitset1B32, "s_bitset1_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 19, Op::S_Bitset1B64, "s_bitset1_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 4, Op::S_BrevB32, "s_brev_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 8, Op::S_Ff1I32B32, "s_ctz_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 9, Op::S_Ff1I32B64, "s_ctz_i32_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 10, Op::S_FlbitI32B32, "s_clz_i32_u32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 11, Op::S_FlbitI32B64, "s_clz_i32_u64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 12, Op::S_FlbitI32, "s_cls_i32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 13, Op::S_FlbitI32I64, "s_cls_i32_i64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 21, Op::S_AbsI32, "s_abs_i32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 30, Op::S_NotB32, "s_not_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Vop1, 28, Op::V_MovB16, "v_mov_b16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 105, Op::V_NotB16, "v_not_b16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Sop1, 1, Op::S_MovB64, "s_mov_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 31, Op::S_NotB64, "s_not_b64", {OperandKind::Sdst, OperandKind::Ssrc}),

    Make(Encoding::Sop2, 22, Op::S_AndB32, "s_and_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 24, Op::S_OrB32, "s_or_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 26, Op::S_XorB32, "s_xor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 8, Op::S_LshlB32, "s_lshl_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 10, Op::S_LshrB32, "s_lshr_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 48, Op::S_CselectB32, "s_cselect_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 49, Op::S_CselectB64, "s_cselect_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 23, Op::S_AndB64, "s_and_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 25, Op::S_OrB64, "s_or_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 27, Op::S_XorB64, "s_xor_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 18, Op::S_MinI32, "s_min_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 19, Op::S_MinU32, "s_min_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 20, Op::S_MaxI32, "s_max_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 21, Op::S_MaxU32, "s_max_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 44, Op::S_MulI32, "s_mul_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 28, Op::S_NandB32, "s_nand_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 30, Op::S_NorB32, "s_nor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 32, Op::S_XnorB32, "s_xnor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 12, Op::S_AshrI32, "s_ashr_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 9, Op::S_LshlB64, "s_lshl_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 11, Op::S_LshrB64, "s_lshr_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 13, Op::S_AshrI64, "s_ashr_i64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 43, Op::S_BfmB64, "s_bfm_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 40, Op::S_BfeU64, "s_bfe_u64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 41, Op::S_BfeI64, "s_bfe_i64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 45, Op::S_MulHiU32, "s_mul_hi_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 46, Op::S_MulHiI32, "s_mul_hi_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 6, Op::S_AbsDiffI32, "s_absdiff_i32",
         {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 42, Op::S_BfmB32, "s_bfm_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 38, Op::S_BfeU32, "s_bfe_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 39, Op::S_BfeI32, "s_bfe_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 34, Op::S_AndNot1B32, "s_and_not1_b32",
         {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 36, Op::S_OrNot1B32, "s_or_not1_b32",
         {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),

    Make(Encoding::Sop1, 30, Op::S_NotB32, "s_not_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 4, Op::S_BrevB32, "s_brev_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 24, Op::S_Bcnt1I32B32, "s_bcnt1_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 21, Op::S_AbsI32, "s_abs_i32", {OperandKind::Sdst, OperandKind::Ssrc}),

    Make(Encoding::SopP, 48, Op::S_Endpgm, "s_endpgm", {}),
    Make(Encoding::SopP, 32, Op::S_Branch, "s_branch", {OperandKind::Label}),
    Make(Encoding::SopP, 33, Op::S_CbranchScc0, "s_cbranch_scc0", {OperandKind::Label}),
    Make(Encoding::SopP, 34, Op::S_CbranchScc1, "s_cbranch_scc1", {OperandKind::Label}),
    Make(Encoding::SopP, 35, Op::S_CbranchVccz, "s_cbranch_vccz", {OperandKind::Label}),
    Make(Encoding::SopP, 36, Op::S_CbranchVccnz, "s_cbranch_vccnz", {OperandKind::Label}),
    Make(Encoding::SopP, 37, Op::S_CbranchExecz, "s_cbranch_execz", {OperandKind::Label}),
    Make(Encoding::SopP, 38, Op::S_CbranchExecnz, "s_cbranch_execnz", {OperandKind::Label}),
    Make(Encoding::SopP, 61, Op::S_Barrier, "s_barrier", {}),

    Make(Encoding::Vop3, 531, Op::V_FmaF32, "v_fma_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 812, Op::V_MulLoU32, "v_mul_lo_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 280, Op::V_LshlrevB32, "v_lshlrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 282, Op::V_AshrrevI32, "v_ashrrev_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop1, 57, Op::V_ClzI32U32, "v_clz_i32_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 58, Op::V_CtzI32B32, "v_ctz_i32_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 12, Op::V_CvtNearestI32F32, "v_cvt_nearest_i32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 13, Op::V_CvtFloorI32F32, "v_cvt_floor_i32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 59, Op::V_ClsI32, "v_cls_i32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 106, Op::V_CvtI32I16, "v_cvt_i32_i16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 107, Op::V_CvtU32U16, "v_cvt_u32_u16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 441, Op::V_ClzI32U32, "v_clz_i32_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 442, Op::V_CtzI32B32, "v_ctz_i32_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 396, Op::V_CvtNearestI32F32, "v_cvt_nearest_i32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 397, Op::V_CvtFloorI32F32, "v_cvt_floor_i32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 443, Op::V_ClsI32, "v_cls_i32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 490, Op::V_CvtI32I16, "v_cvt_i32_i16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 491, Op::V_CvtU32U16, "v_cvt_u32_u16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 798, Op::V_BcntU32B32, "v_bcnt_u32_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 537, Op::V_Min3F32, "v_min3_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 540, Op::V_Max3F32, "v_max3_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 281, Op::V_LshrrevB32, "v_lshrrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 528, Op::V_BfeU32, "v_bfe_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 529, Op::V_BfeI32, "v_bfe_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 530, Op::V_BfiB32, "v_bfi_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 797, Op::V_BfmB32, "v_bfm_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 534, Op::V_AlignbitB32, "v_alignbit_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 535, Op::V_AlignbyteB32, "v_alignbyte_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop2, 25, Op::V_LshrrevB32, "v_lshrrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 7, Op::V_MulDx9ZeroF32, "v_mul_dx9_zero_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 32, Op::V_AddCoCiU32, "v_add_co_ci_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 33, Op::V_SubCoCiU32, "v_sub_co_ci_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 34, Op::V_SubrevCoCiU32, "v_subrev_co_ci_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop3, 263, Op::V_MulDx9ZeroF32, "v_mul_dx9_zero_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 828, Op::V_LshlrevB64, "v_lshlrev_b64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 584, Op::V_FmaF16, "v_fma_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 585, Op::V_Min3F16, "v_min3_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 588, Op::V_Max3F16, "v_max3_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 597, Op::V_Add3U32, "v_add3_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 583, Op::V_AddLshlU32, "v_add_lshl_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 599, Op::V_AndOrB32, "v_and_or_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 600, Op::V_Or3B32, "v_or3_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 576, Op::V_Xor3B32, "v_xor3_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 582, Op::V_LshlAddU32, "v_lshl_add_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 598, Op::V_LshlOrB32, "v_lshl_or_b32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 581, Op::V_XadU32, "v_xad_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 814, Op::V_MulHiI32, "v_mul_hi_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 813, Op::V_MulHiU32, "v_mul_hi_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 538, Op::V_Min3I32, "v_min3_i32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 539, Op::V_Min3U32, "v_min3_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 544, Op::V_Med3I32, "v_med3_i32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 545, Op::V_Med3U32, "v_med3_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 541, Op::V_Max3I32, "v_max3_i32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 542, Op::V_Max3U32, "v_max3_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 522, Op::V_MadI32I24, "v_mad_i32_i24",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 523, Op::V_MadU32U24, "v_mad_u32_u24",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 549, Op::V_SadU32, "v_sad_u32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 796, Op::V_LdexpF32, "v_ldexp_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 806, Op::V_AddNcU32, "v_add_nc_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 805, Op::V_SubNcU32, "v_sub_nc_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),

    Make(Encoding::VopC, 17, Op::V_CmpLtF32, "v_cmp_lt_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 18, Op::V_CmpEqF32, "v_cmp_eq_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 19, Op::V_CmpLeF32, "v_cmp_le_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 20, Op::V_CmpGtF32, "v_cmp_gt_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 21, Op::V_CmpLgF32, "v_cmp_lg_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 22, Op::V_CmpGeF32, "v_cmp_ge_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 65, Op::V_CmpLtI32, "v_cmp_lt_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 66, Op::V_CmpEqI32, "v_cmp_eq_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 67, Op::V_CmpLeI32, "v_cmp_le_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 68, Op::V_CmpGtI32, "v_cmp_gt_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 69, Op::V_CmpNeI32, "v_cmp_ne_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 70, Op::V_CmpGeI32, "v_cmp_ge_i32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 73, Op::V_CmpLtU32, "v_cmp_lt_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 74, Op::V_CmpEqU32, "v_cmp_eq_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 75, Op::V_CmpLeU32, "v_cmp_le_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 76, Op::V_CmpGtU32, "v_cmp_gt_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 77, Op::V_CmpNeU32, "v_cmp_ne_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 78, Op::V_CmpGeU32, "v_cmp_ge_u32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Flat, 20, Op::GlobalLoadB32, "global_load_b32",
         {OperandKind::FlatVdst, OperandKind::FlatVaddr, OperandKind::FlatOffset13}),
    Make(Encoding::Flat, 28, Op::GlobalStoreB32, "global_store_b32",
         {OperandKind::FlatVaddr, OperandKind::FlatData, OperandKind::FlatOffset13}),
    Make(Encoding::Vop2, 24, Op::V_LshlrevB32, "v_lshlrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 26, Op::V_AshrrevI32, "v_ashrrev_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
};

// RDNA4 keeps RDNA3's numbering for this subset and replaces plain S_ADD/S_SUB
// with the carry-out forms.
constexpr std::array kGfx12 = {
    Make(Encoding::Sop2, 0, Op::S_AddCoU32, "s_add_co_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 1, Op::S_SubCoU32, "s_sub_co_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 2, Op::S_AddCoU32, "s_add_co_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 3, Op::S_SubCoU32, "s_sub_co_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),

    Remove(Encoding::Smem, 8),
    Remove(Encoding::Flat, 20),
    Remove(Encoding::Flat, 28),
    Remove(Encoding::Vop2, 15),
    Remove(Encoding::Vop2, 16),
    Remove(Encoding::Vop2, 57),
    Remove(Encoding::Vop2, 58),
    Make(Encoding::Vop2, 48, Op::V_MinF16, "v_min_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 49, Op::V_MaxF16, "v_max_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Remove(Encoding::Vop3, 313),
    Remove(Encoding::Vop3, 314),
    Remove(Encoding::Vop3, 807),
    Remove(Encoding::Vop3, 808),
    Remove(Encoding::Vop3, 809),
    Remove(Encoding::Vop3, 810),
    Remove(Encoding::Vop3P, 17),
    Remove(Encoding::Vop3P, 18),
    Make(Encoding::Vop3P, 27, Op::V_PkMinF16, "v_pk_min_num_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 28, Op::V_PkMaxF16, "v_pk_max_num_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 29, Op::V_PkMinF16, "v_pk_minimum_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3P, 30, Op::V_PkMaxF16, "v_pk_maximum_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 258, Op::V_AddF64, "v_add_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 262, Op::V_MulF64, "v_mul_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 269, Op::V_MinF64, "v_min_num_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 270, Op::V_MaxF64, "v_max_num_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop2, 2, Op::V_AddF64, "v_add_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 6, Op::V_MulF64, "v_mul_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 13, Op::V_MinF64, "v_min_num_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 14, Op::V_MaxF64, "v_max_num_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Remove(Encoding::Vop3, 271),
    Remove(Encoding::Vop3, 272),
    Remove(Encoding::Vop3, 537),
    Remove(Encoding::Vop3, 540),
    Remove(Encoding::Vop3, 828),
    Remove(Encoding::Vop3, 843),
    Remove(Encoding::Vop3, 849),
    Remove(Encoding::Vop3, 852),
    Remove(Encoding::Vop3, 356),
    Remove(Encoding::Vop3, 357),
    Remove(Encoding::Vop3, 358),
    Remove(Encoding::Vop3, 359),
    Remove(Encoding::Vop3, 332),
    Remove(Encoding::Vop3, 360),
    Make(Encoding::Vop3, 807, Op::V_AddF64, "v_add_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 808, Op::V_MulF64, "v_mul_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 809, Op::V_MinF64, "v_min_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 810, Op::V_MaxF64, "v_max_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 532, Op::V_FmaF64, "v_fma_f64",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 811, Op::V_LdexpF64, "v_ldexp_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop2, 31, Op::V_LshlrevB64, "v_lshlrev_b64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop3, 287, Op::V_LshlrevB64, "v_lshlrev_b64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop2, 21, Op::V_MinF32, "v_min_num_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 22, Op::V_MaxF32, "v_max_num_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop3, 277, Op::V_MinF32, "v_min_num_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 278, Op::V_MaxF32, "v_max_num_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 553, Op::V_Min3F32, "v_min3_num_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 554, Op::V_Max3F32, "v_max3_num_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Smem, 0, Op::S_LoadB32, "s_load_b32",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 16, Op::S_BufferLoadB32, "s_buffer_load_b32",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Global, 20, Op::GlobalLoadB32, "global_load_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 26, Op::GlobalStoreB32, "global_store_b32",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
};

constexpr std::span<const Entry> LevelEntries(Arch arch) noexcept
{
    switch (arch)
    {
    case Arch::Gfx10:
        return {}; // base table handled separately
    case Arch::Gfx1013:
    case Arch::Gfx103:
        return {};
    case Arch::Gfx11:
        return kGfx11;
    case Arch::Gfx115:
        return {};
    case Arch::Gfx12:
        return kGfx12;
    }

    return {};
}

const Entry* FindInLevel(Arch arch, Encoding encoding, std::uint16_t opcode) noexcept
{
    const auto matches = [encoding, opcode](const Entry& entry) {
        return entry.encoding == encoding && entry.opcode == opcode;
    };

    const auto search = [&matches](std::span<const Entry> entries) -> const Entry* {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        {
            if (matches(*it))
                return &*it;
        }
        return nullptr;
    };

    const auto compareEntries = [arch]() -> std::span<const Entry> {
        switch (arch)
        {
        case Arch::Gfx10:
            return kGfx10Compare;
        case Arch::Gfx11:
            return kGfx11Compare;
        case Arch::Gfx12:
            return kGfx12Compare;
        default:
            return {};
        }
    };

    if (arch == Arch::Gfx10)
    {
        const Entry* base = search(kGfx10);
        const Entry* compares = search(kGfx10Compare);
        return compares != nullptr ? compares : base;
    }

    const Entry* base = search(LevelEntries(arch));
    const Entry* compares = search(compareEntries());
    return compares != nullptr ? compares : base;
}

std::vector<Arch> Lineage(Arch arch)
{
    std::vector<Arch> chain;
    Arch current = arch;
    for (;;)
    {
        chain.push_back(current);
        const ArchInfo& info = GetArchInfo(current);
        if (info.parent == current)
            break;
        current = info.parent;
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

const Entry* Lookup(Arch arch, Encoding encoding, std::uint16_t opcode)
{
    const std::vector<Arch> chain = Lineage(arch);
    const Entry* found = nullptr;
    for (const Arch level : chain)
    {
        if (const Entry* entry = FindInLevel(level, encoding, opcode))
            found = entry;
    }

    return found;
}

std::uint32_t Extract(std::uint64_t word, std::uint32_t offset, std::uint32_t width) noexcept
{
    return static_cast<std::uint32_t>((word >> offset) & ((std::uint64_t{1} << width) - 1));
}

Diagnostic Error(std::string code, std::string message, std::uint32_t pc)
{
    return Diagnostic::Error(std::move(code), std::move(message), pc);
}

bool NeedsLiteral(OperandKind kind, std::uint32_t value) noexcept
{
    return (kind == OperandKind::Ssrc || kind == OperandKind::Src) && value == 0xFF;
}

} // namespace

const ArchInfo& GetArchInfo(Arch arch) noexcept
{
    static constexpr std::array kArchInfos = {
        ArchInfo{Arch::Gfx10, Arch::Gfx10, "gfx10", "rdna1"},
        ArchInfo{Arch::Gfx1013, Arch::Gfx10, "gfx1013", "rdna2-ps5"},
        ArchInfo{Arch::Gfx103, Arch::Gfx10, "gfx1030", "rdna2"},
        ArchInfo{Arch::Gfx11, Arch::Gfx103, "gfx1100", "rdna3"},
        ArchInfo{Arch::Gfx115, Arch::Gfx11, "gfx1150", "rdna3.5"},
        ArchInfo{Arch::Gfx12, Arch::Gfx11, "gfx1200", "rdna4", 13, 6},
    };

    for (const ArchInfo& info : kArchInfos)
    {
        if (info.arch == arch)
            return info;
    }

    return kArchInfos[0];
}

const ArchInfo* FindArch(std::string_view name) noexcept
{
    if (name == "gfx10" || name == "rdna1")
        return &GetArchInfo(Arch::Gfx10);
    if (name == "gfx1013" || name == "rdna2-ps5")
        return &GetArchInfo(Arch::Gfx1013);
    if (name == "gfx1030" || name == "gfx103" || name == "rdna2")
        return &GetArchInfo(Arch::Gfx103);
    if (name == "gfx1100" || name == "gfx11" || name == "rdna3")
        return &GetArchInfo(Arch::Gfx11);
    if (name == "gfx1150" || name == "gfx115" || name == "rdna3.5")
        return &GetArchInfo(Arch::Gfx115);
    if (name == "gfx1200" || name == "gfx12" || name == "rdna4")
        return &GetArchInfo(Arch::Gfx12);

    return nullptr;
}

bool Inherits(Arch arch, Arch ancestor) noexcept
{
    Arch current = arch;
    for (;;)
    {
        if (current == ancestor)
            return true;
        const ArchInfo& info = GetArchInfo(current);
        if (info.parent == current)
            return false;
        current = info.parent;
    }
}

std::string_view ToString(Encoding encoding) noexcept
{
    switch (encoding)
    {
    case Encoding::Sop1:
        return "sop1";
    case Encoding::Sop2:
        return "sop2";
    case Encoding::SopC:
        return "sopc";
    case Encoding::SopP:
        return "sopp";
    case Encoding::SopK:
        return "sopk";
    case Encoding::Vop1:
        return "vop1";
    case Encoding::Vop2:
        return "vop2";
    case Encoding::Vop3:
        return "vop3";
    case Encoding::Vop3P:
        return "vop3p";
    case Encoding::VopC:
        return "vopc";
    case Encoding::Smem:
        return "smem";
    case Encoding::Ds:
        return "ds";
    case Encoding::Global:
        return "global";
    case Encoding::Flat:
        return "flat";
    case Encoding::Buffer:
        return "buffer";
    case Encoding::Scratch:
        return "scratch";
    }

    return "unknown";
}

std::string_view ToString(Op op) noexcept
{
    switch (op)
    {
    case Op::Invalid:
        return "<invalid>";
    case Op::S_MovB32:
        return "s_mov_b32";
    case Op::S_AddU32:
        return "s_add_u32";
    case Op::S_SubU32:
        return "s_sub_u32";
    case Op::S_AddCoU32:
        return "s_add_co_u32";
    case Op::S_SubCoU32:
        return "s_sub_co_u32";
    case Op::S_AndB32:
        return "s_and_b32";
    case Op::S_OrB32:
        return "s_or_b32";
    case Op::S_XorB32:
        return "s_xor_b32";
    case Op::S_LshlB32:
        return "s_lshl_b32";
    case Op::S_LshrB32:
        return "s_lshr_b32";
    case Op::S_CselectB32:
        return "s_cselect_b32";
    case Op::S_NotB32:
        return "s_not_b32";
    case Op::S_BrevB32:
        return "s_brev_b32";
    case Op::S_Bcnt1I32B32:
        return "s_bcnt1_i32_b32";
    case Op::S_AbsI32:
        return "s_abs_i32";
    case Op::S_MulI32:
        return "s_mul_i32";
    case Op::S_MinI32:
        return "s_min_i32";
    case Op::S_MaxI32:
        return "s_max_i32";
    case Op::S_MinU32:
        return "s_min_u32";
    case Op::S_MaxU32:
        return "s_max_u32";
    case Op::S_NandB32:
        return "s_nand_b32";
    case Op::S_NorB32:
        return "s_nor_b32";
    case Op::S_XnorB32:
        return "s_xnor_b32";
    case Op::S_AshrI32:
        return "s_ashr_i32";
    case Op::S_MovkI32:
        return "s_movk_i32";
    case Op::S_AddI32:
        return "s_add_i32";
    case Op::S_SubI32:
        return "s_sub_i32";
    case Op::S_AddcU32:
        return "s_addc_u32";
    case Op::S_SubbU32:
        return "s_subb_u32";
    case Op::S_AddkI32:
        return "s_addk_i32";
    case Op::S_MulkI32:
        return "s_mulk_i32";
    case Op::S_Andn2B32:
        return "s_andn2_b32";
    case Op::S_Orn2B32:
        return "s_orn2_b32";
    case Op::S_AndNot1B32:
        return "s_and_not1_b32";
    case Op::S_OrNot1B32:
        return "s_or_not1_b32";
    case Op::S_Ff1I32B32:
        return "s_ff1_i32_b32";
    case Op::S_FlbitI32B32:
        return "s_flbit_i32_b32";
    case Op::S_AbsDiffI32:
        return "s_absdiff_i32";
    case Op::S_BfmB32:
        return "s_bfm_b32";
    case Op::S_BfeI32:
        return "s_bfe_i32";
    case Op::S_BfeU32:
        return "s_bfe_u32";
    case Op::S_MovB64:
        return "s_mov_b64";
    case Op::S_NotB64:
        return "s_not_b64";
    case Op::S_AndB64:
        return "s_and_b64";
    case Op::S_OrB64:
        return "s_or_b64";
    case Op::S_XorB64:
        return "s_xor_b64";
    case Op::S_CselectB64:
        return "s_cselect_b64";
    case Op::S_SextI32I8:
        return "s_sext_i32_i8";
    case Op::S_SextI32I16:
        return "s_sext_i32_i16";
    case Op::S_BrevB64:
        return "s_brev_b64";
    case Op::S_Bcnt0I32B32:
        return "s_bcnt0_i32_b32";
    case Op::S_Bcnt0I32B64:
        return "s_bcnt0_i32_b64";
    case Op::S_Bcnt1I32B64:
        return "s_bcnt1_i32_b64";
    case Op::S_Bitset0B32:
        return "s_bitset0_b32";
    case Op::S_Bitset1B32:
        return "s_bitset1_b32";
    case Op::S_Bitset0B64:
        return "s_bitset0_b64";
    case Op::S_Bitset1B64:
        return "s_bitset1_b64";
    case Op::S_LshlB64:
        return "s_lshl_b64";
    case Op::S_LshrB64:
        return "s_lshr_b64";
    case Op::S_AshrI64:
        return "s_ashr_i64";
    case Op::S_BfmB64:
        return "s_bfm_b64";
    case Op::S_BfeI64:
        return "s_bfe_i64";
    case Op::S_BfeU64:
        return "s_bfe_u64";
    case Op::S_MulHiI32:
        return "s_mul_hi_i32";
    case Op::S_MulHiU32:
        return "s_mul_hi_u32";
    case Op::S_Ff0I32B32:
        return "s_ff0_i32_b32";
    case Op::S_Ff1I32B64:
        return "s_ff1_i32_b64";
    case Op::S_FlbitI32B64:
        return "s_flbit_i32_b64";
    case Op::S_FlbitI32:
        return "s_flbit_i32";
    case Op::S_FlbitI32I64:
        return "s_flbit_i32_i64";
    case Op::S_CmpEqI32:
        return "s_cmp_eq_i32";
    case Op::S_CmpLgI32:
        return "s_cmp_lg_i32";
    case Op::S_CmpLtI32:
        return "s_cmp_lt_i32";
    case Op::S_CmpLeI32:
        return "s_cmp_le_i32";
    case Op::S_CmpGtI32:
        return "s_cmp_gt_i32";
    case Op::S_CmpGeI32:
        return "s_cmp_ge_i32";
    case Op::S_CmpEqU32:
        return "s_cmp_eq_u32";
    case Op::S_CmpLgU32:
        return "s_cmp_lg_u32";
    case Op::S_CmpLtU32:
        return "s_cmp_lt_u32";
    case Op::S_CmpLeU32:
        return "s_cmp_le_u32";
    case Op::S_CmpGtU32:
        return "s_cmp_gt_u32";
    case Op::S_CmpGeU32:
        return "s_cmp_ge_u32";
    case Op::S_Endpgm:
        return "s_endpgm";
    case Op::S_Branch:
        return "s_branch";
    case Op::S_CbranchScc0:
        return "s_cbranch_scc0";
    case Op::S_CbranchScc1:
        return "s_cbranch_scc1";
    case Op::S_CbranchVccz:
        return "s_cbranch_vccz";
    case Op::S_CbranchVccnz:
        return "s_cbranch_vccnz";
    case Op::S_CbranchExecz:
        return "s_cbranch_execz";
    case Op::S_CbranchExecnz:
        return "s_cbranch_execnz";
    case Op::S_Barrier:
        return "s_barrier";
    case Op::V_MovB32:
        return "v_mov_b32";
    case Op::V_AddF32:
        return "v_add_f32";
    case Op::V_SubF32:
        return "v_sub_f32";
    case Op::V_MulF32:
        return "v_mul_f32";
    case Op::V_FmaF32:
        return "v_fma_f32";
    case Op::V_AndB32:
        return "v_and_b32";
    case Op::V_OrB32:
        return "v_or_b32";
    case Op::V_XorB32:
        return "v_xor_b32";
    case Op::V_CndmaskB32:
        return "v_cndmask_b32";
    case Op::V_NotB32:
        return "v_not_b32";
    case Op::V_CvtF32I32:
        return "v_cvt_f32_i32";
    case Op::V_CvtF32U32:
        return "v_cvt_f32_u32";
    case Op::V_CvtI32F32:
        return "v_cvt_i32_f32";
    case Op::V_CvtU32F32:
        return "v_cvt_u32_f32";
    case Op::V_MinI32:
        return "v_min_i32";
    case Op::V_MaxI32:
        return "v_max_i32";
    case Op::V_MinU32:
        return "v_min_u32";
    case Op::V_MaxU32:
        return "v_max_u32";
    case Op::V_LshlrevB32:
        return "v_lshlrev_b32";
    case Op::V_AshrrevI32:
        return "v_ashrrev_i32";
    case Op::V_MulLoU32:
        return "v_mul_lo_u32";
    case Op::V_FfbhU32:
        return "v_ffbh_u32";
    case Op::V_FfblB32:
        return "v_ffbl_b32";
    case Op::V_ClzI32U32:
        return "v_clz_i32_u32";
    case Op::V_CtzI32B32:
        return "v_ctz_i32_b32";
    case Op::V_BcntU32B32:
        return "v_bcnt_u32_b32";
    case Op::V_Nop:
        return "v_nop";
    case Op::V_ReadfirstlaneB32:
        return "v_readfirstlane_b32";
    case Op::V_CvtNearestI32F32:
        return "v_cvt_nearest_i32_f32";
    case Op::V_CvtFloorI32F32:
        return "v_cvt_floor_i32_f32";
    case Op::V_CvtF32Ubyte0:
        return "v_cvt_f32_ubyte0";
    case Op::V_CvtF32Ubyte1:
        return "v_cvt_f32_ubyte1";
    case Op::V_CvtF32Ubyte2:
        return "v_cvt_f32_ubyte2";
    case Op::V_CvtF32Ubyte3:
        return "v_cvt_f32_ubyte3";
    case Op::V_FractF32:
        return "v_fract_f32";
    case Op::V_TruncF32:
        return "v_trunc_f32";
    case Op::V_CeilF32:
        return "v_ceil_f32";
    case Op::V_RndneF32:
        return "v_rndne_f32";
    case Op::V_FloorF32:
        return "v_floor_f32";
    case Op::V_SinF32:
        return "v_sin_f32";
    case Op::V_CosF32:
        return "v_cos_f32";
    case Op::V_BfrevB32:
        return "v_bfrev_b32";
    case Op::V_ClsI32:
        return "v_cls_i32";
    case Op::V_CvtI32I16:
        return "v_cvt_i32_i16";
    case Op::V_CvtU32U16:
        return "v_cvt_u32_u16";
    case Op::V_SubrevF32:
        return "v_subrev_f32";
    case Op::V_MulDx9ZeroF32:
        return "v_mul_dx9_zero_f32";
    case Op::V_MulI32I24:
        return "v_mul_i32_i24";
    case Op::V_MulHiI32I24:
        return "v_mul_hi_i32_i24";
    case Op::V_MulU32U24:
        return "v_mul_u32_u24";
    case Op::V_MulHiU32U24:
        return "v_mul_hi_u32_u24";
    case Op::V_XnorB32:
        return "v_xnor_b32";
    case Op::V_LshlrevB64:
        return "v_lshlrev_b64";
    case Op::V_AddCoCiU32:
        return "v_add_co_ci_u32";
    case Op::V_SubCoCiU32:
        return "v_sub_co_ci_u32";
    case Op::V_SubrevCoCiU32:
        return "v_subrev_co_ci_u32";
    case Op::V_SubrevNcU32:
        return "v_subrev_nc_u32";
    case Op::V_Add3U32:
        return "v_add3_u32";
    case Op::V_AddLshlU32:
        return "v_add_lshl_u32";
    case Op::V_AndOrB32:
        return "v_and_or_b32";
    case Op::V_Or3B32:
        return "v_or3_b32";
    case Op::V_Xor3B32:
        return "v_xor3_b32";
    case Op::V_LshlAddU32:
        return "v_lshl_add_u32";
    case Op::V_LshlOrB32:
        return "v_lshl_or_b32";
    case Op::V_XadU32:
        return "v_xad_u32";
    case Op::V_MulHiI32:
        return "v_mul_hi_i32";
    case Op::V_MulHiU32:
        return "v_mul_hi_u32";
    case Op::V_Min3I32:
        return "v_min3_i32";
    case Op::V_Min3U32:
        return "v_min3_u32";
    case Op::V_Med3I32:
        return "v_med3_i32";
    case Op::V_Med3U32:
        return "v_med3_u32";
    case Op::V_Max3I32:
        return "v_max3_i32";
    case Op::V_Max3U32:
        return "v_max3_u32";
    case Op::V_MadI32I24:
        return "v_mad_i32_i24";
    case Op::V_MadU32U24:
        return "v_mad_u32_u24";
    case Op::V_SadU32:
        return "v_sad_u32";
    case Op::V_LdexpF32:
        return "v_ldexp_f32";
    case Op::V_CvtF16F32:
        return "v_cvt_f16_f32";
    case Op::V_CvtF32F16:
        return "v_cvt_f32_f16";
    case Op::V_CvtF16U16:
        return "v_cvt_f16_u16";
    case Op::V_CvtF16I16:
        return "v_cvt_f16_i16";
    case Op::V_CvtU16F16:
        return "v_cvt_u16_f16";
    case Op::V_CvtI16F16:
        return "v_cvt_i16_f16";
    case Op::V_CvtNormI16F16:
        return "v_cvt_norm_i16_f16";
    case Op::V_CvtNormU16F16:
        return "v_cvt_norm_u16_f16";
    case Op::V_RcpF16:
        return "v_rcp_f16";
    case Op::V_RsqF16:
        return "v_rsq_f16";
    case Op::V_SqrtF16:
        return "v_sqrt_f16";
    case Op::V_ExpF16:
        return "v_exp_f16";
    case Op::V_LogF16:
        return "v_log_f16";
    case Op::V_FloorF16:
        return "v_floor_f16";
    case Op::V_CeilF16:
        return "v_ceil_f16";
    case Op::V_TruncF16:
        return "v_trunc_f16";
    case Op::V_RndneF16:
        return "v_rndne_f16";
    case Op::V_FractF16:
        return "v_fract_f16";
    case Op::V_SinF16:
        return "v_sin_f16";
    case Op::V_CosF16:
        return "v_cos_f16";
    case Op::V_MovB16:
        return "v_mov_b16";
    case Op::V_NotB16:
        return "v_not_b16";
    case Op::V_AddF16:
        return "v_add_f16";
    case Op::V_SubF16:
        return "v_sub_f16";
    case Op::V_SubrevF16:
        return "v_subrev_f16";
    case Op::V_MulF16:
        return "v_mul_f16";
    case Op::V_FmacF16:
        return "v_fmac_f16";
    case Op::V_MinF16:
        return "v_min_f16";
    case Op::V_MaxF16:
        return "v_max_f16";
    case Op::V_LdexpF16:
        return "v_ldexp_f16";
    case Op::V_Min3F16:
        return "v_min3_f16";
    case Op::V_Max3F16:
        return "v_max3_f16";
    case Op::V_FmaF16:
        return "v_fma_f16";
    case Op::V_CvtI32F64:
        return "v_cvt_i32_f64";
    case Op::V_CvtF64I32:
        return "v_cvt_f64_i32";
    case Op::V_CvtF32F64:
        return "v_cvt_f32_f64";
    case Op::V_CvtF64F32:
        return "v_cvt_f64_f32";
    case Op::V_CvtU32F64:
        return "v_cvt_u32_f64";
    case Op::V_CvtF64U32:
        return "v_cvt_f64_u32";
    case Op::V_TruncF64:
        return "v_trunc_f64";
    case Op::V_CeilF64:
        return "v_ceil_f64";
    case Op::V_RndneF64:
        return "v_rndne_f64";
    case Op::V_FloorF64:
        return "v_floor_f64";
    case Op::V_RcpF64:
        return "v_rcp_f64";
    case Op::V_RsqF64:
        return "v_rsq_f64";
    case Op::V_SqrtF64:
        return "v_sqrt_f64";
    case Op::V_FractF64:
        return "v_fract_f64";
    case Op::V_AddF64:
        return "v_add_f64";
    case Op::V_MulF64:
        return "v_mul_f64";
    case Op::V_MinF64:
        return "v_min_f64";
    case Op::V_MaxF64:
        return "v_max_f64";
    case Op::V_FmaF64:
        return "v_fma_f64";
    case Op::V_LdexpF64:
        return "v_ldexp_f64";
    case Op::V_PkMadI16:
        return "v_pk_mad_i16";
    case Op::V_PkMulLoU16:
        return "v_pk_mul_lo_u16";
    case Op::V_PkAddI16:
        return "v_pk_add_i16";
    case Op::V_PkSubI16:
        return "v_pk_sub_i16";
    case Op::V_PkLshlrevB16:
        return "v_pk_lshlrev_b16";
    case Op::V_PkLshrrevB16:
        return "v_pk_lshrrev_b16";
    case Op::V_PkAshrrevI16:
        return "v_pk_ashrrev_i16";
    case Op::V_PkMaxI16:
        return "v_pk_max_i16";
    case Op::V_PkMinI16:
        return "v_pk_min_i16";
    case Op::V_PkMadU16:
        return "v_pk_mad_u16";
    case Op::V_PkAddU16:
        return "v_pk_add_u16";
    case Op::V_PkSubU16:
        return "v_pk_sub_u16";
    case Op::V_PkMaxU16:
        return "v_pk_max_u16";
    case Op::V_PkMinU16:
        return "v_pk_min_u16";
    case Op::V_PkFmaF16:
        return "v_pk_fma_f16";
    case Op::V_PkAddF16:
        return "v_pk_add_f16";
    case Op::V_PkMulF16:
        return "v_pk_mul_f16";
    case Op::V_PkMinF16:
        return "v_pk_min_f16";
    case Op::V_PkMaxF16:
        return "v_pk_max_f16";
    case Op::V_AddNcU32:
        return "v_add_nc_u32";
    case Op::V_SubNcU32:
        return "v_sub_nc_u32";
    case Op::V_RcpF32:
        return "v_rcp_f32";
    case Op::V_RsqF32:
        return "v_rsq_f32";
    case Op::V_SqrtF32:
        return "v_sqrt_f32";
    case Op::V_ExpF32:
        return "v_exp_f32";
    case Op::V_LogF32:
        return "v_log_f32";
    case Op::V_MinF32:
        return "v_min_f32";
    case Op::V_MaxF32:
        return "v_max_f32";
    case Op::V_Min3F32:
        return "v_min3_f32";
    case Op::V_Max3F32:
        return "v_max3_f32";
    case Op::V_MacF32:
        return "v_mac_f32";
    case Op::V_MadF32:
        return "v_mad_f32";
    case Op::V_LshrrevB32:
        return "v_lshrrev_b32";
    case Op::V_CmpLtI32:
        return "v_cmp_lt_i32";
    case Op::V_CmpEqI32:
        return "v_cmp_eq_i32";
    case Op::V_CmpLeI32:
        return "v_cmp_le_i32";
    case Op::V_CmpGtI32:
        return "v_cmp_gt_i32";
    case Op::V_CmpNeI32:
        return "v_cmp_ne_i32";
    case Op::V_CmpGeI32:
        return "v_cmp_ge_i32";
    case Op::V_CmpLtU32:
        return "v_cmp_lt_u32";
    case Op::V_CmpEqU32:
        return "v_cmp_eq_u32";
    case Op::V_CmpLeU32:
        return "v_cmp_le_u32";
    case Op::V_CmpGtU32:
        return "v_cmp_gt_u32";
    case Op::V_CmpNeU32:
        return "v_cmp_ne_u32";
    case Op::V_CmpGeU32:
        return "v_cmp_ge_u32";
    case Op::V_CmpLtF32:
        return "v_cmp_lt_f32";
    case Op::V_CmpEqF32:
        return "v_cmp_eq_f32";
    case Op::V_CmpLeF32:
        return "v_cmp_le_f32";
    case Op::V_CmpGtF32:
        return "v_cmp_gt_f32";
    case Op::V_CmpLgF32:
        return "v_cmp_lg_f32";
    case Op::V_CmpGeF32:
        return "v_cmp_ge_f32";
    case Op::V_CmpFalseF32:
        return "v_cmp_f_f32";
    case Op::V_CmpOrdF32:
        return "v_cmp_o_f32";
    case Op::V_CmpUnordF32:
        return "v_cmp_u_f32";
    case Op::V_CmpNgeF32:
        return "v_cmp_nge_f32";
    case Op::V_CmpNlgF32:
        return "v_cmp_nlg_f32";
    case Op::V_CmpNgtF32:
        return "v_cmp_ngt_f32";
    case Op::V_CmpNleF32:
        return "v_cmp_nle_f32";
    case Op::V_CmpNeqF32:
        return "v_cmp_neq_f32";
    case Op::V_CmpNltF32:
        return "v_cmp_nlt_f32";
    case Op::V_CmpTrueF32:
        return "v_cmp_t_f32";
    case Op::V_CmpFalseI32:
        return "v_cmp_f_i32";
    case Op::V_CmpTrueI32:
        return "v_cmp_t_i32";
    case Op::V_CmpFalseU32:
        return "v_cmp_f_u32";
    case Op::V_CmpTrueU32:
        return "v_cmp_t_u32";
    case Op::V_CmpFalseF16:
        return "v_cmp_f_f16";
    case Op::V_CmpLtF16:
        return "v_cmp_lt_f16";
    case Op::V_CmpEqF16:
        return "v_cmp_eq_f16";
    case Op::V_CmpLeF16:
        return "v_cmp_le_f16";
    case Op::V_CmpGtF16:
        return "v_cmp_gt_f16";
    case Op::V_CmpLgF16:
        return "v_cmp_lg_f16";
    case Op::V_CmpGeF16:
        return "v_cmp_ge_f16";
    case Op::V_CmpOrdF16:
        return "v_cmp_o_f16";
    case Op::V_CmpUnordF16:
        return "v_cmp_u_f16";
    case Op::V_CmpNgeF16:
        return "v_cmp_nge_f16";
    case Op::V_CmpNlgF16:
        return "v_cmp_nlg_f16";
    case Op::V_CmpNgtF16:
        return "v_cmp_ngt_f16";
    case Op::V_CmpNleF16:
        return "v_cmp_nle_f16";
    case Op::V_CmpNeqF16:
        return "v_cmp_neq_f16";
    case Op::V_CmpNltF16:
        return "v_cmp_nlt_f16";
    case Op::V_CmpTrueF16:
        return "v_cmp_t_f16";
    case Op::V_CmpFalseF64:
        return "v_cmp_f_f64";
    case Op::V_CmpLtF64:
        return "v_cmp_lt_f64";
    case Op::V_CmpEqF64:
        return "v_cmp_eq_f64";
    case Op::V_CmpLeF64:
        return "v_cmp_le_f64";
    case Op::V_CmpGtF64:
        return "v_cmp_gt_f64";
    case Op::V_CmpLgF64:
        return "v_cmp_lg_f64";
    case Op::V_CmpGeF64:
        return "v_cmp_ge_f64";
    case Op::V_CmpOrdF64:
        return "v_cmp_o_f64";
    case Op::V_CmpUnordF64:
        return "v_cmp_u_f64";
    case Op::V_CmpNgeF64:
        return "v_cmp_nge_f64";
    case Op::V_CmpNlgF64:
        return "v_cmp_nlg_f64";
    case Op::V_CmpNgtF64:
        return "v_cmp_ngt_f64";
    case Op::V_CmpNleF64:
        return "v_cmp_nle_f64";
    case Op::V_CmpNeqF64:
        return "v_cmp_neq_f64";
    case Op::V_CmpNltF64:
        return "v_cmp_nlt_f64";
    case Op::V_CmpTrueF64:
        return "v_cmp_t_f64";
    case Op::V_CmpLtI16:
        return "v_cmp_lt_i16";
    case Op::V_CmpEqI16:
        return "v_cmp_eq_i16";
    case Op::V_CmpLeI16:
        return "v_cmp_le_i16";
    case Op::V_CmpGtI16:
        return "v_cmp_gt_i16";
    case Op::V_CmpNeI16:
        return "v_cmp_ne_i16";
    case Op::V_CmpGeI16:
        return "v_cmp_ge_i16";
    case Op::V_CmpLtU16:
        return "v_cmp_lt_u16";
    case Op::V_CmpEqU16:
        return "v_cmp_eq_u16";
    case Op::V_CmpLeU16:
        return "v_cmp_le_u16";
    case Op::V_CmpGtU16:
        return "v_cmp_gt_u16";
    case Op::V_CmpNeU16:
        return "v_cmp_ne_u16";
    case Op::V_CmpGeU16:
        return "v_cmp_ge_u16";
    case Op::V_CmpFalseI64:
        return "v_cmp_f_i64";
    case Op::V_CmpLtI64:
        return "v_cmp_lt_i64";
    case Op::V_CmpEqI64:
        return "v_cmp_eq_i64";
    case Op::V_CmpLeI64:
        return "v_cmp_le_i64";
    case Op::V_CmpGtI64:
        return "v_cmp_gt_i64";
    case Op::V_CmpNeI64:
        return "v_cmp_ne_i64";
    case Op::V_CmpGeI64:
        return "v_cmp_ge_i64";
    case Op::V_CmpTrueI64:
        return "v_cmp_t_i64";
    case Op::V_CmpFalseU64:
        return "v_cmp_f_u64";
    case Op::V_CmpLtU64:
        return "v_cmp_lt_u64";
    case Op::V_CmpEqU64:
        return "v_cmp_eq_u64";
    case Op::V_CmpLeU64:
        return "v_cmp_le_u64";
    case Op::V_CmpGtU64:
        return "v_cmp_gt_u64";
    case Op::V_CmpNeU64:
        return "v_cmp_ne_u64";
    case Op::V_CmpGeU64:
        return "v_cmp_ge_u64";
    case Op::V_CmpTrueU64:
        return "v_cmp_t_u64";
    case Op::V_BfeI32:
        return "v_bfe_i32";
    case Op::V_BfeU32:
        return "v_bfe_u32";
    case Op::V_BfiB32:
        return "v_bfi_b32";
    case Op::V_BfmB32:
        return "v_bfm_b32";
    case Op::V_AlignbitB32:
        return "v_alignbit_b32";
    case Op::V_AlignbyteB32:
        return "v_alignbyte_b32";
    case Op::S_LoadB32:
        return "s_load_b32";
    case Op::S_BufferLoadB32:
        return "s_buffer_load_b32";
    case Op::GlobalLoadB32:
        return "global_load_b32";
    case Op::GlobalStoreB32:
        return "global_store_b32";
    }

    return "unknown";
}

bool IsVectorOp(Op op) noexcept
{
    return static_cast<std::uint16_t>(op) >= static_cast<std::uint16_t>(Op::V_MovB32);
}

const Operand* FindOperand(const DecodedInstruction& instruction, OperandKind kind) noexcept
{
    for (std::uint8_t i = 0; i < instruction.operandCount; ++i)
    {
        if (instruction.operands[i].kind == kind)
            return &instruction.operands[i];
    }

    return nullptr;
}

std::uint32_t InlineInteger(std::uint32_t index) noexcept
{
    if (index >= 128 && index <= 192)
        return index - 128;
    if (index >= 193 && index <= 208)
        return static_cast<std::uint32_t>(-static_cast<std::int32_t>(index - 192));
    return 0;
}

std::optional<std::uint32_t> InlineFloatBits(std::uint32_t index) noexcept
{
    static constexpr std::array<float, 9> kFloats = {0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -2.0f, 4.0f, -4.0f, 0.15915494f};

    if (index >= 240 && index <= 248)
        return std::bit_cast<std::uint32_t>(kFloats[index - 240]);

    return std::nullopt;
}

Result<DecodedInstruction> Decode(std::span<const std::uint32_t> words, Arch arch, std::uint32_t bytePc)
{
    if (words.empty())
        return std::unexpected(Error("SR-GFX-001", "no instruction words", bytePc));

    const std::uint32_t word0 = words[0];
    Encoding encoding;
    std::uint16_t opcode = 0;
    std::uint32_t baseWords = 1;
    std::uint64_t wide = word0;

    // VGLOBAL (RDNA4): 96-bit encoding, three words.
    if ((word0 >> 24) == 0xEEu)
    {
        if (words.size() < 3)
            return std::unexpected(Error("SR-GFX-006", "truncated VGLOBAL instruction", bytePc));

        const std::uint32_t word1 = words[1];
        const std::uint32_t word2 = words[2];
        const std::uint16_t globalOpcode = static_cast<std::uint16_t>((word0 >> 14) & 0xFF);

        const Entry* globalEntry = Lookup(arch, Encoding::Global, globalOpcode);
        if (globalEntry == nullptr || globalEntry->removed)
        {
            return std::unexpected(Error("SR-GFX-007",
                                         "unknown global opcode " + std::to_string(globalOpcode) + " for " +
                                             std::string(GetArchInfo(arch).name),
                                         bytePc));
        }

        DecodedInstruction global;
        global.op = globalEntry->op;
        global.encoding = Encoding::Global;
        global.arch = arch;
        global.pc = bytePc;
        global.word0 = word0;
        global.word1 = word1;
        global.wordCount = 3;

        for (std::uint8_t i = 0; i < globalEntry->operandCount; ++i)
        {
            Operand operand;
            switch (globalEntry->operands[i])
            {
            case OperandKind::GlobalVdst:
                operand.kind = OperandKind::Vgpr;
                operand.value = word1 & 0xFF; // bits 32..39
                break;
            case OperandKind::GlobalVaddr:
                operand.kind = OperandKind::Vgpr;
                operand.value = word2 & 0xFF; // bits 64..71
                break;
            case OperandKind::GlobalVsrc:
                operand.kind = OperandKind::Vgpr;
                operand.value = (word1 >> 23) & 0xFF; // bits 55..62
                break;
            case OperandKind::GlobalIoffset:
                operand.kind = OperandKind::Imm32;
                operand.value = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(((word2 >> 8) & 0xFFFFFFu) << 8) >> 8); // bits 72..95
                break;
            default:
                return std::unexpected(Error("SR-GFX-008", "unsupported VGLOBAL operand layout", bytePc));
            }
            if (global.operandCount < global.operands.size())
                global.operands[global.operandCount++] = operand;
        }
        return global;
    }

    if ((word0 & 0xFF800000u) == 0xBE800000u)
    {
        encoding = Encoding::Sop1;
        opcode = static_cast<std::uint16_t>((word0 >> 8) & 0xFF);
    }
    else if ((word0 & 0xFF800000u) == 0xBF800000u)
    {
        encoding = Encoding::SopP;
        opcode = static_cast<std::uint16_t>((word0 >> 16) & 0x7F);
    }
    else if ((word0 & 0xFF800000u) == 0xBF000000u)
    {
        encoding = Encoding::SopC;
        opcode = static_cast<std::uint16_t>((word0 >> 16) & 0x7F);
    }
    else if ((word0 >> 28) == 0xB)
    {
        encoding = Encoding::SopK;
        opcode = static_cast<std::uint16_t>((word0 >> 23) & 0x1F);
    }
    else if ((word0 >> 30) == 2)
    {
        encoding = Encoding::Sop2;
        opcode = static_cast<std::uint16_t>((word0 >> 23) & 0x7F);
    }
    else if (((word0 >> 26) & 0x3F) == 0x3D)
    {
        encoding = Encoding::Smem;
        const ArchInfo& info = GetArchInfo(arch);
        opcode = static_cast<std::uint16_t>((word0 >> info.smemOpcodeOffset) &
                                            ((1u << info.smemOpcodeWidth) - 1u));
        if (words.size() < 2)
            return std::unexpected(Error("SR-GFX-009", "truncated SMEM instruction", bytePc));
        baseWords = 2;
        wide = (static_cast<std::uint64_t>(words[1]) << 32) | word0;
    }
    else if (((word0 >> 26) & 0x3F) == 0x37)
    {
        encoding = Encoding::Flat;
        opcode = static_cast<std::uint16_t>((word0 >> 18) & 0x7F);
        if (words.size() < 2)
            return std::unexpected(Error("SR-GFX-010", "truncated GLOBAL instruction", bytePc));
        baseWords = 2;
        wide = (static_cast<std::uint64_t>(words[1]) << 32) | word0;
    }
    else if (((word0 >> 26) & 0x3F) == 0x35)
    {
        encoding = Encoding::Vop3;
        if (words.size() < 2)
            return std::unexpected(Error("SR-GFX-002", "truncated VOP3 instruction", bytePc));
        baseWords = 2;
        wide = (static_cast<std::uint64_t>(words[1]) << 32) | word0;
        opcode = static_cast<std::uint16_t>((wide >> 16) & 0x3FF);
    }
    else if (((word0 >> 26) & 0x3F) == 0x33)
    {
        encoding = Encoding::Vop3P;
        if (words.size() < 2)
            return std::unexpected(Error("SR-GFX-011", "truncated VOP3P instruction", bytePc));
        baseWords = 2;
        wide = (static_cast<std::uint64_t>(words[1]) << 32) | word0;
        opcode = static_cast<std::uint16_t>((wide >> 16) & 0x7F);
    }
    else if (((word0 >> 25) & 0x7F) == 0x3E)
    {
        encoding = Encoding::VopC;
        opcode = static_cast<std::uint16_t>((word0 >> 17) & 0xFF);
    }
    else if (((word0 >> 25) & 0x7F) == 0x3F)
    {
        encoding = Encoding::Vop1;
        opcode = static_cast<std::uint16_t>((word0 >> 9) & 0x7F);
    }
    else if ((word0 & 0x80000000u) == 0)
    {
        encoding = Encoding::Vop2;
        opcode = static_cast<std::uint16_t>((word0 >> 25) & 0x3F);
    }
    else
    {
        return std::unexpected(Error("SR-GFX-003", "unsupported encoding for instruction word " + std::to_string(word0), bytePc));
    }

    const Entry* entry = Lookup(arch, encoding, opcode);
    if (entry == nullptr || entry->removed)
    {
        return std::unexpected(Error("SR-GFX-004",
                                     "unknown " + std::string(ToString(encoding)) + " opcode " + std::to_string(opcode) +
                                         " for " + std::string(GetArchInfo(arch).name),
                                     bytePc));
    }

    DecodedInstruction instruction;
    instruction.op = entry->op;
    instruction.encoding = encoding;
    instruction.arch = arch;
    instruction.pc = bytePc;
    instruction.word0 = word0;
    instruction.word1 = baseWords == 2 ? words[1] : 0;
    instruction.execMask = entry->execMask;

    const auto push = [&instruction](Operand operand) {
        if (instruction.operandCount < instruction.operands.size())
            instruction.operands[instruction.operandCount++] = operand;
    };

    std::uint32_t literalWords = 0;

    for (std::uint8_t i = 0; i < entry->operandCount; ++i)
    {
        const OperandKind kind = entry->operands[i];
        Operand operand;
        operand.kind = kind;

        switch (kind)
        {
        case OperandKind::Sgpr:
            operand.value = Extract(wide, 0, 8);
            break;
        case OperandKind::Vgpr:
            operand.value = Extract(wide, 17, 8); // VOP1/VOP2 VDST
            if (encoding == Encoding::Vop3 || encoding == Encoding::Vop3P)
                operand.value = Extract(wide, 0, 8);
            break;
        case OperandKind::Src:
            operand.value = Extract(wide, 0, 9);
            if (encoding == Encoding::Vop3 || encoding == Encoding::Vop3P)
            {
                // VOP3 sources follow the first word: 32, 41, 50.
                operand.value = i == 1 ? Extract(wide, 32, 9)
                                       : i == 2 ? Extract(wide, 41, 9)
                                                : Extract(wide, 50, 9);
            }
            break;
        case OperandKind::Vsrc:
            operand.value = Extract(wide, 9, 8);
            break;
        case OperandKind::Sdst:
            operand.value = Extract(wide, 16, 7);
            break;
        case OperandKind::Ssrc:
            operand.value = encoding == Encoding::SopC ? Extract(wide, i == 0 ? 0 : 8, 8)
                                                       : Extract(wide, i == 1 ? 0 : 8, 8);
            break;
        case OperandKind::Simm16:
        case OperandKind::Label:
            operand.value = Extract(wide, 0, 16);
            break;
        case OperandKind::Imm32:
            operand.value = Extract(wide, 0, 32);
            break;
        case OperandKind::Vcc:
            operand.kind = OperandKind::Vcc;
            break;
        case OperandKind::Exec:
            operand.kind = OperandKind::Exec;
            break;
        case OperandKind::SmemSdst:
            operand.kind = OperandKind::Sdst;
            operand.value = Extract(wide, 6, 7);
            break;
        case OperandKind::SmemSbase:
            // The field names an SGPR pair; scale it to the first SGPR index.
            operand.kind = OperandKind::Sgpr;
            operand.value = Extract(wide, 0, 6) * 2;
            break;
        case OperandKind::SmemOffset:
            operand.kind = OperandKind::Imm32;
            operand.value = Extract(wide, 32, 21);
            break;
        case OperandKind::SmemOffset24:
            operand.kind = OperandKind::Imm32;
            operand.value = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(Extract(wide, 32, 24) << 8) >> 8);
            break;
        case OperandKind::SmemSoffset:
            operand.kind = OperandKind::Imm32;
            operand.value = Extract(wide, 57, 7);
            break;
        case OperandKind::FlatVdst:
            operand.kind = OperandKind::Vgpr;
            operand.value = Extract(wide, 56, 8);
            break;
        case OperandKind::FlatVaddr:
            operand.kind = OperandKind::Vgpr;
            operand.value = Extract(wide, 32, 8);
            break;
        case OperandKind::FlatData:
            operand.kind = OperandKind::Vgpr;
            operand.value = Extract(wide, 40, 8);
            break;
        case OperandKind::FlatSaddr:
            operand.kind = OperandKind::Imm32;
            operand.value = Extract(wide, 48, 7);
            break;
        case OperandKind::FlatOffset12:
            operand.kind = OperandKind::Imm32;
            operand.value = Extract(wide, 0, 12);
            break;
        case OperandKind::FlatOffset13:
            operand.kind = OperandKind::Imm32;
            operand.value = Extract(wide, 0, 13);
            break;
        case OperandKind::GlobalVdst:
            operand.kind = OperandKind::Vgpr;
            operand.value = Extract(wide, 32, 8);
            break;
        case OperandKind::GlobalVaddr:
            operand.kind = OperandKind::Vgpr;
            operand.value = Extract(wide, 64, 8);
            break;
        case OperandKind::GlobalVsrc:
            operand.kind = OperandKind::Vgpr;
            operand.value = Extract(wide, 55, 8);
            break;
        case OperandKind::GlobalIoffset:
            operand.kind = OperandKind::Imm32;
            operand.value = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(Extract(wide, 72, 24) << 8) >> 8);
            break;
        case OperandKind::Vop1Sdst:
            operand.kind = OperandKind::Sdst;
            operand.value = Extract(wide, 17, 7);
            break;
        case OperandKind::Vop3Sdst:
            operand.kind = OperandKind::Sdst;
            operand.value = Extract(wide, 0, 7);
            break;
        case OperandKind::Literal:
        case OperandKind::None:
            break;
        }

        // Inline constants are resolved later; registers map by class.
        // 9-bit vector sources use 0..255 for scalar/inline and 256..511 for
        // VGPRs; 8-bit scalar sources use 0..127 and 128..254 respectively.
        if (kind == OperandKind::Src && operand.value >= 256)
        {
            operand.value -= 256;
            operand.isInline = false;
            operand.kind = OperandKind::Vgpr;
        }
        else if ((kind == OperandKind::Ssrc || kind == OperandKind::Src) && operand.value < 128)
        {
            operand.kind = OperandKind::Sgpr;
        }
        else if ((kind == OperandKind::Ssrc || kind == OperandKind::Src) && operand.value >= 128 &&
                 operand.value != 0xFF)
        {
            operand.isInline = true;
        }

        if (NeedsLiteral(kind, operand.value))
        {
            operand.kind = OperandKind::Literal;
            ++literalWords;
        }

        push(operand);
    }

    if (encoding == Encoding::Vop3)
    {
        instruction.abs = static_cast<std::uint8_t>((word0 >> 8) & 0x7);
        instruction.clamp = ((word0 >> 15) & 0x1) != 0;
        instruction.omod = static_cast<std::uint8_t>((words[1] >> 27) & 0x3);
        instruction.neg = static_cast<std::uint8_t>((words[1] >> 29) & 0x7);
    }
    else if (encoding == Encoding::Vop3P)
    {
        instruction.opSel = static_cast<std::uint8_t>((word0 >> 11) & 0x7);
        instruction.opSelHi = static_cast<std::uint8_t>(((words[1] >> 27) & 0x3) |
                                                       (((word0 >> 14) & 0x1) << 2));
        instruction.negHi = static_cast<std::uint8_t>((word0 >> 8) & 0x7);
        instruction.neg = static_cast<std::uint8_t>((words[1] >> 29) & 0x7);
        instruction.clamp = ((word0 >> 15) & 0x1) != 0;
    }

    if (words.size() < baseWords + literalWords)
        return std::unexpected(Error("SR-GFX-005", "truncated literal", bytePc));

    std::uint32_t literalIndex = 0;
    for (std::uint8_t i = 0; i < instruction.operandCount; ++i)
    {
        if (instruction.operands[i].kind == OperandKind::Literal)
        {
            instruction.operands[i].value = words[baseWords + literalIndex];
            ++literalIndex;
        }
    }

    instruction.wordCount = baseWords + literalWords;
    return instruction;
}

} // namespace SR::Frontend::Gfx
