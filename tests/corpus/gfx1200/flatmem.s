.text
.globl flatmem
flatmem:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0
  v_mov_b32 v8, 0x11223344
  v_mov_b32 v2, 16
  v_mov_b32 v3, 0
  v_mov_b32 v9, 0x55667788
  flat_store_b32 v[0:1], v8
  flat_store_b32 v[2:3], v9
  flat_load_b32 v4, v[0:1]
  flat_load_b32 v5, v[2:3]
  global_load_u8 v6, v0, s[0:1] offset:1
  global_load_i8 v7, v0, s[0:1] offset:1
  global_load_u16 v10, v0, s[0:1] offset:2
  global_load_i16 v11, v0, s[0:1] offset:2
  v_mov_b32 v12, 32
  v_mov_b32 v13, 0
  v_mov_b32 v14, 5
  flat_store_b32 v[12:13], v14
  v_mov_b32 v15, 3
  flat_atomic_add_u32 v16, v[12:13], v15 th:TH_ATOMIC_RETURN
  flat_load_b32 v17, v[12:13]
  flat_atomic_max_u32 v18, v[12:13], v15 th:TH_ATOMIC_RETURN
  flat_load_b32 v19, v[12:13]
  v_mov_b32 v20, 0xffffff00
  flat_atomic_and_b32 v21, v[12:13], v20 th:TH_ATOMIC_RETURN
  v_mov_b32 v22, 0xff
  flat_atomic_or_b32 v23, v[12:13], v22 th:TH_ATOMIC_RETURN
  flat_load_b32 v24, v[12:13]
  v_mov_b32 v25, 48
  v_mov_b32 v26, 0
  v_mov_b32 v27, 2
  v_mov_b32 v28, 1
  flat_store_b64 v[25:26], v[27:28]
  flat_load_b64 v[29:30], v[25:26]
  v_mov_b32 v31, 64
  v_mov_b32 v32, 0
  v_mov_b32 v33, 1
  v_mov_b32 v34, 0
  flat_store_b32 v[31:32], v34
  v_mov_b32 v35, 1
  v_mov_b32 v36, 7
  flat_atomic_cmpswap_b32 v37, v[31:32], v[35:36] th:TH_ATOMIC_RETURN
  flat_load_b32 v38, v[31:32]
  v_mov_b32 v40, 80
  v_mov_b32 v41, 0
  v_mov_b32 v42, 0x0c
  v_mov_b32 v43, 0x0d
  v_mov_b32 v44, 0x0e
  v_mov_b32 v45, 0x0f
  flat_store_b128 v[40:41], v[42:45]
  flat_load_b128 v[46:49], v[40:41]
  v_mov_b32 v50, 0
  global_store_b32 v50, v4, s[0:1]
  v_mov_b32 v51, 4
  global_store_b32 v51, v5, s[0:1]
  v_mov_b32 v52, 8
  global_store_b32 v52, v6, s[0:1]
  v_mov_b32 v53, 12
  global_store_b32 v53, v7, s[0:1]
  v_mov_b32 v54, 16
  global_store_b32 v54, v10, s[0:1]
  v_mov_b32 v55, 20
  global_store_b32 v55, v11, s[0:1]
  v_mov_b32 v56, 24
  global_store_b32 v56, v16, s[0:1]
  v_mov_b32 v57, 28
  global_store_b32 v57, v17, s[0:1]
  v_mov_b32 v58, 32
  global_store_b32 v58, v18, s[0:1]
  v_mov_b32 v59, 36
  global_store_b32 v59, v19, s[0:1]
  v_mov_b32 v60, 40
  global_store_b32 v60, v21, s[0:1]
  v_mov_b32 v61, 44
  global_store_b32 v61, v24, s[0:1]
  v_mov_b32 v62, 48
  global_store_b32 v62, v29, s[0:1]
  v_mov_b32 v63, 52
  global_store_b32 v63, v30, s[0:1]
  v_mov_b32 v64, 56
  global_store_b32 v64, v37, s[0:1]
  v_mov_b32 v65, 60
  global_store_b32 v65, v38, s[0:1]
  v_mov_b32 v66, 64
  global_store_b32 v66, v46, s[0:1]
  v_mov_b32 v67, 68
  global_store_b32 v67, v49, s[0:1]
  s_endpgm
