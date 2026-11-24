# Xbox Series X|S GPU to SPIR-V

Status: phases 1-5 landed and verified on hardware; phase 6 (merged stages) is
landed for the legacy ES/GS export path and open for NGG; phase 7 (title sweep)
waits on real shader packs. This document records what a console
shader actually is on the wire, what the translation contract with the rest of
the pipeline is, and the order in which the missing pieces are built. Numbers
and layouts are taken from three independent reverse-engineering efforts, and
anything that only one of them reports is marked as unconfirmed.

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
| ISA lift | ALU, compare, conversion, branch, scalar, buffer/global/flat/scratch memory, `v_interp_p1/p2/mov`, `exp` (including `exp prim`), wave ballots, image load/store/sample through T#/S#, typed buffers for raw 32-bit formats | converting typed-buffer formats, NGG, matrices |
| Stages | `GLCompute`, `Vertex`, `Fragment` and legacy `Geometry` with IO variables, interpolation decorations, exports and vertex emission; multi-block functions still use the dispatcher fallback when the CFG is not reducible | NGG mesh, tessellation |
| Waves | thread ids as `LocalInvocationId`, compare masks as `OpGroupNonUniformBallot` (wave32 or wave64 masks), `v_readfirstlane` as `OpGroupNonUniformBroadcastFirst`, one-lane interpreter model | per-lane EXEC predication of stores, `v_readlane`/`v_writelane`, `ds_swizzle`, quad ops |
| Resources | one test buffer at set 0 binding 0 (`--test-abi`) | T#/V#/S# decode, SRT/heap access, push constants |
| Verification | `tests/sweep.ps1`: 82 fixtures translated, driver-accepted and ISA-probed in parallel, none refused; VS/PS and VS/GS/PS pipelines rasterised by `SR.VkDraw`; console container executed; coverage regenerated per generation | title shader pack sweep |

## 6. Roadmap

Each phase ends with something executable, not with a table entry.

1. **Container (landed).** Read a Series X shader container down to the stage's
   code and metadata; feed the code to the existing GFX10 pipeline. Acceptance:
   the same machine code delivered as HSACO and as a console container decodes to
   the same instruction stream and emits byte-identical SPIR-V; the emitted
   module passes `spirv-val` and, when the fixture is observable, runs on the
   detected GPU with the same result. `tests/corpus/xbox/` is that fixture; the
   root layout and used-resource tables are still to be decoded.
2. **RDNA2 decode and lift (landed).** `VINTRP` and `EXP` were the two
   encodings the decoder did not know; both are decoded now (interpolation step
   in the opcode field, attribute slot and channel as fields; export target and
   enabled channels in the first dword with four source bytes in the second) and
   lifted into stage IO. A VOP3-encoded compare also names its mask destination
   in the destination field and keeps the VOPC opcode numbering, which the
   lookup and the operand decoder now honour. Acceptance: the gfx1030 vertex and
   fragment fixtures lift and emit; `tests/sweep.ps1` reports every fixture
   accepted by the driver.
3. **Graphics stages (landed).** Execution model from `Module::stage`, interface
   variables with location/builtin and interpolation decorations, vector types,
   component access chains, `OriginUpperLeft` and `DepthReplacing`. Acceptance:
   `SR.VkDraw` renders the translated pair on the RX 9060 XT (2080 of 4096
   pixels written, centre `255 0 0 255`) and prints the driver ISA of both
   stages.
4. **Waves (ballot landed).** Thread ids arrive as `LocalInvocationId`, a
   compare mask becomes `OpGroupNonUniformBallot` and the lifter writes one
   SGPR (wave32) or the pair (wave64). Acceptance: `waveballot` stores
   `popcount(ballot(tid < 1000))`, which is 64 on the RX 9060 XT in wave64 mode
   and 1 in the one-lane interpreter, from the same module. Predicating stores
   on EXEC and `v_readlane`/`v_writelane`/`ds_swizzle` remain.
5. **Resources (landed for the common cases).** User data as constants or a
   host-filled push block; V# and T#/S# resolved to buffer device addresses and
   image/sampler bindings; `image_load`, `image_store` and `image_sample`
   verified on the device, and typed buffers for the raw 32-bit formats.
   Converting formats (the data/number format table) and the tiled-surface
   decode remain; they are a format system, not an opcode gap. See section 5a
   for the plan taken from AnyPS5.
6. **Merged stages (legacy GS landed, NGG open).** A legacy geometry program is
   lifted from its exports: `exp pos0..pos3`/`exp param<n>` write the current
   vertex's outputs, the `pos0` that starts the next vertex emits the previous
   one, `exp prim` (target 20) emits the last vertex and ends the primitive, and
   `s_endpgm` flushes whatever is open. The emitter publishes plain per-vertex
   outputs with `OpEmitVertex`/`OpEndPrimitive` (geometry outputs are not
   arrays), and the execution modes carry the topologies and vertex budget,
   which arrive through `LiftOptions` because a raw code object does not state
   them. Geometry *inputs* are arrays over the primitive's vertices:
   `ds_read` on the input ring names the vertex in the address and the output
   register in the immediate, and lifts to an indexed `StageInput`, so a GS
   consumes the ES outputs through the normal Vulkan interface. Acceptance:
   `gfx1030/geometry_read` lifts, interprets against the triangle's vertices
   and draws a half-size copy (`904` of 4096 pixels, centre `255 0 0 255`) -
   the same image a vertex stage computing those positions produces without a
   geometry stage. Acceptance: `gfx1030/geometry` lifts, interprets, emits validated
   SPIR-V and draws with an empty vertex stage and the fragment fixture - 4096
   of 4096 pixels, centre `255 0 0 255` - and the driver reports the merged
   `_amdgpu_gs_main` with `s_sendmsg(MSG_GS_ALLOC_REQ)`. NGG programs, and the
   ES/GS pairing that reads the GSVS ring, remain diagnostics.
7. **Sweep (done for the corpus).** `tests/sweep.ps1` translates every
   checked-in fixture in parallel, probes each compute module with the driver's
   compiler and draws the graphics pipelines: 82 of 82, none refused, in about
   two seconds. `tools/coverage.py` reports the translation share per generation
   from the ISA XML; the current numbers are gfx10 667/1149 (58.1%), gfx1030
   667/1153 (57.8%) and gfx1200 1344/1264 - the last above 100% because the
   tool counts table entries (aliases included) against the XML's instruction
   list, so treat it as a relative measure, not a percentage of a real shader.
   The title shader-pack sweep needs real data.

Phases 5-6 extend the IL and the emitter, not the number of special cases: the
stages and resources already have IL declarations (`Module::inputs`,
`outputs`, `resources`, `executionModes`).

### 5a. Resource plan, from AnyPS5

AnyPS5 is the only RDNA2 (gfx1013, PS5) to SPIR-V recompiler with a shipped
resource model, and its shape is the one to copy:

- **Descriptors are values, not bindings.** A V#/T#/S# is decoded from guest
  memory: the shader's SRT walk (`s_load_dwordx4` from user SGPRs, or user data
  itself) is evaluated on the CPU, and the resulting descriptor words are
  either specialized into the module (formats, dimensions, compare/gather,
  swizzles) or carried as runtime metadata (`ResourceMetadata`) when the
  descriptor is only known per draw.
- **Two artifacts per stage.** A *registration* form reads descriptor words at
  run time and is pipeline-library friendly; a *specialized* form folds them
  into the module. AnyPS5 emits the first for pixel stages and the second for
  vertex stages, because a vertex buffer load would otherwise decode a V# per
  vertex.
- **Runtime tables become descriptor arrays.** A T#/S# whose offset the walker
  cannot evaluate (`key * stride + addend`) is planned as a `TableColumn` and
  bound as a variable-count `SAMPLED_IMAGE`/`SAMPLER` array indexed by a map
  buffer; a descriptor the walker *can* evaluate is selected and indexed at
  entry 0.
- **User data is a push constant block** (256 bytes, 128 per stage when
  libraries split the sets); buffers/images are buffer device addresses, for
  which our `TargetProfile::physicalStorageBuffer` is already enabled.
- **The ABI is shared with the host**: `RuntimeAbi.hpp`, `BdaAbi.hpp` and
  `ImageTableAbi.hpp` define the exact layouts the driver fills, which is how
  the recompiler stays independent of the guest memory it cannot see at
  compile time.

For this project the same layering applies with a smaller host: decode the
descriptor from user data (or a literal `s_load`), specialize what is constant,
and bind what is not as one SSBO/image array; the test ABI already gives set 0
binding 0.

User data is seeded today as constants (`LiftOptions::userData`, `--user-data`),
which is the specialization half: the console's root constants and a descriptor
whose base the host tells us in advance. The next step is the other half, a push
constant block the harness fills with the device address it actually bound, so a
fixture can hand a real V# to a `buffer_load_dword` and the module reads guest
memory through a buffer device address.

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
