.text
.globl modifiers
modifiers:
  v_mov_b32 v0, 1.0
  v_mov_b32 v1, -2.0
  v_mov_b32 v2, 3.0
  v_mul_f32 v3, abs(v1), v2
  v_add_f32 v4, -v0, v2
  v_mul_f32 v5, v0, v1 clamp
  v_mul_f32 v6, v0, v1 mul:2
  v_cvt_u32_f32 v7, v3
  v_mov_b32 v13, 0
  global_store_b32 v13, v7, s[0:1]
  v_cvt_u32_f32 v8, v4
  v_mov_b32 v14, 4
  global_store_b32 v14, v8, s[0:1]
  v_cvt_u32_f32 v9, v5
  v_mov_b32 v15, 8
  global_store_b32 v15, v9, s[0:1]
  v_mov_b32 v10, 0
  v_add_f32 v11, -v6, v10
  v_cvt_u32_f32 v12, v11
  v_mov_b32 v16, 12
  global_store_b32 v16, v12, s[0:1]
  s_endpgm
