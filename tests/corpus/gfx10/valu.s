; RDNA vector integer batch: min/max, reverse shifts, mul_lo and VOP3 forms.
.text
.globl valu
valu:
  v_mov_b32 v0, 5
  v_mov_b32 v1, 20
  v_min_i32 v2, v0, v1
  v_max_u32 v3, v0, v1
  v_lshlrev_b32 v4, v0, v1
  v_mov_b32 v5, -16
  v_mov_b32 v6, 2
  v_ashrrev_i32 v7, v6, v5
  v_mul_lo_u32 v8, v0, v1
  v_not_b32 v9, v0
  v_min_i32_e64 v10, v0, v1
  v_lshlrev_b32_e64 v11, v0, v1
  s_endpgm
