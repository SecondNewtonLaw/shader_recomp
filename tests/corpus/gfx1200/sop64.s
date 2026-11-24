.text
.globl sop64
sop64:
  s_mov_b32 s0, 0x00000080
  s_sext_i32_i8 s1, s0
  s_sext_i32_i16 s2, s0
  s_mov_b32 s3, 0x80000001
  s_brev_b64 s[4:5], s[2:3]
  s_mov_b32 s6, 0x00ff00ff
  s_bcnt0_i32_b32 s7, s6
  s_bcnt0_i32_b64 s8, s[2:3]
  s_bcnt1_i32_b64 s9, s[2:3]
  s_mov_b32 s10, 5
  s_mov_b32 s11, 0
  s_bitset1_b32 s11, s10
  s_bitset0_b32 s11, s10
  s_mov_b32 s12, 0xffffffff
  s_mov_b32 s13, 0xffffffff
  s_bitset0_b64 s[12:13], s10
  s_bitset1_b64 s[12:13], s10
  s_mov_b32 s14, 0x00000003
  s_mov_b32 s15, 0x00000000
  s_lshl_b64 s[16:17], s[14:15], 4
  s_lshr_b64 s[18:19], s[16:17], 2
  s_ashr_i64 s[20:21], s[18:19], 1
  s_bfm_b64 s[22:23], 8, 16
  s_bfe_u64 s[24:25], s[22:23], 0x00080008
  s_bfe_i64 s[26:27], s[22:23], 0x00080008
  s_mul_hi_i32 s28, 0x40000000, 4
  s_mul_hi_u32 s29, 0x80000000, 2
  s_flbit_i32 s30, s3
  s_flbit_i32_b64 s31, s[2:3]
  s_flbit_i32_i64 s32, s[2:3]
  v_mov_b32 v0, s1
  v_mov_b32 v1, 0
  global_store_b32 v1, v0, s[0:1]
  v_mov_b32 v2, s2
  v_mov_b32 v3, 4
  global_store_b32 v3, v2, s[0:1]
  v_mov_b32 v4, s4
  v_mov_b32 v5, 8
  global_store_b32 v5, v4, s[0:1]
  v_mov_b32 v6, s5
  v_mov_b32 v7, 12
  global_store_b32 v7, v6, s[0:1]
  v_mov_b32 v8, s7
  v_mov_b32 v9, 16
  global_store_b32 v9, v8, s[0:1]
  v_mov_b32 v10, s8
  v_mov_b32 v11, 20
  global_store_b32 v11, v10, s[0:1]
  v_mov_b32 v12, s9
  v_mov_b32 v13, 24
  global_store_b32 v13, v12, s[0:1]
  v_mov_b32 v14, s11
  v_mov_b32 v15, 28
  global_store_b32 v15, v14, s[0:1]
  v_mov_b32 v16, s12
  v_mov_b32 v17, 32
  global_store_b32 v17, v16, s[0:1]
  v_mov_b32 v18, s13
  v_mov_b32 v19, 36
  global_store_b32 v19, v18, s[0:1]
  v_mov_b32 v20, s16
  v_mov_b32 v21, 40
  global_store_b32 v21, v20, s[0:1]
  v_mov_b32 v22, s17
  v_mov_b32 v23, 44
  global_store_b32 v23, v22, s[0:1]
  v_mov_b32 v24, s18
  v_mov_b32 v25, 48
  global_store_b32 v25, v24, s[0:1]
  v_mov_b32 v26, s19
  v_mov_b32 v27, 52
  global_store_b32 v27, v26, s[0:1]
  v_mov_b32 v28, s20
  v_mov_b32 v29, 56
  global_store_b32 v29, v28, s[0:1]
  v_mov_b32 v30, s21
  v_mov_b32 v31, 60
  global_store_b32 v31, v30, s[0:1]
  v_mov_b32 v32, s22
  v_mov_b32 v33, 64
  global_store_b32 v33, v32, s[0:1]
  v_mov_b32 v34, s23
  v_mov_b32 v35, 68
  global_store_b32 v35, v34, s[0:1]
  v_mov_b32 v36, s24
  v_mov_b32 v37, 72
  global_store_b32 v37, v36, s[0:1]
  v_mov_b32 v38, s26
  v_mov_b32 v39, 76
  global_store_b32 v39, v38, s[0:1]
  v_mov_b32 v40, s28
  v_mov_b32 v41, 80
  global_store_b32 v41, v40, s[0:1]
  v_mov_b32 v42, s29
  v_mov_b32 v43, 84
  global_store_b32 v43, v42, s[0:1]
  v_mov_b32 v44, s30
  v_mov_b32 v45, 88
  global_store_b32 v45, v44, s[0:1]
  v_mov_b32 v46, s31
  v_mov_b32 v47, 92
  global_store_b32 v47, v46, s[0:1]
  v_mov_b32 v48, s32
  v_mov_b32 v49, 96
  global_store_b32 v49, v48, s[0:1]
  s_endpgm
