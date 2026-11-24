.text
.globl compares
compares:
  v_mov_b32 v0, 2.0
  v_mov_b32 v1, 3.0
  v_mov_b32 v18, 1
  v_mov_b32 v19, 0
  v_cmp_lt_f32 v0, v1
  v_cndmask_b32 v2, v18, v19
  v_cmp_eq_f32 v0, v1
  v_cndmask_b32 v3, v18, v19
  v_cmp_neq_f32 v0, v1
  v_cndmask_b32 v4, v18, v19
  v_cmp_nge_f32 v0, v1
  v_cndmask_b32 v5, v18, v19
  v_cmp_o_f32 v0, v1
  v_cndmask_b32 v6, v18, v19
  v_cmp_u_f32 v0, v1
  v_cndmask_b32 v7, v18, v19
  v_cmpx_lt_f32 v0, v1
  s_cbranch_execnz exec_true
  v_mov_b32 v9, 100
  s_branch done
exec_true:
  v_mov_b32 v9, 9
done:
  v_cmpx_eq_f32 v0, v1
  s_cbranch_execz exec_false
  v_mov_b32 v8, 100
  s_branch store
exec_false:
  v_mov_b32 v8, 8
store:
  ; The masked stores above leave EXEC at zero for the failing cmpx; a store
  ; only executes for active lanes, so re-open the wave before storing.
  s_mov_b64 exec, -1
  v_mov_b32 v10, 0
  global_store_dword v10, v2, s[0:1]
  v_mov_b32 v11, 4
  global_store_dword v11, v3, s[0:1]
  v_mov_b32 v12, 8
  global_store_dword v12, v4, s[0:1]
  v_mov_b32 v13, 12
  global_store_dword v13, v5, s[0:1]
  v_mov_b32 v14, 16
  global_store_dword v14, v6, s[0:1]
  v_mov_b32 v15, 20
  global_store_dword v15, v7, s[0:1]
  v_mov_b32 v16, 24
  global_store_dword v16, v9, s[0:1]
  v_mov_b32 v17, 28
  global_store_dword v17, v8, s[0:1]
  s_endpgm
