; Fragment stage fixture: reads the interpolated attribute and exports red.
.text
.globl solid
solid:
  v_interp_p2_f32 v1, v0, attr0.x
  v_mov_b32 v2, 0
  v_mov_b32 v3, 0
  v_mov_b32 v4, 1.0
  exp mrt0 v1, v2, v3, v4
  s_endpgm
