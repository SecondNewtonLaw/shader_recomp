; Geometry stage fixture with geometry inputs: reads the three input vertices
; from the GS input ring and emits a half-size copy of the triangle, passing
; the interpolated colour through. Each vertex's ring address is vertex*256
; bytes; dword 0..3 is position and dword 4..7 is location 0.
.text
.globl geometry_read
geometry_read:
  ; vertex 0
  v_mov_b32 v0, 0
  ds_read_b32 v1, v0 offset:0
  ds_read_b32 v2, v0 offset:4
  ds_read_b32 v3, v0 offset:8
  ds_read_b32 v4, v0 offset:12
  ds_read_b32 v5, v0 offset:16
  ds_read_b32 v6, v0 offset:20
  ds_read_b32 v7, v0 offset:24
  ds_read_b32 v8, v0 offset:28
  v_mul_f32 v1, 0.5, v1
  v_mul_f32 v2, 0.5, v2
  exp param0 v5, v6, v7, v8
  exp pos0 v1, v2, v3, v4
  ; vertex 1
  v_mov_b32 v0, 0x100
  ds_read_b32 v1, v0 offset:0
  ds_read_b32 v2, v0 offset:4
  ds_read_b32 v3, v0 offset:8
  ds_read_b32 v4, v0 offset:12
  ds_read_b32 v5, v0 offset:16
  ds_read_b32 v6, v0 offset:20
  ds_read_b32 v7, v0 offset:24
  ds_read_b32 v8, v0 offset:28
  v_mul_f32 v1, 0.5, v1
  v_mul_f32 v2, 0.5, v2
  exp param0 v5, v6, v7, v8
  exp pos0 v1, v2, v3, v4
  ; vertex 2
  v_mov_b32 v0, 0x200
  ds_read_b32 v1, v0 offset:0
  ds_read_b32 v2, v0 offset:4
  ds_read_b32 v3, v0 offset:8
  ds_read_b32 v4, v0 offset:12
  ds_read_b32 v5, v0 offset:16
  ds_read_b32 v6, v0 offset:20
  ds_read_b32 v7, v0 offset:24
  ds_read_b32 v8, v0 offset:28
  v_mul_f32 v1, 0.5, v1
  v_mul_f32 v2, 0.5, v2
  exp param0 v5, v6, v7, v8
  exp pos0 v1, v2, v3, v4
  exp prim v0, v0, v0, v0
  s_endpgm
