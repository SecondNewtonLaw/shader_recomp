; Image resinfo fixture: query the surface size through the T# in s0..s3 and
; copy width, height and depth to the test buffer.
.text
.globl imageresinfo
imageresinfo:
  v_mov_b32 v4, 0
  image_get_resinfo v[0:2], v4, s[0:7] dmask:0x7 dim:2d
  v_mov_b32 v8, 0
  global_store_dword v8, v0, s[0:1]
  v_mov_b32 v9, 4
  global_store_dword v9, v1, s[0:1]
  v_mov_b32 v10, 8
  global_store_dword v10, v2, s[0:1]
  s_endpgm
