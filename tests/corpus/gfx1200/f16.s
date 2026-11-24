.text
.globl f16
f16:
  v_mov_b32 v0, 0x3c00
  v_mov_b32 v1, 0x4000
  v_add_f16 v2, v0, v1
  v_sub_f16 v3, v1, v0
  v_mul_f16 v4, v1, v1
  v_fma_f16 v5, v1, v1, v0
  v_mov_b32 v6, 0x3c00
  v_fmac_f16 v6, v1, v1
  v_min_num_f16 v7, v0, v1
  v_max_num_f16 v8, v0, v1
  v_mov_b32 v12, 1.5
  v_cvt_f16_f32 v11, v12
  v_cvt_f32_f16 v13, v0
  v_mov_b32 v15, 3
  v_cvt_f16_u16 v14, v15
  v_cvt_u16_f16 v16, v14
  v_mov_b32 v18, -1
  v_cvt_f16_i16 v17, v18
  v_cvt_i16_f16 v19, v17
  v_rcp_f16 v20, v1
  v_sqrt_f16 v21, v4
  v_exp_f16 v22, v1
  v_log_f16 v23, v4
  v_mov_b32 v24, 0x4100
  v_floor_f16 v25, v24
  v_ceil_f16 v26, v24
  v_mov_b32 v27, 0xc100
  v_trunc_f16 v28, v27
  v_rndne_f16 v29, v24
  v_mov_b32 v30, 0x3c00
  v_mov_b32 v31, 3
  v_ldexp_f16 v32, v30, v31
  v_cvt_norm_i16_f16 v33, v0
  v_cvt_norm_u16_f16 v34, v0
  v_mov_b32 v39, 0
  global_store_b32 v39, v2, s[0:1]
  v_mov_b32 v40, 4
  global_store_b32 v40, v3, s[0:1]
  v_mov_b32 v41, 8
  global_store_b32 v41, v4, s[0:1]
  v_mov_b32 v42, 12
  global_store_b32 v42, v5, s[0:1]
  v_mov_b32 v43, 16
  global_store_b32 v43, v6, s[0:1]
  v_mov_b32 v44, 20
  global_store_b32 v44, v7, s[0:1]
  v_mov_b32 v45, 24
  global_store_b32 v45, v8, s[0:1]
  v_mov_b32 v46, 28
  global_store_b32 v46, v11, s[0:1]
  v_mov_b32 v47, 32
  global_store_b32 v47, v13, s[0:1]
  v_mov_b32 v48, 36
  global_store_b32 v48, v14, s[0:1]
  v_mov_b32 v49, 40
  global_store_b32 v49, v16, s[0:1]
  v_mov_b32 v50, 44
  global_store_b32 v50, v17, s[0:1]
  v_mov_b32 v51, 48
  global_store_b32 v51, v19, s[0:1]
  v_mov_b32 v52, 52
  global_store_b32 v52, v20, s[0:1]
  v_mov_b32 v53, 56
  global_store_b32 v53, v21, s[0:1]
  v_mov_b32 v54, 60
  global_store_b32 v54, v22, s[0:1]
  v_mov_b32 v55, 64
  global_store_b32 v55, v23, s[0:1]
  v_mov_b32 v56, 68
  global_store_b32 v56, v25, s[0:1]
  v_mov_b32 v57, 72
  global_store_b32 v57, v26, s[0:1]
  v_mov_b32 v58, 76
  global_store_b32 v58, v28, s[0:1]
  v_mov_b32 v59, 80
  global_store_b32 v59, v29, s[0:1]
  v_mov_b32 v60, 84
  global_store_b32 v60, v32, s[0:1]
  v_mov_b32 v61, 88
  global_store_b32 v61, v33, s[0:1]
  v_mov_b32 v62, 92
  global_store_b32 v62, v34, s[0:1]
  s_endpgm
