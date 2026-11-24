.text
.globl vop3ext
vop3ext:
  v_mov_b32 v0, 1.0
  v_mov_b32 v1, 2.5
  v_mov_b32 v2, -1.0
  v_mov_b32 v10, 0x00010002
  v_mov_b32 v11, 0x00030004
  v_add_nc_u16 v20, v10, v11
  v_sub_nc_u16 v21, v10, v11
  v_mul_lo_u16 v22, v10, v11
  v_min_u16 v23, v10, v11
  v_max_i16 v24, v10, v11
  v_mov_b32 v12, 1
  v_lshlrev_b16 v25, v12, v11
  v_mov_b32 v13, 0x0000fffe
  v_ashrrev_i16 v26, v12, v13
  v_mov_b32 v14, 7
  v_mad_u16 v27, v10, v11, v14
  v_mov_b32 v15, 100
  v_mad_i32_i16 v28, v13, v11, v15
  v_mov_b32 v16, 5
  v_mov_b32 v17, 3
  v_mov_b32 v18, 9
  v_min3_u16 v29, v16, v17, v18
  v_mov_b32 v19, 0xfffffffb
  v_mov_b32 v30, 0xfffffff7
  v_max3_i16 v31, v19, v17, v30
  v_med3_u16 v32, v16, v17, v18
  v_minmax_u32 v33, v16, v17, v18
  v_maxmin_i32 v34, v19, v17, v18
  v_minimum_f32 v35, v0, v1
  v_maximum_f32 v36, v0, v1
  v_minimum3_f32 v37, v0, v1, v2
  v_med3_num_f32 v38, v0, v1, v2
  v_mov_b32 v40, 1.5
  v_mov_b32 v41, -2.5
  v_cvt_pk_i16_f32 v42, v40, v41
  v_mov_b32 v43, 3.5
  v_cvt_pk_u16_f32 v44, v40, v43
  v_cvt_pk_i16_i32 v45, v10, v11
  v_cvt_pk_norm_i16_f32 v46, v0, v2
  v_mov_b32 v47, 0.1
  v_cvt_pk_rtz_f16_f32 v48, v40, v47
  v_mov_b32 v49, 0x3c00
  v_mov_b32 v50, 0x4000
  v_pack_b32_f16 v51, v49, v50
  v_mov_b32 v52, 0x01020304
  v_mov_b32 v53, 0x05060708
  v_mov_b32 v54, 0
  v_sad_u8 v55, v52, v53, v54
  v_sad_hi_u8 v56, v52, v53, v54
  v_mov_b32 v57, 0x00000100
  v_msad_u8 v58, v52, v57, v54
  v_mov_b32 v59, 0x02040608
  v_mov_b32 v60, 0x04080c10
  v_lerp_u8 v61, v59, v60, v54
  v_cubeid_f32 v62, v0, v1, v2
  v_cubema_f32 v63, v0, v1, v2
  v_cubesc_f32 v64, v0, v1, v2
  v_cubetc_f32 v65, v0, v1, v2
  v_mullit_f32 v66, v0, v1, v2
  v_mov_b32 v67, 0
  v_fma_dx9_zero_f32 v68, v67, v1, v0
  v_mov_b32 v69, 0
  v_div_fixup_f32 v70, v0, v69, v1
  v_mov_b32 v100, 0
  global_store_b32 v100, v20, s[0:1]
  v_mov_b32 v101, 4
  global_store_b32 v101, v21, s[0:1]
  v_mov_b32 v102, 8
  global_store_b32 v102, v22, s[0:1]
  v_mov_b32 v103, 12
  global_store_b32 v103, v23, s[0:1]
  v_mov_b32 v104, 16
  global_store_b32 v104, v24, s[0:1]
  v_mov_b32 v105, 20
  global_store_b32 v105, v25, s[0:1]
  v_mov_b32 v106, 24
  global_store_b32 v106, v26, s[0:1]
  v_mov_b32 v107, 28
  global_store_b32 v107, v27, s[0:1]
  v_mov_b32 v108, 32
  global_store_b32 v108, v28, s[0:1]
  v_mov_b32 v109, 36
  global_store_b32 v109, v29, s[0:1]
  v_mov_b32 v110, 40
  global_store_b32 v110, v31, s[0:1]
  v_mov_b32 v111, 44
  global_store_b32 v111, v32, s[0:1]
  v_mov_b32 v112, 48
  global_store_b32 v112, v33, s[0:1]
  v_mov_b32 v113, 52
  global_store_b32 v113, v34, s[0:1]
  v_mov_b32 v114, 56
  global_store_b32 v114, v35, s[0:1]
  v_mov_b32 v115, 60
  global_store_b32 v115, v36, s[0:1]
  v_mov_b32 v116, 64
  global_store_b32 v116, v37, s[0:1]
  v_mov_b32 v117, 68
  global_store_b32 v117, v38, s[0:1]
  v_mov_b32 v118, 72
  global_store_b32 v118, v42, s[0:1]
  v_mov_b32 v119, 76
  global_store_b32 v119, v44, s[0:1]
  v_mov_b32 v120, 80
  global_store_b32 v120, v45, s[0:1]
  v_mov_b32 v121, 84
  global_store_b32 v121, v46, s[0:1]
  v_mov_b32 v122, 88
  global_store_b32 v122, v48, s[0:1]
  v_mov_b32 v123, 92
  global_store_b32 v123, v51, s[0:1]
  v_mov_b32 v124, 96
  global_store_b32 v124, v55, s[0:1]
  v_mov_b32 v125, 100
  global_store_b32 v125, v56, s[0:1]
  v_mov_b32 v126, 104
  global_store_b32 v126, v58, s[0:1]
  v_mov_b32 v127, 108
  global_store_b32 v127, v61, s[0:1]
  v_mov_b32 v128, 112
  global_store_b32 v128, v62, s[0:1]
  v_mov_b32 v129, 116
  global_store_b32 v129, v63, s[0:1]
  v_mov_b32 v130, 120
  global_store_b32 v130, v64, s[0:1]
  v_mov_b32 v131, 124
  global_store_b32 v131, v65, s[0:1]
  v_mov_b32 v132, 128
  global_store_b32 v132, v66, s[0:1]
  v_mov_b32 v133, 132
  global_store_b32 v133, v68, s[0:1]
  v_mov_b32 v134, 136
  global_store_b32 v134, v70, s[0:1]
  s_endpgm
