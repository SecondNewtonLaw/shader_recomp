; Scalar forms that the gfx10 table did not decode: a shift-add and a 16-bit
; pack. s2 = (s1 << 4) + s0, s3 = low16(s0) | low16(s1) << 16.
.text
.globl scalarnew
scalarnew:
  s_mov_b32 s0, 1
  s_mov_b32 s1, 3
  s_lshl4_add_u32 s2, s0, s1
  s_pack_ll_b32_b16 s3, s0, s1
  v_mov_b32 v1, s2
  v_mov_b32 v2, s3
  v_mov_b32 v0, 0
  global_store_dword v0, v1, s[0:1]
  v_mov_b32 v3, 4
  global_store_dword v3, v2, s[0:1]
  s_endpgm
