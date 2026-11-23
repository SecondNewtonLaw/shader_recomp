.text
.globl bitfield
bitfield:
  s_mov_b32 s0, 0x12345678
  s_bfe_u32 s1, s0, 0x00080008
  s_bfe_i32 s2, s0, 0x00080008
  s_bfm_b32 s3, 8, 8
  s_mov_b32 s5, 0x12345600
  s_absdiff_i32 s4, s0, s5
  s_mov_b32 s6, 0xf0000000
  s_bfe_i32 s7, s6, 0x0004001c
  v_mov_b32 v0, 0x12345678
  v_bfe_u32 v1, v0, 8, 8
  v_bfe_i32 v2, v0, 8, 8
  v_bfm_b32 v3, 8, 8
  v_bfi_b32 v4, v3, v0, 0
  v_alignbit_b32 v5, v0, v0, 4
  v_alignbyte_b32 v6, v0, v0, 1
  v_mov_b32 v10, s1
  v_mov_b32 v11, 0
  global_store_b32 v11, v10, s[0:1]
  v_mov_b32 v12, s7
  v_mov_b32 v13, 4
  global_store_b32 v13, v12, s[0:1]
  v_mov_b32 v14, 8
  global_store_b32 v14, v4, s[0:1]
  v_mov_b32 v15, 12
  global_store_b32 v15, v5, s[0:1]
  v_mov_b32 v16, 16
  global_store_b32 v16, v6, s[0:1]
  s_endpgm
