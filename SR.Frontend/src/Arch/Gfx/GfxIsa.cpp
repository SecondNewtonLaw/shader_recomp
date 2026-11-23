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
};

constexpr Entry Make(Encoding encoding,
                     std::uint16_t opcode,
                     Op op,
                     std::string_view mnemonic,
                     std::initializer_list<OperandKind> operands)
{
    Entry entry{};
    entry.encoding = encoding;
    entry.opcode = opcode;
    entry.op = op;
    entry.mnemonic = mnemonic;
    entry.operandCount = static_cast<std::uint8_t>(operands.size());
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

// RDNA1 is the root of the lineage.
constexpr std::array kGfx10 = {
    Make(Encoding::Sop1, 3, Op::S_MovB32, "s_mov_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 0, Op::S_AddU32, "s_add_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 1, Op::S_SubU32, "s_sub_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 14, Op::S_AndB32, "s_and_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 16, Op::S_OrB32, "s_or_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 18, Op::S_XorB32, "s_xor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 30, Op::S_LshlB32, "s_lshl_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 32, Op::S_LshrB32, "s_lshr_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 10, Op::S_CselectB32, "s_cselect_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 6, Op::S_MinI32, "s_min_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 7, Op::S_MinU32, "s_min_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 8, Op::S_MaxI32, "s_max_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 9, Op::S_MaxU32, "s_max_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 38, Op::S_MulI32, "s_mul_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 24, Op::S_NandB32, "s_nand_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 26, Op::S_NorB32, "s_nor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 28, Op::S_XnorB32, "s_xnor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 34, Op::S_AshrI32, "s_ashr_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),

    Make(Encoding::Sop1, 7, Op::S_NotB32, "s_not_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 11, Op::S_BrevB32, "s_brev_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 15, Op::S_Bcnt1I32B32, "s_bcnt1_i32_b32", {OperandKind::Sdst, OperandKind::Ssrc}),
    Make(Encoding::Sop1, 52, Op::S_AbsI32, "s_abs_i32", {OperandKind::Sdst, OperandKind::Ssrc}),

    Make(Encoding::SopK, 0, Op::S_MovkI32, "s_movk_i32", {OperandKind::Sdst, OperandKind::Simm16}),

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
    Make(Encoding::Vop1, 55, Op::V_NotB32, "v_not_b32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 5, Op::V_CvtF32I32, "v_cvt_f32_i32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 6, Op::V_CvtF32U32, "v_cvt_f32_u32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 8, Op::V_CvtI32F32, "v_cvt_i32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop1, 7, Op::V_CvtU32F32, "v_cvt_u32_f32", {OperandKind::Vgpr, OperandKind::Src}),
    Make(Encoding::Vop2, 3, Op::V_AddF32, "v_add_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 4, Op::V_SubF32, "v_sub_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
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

    Make(Encoding::Vop3, 259, Op::V_AddF32, "v_add_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 260, Op::V_SubF32, "v_sub_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 264, Op::V_MulF32, "v_mul_f32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 331, Op::V_FmaF32, "v_fma_f32",
         {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 283, Op::V_AndB32, "v_and_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 284, Op::V_OrB32, "v_or_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 285, Op::V_XorB32, "v_xor_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Src}),
    Make(Encoding::Vop3, 385, Op::V_MovB32, "v_mov_b32", {OperandKind::Vgpr, OperandKind::Src}),
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
};

// RDNA3 renumbered scalar opcodes and VOP3 FMA. Old opcodes are removed
// explicitly: the encoding space is reused, so inheritance alone would decode
// unrelated instructions as the old mnemonic.
constexpr std::array kGfx11 = {
    Remove(Encoding::Sop1, 3),
    Remove(Encoding::Sop1, 7),
    Remove(Encoding::Sop1, 11),
    Remove(Encoding::Sop1, 15),
    Remove(Encoding::Sop1, 52),

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
    Remove(Encoding::Vop2, 24),
    Remove(Encoding::Vop2, 26),

    Make(Encoding::Sop1, 0, Op::S_MovB32, "s_mov_b32", {OperandKind::Sdst, OperandKind::Ssrc}),

    Make(Encoding::Sop2, 22, Op::S_AndB32, "s_and_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 24, Op::S_OrB32, "s_or_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 26, Op::S_XorB32, "s_xor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 8, Op::S_LshlB32, "s_lshl_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 10, Op::S_LshrB32, "s_lshr_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 48, Op::S_CselectB32, "s_cselect_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 18, Op::S_MinI32, "s_min_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 19, Op::S_MinU32, "s_min_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 20, Op::S_MaxI32, "s_max_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 21, Op::S_MaxU32, "s_max_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 44, Op::S_MulI32, "s_mul_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 28, Op::S_NandB32, "s_nand_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 30, Op::S_NorB32, "s_nor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 32, Op::S_XnorB32, "s_xnor_b32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 12, Op::S_AshrI32, "s_ashr_i32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),

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
    Make(Encoding::Vop2, 24, Op::V_LshlrevB32, "v_lshlrev_b32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
    Make(Encoding::Vop2, 26, Op::V_AshrrevI32, "v_ashrrev_i32", {OperandKind::Vgpr, OperandKind::Src, OperandKind::Vsrc}),
};

// RDNA4 keeps RDNA3's numbering for this subset and replaces plain S_ADD/S_SUB
// with the carry-out forms.
constexpr std::array kGfx12 = {
    Make(Encoding::Sop2, 0, Op::S_AddCoU32, "s_add_co_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Sop2, 1, Op::S_SubCoU32, "s_sub_co_u32", {OperandKind::Sdst, OperandKind::Ssrc, OperandKind::Ssrc}),
    Make(Encoding::Global, 26, Op::GlobalStoreB32, "global_store_b32",
         {OperandKind::Vgpr, OperandKind::Vgpr, OperandKind::Imm32}),
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

    if (arch == Arch::Gfx10)
    {
        for (auto it = kGfx10.rbegin(); it != kGfx10.rend(); ++it)
        {
            if (matches(*it))
                return &*it;
        }
        return nullptr;
    }

    const std::span<const Entry> entries = LevelEntries(arch);
    if (!entries.empty())
    {
        for (auto it = entries.rbegin(); it != entries.rend(); ++it)
        {
            if (matches(*it))
                return &*it;
        }
    }

    return nullptr;
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
        ArchInfo{Arch::Gfx12, Arch::Gfx11, "gfx1200", "rdna4"},
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

    // VGLOBAL (RDNA4): 96-bit encoding, three words. GLOBAL_STORE_B32 is the
    // first member of this family used by the test ABI.
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

        const auto pushGlobal = [&global](Operand operand) {
            if (global.operandCount < global.operands.size())
                global.operands[global.operandCount++] = operand;
        };

        // VADDR, VSRC, IOFFSET (24-bit byte offset).
        pushGlobal(Operand{OperandKind::Vgpr, word2 & 0xFF, false});
        pushGlobal(Operand{OperandKind::Vgpr, (word1 >> 23) & 0xFF, false});
        pushGlobal(Operand{OperandKind::Imm32, (word2 >> 8) & 0xFFFFFF, false});
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
    else if (((word0 >> 26) & 0x3F) == 0x35)
    {
        encoding = Encoding::Vop3;
        if (words.size() < 2)
            return std::unexpected(Error("SR-GFX-002", "truncated VOP3 instruction", bytePc));
        baseWords = 2;
        wide = (static_cast<std::uint64_t>(words[1]) << 32) | word0;
        opcode = static_cast<std::uint16_t>((wide >> 16) & 0x3FF);
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
            if (encoding == Encoding::Vop3)
                operand.value = Extract(wide, 0, 8);
            break;
        case OperandKind::Src:
            operand.value = Extract(wide, 0, 9);
            if (encoding == Encoding::Vop3)
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
