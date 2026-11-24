.text
.globl smem
smem:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x11223344
  global_store_b32 v0, v1, s[0:1]
  v_mov_b32 v2, 4
  v_mov_b32 v3, 0x55667788
  global_store_b32 v2, v3, s[0:1]
  v_mov_b32 v4, 8
  v_mov_b32 v5, 0x99aabbcc
  global_store_b32 v4, v5, s[0:1]
  v_mov_b32 v6, 12
  v_mov_b32 v7, 0xddeeff00
  global_store_b32 v6, v7, s[0:1]
  s_mov_b32 s0, 0
  s_mov_b32 s1, 0
  s_load_b32 s4, s[0:1], 0
  s_load_b64 s[6:7], s[0:1], 0
  s_load_b128 s[8:11], s[0:1], 0
  s_load_u8 s12, s[0:1], 1
  s_load_i8 s13, s[0:1], 1
  s_load_u16 s14, s[0:1], 2
  s_load_b32 s15, s[0:1], 12
  v_mov_b32 v10, 64
  v_mov_b32 v11, s4
  global_store_b32 v10, v11, s[0:1]
  v_mov_b32 v12, 68
  v_mov_b32 v13, s7
  global_store_b32 v12, v13, s[0:1]
  v_mov_b32 v14, 72
  v_mov_b32 v15, s11
  global_store_b32 v14, v15, s[0:1]
  v_mov_b32 v16, 76
  v_mov_b32 v17, s12
  global_store_b32 v16, v17, s[0:1]
  v_mov_b32 v18, 80
  v_mov_b32 v19, s13
  global_store_b32 v18, v19, s[0:1]
  v_mov_b32 v20, 84
  v_mov_b32 v21, s14
  global_store_b32 v20, v21, s[0:1]
  v_mov_b32 v22, 88
  v_mov_b32 v23, s15
  global_store_b32 v22, v23, s[0:1]
  s_endpgm
