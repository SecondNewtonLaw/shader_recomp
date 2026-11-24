; User data fixture: v1 = s0 (the first user data dword), stored at index 0.
.text
.globl userdata
userdata:
  v_mov_b32 v1, s0
  v_mov_b32 v0, 0
  global_store_dword v0, v1, s[0:1]
  s_endpgm
