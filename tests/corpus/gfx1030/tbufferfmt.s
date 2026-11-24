; Typed buffer conversions: an 8-bit unsigned load and a 16-bit float load out
; of a seeded buffer. Words are hand-encoded: opcode 0 (load x), data/number
; pairs (0,5)? no - the pairs below are the measured (data, number) codes.
.text
.globl tbufferfmt
tbufferfmt:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x00000080
  buffer_store_dword v1, v0, s[0:3], 0 offen
  v_mov_b32 v2, 0x00003C00
  buffer_store_dword v2, v0, s[0:3], 0 offen offset:4
  ; tbuffer_load_format_x v3, v0, s[0:3], 0 offen format:BUF_FMT_8_UINT
  .long 0xE8280000
  .long 0x80000300
  ; tbuffer_load_format_x v4, v0, s[0:3], 0 offen offset:4 format:16_FLOAT
  .long 0xE8680004
  .long 0x80000400
  v_mov_b32 v5, 8
  global_store_dword v5, v3, s[0:1]
  v_mov_b32 v6, 12
  global_store_dword v6, v4, s[0:1]
  s_endpgm
