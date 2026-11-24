.text
.globl sopscalar
sopscalar:
  s_mov_b32 s0, 0x3fc00000
  s_mov_b32 s1, 0x40200000
  s_mov_b32 s2, 0x3e00
  s_mov_b32 s3, 0x4100
  s_cmp_lt_f32 s0, s1
  s_cselect_b32 s10, 1, 0
  s_cmp_gt_f32 s0, s1
  s_cselect_b32 s11, 1, 0
  s_cmp_eq_f32 s0, s0
  s_cselect_b32 s12, 1, 0
  s_cmp_u_f32 s0, s1
  s_cselect_b32 s13, 1, 0
  s_cmp_lt_f16 s2, s3
  s_cselect_b32 s14, 1, 0
  s_add_f32 s15, s0, s1
  s_mul_f32 s16, s0, s1
  s_min_num_f32 s17, s0, s1
  s_maximum_f32 s18, s0, s1
  s_mov_b32 s19, 0x3fc00000
  s_fmac_f32 s19, s0, s1
  s_add_f16 s20, s2, s3
  s_mov_b32 s21, 0x11112222
  s_mov_b32 s22, 0x33334444
  s_pack_ll_b32_b16 s23, s21, s22
  s_mov_b32 s24, 3
  s_lshl1_add_u32 s25, s24, 5
  s_mov_b32 s26, 7
  s_cmp_lt_f32 s0, s1
  s_cmov_b32 s26, 9
  s_mov_b32 s27, 1
  s_bitreplicate_b64_b32 s[28:29], s27
  s_mov_b32 s30, 0x0000000f
  s_quadmask_b32 s31, s30
  s_ceil_f32 s32, s0
  s_floor_f32 s33, s1
  s_trunc_f32 s34, s1
  s_mov_b32 s35, 0x40200000
  s_rndne_f32 s36, s35
  s_mov_b32 s37, 0xfffffffb
  s_cvt_f32_i32 s38, s37
  s_cvt_i32_f32 s39, s1
  s_cvt_f16_f32 s40, s0
  s_cvt_f32_f16 s41, s2
  s_mov_b32 s42, 0x3e000000
  s_cvt_hi_f32_f16 s43, s42
  v_mov_b32 v0, s10
  v_mov_b32 v1, 0
  global_store_b32 v1, v0, s[0:1]
  v_mov_b32 v2, s11
  v_mov_b32 v3, 4
  global_store_b32 v3, v2, s[0:1]
  v_mov_b32 v4, s12
  v_mov_b32 v5, 8
  global_store_b32 v5, v4, s[0:1]
  v_mov_b32 v6, s13
  v_mov_b32 v7, 12
  global_store_b32 v7, v6, s[0:1]
  v_mov_b32 v8, s14
  v_mov_b32 v9, 16
  global_store_b32 v9, v8, s[0:1]
  v_mov_b32 v10, s15
  v_mov_b32 v11, 20
  global_store_b32 v11, v10, s[0:1]
  v_mov_b32 v12, s16
  v_mov_b32 v13, 24
  global_store_b32 v13, v12, s[0:1]
  v_mov_b32 v14, s17
  v_mov_b32 v15, 28
  global_store_b32 v15, v14, s[0:1]
  v_mov_b32 v16, s18
  v_mov_b32 v17, 32
  global_store_b32 v17, v16, s[0:1]
  v_mov_b32 v18, s19
  v_mov_b32 v19, 36
  global_store_b32 v19, v18, s[0:1]
  v_mov_b32 v20, s20
  v_mov_b32 v21, 40
  global_store_b32 v21, v20, s[0:1]
  v_mov_b32 v22, s23
  v_mov_b32 v23, 44
  global_store_b32 v23, v22, s[0:1]
  v_mov_b32 v24, s25
  v_mov_b32 v25, 48
  global_store_b32 v25, v24, s[0:1]
  v_mov_b32 v26, s26
  v_mov_b32 v27, 52
  global_store_b32 v27, v26, s[0:1]
  v_mov_b32 v28, s28
  v_mov_b32 v29, 56
  global_store_b32 v29, v28, s[0:1]
  v_mov_b32 v30, s31
  v_mov_b32 v31, 60
  global_store_b32 v31, v30, s[0:1]
  v_mov_b32 v32, s32
  v_mov_b32 v33, 64
  global_store_b32 v33, v32, s[0:1]
  v_mov_b32 v34, s33
  v_mov_b32 v35, 68
  global_store_b32 v35, v34, s[0:1]
  v_mov_b32 v36, s34
  v_mov_b32 v37, 72
  global_store_b32 v37, v36, s[0:1]
  v_mov_b32 v38, s36
  v_mov_b32 v39, 76
  global_store_b32 v39, v38, s[0:1]
  v_mov_b32 v40, s38
  v_mov_b32 v41, 80
  global_store_b32 v41, v40, s[0:1]
  v_mov_b32 v42, s39
  v_mov_b32 v43, 84
  global_store_b32 v43, v42, s[0:1]
  v_mov_b32 v44, s40
  v_mov_b32 v45, 88
  global_store_b32 v45, v44, s[0:1]
  v_mov_b32 v46, s41
  v_mov_b32 v47, 92
  global_store_b32 v47, v46, s[0:1]
  v_mov_b32 v48, s43
  v_mov_b32 v49, 96
  global_store_b32 v49, v48, s[0:1]
  s_endpgm
