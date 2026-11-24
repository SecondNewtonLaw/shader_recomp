.text
.globl packed
packed:
  v_mov_b32 v0, 0x40003c00
  v_mov_b32 v1, 0x44004200
  v_pk_add_f16 v2, v0, v1
  v_pk_mul_f16 v3, v0, v1
  v_pk_min_num_f16 v4, v0, v1
  v_pk_max_num_f16 v5, v0, v1
  v_pk_fma_f16 v6, v0, v1, v0
  v_mov_b32 v7, 0x00030002
  v_mov_b32 v8, 0x00050004
  v_pk_add_i16 v9, v7, v8
  v_pk_sub_i16 v10, v7, v8
  v_pk_mul_lo_u16 v11, v7, v8
  v_pk_min_i16 v12, v7, v8
  v_pk_max_u16 v13, v7, v8
  v_pk_lshlrev_b16 v14, v7, v8
  v_mov_b32 v15, 0x0000fff8
  v_pk_ashrrev_i16 v16, v7, v15
  v_pk_mad_i16 v17, v7, v8, v7
  v_mov_b32 v20, 0
  global_store_b32 v20, v2, s[0:1]
  v_mov_b32 v21, 4
  global_store_b32 v21, v3, s[0:1]
  v_mov_b32 v22, 8
  global_store_b32 v22, v4, s[0:1]
  v_mov_b32 v23, 12
  global_store_b32 v23, v5, s[0:1]
  v_mov_b32 v24, 16
  global_store_b32 v24, v6, s[0:1]
  v_mov_b32 v25, 20
  global_store_b32 v25, v9, s[0:1]
  v_mov_b32 v26, 24
  global_store_b32 v26, v10, s[0:1]
  v_mov_b32 v27, 28
  global_store_b32 v27, v11, s[0:1]
  v_mov_b32 v28, 32
  global_store_b32 v28, v12, s[0:1]
  v_mov_b32 v29, 36
  global_store_b32 v29, v13, s[0:1]
  v_mov_b32 v30, 40
  global_store_b32 v30, v14, s[0:1]
  v_mov_b32 v31, 44
  global_store_b32 v31, v16, s[0:1]
  v_mov_b32 v32, 48
  global_store_b32 v32, v17, s[0:1]
  s_endpgm
