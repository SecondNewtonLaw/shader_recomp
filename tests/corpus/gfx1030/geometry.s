; Geometry stage fixture: builds a full-screen triangle from constants and
; passes a colour to the fragment stage. It reads no inputs, so it exercises
; the export model alone: each pos0 begins a vertex, exp prim closes the
; primitive.
.text
.globl geometry
geometry:
  ; vertex 0: position (-1, -1), colour red
  v_mov_b32 v5, 1.0
  v_mov_b32 v6, 0
  v_mov_b32 v7, 0
  v_mov_b32 v8, 1.0
  exp param0 v5, v6, v7, v8
  v_mov_b32 v1, -1.0
  v_mov_b32 v2, -1.0
  v_mov_b32 v3, 0
  v_mov_b32 v4, 1.0
  exp pos0 v1, v2, v3, v4
  ; vertex 1: position (3, -1); this pos0 emits vertex 0
  v_mov_b32 v1, 3.0
  exp pos0 v1, v2, v3, v4
  ; vertex 2: position (-1, 3); this pos0 emits vertex 1
  v_mov_b32 v1, -1.0
  v_mov_b32 v2, 3.0
  exp pos0 v1, v2, v3, v4
  ; exp prim emits vertex 2 and ends the triangle
  exp prim v0, v0, v0, v0
  s_endpgm
