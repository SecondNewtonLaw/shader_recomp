; Console container fixture. The same code is delivered two ways: as a raw
; HSACO code object and inside an Xbox Series X|S container (DXBC -> DXIL ->
; XDAT -> XBCH -> FSSM). Both must decode to the same instruction stream.
;
; s0 counts to 10, then the value is stored to the test buffer at index 0.
.text
.globl compute
compute:
  s_mov_b32 s0, 0
  s_mov_b32 s1, 10
L:
  s_cmp_lt_u32 s0, s1
  s_cbranch_scc0 done
  s_add_u32 s0, s0, 1
  s_branch L
done:
  v_mov_b32 v0, 0
  v_mov_b32 v1, s0
  global_store_dword v0, v1, s[0:1]
  s_endpgm
