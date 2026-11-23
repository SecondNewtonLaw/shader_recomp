.text
.globl vop1
vop1:
  v_mov_b32 v0, 2.75
  v_mov_b32 v1, 2.6
  v_mov_b32 v2, 0x44332211
  v_mov_b32 v3, 0x0000ffff
  v_fract_f32 v4, v0
  v_trunc_f32 v5, v0
  v_ceil_f32 v6, v0
  v_rndne_f32 v7, v0
  v_floor_f32 v8, v0
  v_cvt_f32_ubyte2 v9, v2
  v_cvt_nearest_i32_f32 v10, v1
  v_cvt_floor_i32_f32 v11, v1
  v_cvt_i32_i16 v12, v3
  v_cvt_u32_u16 v13, v3
  v_mov_b32 v15, 0x000000ff
  v_cls_i32 v14, v15
  v_mov_b32 v17, -1
  v_cls_i32 v16, v17
  v_mov_b32 v19, 1
  v_bfrev_b32 v18, v19
  v_sin_f32 v20, v0
  v_cos_f32 v21, v0
  v_mov_b32 v20, 0
  v_mov_b32 v21, 0
  v_readfirstlane_b32 s0, v0
  v_mov_b32 v22, s0
  v_nop
  v_cvt_u32_f32 v4, v4
  v_mov_b32 v30, 0
  global_store_b32 v30, v4, s[0:1]
  v_cvt_u32_f32 v5, v5
  v_mov_b32 v31, 4
  global_store_b32 v31, v5, s[0:1]
  v_cvt_u32_f32 v6, v6
  v_mov_b32 v32, 8
  global_store_b32 v32, v6, s[0:1]
  v_cvt_u32_f32 v7, v7
  v_mov_b32 v33, 12
  global_store_b32 v33, v7, s[0:1]
  v_cvt_u32_f32 v8, v8
  v_mov_b32 v34, 16
  global_store_b32 v34, v8, s[0:1]
  v_cvt_u32_f32 v9, v9
  v_mov_b32 v35, 20
  global_store_b32 v35, v9, s[0:1]
  v_mov_b32 v36, 24
  global_store_b32 v36, v10, s[0:1]
  v_mov_b32 v37, 28
  global_store_b32 v37, v11, s[0:1]
  v_mov_b32 v38, 32
  global_store_b32 v38, v12, s[0:1]
  v_mov_b32 v39, 36
  global_store_b32 v39, v13, s[0:1]
  v_mov_b32 v40, 40
  global_store_b32 v40, v14, s[0:1]
  v_mov_b32 v41, 44
  global_store_b32 v41, v16, s[0:1]
  v_mov_b32 v42, 48
  global_store_b32 v42, v18, s[0:1]
  v_mov_b32 v43, 52
  global_store_b32 v43, v22, s[0:1]
  s_endpgm
