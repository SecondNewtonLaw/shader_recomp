; Wave broadcast fixture: every lane stores the first lane's thread id.
.text
.globl readfirst
readfirst:
  v_readfirstlane_b32 s2, v0
  v_mov_b32 v1, s2
  global_store_dword v0, v1, s[0:1]
  s_endpgm
