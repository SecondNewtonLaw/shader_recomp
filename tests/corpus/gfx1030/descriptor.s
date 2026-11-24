; Descriptor fixture: s0..s3 is a V# the host builds for the bound buffer.
; Load dword 0 through it and copy the value to the test buffer at dword 8.
.text
.globl descriptor
descriptor:
  v_mov_b32 v0, 0
  buffer_load_dword v1, v0, s[0:3], 0 offen
  v_mov_b32 v2, 8
  global_store_dword v2, v1, s[0:1]
  s_endpgm
