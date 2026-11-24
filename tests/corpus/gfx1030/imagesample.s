; Image sample fixture: sample texel (0,0) through the T#/S# pair with
; normalized coordinates and copy the four components to the test buffer.
.text
.globl imagesample
imagesample:
  v_mov_b32 v4, 0.125
  v_mov_b32 v5, 0.125
  image_sample v[0:3], v[4:5], s[0:7], s[8:11] dmask:0xf dim:2d
  v_mov_b32 v8, 0
  global_store_dword v8, v0, s[0:1]
  v_mov_b32 v9, 4
  global_store_dword v9, v1, s[0:1]
  v_mov_b32 v10, 8
  global_store_dword v10, v2, s[0:1]
  v_mov_b32 v11, 12
  global_store_dword v11, v3, s[0:1]
  s_endpgm
