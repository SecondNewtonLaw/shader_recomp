.text
.globl fpalu
fpalu:
  v_mov_b32 v0, 1.0
  v_mov_b32 v1, 2.0
  v_mov_b32 v2, 3.0
  v_min_num_f32 v3, v1, v2
  v_max_num_f32 v4, v1, v2
  v_fmac_f32 v3, v1, v2
  v_fma_f32 v5, v1, v2, v0
  v_min3_num_f32 v6, v1, v2, v0
  v_max3_num_f32 v7, v1, v2, v0
  v_cmp_lt_f32 v1, v2
  v_cndmask_b32 v12, v1, v2
  v_cvt_u32_f32 v19, v5
  v_mov_b32 v20, 0
  global_store_b32 v20, v19, s[0:1]
  v_cvt_u32_f32 v21, v12
  v_mov_b32 v22, 4
  global_store_b32 v22, v21, s[0:1]
  v_mov_b32 v16, 0x80000000
  v_mov_b32 v17, 4
  v_lshrrev_b32 v18, v17, v16
  v_mov_b32 v23, 8
  global_store_b32 v23, v18, s[0:1]
  s_mov_b32 s1, 0xffffffff
  s_mov_b32 s2, 0
  s_add_co_u32 s3, s1, s1
  s_addc_u32 s4, s2, s2
  v_mov_b32 v26, s4
  v_mov_b32 v24, 12
  global_store_b32 v24, v26, s[0:1]
  s_sub_co_u32 s5, s2, s1
  s_subb_u32 s6, s2, s2
  v_mov_b32 v27, s6
  v_mov_b32 v25, 16
  global_store_b32 v25, v27, s[0:1]
  v_rcp_f32 v8, v1
  v_cvt_u32_f32 v28, v8
  v_mov_b32 v29, 20
  global_store_b32 v29, v28, s[0:1]
  v_sqrt_f32 v9, v2
  v_cvt_u32_f32 v30, v9
  v_mov_b32 v31, 24
  global_store_b32 v31, v30, s[0:1]
  v_exp_f32 v10, v0
  v_cvt_u32_f32 v32, v10
  v_mov_b32 v33, 28
  global_store_b32 v33, v32, s[0:1]
  v_log_f32 v11, v2
  v_cvt_u32_f32 v34, v11
  v_mov_b32 v35, 32
  global_store_b32 v35, v34, s[0:1]
  s_endpgm
