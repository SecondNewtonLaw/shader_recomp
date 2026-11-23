.text
.globl bitops
bitops:
  s_mov_b32 s0, 0x0000ff00
  s_ff1_i32_b32 s1, s0
  s_flbit_i32_b32 s2, s0
  s_mov_b32 s3, 0
  s_ff1_i32_b32 s4, s3
  s_flbit_i32_b32 s5, s3
  s_andn2_b32 s6, s0, s3
  s_orn2_b32 s7, s3, s0
  s_bcnt1_i32_b32 s8, s0
  s_cselect_b32 s9, 1, 0
  s_bcnt1_i32_b32 s10, s3
  s_cselect_b32 s11, 1, 0
  s_endpgm
