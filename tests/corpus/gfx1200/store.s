; gfx1200 shader-level test: loop to 10, store s0 to the test buffer at index 0.
.text
.globl store
store:
  s_mov_b32 s0, 0
  s_mov_b32 s1, 10
L:
  s_cmp_lt_u32 s0, s1
  s_cbranch_scc0 done
  s_add_co_u32 s0, s0, 1
  s_branch L
done:
  v_mov_b32 v0, 0
  v_mov_b32 v1, s0
  global_store_b32 v0, v1, s[0:1]
  s_endpgm
