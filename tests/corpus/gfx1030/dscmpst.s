; LDS compare-and-store: seed LDS[0] with 5, store 9 when the comparison
; matches, leave it alone when it does not, then the returning form stores 11
; and hands back what the location held.
.text
.globl dscmpst
dscmpst:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 5
  ds_write_b32 v0, v1
  v_mov_b32 v2, 5
  v_mov_b32 v3, 9
  ds_cmpst_b32 v0, v2, v3
  v_mov_b32 v4, 5
  v_mov_b32 v5, 7
  ds_cmpst_b32 v0, v4, v5
  v_mov_b32 v6, 9
  v_mov_b32 v7, 11
  ds_cmpst_rtn_b32 v8, v0, v6, v7
  ds_read_b32 v9, v0
  v_mov_b32 v10, 0
  global_store_dword v10, v8, s[0:1]
  v_mov_b32 v11, 4
  global_store_dword v11, v9, s[0:1]
  s_endpgm
