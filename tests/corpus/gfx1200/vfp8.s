.text
.globl vfp8
vfp8:
  v_mov_b32 v0, 0x3e3c3f3e
  v_cvt_f32_fp8 v10, v0
  v_cvt_f32_bf8 v11, v0
  v_cvt_pk_f32_fp8 v[12:13], v0
  v_cvt_pk_f32_bf8 v[14:15], v0
  v_mov_b32 v16, 0
  global_store_b32 v16, v10, s[0:1]
  v_mov_b32 v17, 4
  global_store_b32 v17, v11, s[0:1]
  v_mov_b32 v18, 8
  global_store_b32 v18, v12, s[0:1]
  v_mov_b32 v19, 12
  global_store_b32 v19, v13, s[0:1]
  v_mov_b32 v20, 16
  global_store_b32 v20, v14, s[0:1]
  v_mov_b32 v21, 20
  global_store_b32 v21, v15, s[0:1]
  s_endpgm
