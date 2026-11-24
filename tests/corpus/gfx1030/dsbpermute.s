; Cross-lane permutation: every lane computes value = lane + 100, asks for the
; value of lane+1 through ds_bpermute, and writes it to the test buffer at its
; own lane offset. The result at dword i is i + 101 (lane 63 wraps to 100).
.text
.globl dsbpermute
dsbpermute:
  v_mbcnt_lo_u32_b32 v0, -1, 0
  v_mov_b32 v5, 100
  v_add_nc_u32 v1, v0, v5
  v_mov_b32 v6, 1
  v_add_nc_u32 v2, v0, v6
  v_lshlrev_b32 v2, 2, v2
  ds_bpermute_b32 v3, v2, v1
  v_lshlrev_b32 v4, 2, v0
  global_store_dword v4, v3, s[0:1]
  s_endpgm
