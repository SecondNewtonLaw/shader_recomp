; Image atomic fixture: add one to texel (0,0) through the T# in s0..s3 and
; store the previous value. The texel pointer is R32ui, so the surface has to
; be an integer view; the driver probe checks that the module compiles.
.text
.globl imageatomicadd
imageatomicadd:
  v_mov_b32 v4, 0
  v_mov_b32 v5, 0
  v_mov_b32 v0, 1
  image_atomic_add v0, v[4:5], s[0:7] dmask:0x1 dim:2d
  v_mov_b32 v8, 0
  global_store_dword v8, v0, s[0:1]
  s_endpgm
