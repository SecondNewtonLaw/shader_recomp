; Image fixture: the decode is complete, the translation needs the image
; model, so lifting it must name the descriptor registers it needs.
.text
.globl image
image:
  image_sample v[0:3], v[4:5], s[8:15], s[16:19] dmask:0xf dim:2d
  image_load v[8:11], v[12:13], s[8:15] dmask:0xf dim:2d
  image_store v[16:19], v[20:21], s[8:15] dmask:0xf dim:2d
  s_endpgm
