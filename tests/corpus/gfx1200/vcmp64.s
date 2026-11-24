.text
.globl vcmp64
vcmp64:
  v_mov_b32 v0, 0x3fc00000
  v_mov_b32 v1, 0x40200000
  v_cmp_lt_f32_e64 s10, v0, v1
  v_cmp_gt_f32_e64 s11, v0, v1
  v_cmp_eq_f32_e64 s12, v0, v0
  v_cmp_u_f32_e64 s13, v0, v1
  v_cmp_neq_f32_e64 s14, v0, v1
  v_mov_b32 v2, 5
  v_mov_b32 v3, 9
  v_cmp_lt_i32_e64 s15, v2, v3
  v_cmp_gt_i32_e64 s16, v2, v3
  v_mov_b32 v4, 1
  v_mov_b32 v5, 0
  v_mov_b32 v6, 2
  v_mov_b32 v7, 0
  v_cmp_lt_u64_e64 s17, v[4:5], v[6:7]
  v_cmp_ge_u64_e64 s18, v[4:5], v[6:7]
  v_cmpx_lt_f32 v0, v1
  s_cbranch_execnz cmpx_ok
  v_mov_b32 v20, 100
  s_branch done
cmpx_ok:
  v_mov_b32 v20, 7
done:
  v_mov_b32 v40, 0
  v_mov_b32 v41, s10
  global_store_b32 v40, v41, s[0:1]
  v_mov_b32 v42, 4
  v_mov_b32 v43, s11
  global_store_b32 v42, v43, s[0:1]
  v_mov_b32 v44, 8
  v_mov_b32 v45, s12
  global_store_b32 v44, v45, s[0:1]
  v_mov_b32 v46, 12
  v_mov_b32 v47, s13
  global_store_b32 v46, v47, s[0:1]
  v_mov_b32 v48, 16
  v_mov_b32 v49, s14
  global_store_b32 v48, v49, s[0:1]
  v_mov_b32 v50, 20
  v_mov_b32 v51, s15
  global_store_b32 v50, v51, s[0:1]
  v_mov_b32 v52, 24
  v_mov_b32 v53, s16
  global_store_b32 v52, v53, s[0:1]
  v_mov_b32 v54, 28
  v_mov_b32 v55, s17
  global_store_b32 v54, v55, s[0:1]
  v_mov_b32 v56, 32
  v_mov_b32 v57, s18
  global_store_b32 v56, v57, s[0:1]
  v_mov_b32 v58, 36
  global_store_b32 v58, v20, s[0:1]
  s_endpgm
