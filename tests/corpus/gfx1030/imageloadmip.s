; Refusal fixture: a storage image read cannot name a level (OpImageRead has no
; Lod operand; the view selects the level), so image_load_mip is a diagnostic
; rather than a silent level-zero read.
.text
.globl imageloadmip
imageloadmip:
  v_mov_b32 v4, 0
  v_mov_b32 v5, 0
  v_mov_b32 v6, 0
  image_load_mip v[0:3], v[4:6], s[0:7] dmask:0xf dim:2d
  s_endpgm
