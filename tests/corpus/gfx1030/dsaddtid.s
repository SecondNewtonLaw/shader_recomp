; LDS addtid fixture: every lane writes its value at its own thread-id slot and
; reads it back, so the address is the encoded offset plus the thread id.
.text
.globl dsaddtid
dsaddtid:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x1234
  ds_write_addtid_b32 v1
  ds_read_addtid_b32 v2
  v_mov_b32 v3, 0
  global_store_dword v3, v2, s[0:1]
  s_endpgm
