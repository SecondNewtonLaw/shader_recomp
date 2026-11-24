; Predicated store: only lane 0 survives the compare mask, so only dword 0 is
; written; the sentinel dwords of the other lanes must stay untouched.
.text
.globl predicated
predicated:
  v_cmpx_lt_u32 v0, 1
  v_mov_b32 v1, 0xAA
  v_mul_lo_u32 v2, v0, 4
  global_store_dword v2, v1, s[0:1]
  s_endpgm
