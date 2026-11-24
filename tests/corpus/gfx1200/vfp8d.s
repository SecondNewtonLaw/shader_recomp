.text
.globl vfp8d
vfp8d:
  v_mov_b32 v0, 0x3e3c3f3e
  v_mov_b32 v1, 0x3e3c3f3e
  v_mov_b32 v2, 0
  v_dot4_f32_fp8_fp8 v10, v0, v1, v2
  v_dot4_f32_bf8_bf8 v11, v0, v1, v2
  v_mov_b32 v16, 0
  global_store_b32 v16, v10, s[0:1]
  v_mov_b32 v17, 4
  global_store_b32 v17, v11, s[0:1]
  s_endpgm
