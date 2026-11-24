.text
.globl f64
f64:
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x3ff80000
  v_mov_b32 v2, 0
  v_mov_b32 v3, 0x40000000
  v_add_f64 v[4:5], v[0:1], v[2:3]
  v_mul_f64 v[6:7], v[2:3], v[2:3]
  v_fma_f64 v[8:9], v[2:3], v[2:3], v[0:1]
  v_min_num_f64 v[10:11], v[0:1], v[2:3]
  v_max_num_f64 v[12:13], v[0:1], v[2:3]
  v_mov_b32 v16, 3
  v_cvt_f64_i32 v[14:15], v16
  v_cvt_i32_f64 v17, v[14:15]
  v_mov_b32 v20, 1.5
  v_cvt_f64_f32 v[18:19], v20
  v_cvt_f32_f64 v21, v[0:1]
  v_sqrt_f64 v[22:23], v[6:7]
  v_mov_b32 v26, 2
  v_ldexp_f64 v[24:25], v[0:1], v26
  v_mov_b32 v30, 0
  global_store_b32 v30, v5, s[0:1]
  v_mov_b32 v31, 4
  global_store_b32 v31, v7, s[0:1]
  v_mov_b32 v32, 8
  global_store_b32 v32, v9, s[0:1]
  v_mov_b32 v33, 12
  global_store_b32 v33, v15, s[0:1]
  v_mov_b32 v34, 16
  global_store_b32 v34, v17, s[0:1]
  v_mov_b32 v35, 20
  global_store_b32 v35, v19, s[0:1]
  v_mov_b32 v36, 24
  global_store_b32 v36, v21, s[0:1]
  v_mov_b32 v37, 28
  global_store_b32 v37, v23, s[0:1]
  v_mov_b32 v38, 32
  global_store_b32 v38, v25, s[0:1]
  s_endpgm
