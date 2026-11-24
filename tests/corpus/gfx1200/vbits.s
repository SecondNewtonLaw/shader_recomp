.text
.globl vbits
vbits:
  v_mov_b32 v0, 0x0000ff00
  v_clz_i32_u32 v1, v0
  v_ctz_i32_b32 v2, v0
  v_mov_b32 v3, 0
  v_clz_i32_u32 v4, v3
  v_ctz_i32_b32 v5, v3
  v_bcnt_u32_b32 v6, v0, v3
  v_mov_b32 v7, 5
  v_bcnt_u32_b32 v8, v0, v7
  v_clz_i32_u32_e64 v9, v0
  v_bcnt_u32_b32_e64 v10, v0, v7
  s_endpgm
