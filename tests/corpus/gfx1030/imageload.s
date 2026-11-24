; Image fixture: load texel (0,0) through the T# in s0..s3 and copy the four
; components to the test buffer.
.text
.globl imageload
imageload:
  v_mov_b32 v4, 0
  v_mov_b32 v5, 0
  image_load v[0:3], v[4:5], s[0:7] dmask:0xf dim:2d
  v_mov_b32 v8, 0
  global_store_dword v8, v0, s[0:1]
  v_mov_b32 v9, 4
  global_store_dword v9, v1, s[0:1]
  v_mov_b32 v10, 8
  global_store_dword v10, v2, s[0:1]
  v_mov_b32 v11, 12
  global_store_dword v11, v3, s[0:1]
  s_endpgm
