.text
.globl t
t:
  v_mov_b32 v0, 1.5
  v_mov_b32 v1, 0.75
  v_mov_b32 v2, 0x7f800000
  v_mov_b32 v3, 0x7fc00000
  v_mov_b32 v4, 0
  v_mov_b32 v5, 0x00000001
  v_frexp_exp_i32_f32 v10, v0
  v_frexp_mant_f32 v11, v0
  v_frexp_exp_i32_f32 v12, v1
  v_frexp_mant_f32 v13, v1
  v_frexp_exp_i32_f32 v14, v2
  v_frexp_exp_i32_f32 v15, v3
  v_frexp_exp_i32_f32 v16, v4
  v_frexp_exp_i32_f32 v17, v5
  v_frexp_mant_f32 v18, v5
  v_mov_b32 v20, 0
  global_store_b32 v20, v10, s[0:1]
  v_mov_b32 v21, 4
  global_store_b32 v21, v11, s[0:1]
  v_mov_b32 v22, 8
  global_store_b32 v22, v12, s[0:1]
  v_mov_b32 v23, 12
  global_store_b32 v23, v13, s[0:1]
  v_mov_b32 v24, 16
  global_store_b32 v24, v14, s[0:1]
  v_mov_b32 v25, 20
  global_store_b32 v25, v15, s[0:1]
  v_mov_b32 v26, 24
  global_store_b32 v26, v16, s[0:1]
  v_mov_b32 v27, 28
  global_store_b32 v27, v17, s[0:1]
  v_mov_b32 v28, 32
  global_store_b32 v28, v18, s[0:1]
  s_endpgm
