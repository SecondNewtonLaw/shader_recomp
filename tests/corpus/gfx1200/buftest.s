.text
.globl buftest
buftest:
  s_mov_b32 s4, 0
  v_mov_b32 v0, 0
  v_mov_b32 v1, 0x11223344
  buffer_store_b32 v1, v0, s[0:3], s4 offen
  v_mov_b32 v2, 16
  v_mov_b32 v3, 0x55667788
  buffer_store_b32 v3, v2, s[0:3], s4 offen
  buffer_load_b32 v4, v0, s[0:3], s4 offen
  buffer_load_b32 v5, v2, s[0:3], s4 offen
  buffer_load_u8 v6, v0, s[0:3], s4 offen offset:1
  buffer_load_u16 v7, v0, s[0:3], s4 offen offset:2
  v_mov_b32 v8, 32
  v_mov_b32 v9, 5
  buffer_store_b32 v9, v8, s[0:3], s4 offen
  v_mov_b32 v10, 3
  buffer_atomic_add_u32 v10, v8, s[0:3], s4 offen th:TH_ATOMIC_RETURN
  buffer_load_b32 v12, v8, s[0:3], s4 offen
  v_mov_b32 v20, 0
  global_store_b32 v20, v4, s[0:1]
  v_mov_b32 v21, 4
  global_store_b32 v21, v5, s[0:1]
  v_mov_b32 v22, 8
  global_store_b32 v22, v6, s[0:1]
  v_mov_b32 v23, 12
  global_store_b32 v23, v7, s[0:1]
  v_mov_b32 v24, 16
  global_store_b32 v24, v10, s[0:1]
  v_mov_b32 v25, 20
  global_store_b32 v25, v12, s[0:1]
  s_endpgm
