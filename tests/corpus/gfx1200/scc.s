.text
.globl scc
scc:
  s_mov_b32 s0, 0xffffffff
  s_add_co_i32 s1, s0, s0
  s_cselect_b32 s2, 1, 0
  s_mov_b32 s3, 5
  s_add_co_i32 s4, s3, s3
  s_cselect_b32 s5, 1, 0
  s_sub_co_i32 s6, s3, s0
  s_cselect_b32 s7, 1, 0
  s_sub_co_i32 s8, s0, s3
  s_cselect_b32 s9, 1, 0
  s_mov_b32 s10, 5
  s_addk_co_i32 s10, 0xffff
  s_cselect_b32 s11, 1, 0
  s_mov_b32 s14, 5
  s_addk_co_i32 s14, 3
  s_cselect_b32 s15, 1, 0
  s_mov_b32 s12, 0xfffffffd
  s_mulk_i32 s12, 3
  s_mov_b32 s13, 0x7fffffff
  s_mulk_i32 s13, -2
  s_endpgm
