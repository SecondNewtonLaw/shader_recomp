; Buffer fixture: the V# in s0..s3 addresses the test buffer. It seeds four
; dwords through a wide store, reads them back with a wide load, reads the
; narrow forms out of the first dword, and round-trips a dwordx2 pair.
.text
.globl bufferwide
bufferwide:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x0000FF81
  v_mov_b32 v2, 0x11111111
  v_mov_b32 v3, 0x22222222
  v_mov_b32 v4, 0x33333333
  buffer_store_dwordx4 v[1:4], v0, s[0:3], 0 offen
  buffer_load_dwordx4 v[5:8], v0, s[0:3], 0 offen
  buffer_load_ubyte v9, v0, s[0:3], 0 offen
  buffer_load_sbyte v10, v0, s[0:3], 0 offen
  buffer_load_ushort v11, v0, s[0:3], 0 offen
  buffer_load_sshort v12, v0, s[0:3], 0 offen
  v_mov_b32 v13, 64
  buffer_store_dwordx2 v[5:6], v13, s[0:3], 0 offen
  buffer_load_dwordx2 v[14:15], v13, s[0:3], 0 offen
  ; copy the results to the test buffer at dword 4 and on
  v_mov_b32 v16, 16
  global_store_dword v16, v5, s[0:1]
  v_mov_b32 v17, 20
  global_store_dword v17, v6, s[0:1]
  v_mov_b32 v18, 24
  global_store_dword v18, v7, s[0:1]
  v_mov_b32 v19, 28
  global_store_dword v19, v8, s[0:1]
  v_mov_b32 v20, 32
  global_store_dword v20, v9, s[0:1]
  v_mov_b32 v21, 36
  global_store_dword v21, v10, s[0:1]
  v_mov_b32 v22, 40
  global_store_dword v22, v11, s[0:1]
  v_mov_b32 v23, 44
  global_store_dword v23, v12, s[0:1]
  v_mov_b32 v24, 48
  global_store_dword v24, v14, s[0:1]
  v_mov_b32 v25, 52
  global_store_dword v25, v15, s[0:1]
  s_endpgm
