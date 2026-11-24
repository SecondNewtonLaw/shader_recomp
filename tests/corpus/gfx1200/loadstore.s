.text
.globl loadstore
loadstore:
  s_load_b32 s5, s[0:1], 0
  v_mov_b32 v0, 4
  global_load_b32 v1, v0, s[0:1]
  v_mov_b32 v2, s5
  v_mul_lo_u32 v3, v2, v1
  v_mov_b32 v4, 8
  global_store_b32 v4, v3, s[0:1]
  s_endpgm
