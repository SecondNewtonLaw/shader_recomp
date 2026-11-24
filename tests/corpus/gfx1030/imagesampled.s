; Gradient sample fixture: sample texel (0,0) with an explicit gradient pair
; through the T#/S# pair. Zero gradients leave the level at zero, so a
; single-level surface returns the texel itself.
.text
.globl imagesampled
imagesampled:
  v_mov_b32 v4, 0.125
  v_mov_b32 v5, 0.125
  v_mov_b32 v6, 0
  v_mov_b32 v7, 0
  v_mov_b32 v8, 0
  v_mov_b32 v9, 0
  image_sample_d v[0:3], v[4:9], s[0:7], s[8:11] dmask:0xf dim:2d
  v_mov_b32 v10, 0
  global_store_dword v10, v0, s[0:1]
  v_mov_b32 v11, 4
  global_store_dword v11, v1, s[0:1]
  v_mov_b32 v12, 8
  global_store_dword v12, v2, s[0:1]
  v_mov_b32 v13, 12
  global_store_dword v13, v3, s[0:1]
  s_endpgm
