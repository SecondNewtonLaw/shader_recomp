.text
.globl vmore
vmore:
  v_mov_b32 v0, 1
  v_mov_b32 v1, 2
  s_mov_b32 s10, 1
  v_cndmask_b32 v10, v0, v1, s10
  v_mov_b32 v2, 1.5
  v_mov_b32 v3, 2
  v_mov_b32 v4, 0
  v_cvt_pk_u8_f32 v11, v2, v3, v4
  v_mov_b32 v5, 0x00050008
  v_mov_b32 v6, 0x00030001
  v_mov_b32 v7, 4
  v_sad_u16 v12, v5, v6, v7
  v_mov_b32 v8, 4
  v_mov_b32 v20, 0x80000001
  v_mov_b32 v21, 0x80000000
  v_lshrrev_b64 v[22:23], v8, v[20:21]
  v_mov_b32 v24, 0
  global_store_b32 v24, v10, s[0:1]
  v_mov_b32 v25, 4
  global_store_b32 v25, v11, s[0:1]
  v_mov_b32 v26, 8
  global_store_b32 v26, v12, s[0:1]
  v_mov_b32 v27, 12
  global_store_b32 v27, v22, s[0:1]
  v_mov_b32 v28, 16
  global_store_b32 v28, v23, s[0:1]
  s_endpgm
