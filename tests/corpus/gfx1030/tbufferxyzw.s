; Typed buffer round trip: store four 32-bit floats with one typed buffer
; instruction, read them back at the same address and at +8 through the
; immediate offset, then copy both to the test buffer. The two words per
; instruction are hand-encoded (the assembler's gfx1030 MTBUF does not accept a
; format operand): opcode 3 load / 7 store, DFMT 4, NFMT 7 = format 116
; (32-bit float), VDATA in bits 8..15 of the second word.
.text
.globl tbufferxyzw
tbufferxyzw:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 1.0
  v_mov_b32 v2, 2.0
  v_mov_b32 v3, 3.0
  v_mov_b32 v4, 4.0
  ; tbuffer_store_format_xyzw v[1:4], v0, s[0:3], 0 offen offset:0
  .long 0xEA6F0000
  .long 0x80000100
  ; tbuffer_load_format_xyzw v[5:8], v0, s[0:3], 0 offen offset:0
  .long 0xEA6B0000
  .long 0x80000500
  ; tbuffer_store_format_xyzw v[1:4], v0, s[0:3], 0 offen offset:8
  .long 0xEA6F0008
  .long 0x80000100
  ; tbuffer_load_format_xyzw v[9:12], v0, s[0:3], 0 offen offset:8
  .long 0xEA6B0008
  .long 0x80000900
  v_mov_b32 v13, 16
  global_store_dword v13, v5, s[0:1]
  v_mov_b32 v14, 20
  global_store_dword v14, v6, s[0:1]
  v_mov_b32 v15, 24
  global_store_dword v15, v7, s[0:1]
  v_mov_b32 v16, 28
  global_store_dword v16, v8, s[0:1]
  v_mov_b32 v17, 32
  global_store_dword v17, v9, s[0:1]
  v_mov_b32 v18, 36
  global_store_dword v18, v10, s[0:1]
  v_mov_b32 v19, 40
  global_store_dword v19, v11, s[0:1]
  v_mov_b32 v20, 44
  global_store_dword v20, v12, s[0:1]
  s_endpgm
