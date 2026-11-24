.text
.globl cmpwide
cmpwide:
  v_mov_b32 v0, 0x3c00
  v_mov_b32 v1, 0x4000
  v_mov_b32 v20, 1
  v_mov_b32 v21, 0
  v_cmp_lt_f16 v0, v1
  v_cndmask_b32 v2, v20, v21
  v_cmp_eq_f16 v0, v1
  v_cndmask_b32 v3, v20, v21
  v_cmp_neq_f16 v0, v1
  v_cndmask_b32 v4, v20, v21
  v_cmp_o_f16 v0, v1
  v_cndmask_b32 v5, v20, v21
  v_cmp_u_f16 v0, v1
  v_cndmask_b32 v6, v20, v21
  v_mov_b32 v10, 0xffff
  v_mov_b32 v11, 1
  v_cmp_lt_i16 v10, v11
  v_cndmask_b32 v7, v20, v21
  v_cmp_gt_u16 v10, v11
  v_cndmask_b32 v8, v20, v21
  v_mov_b32 v12, -1
  v_mov_b32 v13, -1
  v_mov_b32 v14, 0
  v_mov_b32 v15, 0
  v_cmp_lt_i64 v[12:13], v[14:15]
  v_cndmask_b32 v9, v20, v21
  v_cmp_gt_u64 v[12:13], v[14:15]
  v_cndmask_b32 v16, v20, v21
  v_mov_b32 v24, 0
  v_mov_b32 v25, 0x3ff80000
  v_mov_b32 v26, 0
  v_mov_b32 v27, 0x40000000
  v_cmp_lt_f64 v[24:25], v[26:27]
  v_cndmask_b32 v28, v20, v21
  v_cmp_o_f64 v[24:25], v[26:27]
  v_cndmask_b32 v29, v20, v21
  v_cmpx_lt_f16 v0, v1
  s_cbranch_execnz f16_ok
  v_mov_b32 v30, 100
  s_branch f16_done
f16_ok:
  v_mov_b32 v30, 7
f16_done:
  v_cmpx_lt_i64 v[12:13], v[14:15]
  s_cbranch_execnz i64_ok
  v_mov_b32 v31, 100
  s_branch store
i64_ok:
  v_mov_b32 v31, 8
store:
  v_mov_b32 v40, 0
  global_store_b32 v40, v2, s[0:1]
  v_mov_b32 v41, 4
  global_store_b32 v41, v3, s[0:1]
  v_mov_b32 v42, 8
  global_store_b32 v42, v4, s[0:1]
  v_mov_b32 v43, 12
  global_store_b32 v43, v5, s[0:1]
  v_mov_b32 v44, 16
  global_store_b32 v44, v6, s[0:1]
  v_mov_b32 v45, 20
  global_store_b32 v45, v7, s[0:1]
  v_mov_b32 v46, 24
  global_store_b32 v46, v8, s[0:1]
  v_mov_b32 v47, 28
  global_store_b32 v47, v9, s[0:1]
  v_mov_b32 v48, 32
  global_store_b32 v48, v16, s[0:1]
  v_mov_b32 v49, 36
  global_store_b32 v49, v28, s[0:1]
  v_mov_b32 v50, 40
  global_store_b32 v50, v29, s[0:1]
  v_mov_b32 v51, 44
  global_store_b32 v51, v30, s[0:1]
  v_mov_b32 v52, 48
  global_store_b32 v52, v31, s[0:1]
  s_endpgm
