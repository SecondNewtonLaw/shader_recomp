; RDNA base ALU batch: scalar unary/binary/SOPK and vector conversions.
.text
.globl alu
alu:
  s_mov_b32 s0, 0x0000ffff
  s_not_b32 s1, s0
  s_brev_b32 s2, s0
  s_bcnt1_i32_b32 s3, s0
  s_mul_i32 s4, s0, s0
  s_min_i32 s5, s4, s0
  s_max_u32 s6, s5, s0
  s_nand_b32 s7, s5, s0
  s_mov_b32 s10, 8
  s_ashr_i32 s8, s4, s10
  s_movk_i32 s9, -1
  v_cvt_f32_i32 v0, s5
  v_cvt_i32_f32 v1, v0
  s_endpgm
