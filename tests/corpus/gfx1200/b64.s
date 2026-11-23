.text
.globl b64
b64:
  s_mov_b32 s2, 0x89abcdef
  s_mov_b32 s3, 0x01234567
  s_mov_b64 s[0:1], s[2:3]
  s_not_b64 s[4:5], s[0:1]
  s_and_b64 s[6:7], s[0:1], s[4:5]
  s_cselect_b64 s[8:9], s[0:1], s[10:11]
  s_or_b64 s[12:13], s[0:1], s[4:5]
  s_cselect_b64 s[14:15], s[0:1], s[10:11]
  v_mov_b32 v0, s0
  v_mov_b32 v1, 0
  global_store_b32 v1, v0, s[0:1]
  v_mov_b32 v2, s1
  v_mov_b32 v3, 4
  global_store_b32 v3, v2, s[0:1]
  v_mov_b32 v4, s4
  v_mov_b32 v5, 8
  global_store_b32 v5, v4, s[0:1]
  v_mov_b32 v6, s5
  v_mov_b32 v7, 12
  global_store_b32 v7, v6, s[0:1]
  v_mov_b32 v8, s6
  v_mov_b32 v9, 16
  global_store_b32 v9, v8, s[0:1]
  v_mov_b32 v10, s8
  v_mov_b32 v11, 20
  global_store_b32 v11, v10, s[0:1]
  v_mov_b32 v12, s12
  v_mov_b32 v13, 24
  global_store_b32 v13, v12, s[0:1]
  v_mov_b32 v14, s14
  v_mov_b32 v15, 28
  global_store_b32 v15, v14, s[0:1]
  s_endpgm
