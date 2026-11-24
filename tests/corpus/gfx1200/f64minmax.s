.text
.globl vf64m
vf64m:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x3ff80000
  v_mov_b32 v2, 0
  v_mov_b32 v3, 0x40000000
  v_minimum_f64 v[10:11], v[0:1], v[2:3]
  v_maximum_f64 v[12:13], v[0:1], v[2:3]
  v_mov_b32 v20, 0
  global_store_b32 v20, v10, s[0:1]
  v_mov_b32 v21, 4
  global_store_b32 v21, v11, s[0:1]
  v_mov_b32 v22, 8
  global_store_b32 v22, v12, s[0:1]
  v_mov_b32 v23, 12
  global_store_b32 v23, v13, s[0:1]
  s_endpgm
