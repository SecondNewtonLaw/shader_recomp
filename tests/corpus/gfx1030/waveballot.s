; Wave ballot fixture: every lane satisfies the predicate, so the ballot is the
; wave mask and its popcount is the wave width. The VOP3 compare writes the
; mask to the s0 pair; v0 is the thread index in the test ABI.
.text
.globl waveballot
waveballot:
  v_cmp_lt_u32_e64 s0, v0, 1000
  s_bcnt1_i32_b64 s2, s[0:1]
  v_mov_b32 v1, s2
  global_store_dword v0, v1, s[0:1]
  s_endpgm
