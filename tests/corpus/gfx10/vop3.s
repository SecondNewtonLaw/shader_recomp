.text
.globl vop3
vop3:
  v_mov_b32 v0, 5
  v_mov_b32 v1, 3
  v_mov_b32 v2, 2
  v_add3_u32 v3, v0, v1, v2
  v_add_lshl_u32 v4, v0, v1, v2
  v_and_or_b32 v5, v0, v1, v2
  v_or3_b32 v6, v0, v1, v2
  v_xor3_b32 v7, v0, v1, v2
  v_lshl_add_u32 v8, v0, v1, v2
  v_lshl_or_b32 v9, v0, v1, v2
  v_xad_u32 v10, v0, v1, v2
  v_min3_i32 v11, v0, v1, v2
  v_med3_i32 v12, v0, v1, v2
  v_max3_i32 v13, v0, v1, v2
  v_mov_b32 v20, 0x40000000
  v_mov_b32 v21, 4
  v_mul_hi_u32 v14, v20, v21
  v_mul_hi_i32 v15, v20, v21
  v_mad_i32_i24 v16, v0, v1, v2
  v_mad_u32_u24 v17, v0, v1, v2
  v_sad_u32 v18, v0, v1, v2
  v_mov_b32 v22, 1.5
  v_mov_b32 v23, 3
  v_ldexp_f32 v19, v22, v23
  v_add_nc_i32 v24, v0, v1
  v_sub_nc_i32 v25, v0, v1
  v_cvt_u32_f32 v19, v19
  v_mov_b32 v30, 0
  global_store_dword v30, v3, s[0:1]
  v_mov_b32 v31, 4
  global_store_dword v31, v4, s[0:1]
  v_mov_b32 v32, 8
  global_store_dword v32, v5, s[0:1]
  v_mov_b32 v33, 12
  global_store_dword v33, v6, s[0:1]
  v_mov_b32 v34, 16
  global_store_dword v34, v7, s[0:1]
  v_mov_b32 v35, 20
  global_store_dword v35, v8, s[0:1]
  v_mov_b32 v36, 24
  global_store_dword v36, v9, s[0:1]
  v_mov_b32 v37, 28
  global_store_dword v37, v10, s[0:1]
  v_mov_b32 v38, 32
  global_store_dword v38, v11, s[0:1]
  v_mov_b32 v39, 36
  global_store_dword v39, v12, s[0:1]
  v_mov_b32 v40, 40
  global_store_dword v40, v13, s[0:1]
  v_mov_b32 v41, 44
  global_store_dword v41, v14, s[0:1]
  v_mov_b32 v42, 48
  global_store_dword v42, v16, s[0:1]
  v_mov_b32 v43, 52
  global_store_dword v43, v18, s[0:1]
  v_mov_b32 v44, 56
  global_store_dword v44, v19, s[0:1]
  v_mov_b32 v45, 60
  global_store_dword v45, v24, s[0:1]
  v_mov_b32 v46, 64
  global_store_dword v46, v25, s[0:1]
  s_endpgm
