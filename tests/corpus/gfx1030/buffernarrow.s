; Narrow buffer stores and the immediate offset: seed a dword, merge a byte at
; +1 and a short at +2, read the dword back, and store a dword at +4 through
; the immediate offset alone.
.text
.globl buffernarrow
buffernarrow:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x11223344
  buffer_store_dword v1, v0, s[0:3], 0 offen
  v_mov_b32 v2, 0xAA
  buffer_store_byte v2, v0, s[0:3], 0 offen offset:1
  v_mov_b32 v3, 0xBBCC
  buffer_store_short v3, v0, s[0:3], 0 offen offset:2
  v_mov_b32 v4, 0x55667788
  buffer_store_dword v4, v0, s[0:3], 0 offen offset:4
  buffer_load_dword v5, v0, s[0:3], 0 offen
  v_mov_b32 v6, 4
  buffer_load_dword v7, v6, s[0:3], 0 offen
  v_mov_b32 v8, 8
  global_store_dword v8, v5, s[0:1]
  v_mov_b32 v9, 12
  global_store_dword v9, v7, s[0:1]
  s_endpgm
