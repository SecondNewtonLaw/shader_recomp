.text
.globl vdot
vdot:
  v_mov_b32 v0, 0x3c003800
  v_mov_b32 v1, 0x40003c00
  v_mov_b32 v2, 0x41200000
  v_dot2_f32_f16 v10, v0, v1, v2
  v_mov_b32 v3, 0x3f803f80
  v_mov_b32 v4, 0x40004000
  v_mov_b32 v5, 0
  v_dot2_f32_bf16 v11, v3, v4, v5
  v_mov_b32 v6, 0x01020304
  v_mov_b32 v7, 0x01010101
  v_mov_b32 v8, 0
  v_dot4_i32_iu8 v12, v6, v7, v8
  v_dot4_u32_u8 v13, v6, v7, v8
  v_mov_b32 v9, 0x11111111
  v_dot8_i32_iu4 v14, v9, v9, v8
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
  s_endpgm
