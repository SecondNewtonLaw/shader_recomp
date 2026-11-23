; Portable straight-line fixture: s0 = 1.0f, v0 = s0, v1 = v0 + v0.
.text
.globl basic
basic:
  s_mov_b32 s0, 0x3f800000
  v_mov_b32 v0, s0
  v_add_f32 v1, v0, v0
  s_endpgm
