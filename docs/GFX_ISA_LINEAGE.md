# GFX ISA lineage: base plus deltas

AMD's RDNA encodings are a lineage, not five independent ISAs. RDNA1 defines
the instruction formats and the first opcode assignment; later generations keep
most of it and amend what changed. The decoder models exactly that:

```
gfx10 (RDNA1)          base table + format layouts
  ├─ gfx1013 (PS5)     optional SKU delta
  ├─ gfx1030 (RDNA2)   optional delta
  │    └─ gfx1100 (RDNA3)   renumbering delta
  │         ├─ gfx1150 (RDNA3.5)  delta
  │         └─ gfx1200 (RDNA4)    delta
```

`Lookup` walks the chain from the root to the requested target and applies
every delta in order. A delta can add or remove an `(encoding, opcode)` entry.
The last match wins.

## Why removals are mandatory

RDNA3 reuses the encoding space: the SOP1/SOP2/SOPP opcode that meant
`s_and_b32` on RDNA1 can mean something else on RDNA3. Inheritance alone would
keep decoding the old mnemonic for a word that now has a different meaning.
Every renumbered opcode therefore gets an explicit `Remove` in the child delta,
followed by the new `Make`. This is the one place where the Nomic model needs
discipline: a delta that renumbers must also retire the old number.

## Measured deltas

Parsed from AMD's machine-readable ISA XML (MIT):

| Generation | Instructions | Encodings |
|---|---:|---:|
| RDNA1 | 1149 | 42 |
| RDNA2 | 1153 | 42 |
| RDNA3 | 1148 | 47 |
| RDNA3.5 | 1208 | 47 |
| RDNA4 | 1264 | 46 |

| Step | Added | Removed | What it is |
|---|---:|---:|---|
| RDNA1 → RDNA2 | 17 | 13 | dot products, addtid forms; dropped `V_MAC`/`V_MAD` and PS4-era scalar ops |
| RDNA2 → RDNA3 | 363 | 368 | mostly renames (`BUFFER_ATOMIC_ADD` → `BUFFER_ATOMIC_ADD_U32`), plus VOPD |
| RDNA3 → RDNA3.5 | 60 | 0 | additive |
| RDNA3.5 → RDNA4 | 192 | 136 | VOPD3, true16, FP8/FP4, global/flat split |

Structural changes that matter for the decoder:

- **RDNA3 renumbers** most SOP1, SOP2 and SOPP opcodes, and several VOP3/VOPC
  opcodes. `s_mov_b32` moves from SOP1 opcode 3 to 0, `s_endpgm` from SOPP 1 to
  48, `v_fma_f32` from VOP3 331 to 531.
- **RDNA3 introduces VOPD** (two VOP operations in one 64-bit word) and adds
  encodings that do not exist on RDNA1.
- **RDNA4 narrows VOP1's OP field** from 8 bits to 7 and renames VOP3's
  `OP_SEL` to `OPSEL`.
- **RDNA4 replaces the SMEM layout**: `OFFSET`/`GLC`/`DLC` become
  `IOFFSET`/`SCOPE`/`TH`/`NV`, with a 6-bit opcode at bit 13.
- **RDNA4 splits VGLOBAL from VFLAT/VSCRATCH** and drops plain `S_ADD_U32` /
  `S_SUB_U32` in favour of the carry-out forms.
- **SKU extensions** exist on top of a generation (PS5 `gfx1013` has forms the
  generic RDNA2 table does not). They are modelled as a child delta of their
  base generation.

## Current implementation

`SR.Frontend/src/Arch/Gfx/GfxIsa.cpp` holds a curated subset:

- RDNA1 base entries for SOP1, SOP2, SOPP, VOP1, VOP2 and VOP3.
- `kGfx11` delta: renumbering plus removals of the retired opcodes.
- `kGfx12` delta: removes plain `S_ADD_U32`/`S_SUB_U32`.

The full tables will be generated from the XML once the canonical semantic IDs
are settled. Decoding is already generic over the delta chain; only the data
grows.

## Semantic IDs versus mnemonics

A delta renames, but the lifter should not care about the vendor mnemonic.
Opcodes decode to canonical `Gfx::Op` values (`S_MovB32`, `V_AddF32`, ...). When
a later generation changes the meaning rather than the spelling, the canonical
op gets a new value or a per-generation override in the lifter; the mnemonic
string is diagnostic output only.
