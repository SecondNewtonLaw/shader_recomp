; Typed buffer fixture. The code object's second instruction is hand-encoded as
; TBUFFER_LOAD_FORMAT_X (ENC_MTBUF, opcode 0, data format 4 = 32-bit, number
; format 7 = float) with vaddr v0, vdata v1 and the V# in s0..s3:
;   E8B00000 80000100  (data 6, number 1 = BUF_FMT_32_FLOAT)
; The rest is the descriptor fixture's tail, which copies v1 to the test buffer.
.text
.globl tbuffer
tbuffer:
  v_mov_b32 v0, 0
  ; tbuffer_load_format_x v1, v0, s[0:3], format 32_FLOAT
  .long 0xE8B00000
  .long 0x80000100
  v_mov_b32 v2, 8
  global_store_dword v2, v1, s[0:1]
  s_endpgm
