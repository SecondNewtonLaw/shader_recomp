# Xbox Series X|S GPU to SPIR-V

Status: format research, phase 1 landed (container reader, fixture, identity
test). This document records what a console shader actually is on the wire, what
the translation contract with the rest of the pipeline is, and the order in
which the missing pieces are built. Numbers and layouts are taken from three
independent reverse-engineering efforts, and anything that only one of them
reports is marked as unconfirmed.

## 1. What the hardware is

| | Xbox Series X | Xbox Series S |
|---|---|---|
| SoC codename | Arden | Sparkman |
| GPU | 52 CU RDNA2, 3.6 GHz | 20 CU RDNA2, 1.565 GHz |
| API | D3D12.X only (no D3D11.X) | same |

The ISA lineage is GFX10, not GFX11 or GFX12: the console toolchain emits
GFX10.3-class encodings (`ENC_VINTRP`, `ENC_EXP`, `ENC_MIMG` and the MUBUF /
MTBUF / DS families are all the RDNA1/2 forms we already decode a part of).
Wavefronts are 64 lanes by default with a per-program wave32 opt-in, unlike the
RDNA4 hardware this repository is currently verified on.

D3D12.X names resources through console descriptors (T# for textures, V# for
buffers, S# for samplers) that live in shader-visible heaps, and passes root
signature state to the shader as *user SGPRs*. A console shader therefore knows
its resources through scalar registers, not through a host API binding table.

## 2. Shader delivery

A shipped shader is a DXBC container whose `DXIL` part carries **machine code
instead of LLVM bitcode**. The PC-side parts (`SFI0`, `ISG1`, `OSG1`, `PSG1`,
`PSV0`, `HASH`) are still present and still describe the input/output
signatures, so they remain the authority on attribute rows.

```
DXBC container
  part "DXIL"          DxilProgramHeader (24 B), then:
    +0x18  "XDAT"      u32 entry count, u32 table offset
                       table: count x { u32 id, u32 offset, u32 size }
                       entry offsets are relative to the part data or to
                       XDAT; both encodings have been observed, so both are
                       validated before either is used
      id 0x12          16-byte shader hash
      id 0x0d          "XBCH" header + zlib stream
      id 0x07          nested DxilProgramHeader (bitcode is a "NOBC" stub)
      id 0x0a          "xbop" compiler options
    ...                bitcode offset/size point at a 4-byte "NOBC"
  part "HASH"          container hash

XBCH
  +0x00  "XBCH"        u32 version (3)
  +0x08  packed size   u32 (XCL, DedSec) or u64 (VIctory)
  +0x10  raw size      u32 or u64 - resolved by comparing against the
                       decompressed length; a mismatch is reported, not hidden
  +0x18  zlib stream   -> FSSM

FSSM
  +0x00  "FSSM"        u32 version (0x101), u64 payload size, payload
```

The `FSSM` payload is one or more *records*, one per hardware-stage variant.
A vertex shader that also feeds a geometry shader ships two (ES and VS); a mesh
program ships one NGG source record.

```
payload +0x00  u64 graphics slot count (6)
payload +0x08  u64 record offsets: LS, HS, ES, GS, VS, PS
payload +0x48  u64 record offset: mesh (NGG)
payload +0x60  u64 record offset: compute
               absent records are 0xDEADBEEFDEADBEEF

record +0x00  u64 code offset (payload-relative)
record +0x08  u64 code size
record +0x14  u32 flags, bit 5: wave32 (clear: wave64)
record +0x28  u64 root layout table (payload-relative)
record +0x30  u64 used-resource table (payload-relative)
record +0x38  u32 root layout count
record +0x3c  u32 used-resource count
record +0x4c  u32 LDS bytes
record +0x50  u32 bits 0-9 VGPRs used, bits 20-27 SGPRs used
record +0x54  stage data (see below)
```

Root layout entries are 20 bytes: root type, table offset in dwords, user SGPR
count, base register, first user-data dword, root slot. Used-resource entries
are 20 bytes: resource type, root type, table offset in dwords, lower bound,
stride, descriptor dwords, first user-data dword, root slot. Together they are
the console's root signature: they say which user SGPR holds which root
parameter and which heap slot a descriptor table starts at.

Stage data, offsets from the end of the record (`E = record + 0x54`):

| Stage | Fields |
|---|---|
| CS | `E+0x24` `COMPUTE_PGM_RSRC2`, `E+0x28` thread-group size x/y/z |
| PS | `E+0x24` 32 inputs of 8 B (usage, row, channel mask, interp attribute, default, packed pair row, parameter slot, flags), `E+0x124` count, `E+0x134` `SPI_PS_INPUT_ADDR`, `E+0x138` `DB_SHADER_CONTROL`, `E+0x148` export-patch size and per-MRT patches |
| VS | `E+0x24` 32 inputs of 5 B (usage, row, channel mask, VGPR of channel 0, highest channel), `E+0xc4` count; `E+0xc8` 32 outputs of 6 B (usage, row, `exp param<n>`, row, slot, flags), `E+0x188` count |
| MS | outputs as VS at `E+0xc8`; `E+0x18c` `SPI_VS_OUT_CONFIG`, position format, clip/cull control, streamout, GS rsrc2, output primitive type, instance count, index format; `E+0x448` lanes per group, max vertices, max primitives, user SGPRs, topology; code starts at `E+0x45c` |

The `PS` input table is the PS equivalent of a fetch shader: it says which
`SPI_PS_INPUT_ADDR` bit and which interpolation parameter carries which
signature row. Without it, `v_interp_p1/p2` cannot be named.

### Layout uncertainty

Two published descriptions disagree on where the code pointer sits. VIctory
reads it through the record table above. XCL and DedSec read `FSSM+0xC0`
(offset, relative to `+0x10`) and `FSSM+0xC8` (size), with hardware-stage
descriptors as `u64 0x000000C0_0002_AAHH` at `+0x100`, stride `0x88`. The two
are consistent if the compute record happens to start at `payload+0xB0`; since
the record offset is data, that is plausible but not proven. The reader
therefore validates the record table first and falls back to the direct
pointer, and reports which one matched. A container where neither validates is
a diagnostic, never a guess.

## 3. Execution model

- **Waves.** One console wave is 64 lanes unless the record's wave32 flag is
  set. Cross-lane operations (`v_readlane`, `v_readfirstlane`, DPP, `ds_bpermute`,
  `v_mbcnt`, ballots) are defined over that width.
- **EXEC.** Scalar branches (`s_cbranch_scc*`, `s_cbranch_execz`, `s_cbranch_vccz`)
  are uniform per wave; vector instructions are masked by EXEC, updated by the
  `s_*_saveexec*` family, `v_cmpx_*` and `s_andn1_wrexec`/`s_andn2_wrexec`.
- **Stages.** Export (`exp`) ends a vertex/pixel program; `s_sendmsg
  GS_ALLOC_REQ` starts an NGG wave. A compute program ends with `s_endpgm`
  read from `COMPUTE_PGM_RSRC2`-controlled state.
- **User data.** Scalar registers after the user SGPR base carry root
  constants, descriptor-table pointers and root descriptor addresses in root
  order.

For SPIR-V this means `SubgroupSize` 64 (or an explicit two-half emulation
where the device cannot run 64-lane subgroups), `OpGroupNonUniformBallot`,
`OpGroupNonUniformShuffle`/`BroadcastFirst`, and per-lane predicates instead of
a register-wide EXEC.

## 4. Resources

A T#/V#/S# is 4 or 8 raw dwords. Decoding them at translation time is what makes
a static binding possible: formats, dimensions, compare/gather filters become
part of the emitted image type and sampler state. When a descriptor is only
known at dispatch, the shader must index a heap instead.

Mapping chosen for this project (the backend's `TargetProfile` already carries
`physicalStorageBuffer`):

| Console | SPIR-V |
|---|---|
| root constants, user SGPRs | push constant block, in root order |
| root descriptor (V#/T#/S# by value) | storage buffer / sampled image bound by address, or a 32-bit BDA in the push block |
| descriptor table | SSBO of raw descriptors indexed by (table offset + slot), decoded in the shader |
| directly indexed heap | SSBO of raw descriptors, heap start in a push constant |
| `ds_*` | `OpVariable` in Workgroup storage |
| LDS size | `OpEntryPoint` workgroup array sized from the record |

Descriptor-as-bytes keeps one code path for static and dynamic cases; the
specialisation that turns a raw descriptor into a typed image is an
optimisation, not the contract.

## 5. Where this repository stands

| Area | Today | Needed for Series X|S |
|---|---|---|
| Container | PC DXBC (`SHDR`/`SHEX`), DXIL bitcode, SPIR-V, cubin, HSACO, Series X|S XDAT/XBCH/FSSM (`XboxShaderContainer`) | DXBC signature parts, root layout and used-resource tables |
| ISA decode | gfx10 base table; gfx1013 and gfx103 inherit it with **no delta**; RDNA3/4 have their own tables | real RDNA2 delta, and the four encodings the decoder does not know: `VINTRP` (3 opcodes), `EXP`, `MIMG` (103), `MTBUF` (16) |
| ISA lift | ALU, compare, conversion, branch, scalar, buffer/global/flat/scratch memory, 92.7% of gfx1200 entries | `v_interp_*`, `exp`, image ops, typed buffers |
| Stages | `GLCompute` only; multi-block functions go through the dispatcher fallback | `Vertex`, `Fragment`, then geometry and NGG mesh |
| Waves | single-invocation IL; wave intrinsics declared in `Intrinsics.hpp` but not lowered | EXEC as per-lane predicates, ballots, shuffles, subgroup size |
| Resources | one test buffer at set 0 binding 0 (`--test-abi`) | T#/V#/S# decode, SRT/heap access, push constants |
| Verification | 58 targets, RX 9060 XT (gfx1200) + IL interpreter | console fixtures through the same GPU run, then a title shader pack sweep |

## 6. Roadmap

Each phase ends with something executable, not with a table entry.

1. **Container (landed).** Read a Series X shader container down to the stage's
   code and metadata; feed the code to the existing GFX10 pipeline. Acceptance:
   the same machine code delivered as HSACO and as a console container decodes to
   the same instruction stream and emits byte-identical SPIR-V; the emitted
   module passes `spirv-val` and, when the fixture is observable, runs on the
   detected GPU with the same result. `tests/corpus/xbox/` is that fixture; the
   root layout and used-resource tables are still to be decoded.
2. **RDNA2 decode delta.** Fill `Arch::Gfx103` with the instructions RDNA2 adds
   over RDNA1, add `VINTRP` and `EXP`, lift interpolation and exports. Acceptance:
   a VS/PS pair assembled for gfx1030 lifts, and its exports land in the right
   SPIR-V locations.
3. **Graphics stages.** `Vertex` and `Fragment` execution models, IO variables
   from signature rows, fragment interpolation modes, depth/colour export.
   Acceptance: an offscreen triangle renders on the detected GPU with pixels
   that match a reference computed on the CPU.
4. **Waves.** EXEC to per-lane predicates, ballot/readlane/shuffle, subgroup
   size 64 (with the 32-lane fallback). Acceptance: a wave ballot fixture
   matches the interpreter for every lane.
5. **Resources.** Descriptor decode, SRT and heap indexing, `MIMG`/`MTBUF`.
   Acceptance: sample a known texture and compare on the GPU.
6. **Merged stages.** ES/GS pairs, NGG mesh, `s_sendmsg` allocation.
7. **Sweep.** Run a title's shader pack, publish coverage and the diagnostic
   list of what refuses, per stage.

Phases 2-6 extend the IL and the emitter, not the number of special cases: the
stages and resources already have IL declarations (`Module::inputs`,
`outputs`, `resources`, `executionModes`); the emitter ignores them today.

## 7. Open questions that need real bytes

- XBCH size fields: `u32` at `+0x08`/`+0x10` or `u64` at `+0x08`/`+0x10`.
  Resolvable from one container: decompress and compare.
- FSSM record table vs direct code pointer (section 2).
- Which stage variants a title actually ships, and whether NGG programs carry
  a separate ES record.
- Xbox-specific scalar instructions or `s_setreg` usage that RDNA2 desktop
  code does not contain.
- Whether any shipped stage is wave32 in practice.

## 8. References

- XCL, `crates/xcl/src/gpu/xbox.rs` and `docs/specs/2026-11-20-xcl-design.md`:
  container layout, FSSM direct pointer, wave64-on-wave32 execution notes.
- VIctory, `crates/victory-shader/src/native.rs`, `io.rs`, `gfx10/`: record
  table, root layout and used-resource tables, stage IO, interpolation.
- DedSec, `runtime/src/xbch_parser.cpp`: independent XDAT/XBCH/FSSM reader.
- Rebox, `docs/anyps5.md`: FSSM compute header offsets and the console
  descriptor model (T#/V#/S# shadow heaps, user SGPR packing).
- Mesa `aco_opcodes.py` (gfx10 column) as carried by VIctory's `opcodes.rs`:
  the opcode numbering used to diff our tables.
