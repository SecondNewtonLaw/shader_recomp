; LDS counter fixture: seed the two counters, then append twice and consume
; once. The counters are addressed by the instruction's offset, not a VGPR.
.text
.globl dscounter
dscounter:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0
  ds_write_b32 v0, v1
  v_mov_b32 v2, 4
  ds_write_b32 v2, v1
  ds_append v3
  ds_append v4
  ds_consume v5
  ds_append v6 offset:4
  v_mov_b32 v7, 0
  global_store_dword v7, v3, s[0:1]
  v_mov_b32 v8, 4
  global_store_dword v8, v4, s[0:1]
  v_mov_b32 v9, 8
  global_store_dword v9, v5, s[0:1]
  v_mov_b32 v10, 12
  global_store_dword v10, v6, s[0:1]
  s_endpgm
