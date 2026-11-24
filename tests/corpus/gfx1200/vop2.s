.text
.globl vop2
vop2:
  v_mov_b32 v0, 0x7fffff
  v_mov_b32 v1, 2
  v_mul_i32_i24 v2, v0, v1
  v_mul_u32_u24 v3, v0, v1
  v_mul_hi_i32_i24 v4, v0, v1
  v_mul_hi_u32_u24 v7, v0, v0
  v_mov_b32 v8, 5.0
  v_mov_b32 v9, 2.0
  v_mov_b32 v26, -3.0
  v_subrev_f32 v10, v8, v9
  v_cvt_i32_f32 v10, v10
  v_mov_b32 v11, 0.0
  v_mul_dx9_zero_f32 v12, v8, v11
  v_mov_b32 v13, 0x0f0f0f0f
  v_mov_b32 v14, 0x00ff00ff
  v_xnor_b32 v25, v13, v14
  v_mov_b32 v15, 0x00000002
  v_mov_b32 v16, 0x00000001
  v_mov_b32 v17, 4
  v_lshlrev_b64 v[18:19], v17, v[15:16]
  v_mov_b32 v20, 5
  v_mov_b32 v21, 3
  v_cmp_gt_u32 v20, v21
  v_add_co_ci_u32 v22, v20, v21
  v_sub_co_ci_u32 v23, v20, v21
  v_subrev_co_ci_u32 v24, v20, v21
  v_mov_b32 v30, 0
  global_store_b32 v30, v2, s[0:1]
  v_mov_b32 v31, 4
  global_store_b32 v31, v3, s[0:1]
  v_mov_b32 v32, 8
  global_store_b32 v32, v4, s[0:1]
  v_mov_b32 v33, 12
  global_store_b32 v33, v7, s[0:1]
  v_mov_b32 v34, 16
  global_store_b32 v34, v10, s[0:1]
  v_mov_b32 v35, 20
  global_store_b32 v35, v12, s[0:1]
  v_mov_b32 v36, 24
  global_store_b32 v36, v25, s[0:1]
  v_mov_b32 v37, 28
  global_store_b32 v37, v18, s[0:1]
  v_mov_b32 v38, 32
  global_store_b32 v38, v19, s[0:1]
  v_mov_b32 v39, 36
  global_store_b32 v39, v22, s[0:1]
  v_mov_b32 v40, 40
  global_store_b32 v40, v23, s[0:1]
  v_mov_b32 v41, 44
  global_store_b32 v41, v24, s[0:1]
  s_endpgm
