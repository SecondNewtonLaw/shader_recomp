; Explicit-level sample fixture: sample texel (0,0) with an explicit level
; through the T#/S# pair and copy it to the test buffer. The bound surface has
; one level, so a level-zero sample is the texel itself.
.text
.globl imagemip
imagemip:
  v_mov_b32 v8, 0.125
  v_mov_b32 v9, 0.125
  v_mov_b32 v10, 0
  image_sample_l v[4:7], v[8:10], s[0:7], s[8:11] dmask:0xf dim:2d
  v_mov_b32 v11, 0
  global_store_dword v11, v4, s[0:1]
  v_mov_b32 v12, 4
  global_store_dword v12, v5, s[0:1]
  v_mov_b32 v13, 8
  global_store_dword v13, v6, s[0:1]
  v_mov_b32 v14, 12
  global_store_dword v14, v7, s[0:1]
  s_endpgm
