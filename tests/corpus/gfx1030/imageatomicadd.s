; Image atomic fixture: add one to texel (0,0) through the T# in s0..s3 and
; store the previous value. The module declares the surface R32ui (the format
; arrives with --image-format), so it may only be bound to an R32_UINT view:
; this fixture is translated and compiled by the driver, never executed.
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
