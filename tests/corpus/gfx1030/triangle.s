; Vertex stage fixture: builds a triangle from the vertex index and passes a
; colour to the fragment stage. v0 carries the vertex index (GSF ABI).
.text
.globl triangle
triangle:
  v_cvt_f32_u32 v1, v0
  v_mul_f32 v1, v1, 2.0
  v_sub_f32 v1, v1, 1.0
  v_and_b32 v2, v0, 1
  v_cvt_f32_u32 v2, v2
  v_mul_f32 v2, v2, 2.0
  v_sub_f32 v2, v2, 1.0
  v_mov_b32 v3, 0
  v_mov_b32 v4, 1.0
  exp pos0 v1, v2, v3, v4
  v_mov_b32 v5, 1.0
  v_mov_b32 v6, 0
  v_mov_b32 v7, 0
  v_mov_b32 v8, 1.0
  exp param0 v5, v6, v7, v8
  s_endpgm
