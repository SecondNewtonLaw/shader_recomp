; Geometry stage fixture that publishes vertices by message: each vertex's
; position and colour are staged as four-component registers in local memory
; (slot = dword / 8, register = dword / 4 within the slot) and s_sendmsg
; GS_OP_EMIT copies the staged slot to the outputs and emits it. The last
; message is GS_OP_EMIT_CUT, so the strip closes with the vertex.
.text
.globl geometry_staged
geometry_staged:
  v_mov_b32 v0, 0
  ; vertex 0: position (-1, -1, 0, 1), colour red
  v_mov_b32 v1, -1.0
  v_mov_b32 v2, -1.0
  v_mov_b32 v3, 0
  v_mov_b32 v4, 1.0
  v_mov_b32 v5, 1.0
  v_mov_b32 v6, 0
  v_mov_b32 v7, 0
  v_mov_b32 v8, 1.0
  ds_write_b32 v0, v1 offset:0
  ds_write_b32 v0, v2 offset:4
  ds_write_b32 v0, v3 offset:8
  ds_write_b32 v0, v4 offset:12
  ds_write_b32 v0, v5 offset:16
  ds_write_b32 v0, v6 offset:20
  ds_write_b32 v0, v7 offset:24
  ds_write_b32 v0, v8 offset:28
  s_sendmsg 0x22
  ; vertex 1: position (3, -1, 0, 1)
  v_mov_b32 v1, 3.0
  v_mov_b32 v2, -1.0
  ds_write_b32 v0, v1 offset:32
  ds_write_b32 v0, v2 offset:36
  ds_write_b32 v0, v3 offset:40
  ds_write_b32 v0, v4 offset:44
  ds_write_b32 v0, v5 offset:48
  ds_write_b32 v0, v6 offset:52
  ds_write_b32 v0, v7 offset:56
  ds_write_b32 v0, v8 offset:60
  s_sendmsg 0x22
  ; vertex 2: position (-1, 3, 0, 1); this message also ends the strip
  v_mov_b32 v1, -1.0
  v_mov_b32 v2, 3.0
  ds_write_b32 v0, v1 offset:64
  ds_write_b32 v0, v2 offset:68
  ds_write_b32 v0, v3 offset:72
  ds_write_b32 v0, v4 offset:76
  ds_write_b32 v0, v5 offset:80
  ds_write_b32 v0, v6 offset:84
  ds_write_b32 v0, v7 offset:88
  ds_write_b32 v0, v8 offset:92
  s_sendmsg 0x32
  s_sendmsg 0x3
  s_endpgm
