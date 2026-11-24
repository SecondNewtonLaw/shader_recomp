.text
.globl vclass
vclass:
  v_mov_b32 v30, 0
  v_mov_b32 v31, 1
  v_mov_b32 v0, 0x3f800000
  v_mov_b32 v1, 0x100
  v_cmp_class_f32 v0, v1
  v_cndmask_b32 v10, v31, v30
  v_mov_b32 v2, 0x7f800000
  v_mov_b32 v3, 0x200
  v_cmp_class_f32 v2, v3
  v_cndmask_b32 v11, v31, v30
  v_mov_b32 v4, 0x7fc00000
  v_mov_b32 v5, 0x2
  v_cmp_class_f32 v4, v5
  v_cndmask_b32 v12, v31, v30
  v_mov_b32 v6, 0x00000001
  v_mov_b32 v7, 0x80
  v_cmp_class_f32 v6, v7
  v_cndmask_b32 v13, v31, v30
  v_mov_b32 v8, 0x80000000
  v_mov_b32 v9, 0x20
  v_cmp_class_f32 v8, v9
  v_cndmask_b32 v14, v31, v30
  v_mov_b32 v16, 0xbf800000
  v_mov_b32 v17, 0x8
  v_cmp_class_f32 v16, v17
  v_cndmask_b32 v15, v31, v30
  v_mov_b32 v18, 0x7f800001
  v_mov_b32 v19, 0x1
  v_cmp_class_f32 v18, v19
  v_cndmask_b32 v32, v31, v30
  v_mov_b32 v33, 0x00000400
  v_mov_b32 v34, 0x100
  v_cmp_class_f32 v33, v34
  v_cndmask_b32 v35, v31, v30
  v_mov_b32 v36, 0x400
  v_cmp_class_f32_e64 s20, v0, v36
  v_mov_b32 v37, s20
  v_mov_b32 v20, 0x11112222
  v_mov_b32 v21, 0x33334444
  v_swap_b32 v20, v21
  v_mov_b32 v22, 0x00001122
  v_sat_pk_u8_i16 v23, v22
  v_mov_b32 v24, 0x0000ffee
  v_sat_pk_u8_i16 v25, v24
  v_mov_b32 v40, 0
  global_store_b32 v40, v10, s[0:1]
  v_mov_b32 v41, 4
  global_store_b32 v41, v11, s[0:1]
  v_mov_b32 v42, 8
  global_store_b32 v42, v12, s[0:1]
  v_mov_b32 v43, 12
  global_store_b32 v43, v13, s[0:1]
  v_mov_b32 v44, 16
  global_store_b32 v44, v14, s[0:1]
  v_mov_b32 v45, 20
  global_store_b32 v45, v15, s[0:1]
  v_mov_b32 v46, 24
  global_store_b32 v46, v32, s[0:1]
  v_mov_b32 v47, 28
  global_store_b32 v47, v35, s[0:1]
  v_mov_b32 v48, 32
  global_store_b32 v48, v37, s[0:1]
  v_mov_b32 v49, 36
  global_store_b32 v49, v20, s[0:1]
  v_mov_b32 v50, 40
  global_store_b32 v50, v21, s[0:1]
  v_mov_b32 v51, 44
  global_store_b32 v51, v23, s[0:1]
  v_mov_b32 v52, 48
  global_store_b32 v52, v25, s[0:1]
  s_endpgm
