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
    std::array<OperandKind, 6> operands;
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
    // Data-share (LDS).
    Make(Encoding::Ds, 0, Op::DsAddU32, "ds_add_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 1, Op::DsSubU32, "ds_sub_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 2, Op::DsRsubU32, "ds_rsub_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 3, Op::DsIncU32, "ds_inc_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 4, Op::DsDecU32, "ds_dec_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 5, Op::DsMinI32, "ds_min_i32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 6, Op::DsMaxI32, "ds_max_i32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 7, Op::DsMinU32, "ds_min_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 8, Op::DsMaxU32, "ds_max_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 9, Op::DsAndB32, "ds_and_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 10, Op::DsOrB32, "ds_or_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 11, Op::DsXorB32, "ds_xor_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 12, Op::DsMskorB32, "ds_mskor_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 13, Op::DsStoreB32, "ds_write_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 14, Op::DsStore2addrB32, "ds_write2_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 15, Op::DsStore2addrStride64B32, "ds_write2st64_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 16, Op::DsCmpstoreB32, "ds_cmpst_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 17, Op::DsCmpstF32, "ds_cmpst_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 18, Op::DsMinNumF32, "ds_min_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 19, Op::DsMaxNumF32, "ds_max_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 20, Op::DsNop, "ds_nop",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 21, Op::DsAddF32, "ds_add_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 24, Op::DsGwsSemaReleaseAll, "ds_gws_sema_release_all",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 25, Op::DsGwsInit, "ds_gws_init",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 26, Op::DsGwsSemaV, "ds_gws_sema_v",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 27, Op::DsGwsSemaBr, "ds_gws_sema_br",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 28, Op::DsGwsSemaP, "ds_gws_sema_p",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 29, Op::DsGwsBarrier, "ds_gws_barrier",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 30, Op::DsStoreB8, "ds_write_b8",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 31, Op::DsStoreB16, "ds_write_b16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 32, Op::DsAddRtnU32, "ds_add_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 33, Op::DsSubRtnU32, "ds_sub_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 34, Op::DsRsubRtnU32, "ds_rsub_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 35, Op::DsIncRtnU32, "ds_inc_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 36, Op::DsDecRtnU32, "ds_dec_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 37, Op::DsMinRtnI32, "ds_min_rtn_i32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 38, Op::DsMaxRtnI32, "ds_max_rtn_i32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 39, Op::DsMinRtnU32, "ds_min_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 40, Op::DsMaxRtnU32, "ds_max_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 41, Op::DsAndRtnB32, "ds_and_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 42, Op::DsOrRtnB32, "ds_or_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 43, Op::DsXorRtnB32, "ds_xor_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 44, Op::DsMskorRtnB32, "ds_mskor_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 45, Op::DsStorexchgRtnB32, "ds_wrxchg_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 46, Op::DsStorexchg2addrRtnB32, "ds_wrxchg2_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 47, Op::DsStorexchg2addrStride64RtnB32, "ds_wrxchg2st64_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 48, Op::DsCmpstoreRtnB32, "ds_cmpst_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 49, Op::DsCmpstRtnF32, "ds_cmpst_rtn_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 50, Op::DsMinNumRtnF32, "ds_min_rtn_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 51, Op::DsMaxNumRtnF32, "ds_max_rtn_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 52, Op::DsWrapRtnB32, "ds_wrap_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 53, Op::DsSwizzleB32, "ds_swizzle_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 54, Op::DsLoadB32, "ds_read_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 55, Op::DsLoad2addrB32, "ds_read2_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 56, Op::DsLoad2addrStride64B32, "ds_read2st64_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 57, Op::DsLoadI8, "ds_read_i8",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 58, Op::DsLoadU8, "ds_read_u8",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 59, Op::DsLoadI16, "ds_read_i16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 60, Op::DsLoadU16, "ds_read_u16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 61, Op::DsConsume, "ds_consume",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 62, Op::DsAppend, "ds_append",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 63, Op::DsOrderedCount, "ds_ordered_count",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 64, Op::DsAddU64, "ds_add_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 65, Op::DsSubU64, "ds_sub_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 66, Op::DsRsubU64, "ds_rsub_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 67, Op::DsIncU64, "ds_inc_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 68, Op::DsDecU64, "ds_dec_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 69, Op::DsMinI64, "ds_min_i64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 70, Op::DsMaxI64, "ds_max_i64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 71, Op::DsMinU64, "ds_min_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 72, Op::DsMaxU64, "ds_max_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 73, Op::DsAndB64, "ds_and_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 74, Op::DsOrB64, "ds_or_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 75, Op::DsXorB64, "ds_xor_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 76, Op::DsMskorB64, "ds_mskor_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 77, Op::DsStoreB64, "ds_write_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 78, Op::DsStore2addrB64, "ds_write2_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 79, Op::DsStore2addrStride64B64, "ds_write2st64_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 80, Op::DsCmpstoreB64, "ds_cmpst_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 81, Op::DsCmpstF64, "ds_cmpst_f64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 82, Op::DsMinNumF64, "ds_min_f64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 83, Op::DsMaxNumF64, "ds_max_f64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 85, Op::DsAddRtnF32, "ds_add_rtn_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 96, Op::DsAddRtnU64, "ds_add_rtn_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 97, Op::DsSubRtnU64, "ds_sub_rtn_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 98, Op::DsRsubRtnU64, "ds_rsub_rtn_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 99, Op::DsIncRtnU64, "ds_inc_rtn_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 100, Op::DsDecRtnU64, "ds_dec_rtn_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 101, Op::DsMinRtnI64, "ds_min_rtn_i64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 102, Op::DsMaxRtnI64, "ds_max_rtn_i64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 103, Op::DsMinRtnU64, "ds_min_rtn_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 104, Op::DsMaxRtnU64, "ds_max_rtn_u64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 105, Op::DsAndRtnB64, "ds_and_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 106, Op::DsOrRtnB64, "ds_or_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 107, Op::DsXorRtnB64, "ds_xor_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 108, Op::DsMskorRtnB64, "ds_mskor_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 109, Op::DsStorexchgRtnB64, "ds_wrxchg_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 110, Op::DsStorexchg2addrRtnB64, "ds_wrxchg2_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 111, Op::DsStorexchg2addrStride64RtnB64, "ds_wrxchg2st64_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 112, Op::DsCmpstoreRtnB64, "ds_cmpst_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 113, Op::DsCmpstRtnF64, "ds_cmpst_rtn_f64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 114, Op::DsMinNumRtnF64, "ds_min_rtn_f64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 115, Op::DsMaxNumRtnF64, "ds_max_rtn_f64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 118, Op::DsLoadB64, "ds_read_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 119, Op::DsLoad2addrB64, "ds_read2_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 120, Op::DsLoad2addrStride64B64, "ds_read2st64_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 126, Op::DsCondxchg32RtnB64, "ds_condxchg32_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 160, Op::DsStoreB8D16Hi, "ds_write_b8_d16_hi",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 161, Op::DsStoreB16D16Hi, "ds_write_b16_d16_hi",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 162, Op::DsLoadU8D16, "ds_read_u8_d16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 163, Op::DsLoadU8D16Hi, "ds_read_u8_d16_hi",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 164, Op::DsLoadI8D16, "ds_read_i8_d16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 165, Op::DsLoadI8D16Hi, "ds_read_i8_d16_hi",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 166, Op::DsLoadU16D16, "ds_read_u16_d16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 167, Op::DsLoadU16D16Hi, "ds_read_u16_d16_hi",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 176, Op::DsStoreAddtidB32, "ds_write_addtid_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 177, Op::DsLoadAddtidB32, "ds_read_addtid_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 178, Op::DsPermuteB32, "ds_permute_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 179, Op::DsBpermuteB32, "ds_bpermute_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 222, Op::DsStoreB96, "ds_write_b96",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 223, Op::DsStoreB128, "ds_write_b128",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 254, Op::DsLoadB96, "ds_read_b96",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 255, Op::DsLoadB128, "ds_read_b128",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),

    Make(Encoding::Smem, 1, Op::S_LoadB64, "s_load_dwordx2", {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 2, Op::S_LoadB128, "s_load_dwordx4", {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 3, Op::S_LoadB256, "s_load_dwordx8", {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 4, Op::S_LoadB512, "s_load_dwordx16", {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 9, Op::S_BufferLoadB64, "s_buffer_load_dwordx2", {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 10, Op::S_BufferLoadB128, "s_buffer_load_dwordx4", {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 11, Op::S_BufferLoadB256, "s_buffer_load_dwordx8", {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 12, Op::S_BufferLoadB512, "s_buffer_load_dwordx16", {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset}),
    Make(Encoding::Smem, 31, Op::S_Gl1Inv, "s_gl1_inv", {OperandKind::SmemOffset}),
    Make(Encoding::Smem, 32, Op::S_DcacheInv, "s_dcache_inv", {OperandKind::SmemOffset}),
    Make(Encoding::Smem, 36, Op::S_MemTime, "s_memtime", {OperandKind::SmemSdst}),
    Make(Encoding::Smem, 37, Op::S_MemRealtime, "s_memrealtime", {OperandKind::SmemSdst}),
    Make(Encoding::SopC, 12, Op::S_Bitcmp0B32, "s_bitcmp0_b32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 13, Op::S_Bitcmp1B32, "s_bitcmp1_b32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 14, Op::S_Bitcmp0B64, "s_bitcmp0_b64", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 15, Op::S_Bitcmp1B64, "s_bitcmp1_b64", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 16, Op::S_CmpEqU64, "s_cmp_eq_u64", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 17, Op::S_CmpLgU64, "s_cmp_lg_u64", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::VopC, 125, Op::V_CmpClassF16, "v_cmp_class_f16", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 126, Op::V_CmpClassF32, "v_cmp_class_f32", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::VopC, 127, Op::V_CmpClassF64, "v_cmp_class_f64", {OperandKind::Vcc, OperandKind::Src, OperandKind::Vsrc}),
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
    Make(Encoding::Sop2, 14, Op::S_Lshl1AddU32, "s_lshl1_add_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 15, Op::S_Lshl2AddU32, "s_lshl2_add_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 16, Op::S_Lshl3AddU32, "s_lshl3_add_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 17, Op::S_Lshl4AddU32, "s_lshl4_add_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 50, Op::S_PackLlB32B16, "s_pack_ll_b32_b16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 51, Op::S_PackLhB32B16, "s_pack_lh_b32_b16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 52, Op::S_PackHhB32B16, "s_pack_hh_b32_b16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 53, Op::S_PackHlB32B16, "s_pack_hl_b32_b16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 29, Op::S_NandB64, "s_nand_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 31, Op::S_NorB64, "s_nor_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 33, Op::S_XnorB64, "s_xnor_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 2, Op::S_CmovB32, "s_cmov_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 3, Op::S_CmovB64, "s_cmov_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 20, Op::S_BitreplicateB64B32, "s_bitreplicate_b64_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 26, Op::S_QuadmaskB32, "s_quadmask_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 27, Op::S_QuadmaskB64, "s_quadmask_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 28, Op::S_WqmB32, "s_wqm_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 29, Op::S_WqmB64, "s_wqm_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 32, Op::S_AndSaveexecB32, "s_and_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 33, Op::S_AndSaveexecB64, "s_and_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 34, Op::S_OrSaveexecB32, "s_or_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 35, Op::S_OrSaveexecB64, "s_or_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 36, Op::S_XorSaveexecB32, "s_xor_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 37, Op::S_XorSaveexecB64, "s_xor_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 38, Op::S_NandSaveexecB32, "s_nand_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 39, Op::S_NandSaveexecB64, "s_nand_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 40, Op::S_NorSaveexecB32, "s_nor_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 41, Op::S_NorSaveexecB64, "s_nor_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 42, Op::S_XnorSaveexecB32, "s_xnor_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 43, Op::S_XnorSaveexecB64, "s_xnor_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 44, Op::S_AndNot0SaveexecB32, "s_and_not0_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 45, Op::S_AndNot0SaveexecB64, "s_and_not0_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 46, Op::S_OrNot0SaveexecB32, "s_or_not0_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 47, Op::S_OrNot0SaveexecB64, "s_or_not0_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 48, Op::S_AndNot1SaveexecB32, "s_and_not1_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 49, Op::S_AndNot1SaveexecB64, "s_and_not1_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 50, Op::S_OrNot1SaveexecB32, "s_or_not1_saveexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 51, Op::S_OrNot1SaveexecB64, "s_or_not1_saveexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 52, Op::S_AndNot0WrexecB32, "s_and_not0_wrexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 53, Op::S_AndNot0WrexecB64, "s_and_not0_wrexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 54, Op::S_AndNot1WrexecB32, "s_and_not1_wrexec_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 55, Op::S_AndNot1WrexecB64, "s_and_not1_wrexec_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 64, Op::S_MovrelsB32, "s_movrels_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 65, Op::S_MovrelsB64, "s_movrels_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 66, Op::S_MovreldB32, "s_movreld_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 67, Op::S_MovreldB64, "s_movreld_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 68, Op::S_Movrelsd2B32, "s_movrelsd_2_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 71, Op::S_GetpcB64, "s_getpc_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 72, Op::S_SetpcB64, "s_setpc_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 73, Op::S_SwappcB64, "s_swappc_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 74, Op::S_RfeB64, "s_rfe_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 76, Op::S_SendmsgRtnB32, "s_sendmsg_rtn_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 77, Op::S_SendmsgRtnB64, "s_sendmsg_rtn_b64", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 78, Op::S_BarrierSignal, "s_barrier_signal", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 79, Op::S_BarrierSignalIsfirst, "s_barrier_signal_isfirst", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 83, Op::S_AllocVgpr, "s_alloc_vgpr", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 88, Op::S_SleepVar, "s_sleep_var", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 35, Op::S_AndNot1B64, "s_and_not1_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 37, Op::S_OrNot1B64, "s_or_not1_b64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopP, 0, Op::S_Nop, "s_nop", {OperandKind::Sdst}),
    Make(Encoding::SopP, 1, Op::S_Setkill, "s_setkill", {OperandKind::Sdst}),
    Make(Encoding::SopP, 2, Op::S_Sethalt, "s_sethalt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 3, Op::S_Sleep, "s_sleep", {OperandKind::Sdst}),
    Make(Encoding::SopP, 5, Op::S_Clause, "s_clause", {OperandKind::Sdst}),
    Make(Encoding::SopP, 7, Op::S_DelayAlu, "s_delay_alu", {OperandKind::Sdst}),
    Make(Encoding::SopP, 8, Op::S_WaitAlu, "s_wait_alu", {OperandKind::Sdst}),
    Make(Encoding::SopP, 9, Op::S_Waitcnt, "s_waitcnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 10, Op::S_WaitIdle, "s_wait_idle", {OperandKind::Sdst}),
    Make(Encoding::SopP, 11, Op::S_WaitEvent, "s_wait_event", {OperandKind::Sdst}),
    Make(Encoding::SopP, 16, Op::S_Trap, "s_trap", {OperandKind::Sdst}),
    Make(Encoding::SopP, 17, Op::S_RoundMode, "s_round_mode", {OperandKind::Sdst}),
    Make(Encoding::SopP, 18, Op::S_DenormMode, "s_denorm_mode", {OperandKind::Sdst}),
    Make(Encoding::SopP, 31, Op::S_CodeEnd, "s_code_end", {OperandKind::Sdst}),
    Make(Encoding::SopP, 49, Op::S_EndpgmSaved, "s_endpgm_saved", {OperandKind::Sdst}),
    Make(Encoding::SopP, 52, Op::S_Wakeup, "s_wakeup", {OperandKind::Sdst}),
    Make(Encoding::SopP, 53, Op::S_Setprio, "s_setprio", {OperandKind::Sdst}),
    Make(Encoding::SopP, 54, Op::S_Sendmsg, "s_sendmsg", {OperandKind::Sdst}),
    Make(Encoding::SopP, 55, Op::S_Sendmsghalt, "s_sendmsghalt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 56, Op::S_Incperflevel, "s_incperflevel", {OperandKind::Sdst}),
    Make(Encoding::SopP, 57, Op::S_Decperflevel, "s_decperflevel", {OperandKind::Sdst}),
    Make(Encoding::SopP, 58, Op::S_Ttracedata, "s_ttracedata", {OperandKind::Sdst}),
    Make(Encoding::SopP, 59, Op::S_TtracedataImm, "s_ttracedata_imm", {OperandKind::Sdst}),
    Make(Encoding::SopP, 60, Op::S_IcacheInv, "s_icache_inv", {OperandKind::Sdst}),
    Make(Encoding::SopK, 1, Op::S_Version, "s_version", {OperandKind::Sdst, OperandKind::Simm16}),
    Make(Encoding::SopK, 2, Op::S_CmovkI32, "s_cmovk_i32", {OperandKind::Sdst, OperandKind::Simm16}),
    Make(Encoding::SopK, 15, Op::S_AddkCoI32, "s_addk_co_i32", {OperandKind::Sdst, OperandKind::Simm16}),
    Make(Encoding::SopK, 17, Op::S_GetregB32, "s_getreg_b32", {OperandKind::Sdst, OperandKind::Simm16}),
    Make(Encoding::SopK, 18, Op::S_SetregB32, "s_setreg_b32", {OperandKind::Sdst, OperandKind::Simm16}),
    Make(Encoding::SopK, 20, Op::S_CallB64, "s_call_b64", {OperandKind::Sdst, OperandKind::Simm16}),
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
    Remove(Encoding::Ds, 17),
    Remove(Encoding::Ds, 24),
    Remove(Encoding::Ds, 25),
    Remove(Encoding::Ds, 26),
    Remove(Encoding::Ds, 27),
    Remove(Encoding::Ds, 28),
    Remove(Encoding::Ds, 29),
    Remove(Encoding::Ds, 49),
    Remove(Encoding::Ds, 52),
    Remove(Encoding::Ds, 63),
    Remove(Encoding::Ds, 81),
    Remove(Encoding::Ds, 85),
    Remove(Encoding::Ds, 113),
    Make(Encoding::Ds, 121, Op::DsAddRtnF32, "ds_add_rtn_f32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 152, Op::DsCondSubU32, "ds_cond_sub_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 153, Op::DsSubClampU32, "ds_sub_clamp_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 154, Op::DsPkAddF16, "ds_pk_add_f16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 155, Op::DsPkAddBf16, "ds_pk_add_bf16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 168, Op::DsCondSubRtnU32, "ds_cond_sub_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 169, Op::DsSubClampRtnU32, "ds_sub_clamp_rtn_u32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 170, Op::DsPkAddRtnF16, "ds_pk_add_rtn_f16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 171, Op::DsPkAddRtnBf16, "ds_pk_add_rtn_bf16",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 205, Op::DsBpermuteFiB32, "ds_bpermute_fi_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 224, Op::DsBvhStackPush4Pop1RtnB32, "ds_bvh_stack_push4_pop1_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 225, Op::DsBvhStackPush8Pop1RtnB32, "ds_bvh_stack_push8_pop1_rtn_b32",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),
    Make(Encoding::Ds, 226, Op::DsBvhStackPush8Pop2RtnB64, "ds_bvh_stack_push8_pop2_rtn_b64",
         {OperandKind::DsDest, OperandKind::DsAddr, OperandKind::DsData0, OperandKind::DsData1}),

    // RDNA4 flat instructions.
    Make(Encoding::Flat, 16, Op::FlatLoadU8, "flat_load_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 17, Op::FlatLoadI8, "flat_load_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 18, Op::FlatLoadU16, "flat_load_u16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 19, Op::FlatLoadI16, "flat_load_i16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 20, Op::FlatLoadB32, "flat_load_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 21, Op::FlatLoadB64, "flat_load_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 22, Op::FlatLoadB96, "flat_load_b96",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 23, Op::FlatLoadB128, "flat_load_b128",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 24, Op::FlatStoreB8, "flat_store_b8",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 25, Op::FlatStoreB16, "flat_store_b16",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 26, Op::FlatStoreB32, "flat_store_b32",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 27, Op::FlatStoreB64, "flat_store_b64",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 28, Op::FlatStoreB96, "flat_store_b96",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 29, Op::FlatStoreB128, "flat_store_b128",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 30, Op::FlatLoadD16U8, "flat_load_d16_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 31, Op::FlatLoadD16I8, "flat_load_d16_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 32, Op::FlatLoadD16B16, "flat_load_d16_b16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 33, Op::FlatLoadD16HiU8, "flat_load_d16_hi_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 34, Op::FlatLoadD16HiI8, "flat_load_d16_hi_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 35, Op::FlatLoadD16HiB16, "flat_load_d16_hi_b16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 36, Op::FlatStoreD16HiB8, "flat_store_d16_hi_b8",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 37, Op::FlatStoreD16HiB16, "flat_store_d16_hi_b16",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 51, Op::FlatAtomicSwapB32, "flat_atomic_swap_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 52, Op::FlatAtomicCmpswapB32, "flat_atomic_cmpswap_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 53, Op::FlatAtomicAddU32, "flat_atomic_add_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 54, Op::FlatAtomicSubU32, "flat_atomic_sub_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 55, Op::FlatAtomicSubClampU32, "flat_atomic_sub_clamp_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 56, Op::FlatAtomicMinI32, "flat_atomic_min_i32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 57, Op::FlatAtomicMinU32, "flat_atomic_min_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 58, Op::FlatAtomicMaxI32, "flat_atomic_max_i32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 59, Op::FlatAtomicMaxU32, "flat_atomic_max_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 60, Op::FlatAtomicAndB32, "flat_atomic_and_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 61, Op::FlatAtomicOrB32, "flat_atomic_or_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 62, Op::FlatAtomicXorB32, "flat_atomic_xor_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 63, Op::FlatAtomicIncU32, "flat_atomic_inc_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 64, Op::FlatAtomicDecU32, "flat_atomic_dec_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 65, Op::FlatAtomicSwapB64, "flat_atomic_swap_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 66, Op::FlatAtomicCmpswapB64, "flat_atomic_cmpswap_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 67, Op::FlatAtomicAddU64, "flat_atomic_add_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 68, Op::FlatAtomicSubU64, "flat_atomic_sub_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 69, Op::FlatAtomicMinI64, "flat_atomic_min_i64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 70, Op::FlatAtomicMinU64, "flat_atomic_min_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 71, Op::FlatAtomicMaxI64, "flat_atomic_max_i64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 72, Op::FlatAtomicMaxU64, "flat_atomic_max_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 73, Op::FlatAtomicAndB64, "flat_atomic_and_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 74, Op::FlatAtomicOrB64, "flat_atomic_or_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 75, Op::FlatAtomicXorB64, "flat_atomic_xor_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 76, Op::FlatAtomicIncU64, "flat_atomic_inc_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 77, Op::FlatAtomicDecU64, "flat_atomic_dec_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 80, Op::FlatAtomicCondSubU32, "flat_atomic_cond_sub_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 81, Op::FlatAtomicMinNumF32, "flat_atomic_min_num_f32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 82, Op::FlatAtomicMaxNumF32, "flat_atomic_max_num_f32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 86, Op::FlatAtomicAddF32, "flat_atomic_add_f32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 89, Op::FlatAtomicPkAddF16, "flat_atomic_pk_add_f16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Flat, 90, Op::FlatAtomicPkAddBf16, "flat_atomic_pk_add_bf16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),

    // RDNA4 global instructions.
    Make(Encoding::Global, 16, Op::GlobalLoadU8, "global_load_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 17, Op::GlobalLoadI8, "global_load_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 18, Op::GlobalLoadU16, "global_load_u16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 19, Op::GlobalLoadI16, "global_load_i16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 21, Op::GlobalLoadB64, "global_load_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 22, Op::GlobalLoadB96, "global_load_b96",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 23, Op::GlobalLoadB128, "global_load_b128",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 24, Op::GlobalStoreB8, "global_store_b8",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 25, Op::GlobalStoreB16, "global_store_b16",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 27, Op::GlobalStoreB64, "global_store_b64",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 28, Op::GlobalStoreB96, "global_store_b96",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 29, Op::GlobalStoreB128, "global_store_b128",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 30, Op::GlobalLoadD16U8, "global_load_d16_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 31, Op::GlobalLoadD16I8, "global_load_d16_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 32, Op::GlobalLoadD16B16, "global_load_d16_b16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 33, Op::GlobalLoadD16HiU8, "global_load_d16_hi_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 34, Op::GlobalLoadD16HiI8, "global_load_d16_hi_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 35, Op::GlobalLoadD16HiB16, "global_load_d16_hi_b16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 36, Op::GlobalStoreD16HiB8, "global_store_d16_hi_b8",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 37, Op::GlobalStoreD16HiB16, "global_store_d16_hi_b16",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 40, Op::GlobalLoadAddtidB32, "global_load_addtid_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 41, Op::GlobalStoreAddtidB32, "global_store_addtid_b32",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 43, Op::GlobalInv, "global_inv",
         {OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 44, Op::GlobalWb, "global_wb",
         {OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 51, Op::GlobalAtomicSwapB32, "global_atomic_swap_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 52, Op::GlobalAtomicCmpswapB32, "global_atomic_cmpswap_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 53, Op::GlobalAtomicAddU32, "global_atomic_add_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 54, Op::GlobalAtomicSubU32, "global_atomic_sub_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 55, Op::GlobalAtomicSubClampU32, "global_atomic_sub_clamp_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 56, Op::GlobalAtomicMinI32, "global_atomic_min_i32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 57, Op::GlobalAtomicMinU32, "global_atomic_min_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 58, Op::GlobalAtomicMaxI32, "global_atomic_max_i32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 59, Op::GlobalAtomicMaxU32, "global_atomic_max_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 60, Op::GlobalAtomicAndB32, "global_atomic_and_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 61, Op::GlobalAtomicOrB32, "global_atomic_or_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 62, Op::GlobalAtomicXorB32, "global_atomic_xor_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 63, Op::GlobalAtomicIncU32, "global_atomic_inc_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 64, Op::GlobalAtomicDecU32, "global_atomic_dec_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 65, Op::GlobalAtomicSwapB64, "global_atomic_swap_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 66, Op::GlobalAtomicCmpswapB64, "global_atomic_cmpswap_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 67, Op::GlobalAtomicAddU64, "global_atomic_add_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 68, Op::GlobalAtomicSubU64, "global_atomic_sub_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 69, Op::GlobalAtomicMinI64, "global_atomic_min_i64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 70, Op::GlobalAtomicMinU64, "global_atomic_min_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 71, Op::GlobalAtomicMaxI64, "global_atomic_max_i64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 72, Op::GlobalAtomicMaxU64, "global_atomic_max_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 73, Op::GlobalAtomicAndB64, "global_atomic_and_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 74, Op::GlobalAtomicOrB64, "global_atomic_or_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 75, Op::GlobalAtomicXorB64, "global_atomic_xor_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 76, Op::GlobalAtomicIncU64, "global_atomic_inc_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 77, Op::GlobalAtomicDecU64, "global_atomic_dec_u64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 79, Op::GlobalWbinv, "global_wbinv",
         {OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 80, Op::GlobalAtomicCondSubU32, "global_atomic_cond_sub_u32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 81, Op::GlobalAtomicMinNumF32, "global_atomic_min_num_f32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 82, Op::GlobalAtomicMaxNumF32, "global_atomic_max_num_f32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 83, Op::GlobalLoadBlock, "global_load_block",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 84, Op::GlobalStoreBlock, "global_store_block",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 86, Op::GlobalAtomicAddF32, "global_atomic_add_f32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 87, Op::GlobalLoadTrB128, "global_load_tr_b128",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 88, Op::GlobalLoadTrB64, "global_load_tr_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 89, Op::GlobalAtomicPkAddF16, "global_atomic_pk_add_f16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 90, Op::GlobalAtomicPkAddBf16, "global_atomic_pk_add_bf16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Global, 115, Op::GlobalAtomicOrderedAddB64, "global_atomic_ordered_add_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),

    // RDNA4 scratch instructions.
    Make(Encoding::Scratch, 16, Op::ScratchLoadU8, "scratch_load_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 17, Op::ScratchLoadI8, "scratch_load_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 18, Op::ScratchLoadU16, "scratch_load_u16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 19, Op::ScratchLoadI16, "scratch_load_i16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 20, Op::ScratchLoadB32, "scratch_load_b32",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 21, Op::ScratchLoadB64, "scratch_load_b64",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 22, Op::ScratchLoadB96, "scratch_load_b96",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 23, Op::ScratchLoadB128, "scratch_load_b128",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 24, Op::ScratchStoreB8, "scratch_store_b8",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 25, Op::ScratchStoreB16, "scratch_store_b16",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 26, Op::ScratchStoreB32, "scratch_store_b32",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 27, Op::ScratchStoreB64, "scratch_store_b64",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 28, Op::ScratchStoreB96, "scratch_store_b96",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 29, Op::ScratchStoreB128, "scratch_store_b128",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 30, Op::ScratchLoadD16U8, "scratch_load_d16_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 31, Op::ScratchLoadD16I8, "scratch_load_d16_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 32, Op::ScratchLoadD16B16, "scratch_load_d16_b16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 33, Op::ScratchLoadD16HiU8, "scratch_load_d16_hi_u8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 34, Op::ScratchLoadD16HiI8, "scratch_load_d16_hi_i8",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 35, Op::ScratchLoadD16HiB16, "scratch_load_d16_hi_b16",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 36, Op::ScratchStoreD16HiB8, "scratch_store_d16_hi_b8",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 37, Op::ScratchStoreD16HiB16, "scratch_store_d16_hi_b16",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 83, Op::ScratchLoadBlock, "scratch_load_block",
         {OperandKind::GlobalVdst, OperandKind::GlobalVaddr, OperandKind::GlobalIoffset}),
    Make(Encoding::Scratch, 84, Op::ScratchStoreBlock, "scratch_store_block",
         {OperandKind::GlobalVaddr, OperandKind::GlobalVsrc, OperandKind::GlobalIoffset}),


    Make(Encoding::Buffer, 0, Op::BufferLoadFormatX, "buffer_load_format_x",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 1, Op::BufferLoadFormatXy, "buffer_load_format_xy",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 2, Op::BufferLoadFormatXyz, "buffer_load_format_xyz",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 3, Op::BufferLoadFormatXyzw, "buffer_load_format_xyzw",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 4, Op::BufferStoreFormatX, "buffer_store_format_x",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 5, Op::BufferStoreFormatXy, "buffer_store_format_xy",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 6, Op::BufferStoreFormatXyz, "buffer_store_format_xyz",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 7, Op::BufferStoreFormatXyzw, "buffer_store_format_xyzw",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 8, Op::BufferLoadD16FormatX, "buffer_load_d16_format_x",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 9, Op::BufferLoadD16FormatXy, "buffer_load_d16_format_xy",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 10, Op::BufferLoadD16FormatXyz, "buffer_load_d16_format_xyz",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 11, Op::BufferLoadD16FormatXyzw, "buffer_load_d16_format_xyzw",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 12, Op::BufferStoreD16FormatX, "buffer_store_d16_format_x",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 13, Op::BufferStoreD16FormatXy, "buffer_store_d16_format_xy",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 14, Op::BufferStoreD16FormatXyz, "buffer_store_d16_format_xyz",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 15, Op::BufferStoreD16FormatXyzw, "buffer_store_d16_format_xyzw",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 16, Op::BufferLoadU8, "buffer_load_u8",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 17, Op::BufferLoadI8, "buffer_load_i8",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 18, Op::BufferLoadU16, "buffer_load_u16",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 19, Op::BufferLoadI16, "buffer_load_i16",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 20, Op::BufferLoadB32, "buffer_load_b32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 21, Op::BufferLoadB64, "buffer_load_b64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 22, Op::BufferLoadB96, "buffer_load_b96",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 23, Op::BufferLoadB128, "buffer_load_b128",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 24, Op::BufferStoreB8, "buffer_store_b8",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 25, Op::BufferStoreB16, "buffer_store_b16",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 26, Op::BufferStoreB32, "buffer_store_b32",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 27, Op::BufferStoreB64, "buffer_store_b64",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 28, Op::BufferStoreB96, "buffer_store_b96",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 29, Op::BufferStoreB128, "buffer_store_b128",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 30, Op::BufferLoadD16U8, "buffer_load_d16_u8",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 31, Op::BufferLoadD16I8, "buffer_load_d16_i8",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 32, Op::BufferLoadD16B16, "buffer_load_d16_b16",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 33, Op::BufferLoadD16HiU8, "buffer_load_d16_hi_u8",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 34, Op::BufferLoadD16HiI8, "buffer_load_d16_hi_i8",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 35, Op::BufferLoadD16HiB16, "buffer_load_d16_hi_b16",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 36, Op::BufferStoreD16HiB8, "buffer_store_d16_hi_b8",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 37, Op::BufferStoreD16HiB16, "buffer_store_d16_hi_b16",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 38, Op::BufferLoadD16HiFormatX, "buffer_load_d16_hi_format_x",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 39, Op::BufferStoreD16HiFormatX, "buffer_store_d16_hi_format_x",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 51, Op::BufferAtomicSwapB32, "buffer_atomic_swap_b32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 52, Op::BufferAtomicCmpswapB32, "buffer_atomic_cmpswap_b32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 53, Op::BufferAtomicAddU32, "buffer_atomic_add_u32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 54, Op::BufferAtomicSubU32, "buffer_atomic_sub_u32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 55, Op::BufferAtomicSubClampU32, "buffer_atomic_sub_clamp_u32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 56, Op::BufferAtomicMinI32, "buffer_atomic_min_i32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 57, Op::BufferAtomicMinU32, "buffer_atomic_min_u32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 58, Op::BufferAtomicMaxI32, "buffer_atomic_max_i32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 59, Op::BufferAtomicMaxU32, "buffer_atomic_max_u32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 60, Op::BufferAtomicAndB32, "buffer_atomic_and_b32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 61, Op::BufferAtomicOrB32, "buffer_atomic_or_b32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 62, Op::BufferAtomicXorB32, "buffer_atomic_xor_b32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 63, Op::BufferAtomicIncU32, "buffer_atomic_inc_u32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 64, Op::BufferAtomicDecU32, "buffer_atomic_dec_u32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 65, Op::BufferAtomicSwapB64, "buffer_atomic_swap_b64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 66, Op::BufferAtomicCmpswapB64, "buffer_atomic_cmpswap_b64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 67, Op::BufferAtomicAddU64, "buffer_atomic_add_u64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 68, Op::BufferAtomicSubU64, "buffer_atomic_sub_u64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 69, Op::BufferAtomicMinI64, "buffer_atomic_min_i64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 70, Op::BufferAtomicMinU64, "buffer_atomic_min_u64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 71, Op::BufferAtomicMaxI64, "buffer_atomic_max_i64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 72, Op::BufferAtomicMaxU64, "buffer_atomic_max_u64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 73, Op::BufferAtomicAndB64, "buffer_atomic_and_b64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 74, Op::BufferAtomicOrB64, "buffer_atomic_or_b64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 75, Op::BufferAtomicXorB64, "buffer_atomic_xor_b64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 76, Op::BufferAtomicIncU64, "buffer_atomic_inc_u64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 77, Op::BufferAtomicDecU64, "buffer_atomic_dec_u64",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 80, Op::BufferAtomicCondSubU32, "buffer_atomic_cond_sub_u32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 81, Op::BufferAtomicMinNumF32, "buffer_atomic_min_num_f32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 82, Op::BufferAtomicMaxNumF32, "buffer_atomic_max_num_f32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 86, Op::BufferAtomicAddF32, "buffer_atomic_add_f32",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 89, Op::BufferAtomicPkAddF16, "buffer_atomic_pk_add_f16",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 90, Op::BufferAtomicPkAddBf16, "buffer_atomic_pk_add_bf16",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 128, Op::TbufferLoadFormatX, "tbuffer_load_format_x",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 129, Op::TbufferLoadFormatXy, "tbuffer_load_format_xy",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 130, Op::TbufferLoadFormatXyz, "tbuffer_load_format_xyz",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 131, Op::TbufferLoadFormatXyzw, "tbuffer_load_format_xyzw",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 132, Op::TbufferStoreFormatX, "tbuffer_store_format_x",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 133, Op::TbufferStoreFormatXy, "tbuffer_store_format_xy",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 134, Op::TbufferStoreFormatXyz, "tbuffer_store_format_xyz",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 135, Op::TbufferStoreFormatXyzw, "tbuffer_store_format_xyzw",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 136, Op::TbufferLoadD16FormatX, "tbuffer_load_d16_format_x",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 137, Op::TbufferLoadD16FormatXy, "tbuffer_load_d16_format_xy",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 138, Op::TbufferLoadD16FormatXyz, "tbuffer_load_d16_format_xyz",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 139, Op::TbufferLoadD16FormatXyzw, "tbuffer_load_d16_format_xyzw",
         {OperandKind::BufferVdst, OperandKind::BufferVaddr, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 140, Op::TbufferStoreD16FormatX, "tbuffer_store_d16_format_x",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 141, Op::TbufferStoreD16FormatXy, "tbuffer_store_d16_format_xy",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 142, Op::TbufferStoreD16FormatXyz, "tbuffer_store_d16_format_xyz",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),
    Make(Encoding::Buffer, 143, Op::TbufferStoreD16FormatXyzw, "tbuffer_store_d16_format_xyzw",
         {OperandKind::BufferVaddr, OperandKind::BufferVsrc, OperandKind::BufferSoffset, OperandKind::BufferIoffset}),

    Make(Encoding::Smem, 1, Op::S_LoadB64, "s_load_b64",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 2, Op::S_LoadB128, "s_load_b128",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 3, Op::S_LoadB256, "s_load_b256",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 4, Op::S_LoadB512, "s_load_b512",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 5, Op::S_LoadB96, "s_load_b96",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 8, Op::S_LoadI8, "s_load_i8",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 9, Op::S_LoadU8, "s_load_u8",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 10, Op::S_LoadI16, "s_load_i16",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 11, Op::S_LoadU16, "s_load_u16",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 17, Op::S_BufferLoadB64, "s_buffer_load_b64",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 18, Op::S_BufferLoadB128, "s_buffer_load_b128",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 19, Op::S_BufferLoadB256, "s_buffer_load_b256",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 20, Op::S_BufferLoadB512, "s_buffer_load_b512",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 21, Op::S_BufferLoadB96, "s_buffer_load_b96",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 24, Op::S_BufferLoadI8, "s_buffer_load_i8",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 25, Op::S_BufferLoadU8, "s_buffer_load_u8",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 26, Op::S_BufferLoadI16, "s_buffer_load_i16",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 27, Op::S_BufferLoadU16, "s_buffer_load_u16",
         {OperandKind::SmemSdst, OperandKind::SmemSbase, OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 33, Op::S_DcacheInv, "s_dcache_inv",
         {OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 34, Op::S_AtcProbe, "s_atc_probe",
         {OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 35, Op::S_AtcProbeBuffer, "s_atc_probe_buffer",
         {OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 36, Op::S_PrefetchInst, "s_prefetch_inst",
         {OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 37, Op::S_PrefetchInstPcRel, "s_prefetch_inst_pc_rel",
         {OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 38, Op::S_PrefetchData, "s_prefetch_data",
         {OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 39, Op::S_BufferPrefetchData, "s_buffer_prefetch_data",
         {OperandKind::SmemOffset24}),
    Make(Encoding::Smem, 40, Op::S_PrefetchDataPcRel, "s_prefetch_data_pc_rel",
         {OperandKind::SmemOffset24}),
    Make(Encoding::Sop2, 64, Op::S_AddF32, "s_add_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 65, Op::S_SubF32, "s_sub_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 66, Op::S_MinNumF32, "s_min_num_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 67, Op::S_MaxNumF32, "s_max_num_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 68, Op::S_MulF32, "s_mul_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 71, Op::S_FmacF32, "s_fmac_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 72, Op::S_CvtPkRtzF16F32, "s_cvt_pk_rtz_f16_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 73, Op::S_AddF16, "s_add_f16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 74, Op::S_SubF16, "s_sub_f16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 75, Op::S_MinNumF16, "s_min_num_f16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 76, Op::S_MaxNumF16, "s_max_num_f16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 77, Op::S_MulF16, "s_mul_f16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 78, Op::S_FmacF16, "s_fmac_f16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 79, Op::S_MinimumF32, "s_minimum_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 80, Op::S_MaximumF32, "s_maximum_f32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 81, Op::S_MinimumF16, "s_minimum_f16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 82, Op::S_MaximumF16, "s_maximum_f16", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 83, Op::S_AddNcU64, "s_add_nc_u64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 84, Op::S_SubNcU64, "s_sub_nc_u64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 85, Op::S_MulU64, "s_mul_u64", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 65, Op::S_CmpLtF32, "s_cmp_lt_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 66, Op::S_CmpEqF32, "s_cmp_eq_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 67, Op::S_CmpLeF32, "s_cmp_le_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 68, Op::S_CmpGtF32, "s_cmp_gt_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 69, Op::S_CmpLgF32, "s_cmp_lg_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 70, Op::S_CmpGeF32, "s_cmp_ge_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 71, Op::S_CmpOF32, "s_cmp_o_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 72, Op::S_CmpUF32, "s_cmp_u_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 73, Op::S_CmpNgeF32, "s_cmp_nge_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 74, Op::S_CmpNlgF32, "s_cmp_nlg_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 75, Op::S_CmpNgtF32, "s_cmp_ngt_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 76, Op::S_CmpNleF32, "s_cmp_nle_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 77, Op::S_CmpNeqF32, "s_cmp_neq_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 78, Op::S_CmpNltF32, "s_cmp_nlt_f32", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 81, Op::S_CmpLtF16, "s_cmp_lt_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 82, Op::S_CmpEqF16, "s_cmp_eq_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 83, Op::S_CmpLeF16, "s_cmp_le_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 84, Op::S_CmpGtF16, "s_cmp_gt_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 85, Op::S_CmpLgF16, "s_cmp_lg_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 86, Op::S_CmpGeF16, "s_cmp_ge_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 87, Op::S_CmpOF16, "s_cmp_o_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 88, Op::S_CmpUF16, "s_cmp_u_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 89, Op::S_CmpNgeF16, "s_cmp_nge_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 90, Op::S_CmpNlgF16, "s_cmp_nlg_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 91, Op::S_CmpNgtF16, "s_cmp_ngt_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 92, Op::S_CmpNleF16, "s_cmp_nle_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 93, Op::S_CmpNeqF16, "s_cmp_neq_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::SopC, 94, Op::S_CmpNltF16, "s_cmp_nlt_f16", {OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 96, Op::S_CeilF32, "s_ceil_f32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 97, Op::S_FloorF32, "s_floor_f32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 98, Op::S_TruncF32, "s_trunc_f32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 99, Op::S_RndneF32, "s_rndne_f32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 100, Op::S_CvtF32I32, "s_cvt_f32_i32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 101, Op::S_CvtF32U32, "s_cvt_f32_u32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 102, Op::S_CvtI32F32, "s_cvt_i32_f32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 103, Op::S_CvtU32F32, "s_cvt_u32_f32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 104, Op::S_CvtF16F32, "s_cvt_f16_f32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 105, Op::S_CvtF32F16, "s_cvt_f32_f16", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 106, Op::S_CvtHiF32F16, "s_cvt_hi_f32_f16", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 107, Op::S_CeilF16, "s_ceil_f16", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 108, Op::S_FloorF16, "s_floor_f16", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 109, Op::S_TruncF16, "s_trunc_f16", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 110, Op::S_RndneF16, "s_rndne_f16", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::SopP, 20, Op::S_BarrierWait, "s_barrier_wait", {OperandKind::Sdst}),
    Make(Encoding::SopP, 64, Op::S_WaitLoadcnt, "s_wait_loadcnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 65, Op::S_WaitStorecnt, "s_wait_storecnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 66, Op::S_WaitSamplecnt, "s_wait_samplecnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 67, Op::S_WaitBvhcnt, "s_wait_bvhcnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 68, Op::S_WaitExpcnt, "s_wait_expcnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 70, Op::S_WaitDscnt, "s_wait_dscnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 71, Op::S_WaitKmcnt, "s_wait_kmcnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 72, Op::S_WaitLoadcntDscnt, "s_wait_loadcnt_dscnt", {OperandKind::Sdst}),
    Make(Encoding::SopP, 73, Op::S_WaitStorecntDscnt, "s_wait_storecnt_dscnt", {OperandKind::Sdst}),
    // VOP3-encoded compares write a 32-bit mask to an explicit SGPR.
    Make(Encoding::Vop3, 1, Op::V_CmpLtF16, "v_cmp_lt_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 2, Op::V_CmpEqF16, "v_cmp_eq_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 3, Op::V_CmpLeF16, "v_cmp_le_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 4, Op::V_CmpGtF16, "v_cmp_gt_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 5, Op::V_CmpLgF16, "v_cmp_lg_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 6, Op::V_CmpGeF16, "v_cmp_ge_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 7, Op::V_CmpOrdF16, "v_cmp_o_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 8, Op::V_CmpUnordF16, "v_cmp_u_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 9, Op::V_CmpNgeF16, "v_cmp_nge_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 10, Op::V_CmpNlgF16, "v_cmp_nlg_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 11, Op::V_CmpNgtF16, "v_cmp_ngt_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 12, Op::V_CmpNleF16, "v_cmp_nle_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 13, Op::V_CmpNeqF16, "v_cmp_neq_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 14, Op::V_CmpNltF16, "v_cmp_nlt_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 17, Op::V_CmpLtF32, "v_cmp_lt_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 18, Op::V_CmpEqF32, "v_cmp_eq_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 19, Op::V_CmpLeF32, "v_cmp_le_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 20, Op::V_CmpGtF32, "v_cmp_gt_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 21, Op::V_CmpLgF32, "v_cmp_lg_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 22, Op::V_CmpGeF32, "v_cmp_ge_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 23, Op::V_CmpOrdF32, "v_cmp_o_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 24, Op::V_CmpUnordF32, "v_cmp_u_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 25, Op::V_CmpNgeF32, "v_cmp_nge_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 26, Op::V_CmpNlgF32, "v_cmp_nlg_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 27, Op::V_CmpNgtF32, "v_cmp_ngt_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 28, Op::V_CmpNleF32, "v_cmp_nle_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 29, Op::V_CmpNeqF32, "v_cmp_neq_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 30, Op::V_CmpNltF32, "v_cmp_nlt_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 33, Op::V_CmpLtF64, "v_cmp_lt_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 34, Op::V_CmpEqF64, "v_cmp_eq_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 35, Op::V_CmpLeF64, "v_cmp_le_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 36, Op::V_CmpGtF64, "v_cmp_gt_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 37, Op::V_CmpLgF64, "v_cmp_lg_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 38, Op::V_CmpGeF64, "v_cmp_ge_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 39, Op::V_CmpOrdF64, "v_cmp_o_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 40, Op::V_CmpUnordF64, "v_cmp_u_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 41, Op::V_CmpNgeF64, "v_cmp_nge_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 42, Op::V_CmpNlgF64, "v_cmp_nlg_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 43, Op::V_CmpNgtF64, "v_cmp_ngt_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 44, Op::V_CmpNleF64, "v_cmp_nle_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 45, Op::V_CmpNeqF64, "v_cmp_neq_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 46, Op::V_CmpNltF64, "v_cmp_nlt_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 49, Op::V_CmpLtI16, "v_cmp_lt_i16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 50, Op::V_CmpEqI16, "v_cmp_eq_i16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 51, Op::V_CmpLeI16, "v_cmp_le_i16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 52, Op::V_CmpGtI16, "v_cmp_gt_i16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 53, Op::V_CmpNeI16, "v_cmp_ne_i16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 54, Op::V_CmpGeI16, "v_cmp_ge_i16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 57, Op::V_CmpLtU16, "v_cmp_lt_u16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 58, Op::V_CmpEqU16, "v_cmp_eq_u16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 59, Op::V_CmpLeU16, "v_cmp_le_u16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 60, Op::V_CmpGtU16, "v_cmp_gt_u16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 61, Op::V_CmpNeU16, "v_cmp_ne_u16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 62, Op::V_CmpGeU16, "v_cmp_ge_u16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 65, Op::V_CmpLtI32, "v_cmp_lt_i32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 66, Op::V_CmpEqI32, "v_cmp_eq_i32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 67, Op::V_CmpLeI32, "v_cmp_le_i32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 68, Op::V_CmpGtI32, "v_cmp_gt_i32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 69, Op::V_CmpNeI32, "v_cmp_ne_i32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 70, Op::V_CmpGeI32, "v_cmp_ge_i32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 73, Op::V_CmpLtU32, "v_cmp_lt_u32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 74, Op::V_CmpEqU32, "v_cmp_eq_u32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 75, Op::V_CmpLeU32, "v_cmp_le_u32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 76, Op::V_CmpGtU32, "v_cmp_gt_u32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 77, Op::V_CmpNeU32, "v_cmp_ne_u32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 78, Op::V_CmpGeU32, "v_cmp_ge_u32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 81, Op::V_CmpLtI64, "v_cmp_lt_i64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 82, Op::V_CmpEqI64, "v_cmp_eq_i64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 83, Op::V_CmpLeI64, "v_cmp_le_i64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 84, Op::V_CmpGtI64, "v_cmp_gt_i64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 85, Op::V_CmpNeI64, "v_cmp_ne_i64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 86, Op::V_CmpGeI64, "v_cmp_ge_i64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 89, Op::V_CmpLtU64, "v_cmp_lt_u64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 90, Op::V_CmpEqU64, "v_cmp_eq_u64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 91, Op::V_CmpLeU64, "v_cmp_le_u64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 92, Op::V_CmpGtU64, "v_cmp_gt_u64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 93, Op::V_CmpNeU64, "v_cmp_ne_u64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 94, Op::V_CmpGeU64, "v_cmp_ge_u64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 129, Op::V_CmpLtF16, "v_cmpx_lt_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 130, Op::V_CmpEqF16, "v_cmpx_eq_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 131, Op::V_CmpLeF16, "v_cmpx_le_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 132, Op::V_CmpGtF16, "v_cmpx_gt_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 133, Op::V_CmpLgF16, "v_cmpx_lg_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 134, Op::V_CmpGeF16, "v_cmpx_ge_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 135, Op::V_CmpOrdF16, "v_cmpx_o_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 136, Op::V_CmpUnordF16, "v_cmpx_u_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 137, Op::V_CmpNgeF16, "v_cmpx_nge_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 138, Op::V_CmpNlgF16, "v_cmpx_nlg_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 139, Op::V_CmpNgtF16, "v_cmpx_ngt_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 140, Op::V_CmpNleF16, "v_cmpx_nle_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 141, Op::V_CmpNeqF16, "v_cmpx_neq_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 142, Op::V_CmpNltF16, "v_cmpx_nlt_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 145, Op::V_CmpLtF32, "v_cmpx_lt_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 146, Op::V_CmpEqF32, "v_cmpx_eq_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 147, Op::V_CmpLeF32, "v_cmpx_le_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 148, Op::V_CmpGtF32, "v_cmpx_gt_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 149, Op::V_CmpLgF32, "v_cmpx_lg_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 150, Op::V_CmpGeF32, "v_cmpx_ge_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 151, Op::V_CmpOrdF32, "v_cmpx_o_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 152, Op::V_CmpUnordF32, "v_cmpx_u_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 153, Op::V_CmpNgeF32, "v_cmpx_nge_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 154, Op::V_CmpNlgF32, "v_cmpx_nlg_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 155, Op::V_CmpNgtF32, "v_cmpx_ngt_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 156, Op::V_CmpNleF32, "v_cmpx_nle_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 157, Op::V_CmpNeqF32, "v_cmpx_neq_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 158, Op::V_CmpNltF32, "v_cmpx_nlt_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 161, Op::V_CmpLtF64, "v_cmpx_lt_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 162, Op::V_CmpEqF64, "v_cmpx_eq_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 163, Op::V_CmpLeF64, "v_cmpx_le_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 164, Op::V_CmpGtF64, "v_cmpx_gt_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 165, Op::V_CmpLgF64, "v_cmpx_lg_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 166, Op::V_CmpGeF64, "v_cmpx_ge_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 167, Op::V_CmpOrdF64, "v_cmpx_o_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 168, Op::V_CmpUnordF64, "v_cmpx_u_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 169, Op::V_CmpNgeF64, "v_cmpx_nge_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 170, Op::V_CmpNlgF64, "v_cmpx_nlg_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 171, Op::V_CmpNgtF64, "v_cmpx_ngt_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 172, Op::V_CmpNleF64, "v_cmpx_nle_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 173, Op::V_CmpNeqF64, "v_cmpx_neq_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 174, Op::V_CmpNltF64, "v_cmpx_nlt_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 177, Op::V_CmpLtI16, "v_cmpx_lt_i16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 178, Op::V_CmpEqI16, "v_cmpx_eq_i16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 179, Op::V_CmpLeI16, "v_cmpx_le_i16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 180, Op::V_CmpGtI16, "v_cmpx_gt_i16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 181, Op::V_CmpNeI16, "v_cmpx_ne_i16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 182, Op::V_CmpGeI16, "v_cmpx_ge_i16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 185, Op::V_CmpLtU16, "v_cmpx_lt_u16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 186, Op::V_CmpEqU16, "v_cmpx_eq_u16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 187, Op::V_CmpLeU16, "v_cmpx_le_u16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 188, Op::V_CmpGtU16, "v_cmpx_gt_u16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 189, Op::V_CmpNeU16, "v_cmpx_ne_u16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 190, Op::V_CmpGeU16, "v_cmpx_ge_u16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 193, Op::V_CmpLtI32, "v_cmpx_lt_i32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 194, Op::V_CmpEqI32, "v_cmpx_eq_i32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 195, Op::V_CmpLeI32, "v_cmpx_le_i32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 196, Op::V_CmpGtI32, "v_cmpx_gt_i32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 197, Op::V_CmpNeI32, "v_cmpx_ne_i32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 198, Op::V_CmpGeI32, "v_cmpx_ge_i32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 201, Op::V_CmpLtU32, "v_cmpx_lt_u32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 202, Op::V_CmpEqU32, "v_cmpx_eq_u32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 203, Op::V_CmpLeU32, "v_cmpx_le_u32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 204, Op::V_CmpGtU32, "v_cmpx_gt_u32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 205, Op::V_CmpNeU32, "v_cmpx_ne_u32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 206, Op::V_CmpGeU32, "v_cmpx_ge_u32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 209, Op::V_CmpLtI64, "v_cmpx_lt_i64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 210, Op::V_CmpEqI64, "v_cmpx_eq_i64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 211, Op::V_CmpLeI64, "v_cmpx_le_i64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 212, Op::V_CmpGtI64, "v_cmpx_gt_i64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 213, Op::V_CmpNeI64, "v_cmpx_ne_i64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 214, Op::V_CmpGeI64, "v_cmpx_ge_i64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 217, Op::V_CmpLtU64, "v_cmpx_lt_u64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 218, Op::V_CmpEqU64, "v_cmpx_eq_u64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 219, Op::V_CmpLeU64, "v_cmpx_le_u64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 220, Op::V_CmpGtU64, "v_cmpx_gt_u64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 221, Op::V_CmpNeU64, "v_cmpx_ne_u64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 222, Op::V_CmpGeU64, "v_cmpx_ge_u64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 125, Op::V_CmpClassF16, "v_cmp_class_f16",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 126, Op::V_CmpClassF32, "v_cmp_class_f32",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 127, Op::V_CmpClassF64, "v_cmp_class_f64",
         {OperandKind::Vop3Sdst, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 253, Op::V_CmpClassF16, "v_cmpx_class_f16",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 254, Op::V_CmpClassF32, "v_cmpx_class_f32",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop3, 255, Op::V_CmpClassF64, "v_cmpx_class_f64",
         {OperandKind::Vcc, OperandKind::Src, OperandKind::Src}, true),
    Make(Encoding::Vop1, 101, Op::V_SwapB32, "v_swap_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 102, Op::V_SwapB16, "v_swap_b16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 98, Op::V_SatPkU8I16, "v_sat_pk_u8_i16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 27, Op::V_Pipeflush, "v_pipeflush", {}),
    Make(Encoding::Vop1, 63, Op::V_FrexpExpI32F32, "v_frexp_exp_i32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 64, Op::V_FrexpMantF32, "v_frexp_mant_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 90, Op::V_FrexpExpI16F16, "v_frexp_exp_i16_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 89, Op::V_FrexpMantF16, "v_frexp_mant_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 398, Op::V_CvtOffF32I4, "v_cvt_off_f32_i4", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 427, Op::V_RcpIflagF32, "v_rcp_iflag_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 444, Op::V_FrexpExpI32F64, "v_frexp_exp_i32_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 445, Op::V_FrexpMantF64, "v_frexp_mant_f64", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 450, Op::V_MovreldB32, "v_movreld_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 451, Op::V_MovrelsB32, "v_movrels_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 452, Op::V_MovrelsdB32, "v_movrelsd_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 456, Op::V_Movrelsd2B32, "v_movrelsd_2_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 492, Op::V_CvtF32Fp8, "v_cvt_f32_fp8", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 493, Op::V_CvtF32Bf8, "v_cvt_f32_bf8", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 494, Op::V_CvtPkF32Fp8, "v_cvt_pk_f32_fp8", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 495, Op::V_CvtPkF32Bf8, "v_cvt_pk_f32_bf8", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 303, Op::V_CvtPkRtzF16F32, "v_cvt_pk_rtz_f16_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 304, Op::V_MinNumF16, "v_min_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 305, Op::V_MaxNumF16, "v_max_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 521, Op::V_FmaDx9ZeroF32, "v_fma_dx9_zero_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 524, Op::V_CubeidF32, "v_cubeid_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 525, Op::V_CubescF32, "v_cubesc_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 526, Op::V_CubetcF32, "v_cubetc_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 527, Op::V_CubemaF32, "v_cubema_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 533, Op::V_LerpU8, "v_lerp_u8", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 536, Op::V_MullitF32, "v_mullit_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 546, Op::V_SadU8, "v_sad_u8", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 547, Op::V_SadHiU8, "v_sad_hi_u8", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 548, Op::V_SadU16, "v_sad_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 550, Op::V_CvtPkU8F32, "v_cvt_pk_u8_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 551, Op::V_DivFixupF32, "v_div_fixup_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 552, Op::V_DivFixupF64, "v_div_fixup_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 555, Op::V_Min3NumF16, "v_min3_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 556, Op::V_Max3NumF16, "v_max3_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 557, Op::V_Minimum3F32, "v_minimum3_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 558, Op::V_Maximum3F32, "v_maximum3_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 559, Op::V_Minimum3F16, "v_minimum3_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 560, Op::V_Maximum3F16, "v_maximum3_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 561, Op::V_Med3NumF32, "v_med3_num_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 562, Op::V_Med3NumF16, "v_med3_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 567, Op::V_DivFmasF32, "v_div_fmas_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 568, Op::V_DivFmasF64, "v_div_fmas_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 569, Op::V_MsadU8, "v_msad_u8", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 570, Op::V_QsadPkU16U8, "v_qsad_pk_u16_u8", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 571, Op::V_MqsadPkU16U8, "v_mqsad_pk_u16_u8", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 573, Op::V_MqsadU32U8, "v_mqsad_u32_u8", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 577, Op::V_MadU16, "v_mad_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 580, Op::V_PermB32, "v_perm_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 586, Op::V_Min3I16, "v_min3_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 587, Op::V_Min3U16, "v_min3_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 589, Op::V_Max3I16, "v_max3_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 590, Op::V_Max3U16, "v_max3_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 592, Op::V_Med3I16, "v_med3_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 593, Op::V_Med3U16, "v_med3_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 595, Op::V_MadI16, "v_mad_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 596, Op::V_DivFixupF16, "v_div_fixup_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 601, Op::V_MadU32U16, "v_mad_u32_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 602, Op::V_MadI32I16, "v_mad_i32_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 603, Op::V_Permlane16B32, "v_permlane16_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 604, Op::V_Permlanex16B32, "v_permlanex16_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 605, Op::V_CndmaskB16, "v_cndmask_b16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 610, Op::V_MaxminU32, "v_maxmin_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 611, Op::V_MinmaxU32, "v_minmax_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 612, Op::V_MaxminI32, "v_maxmin_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 613, Op::V_MinmaxI32, "v_minmax_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 614, Op::V_Dot2F16F16, "v_dot2_f16_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 615, Op::V_Dot2Bf16Bf16, "v_dot2_bf16_bf16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 616, Op::V_MinmaxNumF32, "v_minmax_num_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 617, Op::V_MaxminNumF32, "v_maxmin_num_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 618, Op::V_MinmaxNumF16, "v_minmax_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 619, Op::V_MaxminNumF16, "v_maxmin_num_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 620, Op::V_MinimummaximumF32, "v_minimummaximum_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 621, Op::V_MaximumminimumF32, "v_maximumminimum_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 622, Op::V_MinimummaximumF16, "v_minimummaximum_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 623, Op::V_MaximumminimumF16, "v_maximumminimum_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 640, Op::V_SExpF32, "v_s_exp_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 641, Op::V_SExpF16, "v_s_exp_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 642, Op::V_SLogF32, "v_s_log_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 643, Op::V_SLogF16, "v_s_log_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 644, Op::V_SRcpF32, "v_s_rcp_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 645, Op::V_SRcpF16, "v_s_rcp_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 646, Op::V_SRsqF32, "v_s_rsq_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 647, Op::V_SRsqF16, "v_s_rsq_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 648, Op::V_SSqrtF32, "v_s_sqrt_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 649, Op::V_SSqrtF16, "v_s_sqrt_f16", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop3, 771, Op::V_AddNcU16, "v_add_nc_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 772, Op::V_SubNcU16, "v_sub_nc_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 773, Op::V_MulLoU16, "v_mul_lo_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 774, Op::V_CvtPkI16F32, "v_cvt_pk_i16_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 775, Op::V_CvtPkU16F32, "v_cvt_pk_u16_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 777, Op::V_MaxU16, "v_max_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 778, Op::V_MaxI16, "v_max_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 779, Op::V_MinU16, "v_min_u16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 780, Op::V_MinI16, "v_min_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 781, Op::V_AddNcI16, "v_add_nc_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 782, Op::V_SubNcI16, "v_sub_nc_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 783, Op::V_Permlane16VarB32, "v_permlane16_var_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 784, Op::V_Permlanex16VarB32, "v_permlanex16_var_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 785, Op::V_PackB32F16, "v_pack_b32_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 786, Op::V_CvtPkNormI16F16, "v_cvt_pk_norm_i16_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 787, Op::V_CvtPkNormU16F16, "v_cvt_pk_norm_u16_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 799, Op::V_MbcntLoU32B32, "v_mbcnt_lo_u32_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 800, Op::V_MbcntHiU32B32, "v_mbcnt_hi_u32_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 801, Op::V_CvtPkNormI16F32, "v_cvt_pk_norm_i16_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 802, Op::V_CvtPkNormU16F32, "v_cvt_pk_norm_u16_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 803, Op::V_CvtPkU16U32, "v_cvt_pk_u16_u32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 804, Op::V_CvtPkI16I32, "v_cvt_pk_i16_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 815, Op::V_TrigPreopF64, "v_trig_preop_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 824, Op::V_LshlrevB16, "v_lshlrev_b16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 825, Op::V_LshrrevB16, "v_lshrrev_b16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 826, Op::V_AshrrevI16, "v_ashrrev_i16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 829, Op::V_LshrrevB64, "v_lshrrev_b64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 830, Op::V_AshrrevI64, "v_ashrrev_i64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 833, Op::V_MinimumF64, "v_minimum_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 834, Op::V_MaximumF64, "v_maximum_f64", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 864, Op::V_ReadlaneB32, "v_readlane_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 865, Op::V_WritelaneB32, "v_writelane_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 869, Op::V_MinimumF32, "v_minimum_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 870, Op::V_MaximumF32, "v_maximum_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 871, Op::V_MinimumF16, "v_minimum_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 872, Op::V_MaximumF16, "v_maximum_f16", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 873, Op::V_CvtPkFp8F32, "v_cvt_pk_fp8_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 874, Op::V_CvtPkBf8F32, "v_cvt_pk_bf8_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 875, Op::V_CvtSrFp8F32, "v_cvt_sr_fp8_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 876, Op::V_CvtSrBf8F32, "v_cvt_sr_bf8_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop2, 47, Op::V_CvtPkRtzF16F32, "v_cvt_pk_rtz_f16_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 60, Op::V_PkFmacF16, "v_pk_fmac_f16",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
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
    case Op::S_AndNot1B64:
        return "s_and_not1_b64";
    case Op::S_OrNot1B64:
        return "s_or_not1_b64";
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
    case Op::DsAddF32:
        return "ds_add_f32";
    case Op::FlatLoadU8:
        return "flat_load_u8";
    case Op::BufferLoadFormatX:
        return "buffer_load_format_x";
    case Op::BufferLoadFormatXy:
        return "buffer_load_format_xy";
    case Op::BufferLoadFormatXyz:
        return "buffer_load_format_xyz";
    case Op::BufferLoadFormatXyzw:
        return "buffer_load_format_xyzw";
    case Op::BufferStoreFormatX:
        return "buffer_store_format_x";
    case Op::BufferStoreFormatXy:
        return "buffer_store_format_xy";
    case Op::BufferStoreFormatXyz:
        return "buffer_store_format_xyz";
    case Op::BufferStoreFormatXyzw:
        return "buffer_store_format_xyzw";
    case Op::BufferLoadD16FormatX:
        return "buffer_load_d16_format_x";
    case Op::BufferLoadD16FormatXy:
        return "buffer_load_d16_format_xy";
    case Op::BufferLoadD16FormatXyz:
        return "buffer_load_d16_format_xyz";
    case Op::BufferLoadD16FormatXyzw:
        return "buffer_load_d16_format_xyzw";
    case Op::BufferStoreD16FormatX:
        return "buffer_store_d16_format_x";
    case Op::BufferStoreD16FormatXy:
        return "buffer_store_d16_format_xy";
    case Op::BufferStoreD16FormatXyz:
        return "buffer_store_d16_format_xyz";
    case Op::BufferStoreD16FormatXyzw:
        return "buffer_store_d16_format_xyzw";
    case Op::BufferLoadU8:
        return "buffer_load_u8";
    case Op::BufferLoadI8:
        return "buffer_load_i8";
    case Op::BufferLoadU16:
        return "buffer_load_u16";
    case Op::BufferLoadI16:
        return "buffer_load_i16";
    case Op::BufferLoadB32:
        return "buffer_load_b32";
    case Op::BufferLoadB64:
        return "buffer_load_b64";
    case Op::BufferLoadB96:
        return "buffer_load_b96";
    case Op::BufferLoadB128:
        return "buffer_load_b128";
    case Op::BufferStoreB8:
        return "buffer_store_b8";
    case Op::BufferStoreB16:
        return "buffer_store_b16";
    case Op::BufferStoreB32:
        return "buffer_store_b32";
    case Op::BufferStoreB64:
        return "buffer_store_b64";
    case Op::BufferStoreB96:
        return "buffer_store_b96";
    case Op::BufferStoreB128:
        return "buffer_store_b128";
    case Op::BufferLoadD16U8:
        return "buffer_load_d16_u8";
    case Op::BufferLoadD16I8:
        return "buffer_load_d16_i8";
    case Op::BufferLoadD16B16:
        return "buffer_load_d16_b16";
    case Op::BufferLoadD16HiU8:
        return "buffer_load_d16_hi_u8";
    case Op::BufferLoadD16HiI8:
        return "buffer_load_d16_hi_i8";
    case Op::BufferLoadD16HiB16:
        return "buffer_load_d16_hi_b16";
    case Op::BufferStoreD16HiB8:
        return "buffer_store_d16_hi_b8";
    case Op::BufferStoreD16HiB16:
        return "buffer_store_d16_hi_b16";
    case Op::BufferLoadD16HiFormatX:
        return "buffer_load_d16_hi_format_x";
    case Op::BufferStoreD16HiFormatX:
        return "buffer_store_d16_hi_format_x";
    case Op::BufferAtomicSwapB32:
        return "buffer_atomic_swap_b32";
    case Op::BufferAtomicCmpswapB32:
        return "buffer_atomic_cmpswap_b32";
    case Op::BufferAtomicAddU32:
        return "buffer_atomic_add_u32";
    case Op::BufferAtomicSubU32:
        return "buffer_atomic_sub_u32";
    case Op::BufferAtomicSubClampU32:
        return "buffer_atomic_sub_clamp_u32";
    case Op::BufferAtomicMinI32:
        return "buffer_atomic_min_i32";
    case Op::BufferAtomicMinU32:
        return "buffer_atomic_min_u32";
    case Op::BufferAtomicMaxI32:
        return "buffer_atomic_max_i32";
    case Op::BufferAtomicMaxU32:
        return "buffer_atomic_max_u32";
    case Op::BufferAtomicAndB32:
        return "buffer_atomic_and_b32";
    case Op::BufferAtomicOrB32:
        return "buffer_atomic_or_b32";
    case Op::BufferAtomicXorB32:
        return "buffer_atomic_xor_b32";
    case Op::BufferAtomicIncU32:
        return "buffer_atomic_inc_u32";
    case Op::BufferAtomicDecU32:
        return "buffer_atomic_dec_u32";
    case Op::BufferAtomicSwapB64:
        return "buffer_atomic_swap_b64";
    case Op::BufferAtomicCmpswapB64:
        return "buffer_atomic_cmpswap_b64";
    case Op::BufferAtomicAddU64:
        return "buffer_atomic_add_u64";
    case Op::BufferAtomicSubU64:
        return "buffer_atomic_sub_u64";
    case Op::BufferAtomicMinI64:
        return "buffer_atomic_min_i64";
    case Op::BufferAtomicMinU64:
        return "buffer_atomic_min_u64";
    case Op::BufferAtomicMaxI64:
        return "buffer_atomic_max_i64";
    case Op::BufferAtomicMaxU64:
        return "buffer_atomic_max_u64";
    case Op::BufferAtomicAndB64:
        return "buffer_atomic_and_b64";
    case Op::BufferAtomicOrB64:
        return "buffer_atomic_or_b64";
    case Op::BufferAtomicXorB64:
        return "buffer_atomic_xor_b64";
    case Op::BufferAtomicIncU64:
        return "buffer_atomic_inc_u64";
    case Op::BufferAtomicDecU64:
        return "buffer_atomic_dec_u64";
    case Op::BufferAtomicCondSubU32:
        return "buffer_atomic_cond_sub_u32";
    case Op::BufferAtomicMinNumF32:
        return "buffer_atomic_min_num_f32";
    case Op::BufferAtomicMaxNumF32:
        return "buffer_atomic_max_num_f32";
    case Op::BufferAtomicAddF32:
        return "buffer_atomic_add_f32";
    case Op::BufferAtomicPkAddF16:
        return "buffer_atomic_pk_add_f16";
    case Op::BufferAtomicPkAddBf16:
        return "buffer_atomic_pk_add_bf16";
    case Op::TbufferLoadFormatX:
        return "tbuffer_load_format_x";
    case Op::TbufferLoadFormatXy:
        return "tbuffer_load_format_xy";
    case Op::TbufferLoadFormatXyz:
        return "tbuffer_load_format_xyz";
    case Op::TbufferLoadFormatXyzw:
        return "tbuffer_load_format_xyzw";
    case Op::TbufferStoreFormatX:
        return "tbuffer_store_format_x";
    case Op::TbufferStoreFormatXy:
        return "tbuffer_store_format_xy";
    case Op::TbufferStoreFormatXyz:
        return "tbuffer_store_format_xyz";
    case Op::TbufferStoreFormatXyzw:
        return "tbuffer_store_format_xyzw";
    case Op::TbufferLoadD16FormatX:
        return "tbuffer_load_d16_format_x";
    case Op::TbufferLoadD16FormatXy:
        return "tbuffer_load_d16_format_xy";
    case Op::TbufferLoadD16FormatXyz:
        return "tbuffer_load_d16_format_xyz";
    case Op::TbufferLoadD16FormatXyzw:
        return "tbuffer_load_d16_format_xyzw";
    case Op::TbufferStoreD16FormatX:
        return "tbuffer_store_d16_format_x";
    case Op::TbufferStoreD16FormatXy:
        return "tbuffer_store_d16_format_xy";
    case Op::TbufferStoreD16FormatXyz:
        return "tbuffer_store_d16_format_xyz";
    case Op::TbufferStoreD16FormatXyzw:
        return "tbuffer_store_d16_format_xyzw";
    case Op::FlatLoadI8:
        return "flat_load_i8";
    case Op::FlatLoadU16:
        return "flat_load_u16";
    case Op::FlatLoadI16:
        return "flat_load_i16";
    case Op::FlatLoadB32:
        return "flat_load_b32";
    case Op::FlatLoadB64:
        return "flat_load_b64";
    case Op::FlatLoadB96:
        return "flat_load_b96";
    case Op::FlatLoadB128:
        return "flat_load_b128";
    case Op::FlatStoreB8:
        return "flat_store_b8";
    case Op::FlatStoreB16:
        return "flat_store_b16";
    case Op::FlatStoreB32:
        return "flat_store_b32";
    case Op::FlatStoreB64:
        return "flat_store_b64";
    case Op::FlatStoreB96:
        return "flat_store_b96";
    case Op::FlatStoreB128:
        return "flat_store_b128";
    case Op::FlatLoadD16U8:
        return "flat_load_d16_u8";
    case Op::FlatLoadD16I8:
        return "flat_load_d16_i8";
    case Op::FlatLoadD16B16:
        return "flat_load_d16_b16";
    case Op::FlatLoadD16HiU8:
        return "flat_load_d16_hi_u8";
    case Op::FlatLoadD16HiI8:
        return "flat_load_d16_hi_i8";
    case Op::FlatLoadD16HiB16:
        return "flat_load_d16_hi_b16";
    case Op::FlatStoreD16HiB8:
        return "flat_store_d16_hi_b8";
    case Op::FlatStoreD16HiB16:
        return "flat_store_d16_hi_b16";
    case Op::FlatAtomicSwapB32:
        return "flat_atomic_swap_b32";
    case Op::FlatAtomicCmpswapB32:
        return "flat_atomic_cmpswap_b32";
    case Op::FlatAtomicAddU32:
        return "flat_atomic_add_u32";
    case Op::FlatAtomicSubU32:
        return "flat_atomic_sub_u32";
    case Op::FlatAtomicSubClampU32:
        return "flat_atomic_sub_clamp_u32";
    case Op::FlatAtomicMinI32:
        return "flat_atomic_min_i32";
    case Op::FlatAtomicMinU32:
        return "flat_atomic_min_u32";
    case Op::FlatAtomicMaxI32:
        return "flat_atomic_max_i32";
    case Op::FlatAtomicMaxU32:
        return "flat_atomic_max_u32";
    case Op::FlatAtomicAndB32:
        return "flat_atomic_and_b32";
    case Op::FlatAtomicOrB32:
        return "flat_atomic_or_b32";
    case Op::FlatAtomicXorB32:
        return "flat_atomic_xor_b32";
    case Op::FlatAtomicIncU32:
        return "flat_atomic_inc_u32";
    case Op::FlatAtomicDecU32:
        return "flat_atomic_dec_u32";
    case Op::FlatAtomicSwapB64:
        return "flat_atomic_swap_b64";
    case Op::FlatAtomicCmpswapB64:
        return "flat_atomic_cmpswap_b64";
    case Op::FlatAtomicAddU64:
        return "flat_atomic_add_u64";
    case Op::FlatAtomicSubU64:
        return "flat_atomic_sub_u64";
    case Op::FlatAtomicMinI64:
        return "flat_atomic_min_i64";
    case Op::FlatAtomicMinU64:
        return "flat_atomic_min_u64";
    case Op::FlatAtomicMaxI64:
        return "flat_atomic_max_i64";
    case Op::FlatAtomicMaxU64:
        return "flat_atomic_max_u64";
    case Op::FlatAtomicAndB64:
        return "flat_atomic_and_b64";
    case Op::FlatAtomicOrB64:
        return "flat_atomic_or_b64";
    case Op::FlatAtomicXorB64:
        return "flat_atomic_xor_b64";
    case Op::FlatAtomicIncU64:
        return "flat_atomic_inc_u64";
    case Op::FlatAtomicDecU64:
        return "flat_atomic_dec_u64";
    case Op::FlatAtomicCondSubU32:
        return "flat_atomic_cond_sub_u32";
    case Op::FlatAtomicMinNumF32:
        return "flat_atomic_min_num_f32";
    case Op::FlatAtomicMaxNumF32:
        return "flat_atomic_max_num_f32";
    case Op::FlatAtomicAddF32:
        return "flat_atomic_add_f32";
    case Op::FlatAtomicPkAddF16:
        return "flat_atomic_pk_add_f16";
    case Op::FlatAtomicPkAddBf16:
        return "flat_atomic_pk_add_bf16";
    case Op::GlobalLoadU8:
        return "global_load_u8";
    case Op::GlobalLoadI8:
        return "global_load_i8";
    case Op::GlobalLoadU16:
        return "global_load_u16";
    case Op::GlobalLoadI16:
        return "global_load_i16";
    case Op::GlobalLoadB32:
        return "global_load_b32";
    case Op::GlobalLoadB64:
        return "global_load_b64";
    case Op::GlobalLoadB96:
        return "global_load_b96";
    case Op::GlobalLoadB128:
        return "global_load_b128";
    case Op::GlobalStoreB8:
        return "global_store_b8";
    case Op::GlobalStoreB16:
        return "global_store_b16";
    case Op::GlobalStoreB32:
        return "global_store_b32";
    case Op::GlobalStoreB64:
        return "global_store_b64";
    case Op::GlobalStoreB96:
        return "global_store_b96";
    case Op::GlobalStoreB128:
        return "global_store_b128";
    case Op::GlobalLoadD16U8:
        return "global_load_d16_u8";
    case Op::GlobalLoadD16I8:
        return "global_load_d16_i8";
    case Op::GlobalLoadD16B16:
        return "global_load_d16_b16";
    case Op::GlobalLoadD16HiU8:
        return "global_load_d16_hi_u8";
    case Op::GlobalLoadD16HiI8:
        return "global_load_d16_hi_i8";
    case Op::GlobalLoadD16HiB16:
        return "global_load_d16_hi_b16";
    case Op::GlobalStoreD16HiB8:
        return "global_store_d16_hi_b8";
    case Op::GlobalStoreD16HiB16:
        return "global_store_d16_hi_b16";
    case Op::GlobalLoadAddtidB32:
        return "global_load_addtid_b32";
    case Op::GlobalStoreAddtidB32:
        return "global_store_addtid_b32";
    case Op::GlobalInv:
        return "global_inv";
    case Op::GlobalWb:
        return "global_wb";
    case Op::GlobalAtomicSwapB32:
        return "global_atomic_swap_b32";
    case Op::GlobalAtomicCmpswapB32:
        return "global_atomic_cmpswap_b32";
    case Op::GlobalAtomicAddU32:
        return "global_atomic_add_u32";
    case Op::GlobalAtomicSubU32:
        return "global_atomic_sub_u32";
    case Op::GlobalAtomicSubClampU32:
        return "global_atomic_sub_clamp_u32";
    case Op::GlobalAtomicMinI32:
        return "global_atomic_min_i32";
    case Op::GlobalAtomicMinU32:
        return "global_atomic_min_u32";
    case Op::GlobalAtomicMaxI32:
        return "global_atomic_max_i32";
    case Op::GlobalAtomicMaxU32:
        return "global_atomic_max_u32";
    case Op::GlobalAtomicAndB32:
        return "global_atomic_and_b32";
    case Op::GlobalAtomicOrB32:
        return "global_atomic_or_b32";
    case Op::GlobalAtomicXorB32:
        return "global_atomic_xor_b32";
    case Op::GlobalAtomicIncU32:
        return "global_atomic_inc_u32";
    case Op::GlobalAtomicDecU32:
        return "global_atomic_dec_u32";
    case Op::GlobalAtomicSwapB64:
        return "global_atomic_swap_b64";
    case Op::GlobalAtomicCmpswapB64:
        return "global_atomic_cmpswap_b64";
    case Op::GlobalAtomicAddU64:
        return "global_atomic_add_u64";
    case Op::GlobalAtomicSubU64:
        return "global_atomic_sub_u64";
    case Op::GlobalAtomicMinI64:
        return "global_atomic_min_i64";
    case Op::GlobalAtomicMinU64:
        return "global_atomic_min_u64";
    case Op::GlobalAtomicMaxI64:
        return "global_atomic_max_i64";
    case Op::GlobalAtomicMaxU64:
        return "global_atomic_max_u64";
    case Op::GlobalAtomicAndB64:
        return "global_atomic_and_b64";
    case Op::GlobalAtomicOrB64:
        return "global_atomic_or_b64";
    case Op::GlobalAtomicXorB64:
        return "global_atomic_xor_b64";
    case Op::GlobalAtomicIncU64:
        return "global_atomic_inc_u64";
    case Op::GlobalAtomicDecU64:
        return "global_atomic_dec_u64";
    case Op::GlobalWbinv:
        return "global_wbinv";
    case Op::GlobalAtomicCondSubU32:
        return "global_atomic_cond_sub_u32";
    case Op::GlobalAtomicMinNumF32:
        return "global_atomic_min_num_f32";
    case Op::GlobalAtomicMaxNumF32:
        return "global_atomic_max_num_f32";
    case Op::GlobalLoadBlock:
        return "global_load_block";
    case Op::GlobalStoreBlock:
        return "global_store_block";
    case Op::GlobalAtomicAddF32:
        return "global_atomic_add_f32";
    case Op::GlobalLoadTrB128:
        return "global_load_tr_b128";
    case Op::GlobalLoadTrB64:
        return "global_load_tr_b64";
    case Op::GlobalAtomicPkAddF16:
        return "global_atomic_pk_add_f16";
    case Op::GlobalAtomicPkAddBf16:
        return "global_atomic_pk_add_bf16";
    case Op::GlobalAtomicOrderedAddB64:
        return "global_atomic_ordered_add_b64";
    case Op::ScratchLoadU8:
        return "scratch_load_u8";
    case Op::ScratchLoadI8:
        return "scratch_load_i8";
    case Op::ScratchLoadU16:
        return "scratch_load_u16";
    case Op::ScratchLoadI16:
        return "scratch_load_i16";
    case Op::ScratchLoadB32:
        return "scratch_load_b32";
    case Op::ScratchLoadB64:
        return "scratch_load_b64";
    case Op::ScratchLoadB96:
        return "scratch_load_b96";
    case Op::ScratchLoadB128:
        return "scratch_load_b128";
    case Op::ScratchStoreB8:
        return "scratch_store_b8";
    case Op::ScratchStoreB16:
        return "scratch_store_b16";
    case Op::ScratchStoreB32:
        return "scratch_store_b32";
    case Op::ScratchStoreB64:
        return "scratch_store_b64";
    case Op::ScratchStoreB96:
        return "scratch_store_b96";
    case Op::ScratchStoreB128:
        return "scratch_store_b128";
    case Op::ScratchLoadD16U8:
        return "scratch_load_d16_u8";
    case Op::ScratchLoadD16I8:
        return "scratch_load_d16_i8";
    case Op::ScratchLoadD16B16:
        return "scratch_load_d16_b16";
    case Op::ScratchLoadD16HiU8:
        return "scratch_load_d16_hi_u8";
    case Op::ScratchLoadD16HiI8:
        return "scratch_load_d16_hi_i8";
    case Op::ScratchLoadD16HiB16:
        return "scratch_load_d16_hi_b16";
    case Op::ScratchStoreD16HiB8:
        return "scratch_store_d16_hi_b8";
    case Op::ScratchStoreD16HiB16:
        return "scratch_store_d16_hi_b16";
    case Op::ScratchLoadBlock:
        return "scratch_load_block";
    case Op::ScratchStoreBlock:
        return "scratch_store_block";
    case Op::DsAddRtnF32:
        return "ds_add_rtn_f32";
    case Op::DsAddRtnU32:
        return "ds_add_rtn_u32";
    case Op::DsAddRtnU64:
        return "ds_add_rtn_u64";
    case Op::DsAddU32:
        return "ds_add_u32";
    case Op::DsAddU64:
        return "ds_add_u64";
    case Op::DsAndB32:
        return "ds_and_b32";
    case Op::DsAndB64:
        return "ds_and_b64";
    case Op::DsAndRtnB32:
        return "ds_and_rtn_b32";
    case Op::DsAndRtnB64:
        return "ds_and_rtn_b64";
    case Op::DsAppend:
        return "ds_append";
    case Op::DsBpermuteB32:
        return "ds_bpermute_b32";
    case Op::DsBpermuteFiB32:
        return "ds_bpermute_fi_b32";
    case Op::DsBvhStackPush4Pop1RtnB32:
        return "ds_bvh_stack_push4_pop1_rtn_b32";
    case Op::DsBvhStackPush8Pop1RtnB32:
        return "ds_bvh_stack_push8_pop1_rtn_b32";
    case Op::DsBvhStackPush8Pop2RtnB64:
        return "ds_bvh_stack_push8_pop2_rtn_b64";
    case Op::DsCmpstF32:
        return "ds_cmpst_f32";
    case Op::DsCmpstF64:
        return "ds_cmpst_f64";
    case Op::DsCmpstRtnF32:
        return "ds_cmpst_rtn_f32";
    case Op::DsCmpstRtnF64:
        return "ds_cmpst_rtn_f64";
    case Op::DsCmpstoreB32:
        return "ds_cmpstore_b32";
    case Op::DsCmpstoreB64:
        return "ds_cmpstore_b64";
    case Op::DsCmpstoreRtnB32:
        return "ds_cmpstore_rtn_b32";
    case Op::DsCmpstoreRtnB64:
        return "ds_cmpstore_rtn_b64";
    case Op::DsCondSubRtnU32:
        return "ds_cond_sub_rtn_u32";
    case Op::DsCondSubU32:
        return "ds_cond_sub_u32";
    case Op::DsCondxchg32RtnB64:
        return "ds_condxchg32_rtn_b64";
    case Op::DsConsume:
        return "ds_consume";
    case Op::DsDecRtnU32:
        return "ds_dec_rtn_u32";
    case Op::DsDecRtnU64:
        return "ds_dec_rtn_u64";
    case Op::DsDecU32:
        return "ds_dec_u32";
    case Op::DsDecU64:
        return "ds_dec_u64";
    case Op::DsGwsBarrier:
        return "ds_gws_barrier";
    case Op::DsGwsInit:
        return "ds_gws_init";
    case Op::DsGwsSemaBr:
        return "ds_gws_sema_br";
    case Op::DsGwsSemaP:
        return "ds_gws_sema_p";
    case Op::DsGwsSemaReleaseAll:
        return "ds_gws_sema_release_all";
    case Op::DsGwsSemaV:
        return "ds_gws_sema_v";
    case Op::DsIncRtnU32:
        return "ds_inc_rtn_u32";
    case Op::DsIncRtnU64:
        return "ds_inc_rtn_u64";
    case Op::DsIncU32:
        return "ds_inc_u32";
    case Op::DsIncU64:
        return "ds_inc_u64";
    case Op::DsLoad2addrB32:
        return "ds_load_2addr_b32";
    case Op::DsLoad2addrB64:
        return "ds_load_2addr_b64";
    case Op::DsLoad2addrStride64B32:
        return "ds_load_2addr_stride64_b32";
    case Op::DsLoad2addrStride64B64:
        return "ds_load_2addr_stride64_b64";
    case Op::DsLoadAddtidB32:
        return "ds_load_addtid_b32";
    case Op::DsLoadB128:
        return "ds_load_b128";
    case Op::DsLoadB32:
        return "ds_load_b32";
    case Op::DsLoadB64:
        return "ds_load_b64";
    case Op::DsLoadB96:
        return "ds_load_b96";
    case Op::DsLoadI16:
        return "ds_load_i16";
    case Op::DsLoadI8:
        return "ds_load_i8";
    case Op::DsLoadI8D16:
        return "ds_load_i8_d16";
    case Op::DsLoadI8D16Hi:
        return "ds_load_i8_d16_hi";
    case Op::DsLoadU16:
        return "ds_load_u16";
    case Op::DsLoadU16D16:
        return "ds_load_u16_d16";
    case Op::DsLoadU16D16Hi:
        return "ds_load_u16_d16_hi";
    case Op::DsLoadU8:
        return "ds_load_u8";
    case Op::DsLoadU8D16:
        return "ds_load_u8_d16";
    case Op::DsLoadU8D16Hi:
        return "ds_load_u8_d16_hi";
    case Op::DsMaxI32:
        return "ds_max_i32";
    case Op::DsMaxI64:
        return "ds_max_i64";
    case Op::DsMaxNumF32:
        return "ds_max_num_f32";
    case Op::DsMaxNumF64:
        return "ds_max_num_f64";
    case Op::DsMaxNumRtnF32:
        return "ds_max_num_rtn_f32";
    case Op::DsMaxNumRtnF64:
        return "ds_max_num_rtn_f64";
    case Op::DsMaxRtnI32:
        return "ds_max_rtn_i32";
    case Op::DsMaxRtnI64:
        return "ds_max_rtn_i64";
    case Op::DsMaxRtnU32:
        return "ds_max_rtn_u32";
    case Op::DsMaxRtnU64:
        return "ds_max_rtn_u64";
    case Op::DsMaxU32:
        return "ds_max_u32";
    case Op::DsMaxU64:
        return "ds_max_u64";
    case Op::DsMinI32:
        return "ds_min_i32";
    case Op::DsMinI64:
        return "ds_min_i64";
    case Op::DsMinNumF32:
        return "ds_min_num_f32";
    case Op::DsMinNumF64:
        return "ds_min_num_f64";
    case Op::DsMinNumRtnF32:
        return "ds_min_num_rtn_f32";
    case Op::DsMinNumRtnF64:
        return "ds_min_num_rtn_f64";
    case Op::DsMinRtnI32:
        return "ds_min_rtn_i32";
    case Op::DsMinRtnI64:
        return "ds_min_rtn_i64";
    case Op::DsMinRtnU32:
        return "ds_min_rtn_u32";
    case Op::DsMinRtnU64:
        return "ds_min_rtn_u64";
    case Op::DsMinU32:
        return "ds_min_u32";
    case Op::DsMinU64:
        return "ds_min_u64";
    case Op::DsMskorB32:
        return "ds_mskor_b32";
    case Op::DsMskorB64:
        return "ds_mskor_b64";
    case Op::DsMskorRtnB32:
        return "ds_mskor_rtn_b32";
    case Op::DsMskorRtnB64:
        return "ds_mskor_rtn_b64";
    case Op::DsNop:
        return "ds_nop";
    case Op::DsOrB32:
        return "ds_or_b32";
    case Op::DsOrB64:
        return "ds_or_b64";
    case Op::DsOrRtnB32:
        return "ds_or_rtn_b32";
    case Op::DsOrRtnB64:
        return "ds_or_rtn_b64";
    case Op::DsOrderedCount:
        return "ds_ordered_count";
    case Op::DsPermuteB32:
        return "ds_permute_b32";
    case Op::DsPkAddBf16:
        return "ds_pk_add_bf16";
    case Op::DsPkAddF16:
        return "ds_pk_add_f16";
    case Op::DsPkAddRtnBf16:
        return "ds_pk_add_rtn_bf16";
    case Op::DsPkAddRtnF16:
        return "ds_pk_add_rtn_f16";
    case Op::DsRsubRtnU32:
        return "ds_rsub_rtn_u32";
    case Op::DsRsubRtnU64:
        return "ds_rsub_rtn_u64";
    case Op::DsRsubU32:
        return "ds_rsub_u32";
    case Op::DsRsubU64:
        return "ds_rsub_u64";
    case Op::DsStore2addrB32:
        return "ds_store_2addr_b32";
    case Op::DsStore2addrB64:
        return "ds_store_2addr_b64";
    case Op::DsStore2addrStride64B32:
        return "ds_store_2addr_stride64_b32";
    case Op::DsStore2addrStride64B64:
        return "ds_store_2addr_stride64_b64";
    case Op::DsStoreAddtidB32:
        return "ds_store_addtid_b32";
    case Op::DsStoreB128:
        return "ds_store_b128";
    case Op::DsStoreB16:
        return "ds_store_b16";
    case Op::DsStoreB16D16Hi:
        return "ds_store_b16_d16_hi";
    case Op::DsStoreB32:
        return "ds_store_b32";
    case Op::DsStoreB64:
        return "ds_store_b64";
    case Op::DsStoreB8:
        return "ds_store_b8";
    case Op::DsStoreB8D16Hi:
        return "ds_store_b8_d16_hi";
    case Op::DsStoreB96:
        return "ds_store_b96";
    case Op::DsStorexchg2addrRtnB32:
        return "ds_storexchg_2addr_rtn_b32";
    case Op::DsStorexchg2addrRtnB64:
        return "ds_storexchg_2addr_rtn_b64";
    case Op::DsStorexchg2addrStride64RtnB32:
        return "ds_storexchg_2addr_stride64_rtn_b32";
    case Op::DsStorexchg2addrStride64RtnB64:
        return "ds_storexchg_2addr_stride64_rtn_b64";
    case Op::DsStorexchgRtnB32:
        return "ds_storexchg_rtn_b32";
    case Op::DsStorexchgRtnB64:
        return "ds_storexchg_rtn_b64";
    case Op::DsSubClampRtnU32:
        return "ds_sub_clamp_rtn_u32";
    case Op::DsSubClampU32:
        return "ds_sub_clamp_u32";
    case Op::DsSubRtnU32:
        return "ds_sub_rtn_u32";
    case Op::DsSubRtnU64:
        return "ds_sub_rtn_u64";
    case Op::DsSubU32:
        return "ds_sub_u32";
    case Op::DsSubU64:
        return "ds_sub_u64";
    case Op::DsSwizzleB32:
        return "ds_swizzle_b32";
    case Op::DsWrapRtnB32:
        return "ds_wrap_rtn_b32";
    case Op::DsXorB32:
        return "ds_xor_b32";
    case Op::DsXorB64:
        return "ds_xor_b64";
    case Op::DsXorRtnB32:
        return "ds_xor_rtn_b32";
    case Op::DsXorRtnB64:
        return "ds_xor_rtn_b64";
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
    case Op::S_Bitcmp0B32:
        return "s_bitcmp0_b32";
    case Op::S_Bitcmp1B32:
        return "s_bitcmp1_b32";
    case Op::S_Bitcmp0B64:
        return "s_bitcmp0_b64";
    case Op::S_Bitcmp1B64:
        return "s_bitcmp1_b64";
    case Op::S_CmpEqU64:
        return "s_cmp_eq_u64";
    case Op::S_CmpLgU64:
        return "s_cmp_lg_u64";
    case Op::S_CmpLtF32:
        return "s_cmp_lt_f32";
    case Op::S_CmpEqF32:
        return "s_cmp_eq_f32";
    case Op::S_CmpLeF32:
        return "s_cmp_le_f32";
    case Op::S_CmpGtF32:
        return "s_cmp_gt_f32";
    case Op::S_CmpLgF32:
        return "s_cmp_lg_f32";
    case Op::S_CmpGeF32:
        return "s_cmp_ge_f32";
    case Op::S_CmpOF32:
        return "s_cmp_o_f32";
    case Op::S_CmpUF32:
        return "s_cmp_u_f32";
    case Op::S_CmpNgeF32:
        return "s_cmp_nge_f32";
    case Op::S_CmpNlgF32:
        return "s_cmp_nlg_f32";
    case Op::S_CmpNgtF32:
        return "s_cmp_ngt_f32";
    case Op::S_CmpNleF32:
        return "s_cmp_nle_f32";
    case Op::S_CmpNeqF32:
        return "s_cmp_neq_f32";
    case Op::S_CmpNltF32:
        return "s_cmp_nlt_f32";
    case Op::S_CmpLtF16:
        return "s_cmp_lt_f16";
    case Op::S_CmpEqF16:
        return "s_cmp_eq_f16";
    case Op::S_CmpLeF16:
        return "s_cmp_le_f16";
    case Op::S_CmpGtF16:
        return "s_cmp_gt_f16";
    case Op::S_CmpLgF16:
        return "s_cmp_lg_f16";
    case Op::S_CmpGeF16:
        return "s_cmp_ge_f16";
    case Op::S_CmpOF16:
        return "s_cmp_o_f16";
    case Op::S_CmpUF16:
        return "s_cmp_u_f16";
    case Op::S_CmpNgeF16:
        return "s_cmp_nge_f16";
    case Op::S_CmpNlgF16:
        return "s_cmp_nlg_f16";
    case Op::S_CmpNgtF16:
        return "s_cmp_ngt_f16";
    case Op::S_CmpNleF16:
        return "s_cmp_nle_f16";
    case Op::S_CmpNeqF16:
        return "s_cmp_neq_f16";
    case Op::S_CmpNltF16:
        return "s_cmp_nlt_f16";
    case Op::S_Lshl1AddU32:
        return "s_lshl1_add_u32";
    case Op::S_Lshl2AddU32:
        return "s_lshl2_add_u32";
    case Op::S_Lshl3AddU32:
        return "s_lshl3_add_u32";
    case Op::S_Lshl4AddU32:
        return "s_lshl4_add_u32";
    case Op::S_PackLlB32B16:
        return "s_pack_ll_b32_b16";
    case Op::S_PackLhB32B16:
        return "s_pack_lh_b32_b16";
    case Op::S_PackHhB32B16:
        return "s_pack_hh_b32_b16";
    case Op::S_PackHlB32B16:
        return "s_pack_hl_b32_b16";
    case Op::S_NandB64:
        return "s_nand_b64";
    case Op::S_NorB64:
        return "s_nor_b64";
    case Op::S_XnorB64:
        return "s_xnor_b64";
    case Op::S_AddF32:
        return "s_add_f32";
    case Op::S_SubF32:
        return "s_sub_f32";
    case Op::S_MulF32:
        return "s_mul_f32";
    case Op::S_MinNumF32:
        return "s_min_num_f32";
    case Op::S_MaxNumF32:
        return "s_max_num_f32";
    case Op::S_MinimumF32:
        return "s_minimum_f32";
    case Op::S_MaximumF32:
        return "s_maximum_f32";
    case Op::S_FmacF32:
        return "s_fmac_f32";
    case Op::S_CvtPkRtzF16F32:
        return "s_cvt_pk_rtz_f16_f32";
    case Op::S_AddF16:
        return "s_add_f16";
    case Op::S_SubF16:
        return "s_sub_f16";
    case Op::S_MulF16:
        return "s_mul_f16";
    case Op::S_MinNumF16:
        return "s_min_num_f16";
    case Op::S_MaxNumF16:
        return "s_max_num_f16";
    case Op::S_MinimumF16:
        return "s_minimum_f16";
    case Op::S_MaximumF16:
        return "s_maximum_f16";
    case Op::S_FmacF16:
        return "s_fmac_f16";
    case Op::S_AddNcU64:
        return "s_add_nc_u64";
    case Op::S_SubNcU64:
        return "s_sub_nc_u64";
    case Op::S_MulU64:
        return "s_mul_u64";
    case Op::S_CmovB32:
        return "s_cmov_b32";
    case Op::S_CmovB64:
        return "s_cmov_b64";
    case Op::S_BitreplicateB64B32:
        return "s_bitreplicate_b64_b32";
    case Op::S_QuadmaskB32:
        return "s_quadmask_b32";
    case Op::S_QuadmaskB64:
        return "s_quadmask_b64";
    case Op::S_CeilF32:
        return "s_ceil_f32";
    case Op::S_FloorF32:
        return "s_floor_f32";
    case Op::S_TruncF32:
        return "s_trunc_f32";
    case Op::S_RndneF32:
        return "s_rndne_f32";
    case Op::S_CvtF32I32:
        return "s_cvt_f32_i32";
    case Op::S_CvtF32U32:
        return "s_cvt_f32_u32";
    case Op::S_CvtI32F32:
        return "s_cvt_i32_f32";
    case Op::S_CvtU32F32:
        return "s_cvt_u32_f32";
    case Op::S_CvtF16F32:
        return "s_cvt_f16_f32";
    case Op::S_CvtF32F16:
        return "s_cvt_f32_f16";
    case Op::S_CvtHiF32F16:
        return "s_cvt_hi_f32_f16";
    case Op::S_CeilF16:
        return "s_ceil_f16";
    case Op::S_FloorF16:
        return "s_floor_f16";
    case Op::S_TruncF16:
        return "s_trunc_f16";
    case Op::S_RndneF16:
        return "s_rndne_f16";
    case Op::S_WqmB32:
        return "s_wqm_b32";
    case Op::S_WqmB64:
        return "s_wqm_b64";
    case Op::S_MovrelsB32:
        return "s_movrels_b32";
    case Op::S_MovrelsB64:
        return "s_movrels_b64";
    case Op::S_MovreldB32:
        return "s_movreld_b32";
    case Op::S_MovreldB64:
        return "s_movreld_b64";
    case Op::S_Movrelsd2B32:
        return "s_movrelsd_2_b32";
    case Op::S_GetpcB64:
        return "s_getpc_b64";
    case Op::S_SetpcB64:
        return "s_setpc_b64";
    case Op::S_SwappcB64:
        return "s_swappc_b64";
    case Op::S_RfeB64:
        return "s_rfe_b64";
    case Op::S_AndSaveexecB32:
        return "s_and_saveexec_b32";
    case Op::S_AndSaveexecB64:
        return "s_and_saveexec_b64";
    case Op::S_OrSaveexecB32:
        return "s_or_saveexec_b32";
    case Op::S_OrSaveexecB64:
        return "s_or_saveexec_b64";
    case Op::S_XorSaveexecB32:
        return "s_xor_saveexec_b32";
    case Op::S_XorSaveexecB64:
        return "s_xor_saveexec_b64";
    case Op::S_NandSaveexecB32:
        return "s_nand_saveexec_b32";
    case Op::S_NandSaveexecB64:
        return "s_nand_saveexec_b64";
    case Op::S_NorSaveexecB32:
        return "s_nor_saveexec_b32";
    case Op::S_NorSaveexecB64:
        return "s_nor_saveexec_b64";
    case Op::S_XnorSaveexecB32:
        return "s_xnor_saveexec_b32";
    case Op::S_XnorSaveexecB64:
        return "s_xnor_saveexec_b64";
    case Op::S_AndNot0SaveexecB32:
        return "s_and_not0_saveexec_b32";
    case Op::S_AndNot0SaveexecB64:
        return "s_and_not0_saveexec_b64";
    case Op::S_OrNot0SaveexecB32:
        return "s_or_not0_saveexec_b32";
    case Op::S_OrNot0SaveexecB64:
        return "s_or_not0_saveexec_b64";
    case Op::S_AndNot1SaveexecB32:
        return "s_and_not1_saveexec_b32";
    case Op::S_AndNot1SaveexecB64:
        return "s_and_not1_saveexec_b64";
    case Op::S_OrNot1SaveexecB32:
        return "s_or_not1_saveexec_b32";
    case Op::S_OrNot1SaveexecB64:
        return "s_or_not1_saveexec_b64";
    case Op::S_AndNot0WrexecB32:
        return "s_and_not0_wrexec_b32";
    case Op::S_AndNot0WrexecB64:
        return "s_and_not0_wrexec_b64";
    case Op::S_AndNot1WrexecB32:
        return "s_and_not1_wrexec_b32";
    case Op::S_AndNot1WrexecB64:
        return "s_and_not1_wrexec_b64";
    case Op::S_SendmsgRtnB32:
        return "s_sendmsg_rtn_b32";
    case Op::S_SendmsgRtnB64:
        return "s_sendmsg_rtn_b64";
    case Op::S_BarrierSignal:
        return "s_barrier_signal";
    case Op::S_BarrierSignalIsfirst:
        return "s_barrier_signal_isfirst";
    case Op::S_AllocVgpr:
        return "s_alloc_vgpr";
    case Op::S_SleepVar:
        return "s_sleep_var";
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
    case Op::S_Nop:
        return "s_nop";
    case Op::S_Setkill:
        return "s_setkill";
    case Op::S_Sethalt:
        return "s_sethalt";
    case Op::S_Sleep:
        return "s_sleep";
    case Op::S_Clause:
        return "s_clause";
    case Op::S_DelayAlu:
        return "s_delay_alu";
    case Op::S_WaitAlu:
        return "s_wait_alu";
    case Op::S_Waitcnt:
        return "s_waitcnt";
    case Op::S_WaitIdle:
        return "s_wait_idle";
    case Op::S_WaitEvent:
        return "s_wait_event";
    case Op::S_Trap:
        return "s_trap";
    case Op::S_RoundMode:
        return "s_round_mode";
    case Op::S_DenormMode:
        return "s_denorm_mode";
    case Op::S_CodeEnd:
        return "s_code_end";
    case Op::S_EndpgmSaved:
        return "s_endpgm_saved";
    case Op::S_Wakeup:
        return "s_wakeup";
    case Op::S_Setprio:
        return "s_setprio";
    case Op::S_Sendmsg:
        return "s_sendmsg";
    case Op::S_Sendmsghalt:
        return "s_sendmsghalt";
    case Op::S_Incperflevel:
        return "s_incperflevel";
    case Op::S_Decperflevel:
        return "s_decperflevel";
    case Op::S_Ttracedata:
        return "s_ttracedata";
    case Op::S_TtracedataImm:
        return "s_ttracedata_imm";
    case Op::S_IcacheInv:
        return "s_icache_inv";
    case Op::S_BarrierWait:
        return "s_barrier_wait";
    case Op::S_WaitLoadcnt:
        return "s_wait_loadcnt";
    case Op::S_WaitStorecnt:
        return "s_wait_storecnt";
    case Op::S_WaitSamplecnt:
        return "s_wait_samplecnt";
    case Op::S_WaitBvhcnt:
        return "s_wait_bvhcnt";
    case Op::S_WaitExpcnt:
        return "s_wait_expcnt";
    case Op::S_WaitDscnt:
        return "s_wait_dscnt";
    case Op::S_WaitKmcnt:
        return "s_wait_kmcnt";
    case Op::S_WaitLoadcntDscnt:
        return "s_wait_loadcnt_dscnt";
    case Op::S_WaitStorecntDscnt:
        return "s_wait_storecnt_dscnt";
    case Op::S_Version:
        return "s_version";
    case Op::S_CmovkI32:
        return "s_cmovk_i32";
    case Op::S_AddkCoI32:
        return "s_addk_co_i32";
    case Op::S_GetregB32:
        return "s_getreg_b32";
    case Op::S_SetregB32:
        return "s_setreg_b32";
    case Op::S_CallB64:
        return "s_call_b64";
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
    case Op::V_CmpClassF16:
        return "v_cmp_class_f16";
    case Op::V_CvtOffF32I4:
        return "v_cvt_off_f32_i4";
    case Op::V_RcpIflagF32:
        return "v_rcp_iflag_f32";
    case Op::V_FrexpExpI32F64:
        return "v_frexp_exp_i32_f64";
    case Op::V_FrexpMantF64:
        return "v_frexp_mant_f64";
    case Op::V_MovreldB32:
        return "v_movreld_b32";
    case Op::V_MovrelsB32:
        return "v_movrels_b32";
    case Op::V_MovrelsdB32:
        return "v_movrelsd_b32";
    case Op::V_Movrelsd2B32:
        return "v_movrelsd_2_b32";
    case Op::V_CvtF32Fp8:
        return "v_cvt_f32_fp8";
    case Op::V_CvtF32Bf8:
        return "v_cvt_f32_bf8";
    case Op::V_CvtPkF32Fp8:
        return "v_cvt_pk_f32_fp8";
    case Op::V_CvtPkF32Bf8:
        return "v_cvt_pk_f32_bf8";
    case Op::V_CvtPkRtzF16F32:
        return "v_cvt_pk_rtz_f16_f32";
    case Op::V_PkFmacF16:
        return "v_pk_fmac_f16";
    case Op::V_MinNumF16:
        return "v_min_num_f16";
    case Op::V_MaxNumF16:
        return "v_max_num_f16";
    case Op::V_FmaDx9ZeroF32:
        return "v_fma_dx9_zero_f32";
    case Op::V_CubeidF32:
        return "v_cubeid_f32";
    case Op::V_CubescF32:
        return "v_cubesc_f32";
    case Op::V_CubetcF32:
        return "v_cubetc_f32";
    case Op::V_CubemaF32:
        return "v_cubema_f32";
    case Op::V_LerpU8:
        return "v_lerp_u8";
    case Op::V_MullitF32:
        return "v_mullit_f32";
    case Op::V_SadU8:
        return "v_sad_u8";
    case Op::V_SadHiU8:
        return "v_sad_hi_u8";
    case Op::V_SadU16:
        return "v_sad_u16";
    case Op::V_CvtPkU8F32:
        return "v_cvt_pk_u8_f32";
    case Op::V_DivFixupF32:
        return "v_div_fixup_f32";
    case Op::V_DivFixupF64:
        return "v_div_fixup_f64";
    case Op::V_Min3NumF16:
        return "v_min3_num_f16";
    case Op::V_Max3NumF16:
        return "v_max3_num_f16";
    case Op::V_Minimum3F32:
        return "v_minimum3_f32";
    case Op::V_Maximum3F32:
        return "v_maximum3_f32";
    case Op::V_Minimum3F16:
        return "v_minimum3_f16";
    case Op::V_Maximum3F16:
        return "v_maximum3_f16";
    case Op::V_Med3NumF32:
        return "v_med3_num_f32";
    case Op::V_Med3NumF16:
        return "v_med3_num_f16";
    case Op::V_DivFmasF32:
        return "v_div_fmas_f32";
    case Op::V_DivFmasF64:
        return "v_div_fmas_f64";
    case Op::V_MsadU8:
        return "v_msad_u8";
    case Op::V_QsadPkU16U8:
        return "v_qsad_pk_u16_u8";
    case Op::V_MqsadPkU16U8:
        return "v_mqsad_pk_u16_u8";
    case Op::V_MqsadU32U8:
        return "v_mqsad_u32_u8";
    case Op::V_MadU16:
        return "v_mad_u16";
    case Op::V_PermB32:
        return "v_perm_b32";
    case Op::V_Min3I16:
        return "v_min3_i16";
    case Op::V_Min3U16:
        return "v_min3_u16";
    case Op::V_Max3I16:
        return "v_max3_i16";
    case Op::V_Max3U16:
        return "v_max3_u16";
    case Op::V_Med3I16:
        return "v_med3_i16";
    case Op::V_Med3U16:
        return "v_med3_u16";
    case Op::V_MadI16:
        return "v_mad_i16";
    case Op::V_DivFixupF16:
        return "v_div_fixup_f16";
    case Op::V_MadU32U16:
        return "v_mad_u32_u16";
    case Op::V_MadI32I16:
        return "v_mad_i32_i16";
    case Op::V_Permlane16B32:
        return "v_permlane16_b32";
    case Op::V_Permlanex16B32:
        return "v_permlanex16_b32";
    case Op::V_CndmaskB16:
        return "v_cndmask_b16";
    case Op::V_MaxminU32:
        return "v_maxmin_u32";
    case Op::V_MinmaxU32:
        return "v_minmax_u32";
    case Op::V_MaxminI32:
        return "v_maxmin_i32";
    case Op::V_MinmaxI32:
        return "v_minmax_i32";
    case Op::V_Dot2F16F16:
        return "v_dot2_f16_f16";
    case Op::V_Dot2Bf16Bf16:
        return "v_dot2_bf16_bf16";
    case Op::V_MinmaxNumF32:
        return "v_minmax_num_f32";
    case Op::V_MaxminNumF32:
        return "v_maxmin_num_f32";
    case Op::V_MinmaxNumF16:
        return "v_minmax_num_f16";
    case Op::V_MaxminNumF16:
        return "v_maxmin_num_f16";
    case Op::V_MinimummaximumF32:
        return "v_minimummaximum_f32";
    case Op::V_MaximumminimumF32:
        return "v_maximumminimum_f32";
    case Op::V_MinimummaximumF16:
        return "v_minimummaximum_f16";
    case Op::V_MaximumminimumF16:
        return "v_maximumminimum_f16";
    case Op::V_SExpF32:
        return "v_s_exp_f32";
    case Op::V_SExpF16:
        return "v_s_exp_f16";
    case Op::V_SLogF32:
        return "v_s_log_f32";
    case Op::V_SLogF16:
        return "v_s_log_f16";
    case Op::V_SRcpF32:
        return "v_s_rcp_f32";
    case Op::V_SRcpF16:
        return "v_s_rcp_f16";
    case Op::V_SRsqF32:
        return "v_s_rsq_f32";
    case Op::V_SRsqF16:
        return "v_s_rsq_f16";
    case Op::V_SSqrtF32:
        return "v_s_sqrt_f32";
    case Op::V_SSqrtF16:
        return "v_s_sqrt_f16";
    case Op::V_AddNcU16:
        return "v_add_nc_u16";
    case Op::V_SubNcU16:
        return "v_sub_nc_u16";
    case Op::V_MulLoU16:
        return "v_mul_lo_u16";
    case Op::V_CvtPkI16F32:
        return "v_cvt_pk_i16_f32";
    case Op::V_CvtPkU16F32:
        return "v_cvt_pk_u16_f32";
    case Op::V_MaxU16:
        return "v_max_u16";
    case Op::V_MaxI16:
        return "v_max_i16";
    case Op::V_MinU16:
        return "v_min_u16";
    case Op::V_MinI16:
        return "v_min_i16";
    case Op::V_AddNcI16:
        return "v_add_nc_i16";
    case Op::V_SubNcI16:
        return "v_sub_nc_i16";
    case Op::V_Permlane16VarB32:
        return "v_permlane16_var_b32";
    case Op::V_Permlanex16VarB32:
        return "v_permlanex16_var_b32";
    case Op::V_PackB32F16:
        return "v_pack_b32_f16";
    case Op::V_CvtPkNormI16F16:
        return "v_cvt_pk_norm_i16_f16";
    case Op::V_CvtPkNormU16F16:
        return "v_cvt_pk_norm_u16_f16";
    case Op::V_MbcntLoU32B32:
        return "v_mbcnt_lo_u32_b32";
    case Op::V_MbcntHiU32B32:
        return "v_mbcnt_hi_u32_b32";
    case Op::V_CvtPkNormI16F32:
        return "v_cvt_pk_norm_i16_f32";
    case Op::V_CvtPkNormU16F32:
        return "v_cvt_pk_norm_u16_f32";
    case Op::V_CvtPkU16U32:
        return "v_cvt_pk_u16_u32";
    case Op::V_CvtPkI16I32:
        return "v_cvt_pk_i16_i32";
    case Op::V_TrigPreopF64:
        return "v_trig_preop_f64";
    case Op::V_LshlrevB16:
        return "v_lshlrev_b16";
    case Op::V_LshrrevB16:
        return "v_lshrrev_b16";
    case Op::V_AshrrevI16:
        return "v_ashrrev_i16";
    case Op::V_LshrrevB64:
        return "v_lshrrev_b64";
    case Op::V_AshrrevI64:
        return "v_ashrrev_i64";
    case Op::V_MinimumF64:
        return "v_minimum_f64";
    case Op::V_MaximumF64:
        return "v_maximum_f64";
    case Op::V_ReadlaneB32:
        return "v_readlane_b32";
    case Op::V_WritelaneB32:
        return "v_writelane_b32";
    case Op::V_MinimumF32:
        return "v_minimum_f32";
    case Op::V_MaximumF32:
        return "v_maximum_f32";
    case Op::V_MinimumF16:
        return "v_minimum_f16";
    case Op::V_MaximumF16:
        return "v_maximum_f16";
    case Op::V_CvtPkFp8F32:
        return "v_cvt_pk_fp8_f32";
    case Op::V_CvtPkBf8F32:
        return "v_cvt_pk_bf8_f32";
    case Op::V_CvtSrFp8F32:
        return "v_cvt_sr_fp8_f32";
    case Op::V_CvtSrBf8F32:
        return "v_cvt_sr_bf8_f32";
    case Op::V_CmpClassF32:
        return "v_cmp_class_f32";
    case Op::V_CmpClassF64:
        return "v_cmp_class_f64";
    case Op::V_SwapB32:
        return "v_swap_b32";
    case Op::V_SwapB16:
        return "v_swap_b16";
    case Op::V_SatPkU8I16:
        return "v_sat_pk_u8_i16";
    case Op::V_Pipeflush:
        return "v_pipeflush";
    case Op::V_FrexpExpI32F32:
        return "v_frexp_exp_i32_f32";
    case Op::V_FrexpMantF32:
        return "v_frexp_mant_f32";
    case Op::V_FrexpExpI16F16:
        return "v_frexp_exp_i16_f16";
    case Op::V_FrexpMantF16:
        return "v_frexp_mant_f16";
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
    case Op::S_LoadB64:
        return "s_load_b64";
    case Op::S_LoadB96:
        return "s_load_b96";
    case Op::S_LoadB128:
        return "s_load_b128";
    case Op::S_LoadB256:
        return "s_load_b256";
    case Op::S_LoadB512:
        return "s_load_b512";
    case Op::S_LoadI8:
        return "s_load_i8";
    case Op::S_LoadU8:
        return "s_load_u8";
    case Op::S_LoadI16:
        return "s_load_i16";
    case Op::S_LoadU16:
        return "s_load_u16";
    case Op::S_BufferLoadB64:
        return "s_buffer_load_b64";
    case Op::S_BufferLoadB96:
        return "s_buffer_load_b96";
    case Op::S_BufferLoadB128:
        return "s_buffer_load_b128";
    case Op::S_BufferLoadB256:
        return "s_buffer_load_b256";
    case Op::S_BufferLoadB512:
        return "s_buffer_load_b512";
    case Op::S_BufferLoadI8:
        return "s_buffer_load_i8";
    case Op::S_BufferLoadU8:
        return "s_buffer_load_u8";
    case Op::S_BufferLoadI16:
        return "s_buffer_load_i16";
    case Op::S_BufferLoadU16:
        return "s_buffer_load_u16";
    case Op::S_DcacheInv:
        return "s_dcache_inv";
    case Op::S_Gl1Inv:
        return "s_gl1_inv";
    case Op::S_AtcProbe:
        return "s_atc_probe";
    case Op::S_AtcProbeBuffer:
        return "s_atc_probe_buffer";
    case Op::S_PrefetchInst:
        return "s_prefetch_inst";
    case Op::S_PrefetchInstPcRel:
        return "s_prefetch_inst_pc_rel";
    case Op::S_PrefetchData:
        return "s_prefetch_data";
    case Op::S_BufferPrefetchData:
        return "s_buffer_prefetch_data";
    case Op::S_PrefetchDataPcRel:
        return "s_prefetch_data_pc_rel";
    case Op::S_MemTime:
        return "s_memtime";
    case Op::S_MemRealtime:
        return "s_memrealtime";
    case Op::S_BufferLoadB32:
        return "s_buffer_load_b32";
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

    // VBUFFER (RDNA4): 96-bit encoding, six-bit prefix in bits 26..31.
    if (((word0 >> 26) & 0x3F) == 0x31u)
    {
        if (words.size() < 3)
            return std::unexpected(Error("SR-GFX-013", "truncated VBUFFER instruction", bytePc));

        const std::uint32_t word1 = words[1];
        const std::uint32_t word2 = words[2];
        const std::uint16_t bufferOpcode = static_cast<std::uint16_t>((word0 >> 14) & 0xFF);

        const Entry* bufferEntry = Lookup(arch, Encoding::Buffer, bufferOpcode);
        if (bufferEntry == nullptr || bufferEntry->removed)
        {
            return std::unexpected(Error("SR-GFX-014",
                                         "unknown buffer opcode " + std::to_string(bufferOpcode) +
                                             " for " + std::string(GetArchInfo(arch).name),
                                         bytePc));
        }

        DecodedInstruction buffer;
        buffer.op = bufferEntry->op;
        buffer.opcode = bufferOpcode;
        buffer.encoding = Encoding::Buffer;
        buffer.arch = arch;
        buffer.pc = bytePc;
        buffer.word0 = word0;
        buffer.word1 = word1;
        buffer.wordCount = 3;
        buffer.bufferOffen = ((word1 >> 30) & 0x1) != 0;
        buffer.bufferIdxen = ((word1 >> 31) & 0x1) != 0;
        buffer.bufferTfe = ((word0 >> 22) & 0x1) != 0;
        buffer.bufferFormat = static_cast<std::uint8_t>((word1 >> 23) & 0x7F);

        for (std::uint8_t i = 0; i < bufferEntry->operandCount; ++i)
        {
            Operand operand;
            switch (bufferEntry->operands[i])
            {
            case OperandKind::BufferVdst:
                operand.kind = OperandKind::GlobalVdst;
                operand.value = word1 & 0xFF; // bits 32..39
                break;
            case OperandKind::BufferVsrc:
                operand.kind = OperandKind::GlobalVsrc;
                operand.value = word1 & 0xFF; // bits 32..39
                break;
            case OperandKind::BufferVaddr:
                operand.kind = OperandKind::GlobalVaddr;
                operand.value = word2 & 0xFF; // bits 64..71
                break;
            case OperandKind::BufferSoffset:
                operand.kind = OperandKind::Sgpr;
                operand.value = word0 & 0x7F; // bits 0..6
                break;
            case OperandKind::BufferIoffset:
                operand.kind = OperandKind::GlobalIoffset;
                operand.value = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(((word2 >> 8) & 0xFFFFFFu) << 8) >> 8);
                break;
            default:
                return std::unexpected(Error("SR-GFX-015", "unsupported buffer operand layout", bytePc));
            }
            if (buffer.operandCount < buffer.operands.size())
                buffer.operands[buffer.operandCount++] = operand;
        }
        return buffer;
    }

    // RDNA4 memory families use 96-bit encodings that differ only in the
    // leading byte: VFLAT 0xEC, VSCRATCH 0xED and VGLOBAL 0xEE.
    const std::uint32_t memoryPrefix = word0 >> 24;
    if (memoryPrefix == 0xECu || memoryPrefix == 0xEDu || memoryPrefix == 0xEEu)
    {
        if (words.size() < 3)
            return std::unexpected(Error("SR-GFX-006", "truncated 3-word memory instruction", bytePc));

        const Encoding memoryEncoding = memoryPrefix == 0xECu   ? Encoding::Flat
                                        : memoryPrefix == 0xEDu ? Encoding::Scratch
                                                                : Encoding::Global;
        const std::uint32_t word1 = words[1];
        const std::uint32_t word2 = words[2];
        const std::uint16_t memoryOpcode = static_cast<std::uint16_t>((word0 >> 14) & 0xFF);

        const Entry* memoryEntry = Lookup(arch, memoryEncoding, memoryOpcode);
        if (memoryEntry == nullptr || memoryEntry->removed)
        {
            return std::unexpected(Error("SR-GFX-007",
                                         "unknown " + std::string(ToString(memoryEncoding)) + " opcode " +
                                             std::to_string(memoryOpcode) + " for " +
                                             std::string(GetArchInfo(arch).name),
                                         bytePc));
        }

        DecodedInstruction memory;
        memory.op = memoryEntry->op;
        memory.opcode = memoryOpcode;
        memory.encoding = memoryEncoding;
        memory.arch = arch;
        memory.pc = bytePc;
        memory.word0 = word0;
        memory.word1 = word1;
        memory.wordCount = 3;

        for (std::uint8_t i = 0; i < memoryEntry->operandCount; ++i)
        {
            Operand operand;
            switch (memoryEntry->operands[i])
            {
            case OperandKind::GlobalVdst:
                operand.kind = OperandKind::GlobalVdst;
                operand.value = word1 & 0xFF; // bits 32..39
                break;
            case OperandKind::GlobalVaddr:
                operand.kind = OperandKind::GlobalVaddr;
                operand.value = word2 & 0xFF; // bits 64..71
                break;
            case OperandKind::GlobalVsrc:
                operand.kind = OperandKind::GlobalVsrc;
                operand.value = (word1 >> 23) & 0xFF; // bits 55..62
                break;
            case OperandKind::GlobalIoffset:
                operand.kind = OperandKind::GlobalIoffset;
                operand.value = static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(((word2 >> 8) & 0xFFFFFFu) << 8) >> 8); // bits 72..95
                break;
            default:
                return std::unexpected(Error("SR-GFX-008", "unsupported memory operand layout", bytePc));
            }
            if (memory.operandCount < memory.operands.size())
                memory.operands[memory.operandCount++] = operand;
        }
        return memory;
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
    else if (((word0 >> 26) & 0x3F) == 0x36)
    {
        encoding = Encoding::Ds;
        if (words.size() < 2)
            return std::unexpected(Error("SR-GFX-012", "truncated DS instruction", bytePc));
        baseWords = 2;
        wide = (static_cast<std::uint64_t>(words[1]) << 32) | word0;
        opcode = static_cast<std::uint16_t>((wide >> 18) & 0xFF);
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
    instruction.opcode = opcode;
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
        // The flat family keeps its operand roles so the lifter can find
        // them by kind rather than by position; older encodings use the same
        // roles as the RDNA4 three-word form.
        case OperandKind::FlatVdst:
            operand.kind = OperandKind::GlobalVdst;
            operand.value = Extract(wide, 56, 8);
            break;
        case OperandKind::FlatVaddr:
            operand.kind = OperandKind::GlobalVaddr;
            operand.value = Extract(wide, 32, 8);
            break;
        case OperandKind::FlatData:
            operand.kind = OperandKind::GlobalVsrc;
            operand.value = Extract(wide, 40, 8);
            break;
        case OperandKind::FlatSaddr:
            operand.kind = OperandKind::Sgpr;
            operand.value = Extract(wide, 48, 7);
            break;
        case OperandKind::FlatOffset12:
            operand.kind = OperandKind::GlobalIoffset;
            operand.value = Extract(wide, 0, 12);
            break;
        case OperandKind::FlatOffset13:
            operand.kind = OperandKind::GlobalIoffset;
            operand.value = Extract(wide, 0, 13);
            break;
        case OperandKind::GlobalVdst:
            operand.kind = OperandKind::GlobalVdst;
            operand.value = Extract(wide, 32, 8);
            break;
        case OperandKind::GlobalVaddr:
            operand.kind = OperandKind::GlobalVaddr;
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
        case OperandKind::BufferVdst:
        case OperandKind::BufferVaddr:
        case OperandKind::BufferVsrc:
        case OperandKind::BufferSoffset:
        case OperandKind::BufferIoffset:
            break;
        case OperandKind::DsAddr:
            operand.value = Extract(wide, 32, 8);
            break;
        case OperandKind::DsData0:
            operand.value = Extract(wide, 40, 8);
            break;
        case OperandKind::DsData1:
            operand.value = Extract(wide, 48, 8);
            break;
        case OperandKind::DsDest:
            operand.value = Extract(wide, 56, 8);
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

    else if (encoding == Encoding::Ds)
    {
        instruction.dsOffset0 = static_cast<std::uint32_t>(word0 & 0xFF);
        instruction.dsOffset1 = static_cast<std::uint32_t>((word0 >> 8) & 0xFF);
        instruction.dsGds = ((word0 >> 17) & 0x1) != 0;
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
