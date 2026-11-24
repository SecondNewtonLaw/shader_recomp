.text
.globl branch
branch:
  s_mov_b32 s0, 1
  s_cmp_eq_u32 s0, 1
  s_cbranch_scc0 skip
  s_mov_b32 s1, 7
  s_branch done
skip:
  s_mov_b32 s1, 9
done:
  v_mov_b32 v0, 0
  v_mov_b32 v1, s1
  global_store_b32 v0, v1, s[0:1]
  s_endpgm
