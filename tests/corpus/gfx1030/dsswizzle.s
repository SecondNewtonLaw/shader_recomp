; Swizzle fixture: every lane computes value = lane + 100, applies the quad
; permutation [1,0,3,2] through ds_swizzle, and stores the result at its own
; offset. Dword i is therefore 100 + (i XOR 1).
.text
.globl dsswizzle
dsswizzle:
  v_mbcnt_lo_u32_b32 v0, -1, 0
  v_mov_b32 v5, 100
  v_add_nc_u32 v1, v0, v5
  ds_swizzle_b32 v2, v1 offset:0x80B1
  v_lshlrev_b32 v3, 2, v0
  global_store_dword v3, v2, s[0:1]
  s_endpgm
