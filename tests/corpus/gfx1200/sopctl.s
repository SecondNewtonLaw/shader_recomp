.text
.globl sopctl
sopctl:
  s_mov_b32 s0, 10
  s_addk_co_i32 s0, 5
  s_cmp_eq_u32 s0, 15
  s_cmovk_i32 s0, 20
  s_nop 0
  s_cmovk_i32 s1, 3
  s_cmp_eq_u32 s0, 0
  s_cmovk_i32 s2, 9
  s_clause 0x1
  s_wait_loadcnt 0x0
  s_wait_storecnt 0x0
  v_mov_b32 v0, s0
  v_mov_b32 v1, 0
  global_store_b32 v1, v0, s[0:1]
  v_mov_b32 v2, s1
  v_mov_b32 v3, 4
  global_store_b32 v3, v2, s[0:1]
  v_mov_b32 v4, s2
  v_mov_b32 v5, 8
  global_store_b32 v5, v4, s[0:1]
  s_endpgm
