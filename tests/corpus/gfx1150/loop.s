; Portable loop fixture using s_add_u32.
.text
.globl loop
loop:
  s_mov_b32 s0, 0
  s_mov_b32 s1, 10
L:
  s_cmp_lt_u32 s0, s1
  s_cbranch_scc0 done
  s_add_u32 s0, s0, 1
  s_branch L
done:
  s_endpgm
