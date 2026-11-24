.text
.globl lds
lds:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x11223344
  ds_store_b32 v0, v1
  v_mov_b32 v2, 4
  v_mov_b32 v3, 0x55667788
  ds_store_b32 v2, v3
  ds_load_b32 v4, v0
  ds_load_b32 v5, v0 offset:4
  v_mov_b32 v6, 8
  v_mov_b32 v7, 5
  ds_store_b32 v6, v7
  v_mov_b32 v8, 3
  ds_add_rtn_u32 v9, v6, v8
  ds_load_b32 v10, v6
  ds_sub_u32 v6, v8
  ds_load_b32 v11, v6
  v_mov_b32 v12, 10
  ds_min_u32 v6, v12
  ds_max_u32 v6, v12
  ds_load_b32 v13, v6
  v_mov_b32 v14, 0xff
  ds_and_b32 v6, v14
  ds_or_b32 v6, v14
  ds_xor_b32 v6, v14
  ds_load_b32 v15, v6
  v_mov_b32 v16, 12
  v_mov_b32 v17, 0xaabbccdd
  ds_store_b32 v16, v17
  ds_load_u8 v18, v16 offset:1
  ds_load_i8 v19, v16 offset:1
  ds_load_u16 v20, v16 offset:2
  ds_load_i16 v21, v16 offset:2
  v_mov_b32 v22, 0x1122
  ds_store_b16 v16, v22
  ds_load_b32 v23, v16
  v_mov_b32 v24, 16
  v_mov_b32 v25, 2
  v_mov_b32 v26, 1
  ds_store_b64 v24, v[25:26]
  ds_load_b64 v[27:28], v24
  v_mov_b32 v29, 0
  v_mov_b32 v30, 0
  ds_add_rtn_u64 v[31:32], v24, v[29:30]
  ds_load_b64 v[33:34], v24
  v_mov_b32 v40, 32
  v_mov_b32 v41, 0xa1
  v_mov_b32 v42, 0xa2
  v_mov_b32 v43, 0xa3
  v_mov_b32 v44, 0xa4
  ds_store_b128 v40, v[41:44]
  ds_load_b128 v[45:48], v40
  v_mov_b32 v50, 0
  global_store_b32 v50, v4, s[0:1]
  v_mov_b32 v51, 4
  global_store_b32 v51, v5, s[0:1]
  v_mov_b32 v52, 8
  global_store_b32 v52, v9, s[0:1]
  v_mov_b32 v53, 12
  global_store_b32 v53, v10, s[0:1]
  v_mov_b32 v54, 16
  global_store_b32 v54, v11, s[0:1]
  v_mov_b32 v55, 20
  global_store_b32 v55, v13, s[0:1]
  v_mov_b32 v56, 24
  global_store_b32 v56, v15, s[0:1]
  v_mov_b32 v57, 28
  global_store_b32 v57, v18, s[0:1]
  v_mov_b32 v58, 32
  global_store_b32 v58, v19, s[0:1]
  v_mov_b32 v59, 36
  global_store_b32 v59, v20, s[0:1]
  v_mov_b32 v60, 40
  global_store_b32 v60, v21, s[0:1]
  v_mov_b32 v61, 44
  global_store_b32 v61, v23, s[0:1]
  v_mov_b32 v62, 48
  global_store_b32 v62, v27, s[0:1]
  v_mov_b32 v63, 52
  global_store_b32 v63, v28, s[0:1]
  v_mov_b32 v64, 56
  global_store_b32 v64, v31, s[0:1]
  v_mov_b32 v65, 60
  global_store_b32 v65, v33, s[0:1]
  v_mov_b32 v66, 64
  global_store_b32 v66, v45, s[0:1]
  v_mov_b32 v67, 68
  global_store_b32 v67, v48, s[0:1]
  s_endpgm
