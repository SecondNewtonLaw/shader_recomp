# ISA survey: NVIDIA, AMD, Intel

Status: final survey, no implementation. It records what `COMMON_SHADER` must
express so that each vendor's instruction set maps into it without special
cases. The normative IL model is in [IL_ARCHITECTURE.md](IL_ARCHITECTURE.md).

## 1. Sources and method

| Vendor | Primary source | Notes |
|---|---|---|
| AMD | [RDNA3 ISA reference](https://docs.amd.com/v/u/en-US/rdna3-shader-instruction-set-architecture-feb-2023_0), [machine-readable ISA XML](https://gpuopen.com/machine-readable-isa/) (MIT, RDNA1-4, CDNA1-5) | Opcode counts below come from parsing the XML `InstructionName` entries |
| NVIDIA | [CUDA Binary Utilities, ch. 4](https://docs.nvidia.com/cuda/cuda-binary-utilities/index.html) (Turing, Ampere/Ada, Hopper, Blackwell/Rubin), [PTX ISA](https://docs.nvidia.com/cuda/parallel-thread-execution/index.html) | SASS has no full public encoding spec; the opcode lists and naming are official |
| Intel | [IGC vISA documentation](https://github.com/intel/intel-graphics-compiler/blob/master/documentation/visa/6_instructions.md) (MIT), [datatypes](https://github.com/intel/intel-graphics-compiler/blob/master/documentation/visa/2_datatypes.md), [operands](https://github.com/intel/intel-graphics-compiler/blob/master/documentation/visa/5_operands.md), [Gen11 architecture](https://cdrdv2-public.intel.com/686065/the-architecture-of-intel-processor-graphics-gen11-r1new.pdf) | vISA is the virtual ISA that IGC lowers; final Xe encodings are not fully public |

AMD's XML gives exact mnemonic counts per architecture:

| Architecture | Mnemonics |
|---|---|
| RDNA1 | 1149 |
| RDNA2 | 1153 |
| RDNA3 | 1457 |
| RDNA3.5 | 1517 |
| RDNA4 | 1622 |
| CDNA1 | 1185 |
| CDNA5 | 1664 |

NVIDIA's published SASS list:

| Architecture | Mnemonics |
|---|---|
| Turing | 161 |
| Ampere / Ada | 166 |
| Hopper | 199 |
| Blackwell / Rubin | 254 |

Intel vISA documents 122 mnemonics, before message and region variants.

The counts are mnemonic-level, not semantic-level. Most AMD entries are the
same operation repeated across types, encodings and modifiers (`V_ADD_F16`,
`V_ADD_F32`, `V_ADD_F64`, VOP2/VOP3 forms, DPP/SDWA forms). A descriptor table
must be compositional, not 1600 rows.

## 2. At a glance

| Dimension | NVIDIA SASS | AMD RDNA / CDNA | Intel Gen / Xe |
|---|---|---|---|
| Lockstep width | 32 | 32 or 64 | exec size 1-32 per instruction |
| Uniform path | UR registers, UP predicates, `U*` ops | SGPR, SALU (`S_*`) | address registers, scalar `SEND`/state, no separate uniform ALU |
| Divergence | per-thread PC, predicates `@P`, `BSSY`/`BSYNC` | `EXEC` mask, scalar branches | per-instruction predicate + EM/VM masks |
| Register file | 255 GPR/thread, UR, P | 256 VGPR, SGPR, AGPR, EXEC/VCC/SCC | GRF (128/256), flag, predicate, address |
| Low precision | F64/F32/F16/BF16/FP8/FP4 | F64/F32/F16/BF16/FP8/FP6/FP4 | F64/F32/F16/BF16/FP8/TF32 |
| Memory spaces | global, shared, local, constant, tensor | global, flat, scratch, LDS, GDS, constant, buffer, image | global, SLM, local, SVM, surface, URB |
| Resource access | `desc[UR]`, texture/surface units | `V#` buffer and `T#` image descriptors | surface/sampler state through `SEND` |
| Matrix | HMMA/IMMA/BMMA/DMMA, Blackwell OMMA/QMMA | WMMA/SWMMAC/MFMA | DPAS/DPASW |
| Fixed function | TEX/surface opcodes | MIMG/EXP opcodes | generic `SEND` messages |
| Control | BRA/BRX/JMP/JMX/CALL/RET, BSSY/BSYNC | S_BRANCH/S_CBRANCH/S_SETPC/S_SWAPPC | JMP/GOTO/CALL/IFCALL/SWITCHJMP |

## 3. AMD

Program model: a wave executes scalar ALU instructions once per wave and vector
ALU instructions once per active lane. `EXEC` is the per-lane execution mask;
vector compares and mask operations write it. Scalar branches choose the wave's
next block, so control flow is wave-level and vector predication is data-level.
Wave width is 32 or 64.

| Family | Examples | Notes |
|---|---|---|
| Scalar ALU (SOP1/2/K/C, SOPP) | `S_MOV_B32`, `S_ADD_I32`, `S_CMP_LT_I32`, `S_AND_SAVEEXEC_B32`, `S_CBRANCH_EXECZ`, `S_BRANCH`, `S_CALL_B64`, `S_SWAPPC_B64`, `S_WAITCNT`, `S_BARRIER`, `S_SENDMSG`, `S_TRAP`, `S_ENDPGM` | Uniform arithmetic, control flow, mask bookkeeping, waits |
| Vector ALU (VOP1/2/3/3P/D) | `V_ADD_F32`, `V_FMA_F32`, `V_MUL_HI_U32`, `V_MIN3_F32`, `V_CNDMASK_B32`, `V_CMP_LT_F32`, `V_CVT_F32_F16`, `V_RCP_F32`, `V_SQRT_F64`, `V_MBCNT_LO_U32_B32`, `V_READLANE_B32`, `V_WRITELANE_B32`, `V_PERMLANE16_B32`, `V_MOVRELD_B32` | Per-lane arithmetic, conversions, cross-lane movement, indexed register access |
| Dot / matrix | `V_DOT2_F32_F16`, `V_DOT4_I32_IU8`, `V_DOT8_I32_IU4`, `V_WMMA_F32_16X16X16_F16`, `V_SWMMAC_F32_16X16X32_FP8_FP8` | Accumulating dot products and matrix tiles |
| Scalar memory | `S_LOAD_DWORDX4`, `S_BUFFER_LOAD_DWORD`, `S_DCACHE_INV`, `S_GL1_INV` | Constant/descriptor loads |
| Vector memory | `GLOBAL_LOAD_DWORDX2`, `FLAT_STORE_B32`, `SCRATCH_LOAD_DWORD`, `BUFFER_LOAD_DWORDX4`, `BUFFER_ATOMIC_ADD`, `IMAGE_SAMPLE`, `IMAGE_LOAD`, `IMAGE_ATOMIC_ADD`, `TBUFFER_LOAD_FORMAT_X` | Global/flat/scratch, descriptor-based buffer and image access |
| LDS / GDS | `DS_READ_B32`, `DS_WRITE2_B32`, `DS_ADD_U32`, `DS_CMPST_F32`, `DS_APPEND`, `DS_CONSUME`, `DS_GWS_BARRIER`, `DS_SWIZZLE_B32` | Workgroup scratchpad and atomics |
| Graphics | `EXP` | Position/parameter/color export |
| Ray tracing (RDNA4) | `BVH_INTERSECT_RAY`, `BVH8_INTERSECT_RAY`, `IMAGE_BVH_INTERSECT_RAY`, `DS_BVH_STACK_PUSH4_POP1_RTN_B32` | Ray/BVH traversal and BVH stack management |
| CDNA5 additions | `TENSOR_LOAD_TO_LDS`, `TENSOR_STORE_FROM_LDS`, `CLUSTER_LOAD_ASYNC_TO_LDS_B128`, `S_WAIT_TENSORCNT` | Tensor memory and cluster async copies |

Operand and instruction modifiers that carry semantics:

- source negate and absolute value, output modifier (mul by 0.5/2/4), clamp;
- `SDWA` sub-dword addressing (byte/word select, merge) and 16-bit forms;
- `DPP` lane permutation in the instruction itself;
- SGPR, VGPR, AGPR, literal constant and inline constant operands;
- `V_CMP` results are lane masks in SGPR/VCC, not booleans.

## 4. NVIDIA

SASS is the machine ISA. NVIDIA publishes the opcode list and operand shapes but
not the full encoding or every semantic detail. PTX is the documented virtual
ISA and is useful as a semantic reference, not as the input format.

| Family | Examples | Notes |
|---|---|---|
| FP32/FP64 | `FADD`, `FMUL`, `FFMA`, `FMNMX`, `FSEL`, `FSETP`, `MUFU`, `DADD`, `DFMA`, `DMUL`, `DSETP`, `FRND` | `MUFU` covers rcp/rsq/sqrt/sin/cos/ex2/lg2/tanh |
| FP16 / packed | `HADD2`, `HMUL2`, `HFMA2`, `HSET2`, `HSETP2`, `HMNMX2` | Two lanes per register |
| Integer | `IADD`, `IADD3`, `IMAD`, `IMUL`, `IMNMX`, `IABS`, `ISETP`, `ISCADD`, `LEA`, `LOP3`, `SHF`, `SHL`, `SHR`, `POPC`, `FLO`, `BMSK`, `BREV`, `SGXT`, `PRMT` | `IMAD` doubles as move and address arithmetic |
| SIMD-in-register | `VIADD`, `VIADDMNMX`, `VIMNMX`, `VHMNMX`, `VABSDIFF4` | Packed integer/half arithmetic |
| Conversions | `F2F`, `F2I`, `I2F`, `I2I`, `I2IP`, `I2FP`, `F2IP` | Packing variants included |
| Predicate | `PLOP3`, `PSETP`, `P2R`, `R2P` | Predicates are first-class registers |
| Move / cross-lane | `MOV`, `MOVM`, `SEL`, `SHFL`, `MATCH`, `VOTE`, `VOTEU` | Warp shuffle, match, vote |
| Memory | `LD`, `ST`, `LDG`, `STG`, `LDC`, `ULDC`, `LDL`, `STL`, `LDS`, `STS`, `LDSM`, `STSM`, `LDGSTS`, `LDGDEPBAR` | Generic/global/constant/local/shared, async global-to-shared |
| Atomics / reductions | `ATOM`, `ATOMS`, `ATOMG`, `RED`, `REDS`, `REDG`, `REDUX` | Return value optional; `REDUX` reduces to a uniform register |
| Barriers / fences | `BAR`, `MEMBAR`, `FENCE`, `DEPBAR`, `B2R`, `R2B` | Named barriers and dependency barriers |
| Texture / surface | `TEX`, `TLD`, `TLD4`, `TMML`, `TXD`, `TXQ`, `SULD`, `SUST`, `SUATOM`, `SURED` | Sampler, gather4, LOD, derivatives, surface atomics |
| Uniform datapath | `UIADD3`, `UIMAD`, `ULOP3`, `USHF`, `ULDC`, `UMOV`, `USEL`, `USETP`, `R2UR`, `S2UR`, `REDUX`, `VOTEU` | Scalar-per-warp counterparts of common ops |
| Control | `BRA`, `BRX`, `BRXU`, `JMP`, `JMX`, `JMXU`, `CALL`, `RET`, `EXIT`, `KILL`, `BPT`, `NANOSLEEP`, `YIELD`, `WARPSYNC`, `BSSY`, `BSYNC`, `BMOV`, `ELECT` | Direct/indirect branches, calls, convergence barriers, leader election |
| Special registers | `S2R`, `CS2R`, `S2UR`, `LEPC`, `RPCMOV`, `SETCTAID`, `PMTRIG` | Thread/CTA/lane/clock/SM identifiers, PC |
| Tensor (Hopper) | `HGMMA`, `IGMMA`, `QGMMA`, `BGMMA`, `WARPGROUP`, `WARPGROUPSET` | Warpgroup-scope MMA |
| Tensor (Blackwell) | `OMMA` (FP4), `QMMA` (FP8), `DMMA`, `HMMA`, `IMMA`, `LDT`, `LDTM`, `STT`, `STTM`, `UTC*`, `UTMA*` | Tensor memory, uniform tensor ops, bulk tensor copies |
| Blackwell additions | `FADD2`, `FMUL2`, `FFMA2` (dual-issue FP32), `MOV64IUR`, `DECOMPRESS`, `SPARSIFY`, `UBLKCP`, `UBLKRED`, `CREDUX`, `VISET` | Vector compression, bulk copies, coupled reductions |

Semantic modifiers seen in SASS text:

- `.FTZ` flush-to-zero, `.SAT` saturate, `.REUSE`/control codes are scheduling
  hints (not semantics), `.E`/`.SYS`/`.STRONG`/`.MMIO`/`.CONSTANT`/`.LU`/`.CV`
  memory qualifiers, `@P`/`@!P` predication, negate/absolute source modifiers,
  `.WIDE` address arithmetic, `.64` operand widths.

Notable generational drift: Blackwell drops the Hopper `HGMMA/IGMMA/QGMMA/BGMMA`
names in favour of `OMMA/QMMA` plus uniform tensor ops, and adds FP32
dual-issue (`FADD2`, `FFMA2`) and packed FP32 (`FHADD`, `FHFMA`, `FHMUL2`).
The IL must not treat a mnemonic as a stable identity across generations.

## 5. Intel

Intel's public vISA is a virtual instruction set with explicit SIMD regions.
Execution size is per instruction (1-32), predicates select channels, and
memory/fixed-function work is performed by message sends.

| Family | Examples | Notes |
|---|---|---|
| Arithmetic | `ADD`, `ADD3`, `ADD3O`, `ADDC`, `SUBB`, `MUL`, `MULH`, `MAD`, `MADW`, `AVG`, `DP4A`, `DPAS`, `DPASW`, `SAD2`, `SAD2ADD` | Carry/borrow and overflow outputs are explicit |
| Math | `DIV`, `DIVM`, `INV`, `INVM`, `RSQRT`, `RSQTM`, `SQRT`, `SQRTM`, `LOG`, `EXP`, `POW`, `SIN`, `COS`, `FRC`, `LRP`, `RNDD/RNDE/RNDU/RNDZ`, `SRND`, `FCVT`, `PLN` | `SRND` stochastic rounding, `FCVT` FP8/TF32 conversion |
| Logic / shift | `AND`, `OR`, `XOR`, `NOT`, `SHL`, `SHR`, `ASR`, `ROL`, `ROR`, `BFE`, `BFI`, `BFN`, `BFREV`, `CBIT`, `FBH`, `FBL`, `LZD` | Bitfield and bit-scan family |
| Compare / select | `CMP`, `SETP`, `SEL`, `MIN_MAX`, `VISET` | Results are predicates |
| Data movement | `MOV`, `MOVS`, `ADDR_ADD` | State operands and address registers |
| LSC memory | `LSC_TYPED`, `LSC_UNTYPED`, `LSC_FENCE` | Unified load/store/atomic messages for Xe |
| Surface memory | `OWORD_LD/ST`, `GATHER_SCALED`, `SCATTER_SCALED`, `GATHER4_TYPED`, `SCATTER4_TYPED`, `DWORD_ATOMIC`, `TYPED_ATOMIC`, `MEDIA_LD/ST` | Typed/untyped surface access, gather/scatter |
| SVM | `SVM_BLOCK_LD/ST`, `SVM_GATHER`, `SVM_SCATTER`, `SVM_ATOMIC` | Shared virtual memory |
| 3D / fixed function | `3D_LOAD`, `3D_SAMPLE`, `3D_SAMPLE4`, `SAMPLE_UNORM`, `INFO`, `RT_WRITE`, `URB_WRITE` | Sampler and render-target/URB traffic |
| Message | `RAW_SEND`, `RAW_SENDS` | Arbitrary shared-function messages |
| Sync | `BARRIER`, `NBARRIER`, `SBARRIER`, `FENCE`, `CACHE_FLUSH`, `WAIT`, `YIELD` | Thread-group and split-phase barriers |
| Control | `JMP`, `GOTO`, `CALL`, `FCALL`, `IFCALL`, `RET`, `FRET`, `SUBROUTINE`, `SWITCHJMP`, `LABEL`, `FADDR` | SIMD and function control flow |

Operand model that matters for the IL:

- **Region addressing.** A source operand names a 2D region of a GRF:
  `V( R, C )<VertStride; Width, HorzStride>`, with legal widths 1/2/4/8/16 and
  strides 0/1/2/4/8/16/32. The same register can be read as different vector
  shapes per instruction. Destination regions are 1D.
- **Predication.** A predicate field has inverse and combine (`any`/`all`)
  semantics plus a mask control (`M1`..`M8`, `NoMask`) selecting which EM bits
  apply. Disabled channels do not write their destination.
- **Indirect operands.** Address registers select elements or whole variables;
  multi-address indirect reads gather from different variables.
- **Types.** UD/D, UW/W, UB/B, DF/F, HF/BF, UQ/Q, plus packed vectors V/UV
  (4-bit integers) and VF (restricted 8-bit floats), and BOOL.
- **FP modes.** IEEE mode versus ALT (DX9 legacy) mode; denorm behaviour and
  rounding mode are control-register state; saturation clamps to [0,1] for
  floats or the destination integer range.

Intel's fixed-function interface is the most divergent: `SEND` can target
sampler, dataport, URB, gateway and other shared functions with arbitrary
message descriptors. There is no finite opcode list for "everything SEND can
do"; known messages must be normalized, unknown ones must be an explicit
unsupported case or an opaque intrinsic.

## 6. What is common

The same concepts appear everywhere, with different parameters:

| Concept | NVIDIA | AMD | Intel | Common primitive |
|---|---|---|---|---|
| Lockstep group | warp 32 | wave 32/64 | exec size 1-32 | wave metadata + per-invocation predicate |
| Lane predicate | `P0..P6` | `VCC`, `SCC`, EXEC | predicate, EM/VM | bool/mask value |
| Uniform value | UR/UP | SGPR | address register | uniform-qualified value |
| Scalar branch | uniform `BRA` | `S_BRANCH`/`S_CBRANCH` | `JMP` with uniform mask | branch on uniform condition |
| Divergent branch | `@P BRA` | vector compare -> EXEC + scalar branch | predicated `JMP`/`GOTO` | branch on varying condition + masks |
| Call/return | `CALL`/`RET` | `S_CALL`/`S_SWAPPC` | `CALL`/`RET` | call terminator |
| Indirect branch | `BRX`, `JMX` | `S_SETPC` | `SWITCHJMP`, `IFCALL` | indirect branch/switch |
| Cross-lane move | `SHFL`, `MATCH`, `VOTE` | `V_READLANE`, `V_WRITELANE`, `V_PERMLANE`, DPP | sub-group ops (not in public vISA) | wave intrinsics |
| Barrier | `BAR`, `MEMBAR` | `S_BARRIER`, `S_WAITCNT` | `BARRIER`, `SBARRIER`, `FENCE`, `WAIT` | barrier/fence/wait intrinsics |
| Atomic RMW | `ATOM*`, `RED*` | `DS_*`, `BUFFER_ATOMIC*`, `GLOBAL_ATOMIC*` | `DWORD_ATOMIC`, `TYPED_ATOMIC`, `LSC_*` | atomic/reduction op with op enum |
| Descriptor access | `desc[UR]` | `V#`/`T#` | surface/sampler state | resource handle + offset |
| Texture | `TEX`, `TLD`, `TXD` | `IMAGE_SAMPLE`, `IMAGE_LOAD` | `SAMPLE*`, `3D_SAMPLE*` | texture intrinsic |
| Matrix | `HMMA`/`QMMA`/`OMMA` | `V_WMMA`/`V_SWMMAC` | `DPAS`/`DPASW` | tile type + MMA intrinsic |
| Special register | `S2R` | `S_GETREG`, `V0` | `sr0`, `FADDR` | special-register read |
| Async copy | `LDGSTS`, `UTMA*` | `CLUSTER_LOAD_ASYNC_TO_LDS` | `LSC_*` + `SBARRIER` | async copy + token/wait |
| Cache control | `CCTL*` | `S_DCACHE_INV`, `S_GL1_INV` | `CACHE_FLUSH` | cache op / policy attribute |

## 7. What does not map cleanly

These are the areas that force abstraction rather than one-to-one lowering:

1. **Divergence model.** NVIDIA has per-thread PCs and convergence barriers;
   AMD has a wave-level CFG plus `EXEC`; Intel has per-instruction predicates
   and masks. Resolution: masks become per-invocation booleans and control
   flow; branch terminators carry the uniform/varying classification.
2. **Uniform datapath.** AMD SALU and NVIDIA `U*` ops are wave-invariant.
   Resolution: uniformity is a lift-time annotation on values and branches, and
   a wave-uniform destination may only be guarded by a uniform condition.
3. **Fixed function.** Intel's `SEND` is a general message interface. Known
   messages map to texture/memory/URB ops; unknown messages are diagnosed with
   the message descriptor.
4. **Region addressing and sub-register writes.** Intel regions, AMD SDWA and
   16-bit ops, NVIDIA packed half/FP32 pairs, and vector write masks all change
   which bytes of a register are written. SSA construction must normalize these
   at definition; phi nodes cannot track components.
5. **Matrix layouts.** Tile shapes, operand layouts (row/col, transposed,
   strided), accumulator registers and scale factors differ per vendor and
   generation. Resolution: matrix operations are intrinsics with a layout
   payload, and unsupported layouts are diagnosed.
6. **Memory semantics.** Scoreboard waits (AMD `S_WAITCNT`, NVIDIA `DEPBAR`,
   Intel `WAIT`) are not portable fences. If the IL makes memory dependencies
   explicit in SSA, most waits can be dropped at emission; real fences and
   barriers must remain.
7. **Opcode identity drift.** `HGMMA` disappears in Blackwell, AMD renames
   buffer/image forms across RDNA generations, Intel adds `LSC_*` and `DPAS`.
   The IL must key on semantics, not mnemonic strings.

## 8. Requirements for COMMON_SHADER

The concrete IL is specified in [IL_ARCHITECTURE.md](IL_ARCHITECTURE.md). This
section is the requirements list that drove it. The final model simplifies two
points: execution masks are lowered to per-invocation control flow rather than
carried as values, and uniformity is a lift-time annotation rather than a
distinct type.

The final IL uses typed SSA, an explicit CFG, partial-write normalization and an
intrinsic registry. This survey's requirements informed those choices.

### 8.1 Values and types

- `Bool` per invocation for predicates. A `Mask` type (u32/u64) exists only for
  data produced by wave ballots/active-mask queries; execution masks are not
  values.
- Uniformity is a lift-time annotation on values and branches, not a separate
  `Uniform<T>` type.
- First-class scalars: i8/i16/i32/i64, f16, bf16, f32, f64, plus fp8 e4m3/e5m2,
  fp6, fp4 and tf32.
- Packed types: v2f16, v2bf16, v4f8, v4i8, v8i4, and their unsigned
  counterparts, carried as an explicit packed type.
- Matrix/tile operations are intrinsics with a layout payload; a universal
  tile type is not required.
- Pointer with an address space: `Private`, `Workgroup`, `DeviceShared`,
  `Global`, `Constant`, `PushConstant`, `Image`, `Tensor`, `Opaque`.
- `Resource` handle: buffer/image/sampler/tensor, with format and binding
  metadata; loads and samples take a handle plus a byte/element offset.

### 8.2 Execution model

- Execution masks (`EXEC`, Intel masks, NVIDIA predicates) become
  per-invocation boolean guards, then control flow. They are never carried as
  lane-mask values.
- A guard on an instruction is a bool. Pure instructions lower to `Select`;
  effectful instructions lower to a guarded region. Guards are eliminated
  before SSA.
- Branch terminators record whether the condition is uniform or varying. The
  lifter classifies mask-any branches as uniform votes and exec-mask branches as
  varying.
- Convergence hints (`BSSY`/`BSYNC` targets, AMD saveexec patterns, Intel mask
  control) are block metadata for the structurizer, not runtime ops.
- Indirect branches become switches when the target set is finite; otherwise the
  function is flattened into a dispatcher loop.
- Direct calls become function calls. Indirect calls and recursion are rejected
  with a diagnostic.

### 8.3 Instruction attributes

Per-instruction (not global) attributes:

- rounding: RTNE, RTZ, RU, RD, stochastic;
- denorm handling: keep or flush, input/output;
- saturation/clamp;
- NaN policy and fast-math level (`MUFU`, `DIVM`, `RSQTM` are approximate);
- memory order: relaxed/acquire/release/acq_rel, scope: wave/workgroup/
  device/system, volatile, cache policy;
- signedness and wrap/saturate for integer ops;
- predicate guard and predicate result;
- multi-result: carry, overflow, hi/lo, atomic return, lane mask, predicate.

### 8.4 Core operations

Core ops should cover what all three express directly:

- add/sub/mul/fma/mad, two- and three-input min/max, neg/abs, select/cndmask;
- shifts, rotates, funnel shift, bitfield extract/insert, bit reverse,
  population count, count-leading/trailing zeros, find first/last bit;
- byte permute, pack/unpack, sign/zero extend, saturating and packing
  conversions;
- compare producing bool/mask, with ordered/unordered float predicates;
- wave ops: ballot, vote, shuffle (index/up/down/xor), permute, match,
  reduction/prefix, leader election, lane id, quad ops, derivatives;
- memory: load/store (scalar, vector, gather/scatter), atomic/reduction with
  optional return, async copy with token, barrier, fence, wait;
- special-register read;
- texture sample/load/gather/query as one intrinsic family with normalized
  fields (resource, sampler, coord, array, lod, grad, offset, compare);
- tile/MMA as an intrinsic family with layout metadata.

### 8.5 Intrinsics rather than core

- approximate transcendentals and division;
- dot products (`DP4A`, `V_DOT*`, `IDP`), SAD, average;
- packed 4/8-bit arithmetic and FP4/FP6/FP8 conversions;
- matrix multiply and tensor-memory movement;
- texture/surface operations with format-specific behaviour;
- stochastic rounding, vector compress/decompress;
- Intel `SendMessage` with a message descriptor (known messages lowered to the
  normalized ops above; unknown messages are diagnosed);
- hardware-specific special registers (SM id, clock, performance counters).

### 8.6 What must not enter the IL

- physical register numbers and register classes;
- instruction encodings, control/scheduling bits, reuse caches;
- scoreboard counters as first-class values (only fences/barriers survive);
- vendor mnemonics as opcode identity;
- fixed-function message bit layouts (wrap them in a descriptor object);
- per-ISA cache hints that do not change semantics.

## 9. Consequences for the architecture descriptor

1. **Compositional opcode tables.** Define base operations with type, modifier
   and uniformity variants; do not enumerate 1600 AMD rows or 254 SASS rows by
   hand. The descriptor needs `(operation, type variant, modifier set, encoding
   form, lowering)` as separate axes.
2. **Generated descriptors where possible.** AMD ships MIT-licensed XML with
   encodings, operands, formats and descriptions; generate the AMD table from
   it and keep a small semantic overlay for lowering. NVIDIA has no equivalent,
   so its table is hand-maintained against the CUDA opcode list. Intel's vISA
   docs and IGC headers can generate the base table.
3. **Intrinsic fallback.** Every architecture table may mark an opcode
   `Unimplemented` with a reason; the pipeline reports it with the source pc
   instead of emitting wrong code.
4. **Per-ISA policy hooks.** Resource mapping, special-register mapping and
   unsupported fixed-function behaviour are architecture data, not code paths
   in the remapper.

## 10. Resolved decisions

1. **Graphics scope.** The architecture covers every SPIR-V execution model.
   Implementation order is compute, vertex and fragment first; geometry,
   tessellation, mesh/task and ray stages follow. Textures, URB/render-target
   traffic and exports are represented as intrinsics from the start. A stage or
   variant whose signatures or intrinsics are not implemented is diagnosed, never
   approximated.
2. **Uniformity.** A lift-time annotation on values and branch terminators, not
   a separate type.
3. **Execution masks.** Lowered to per-invocation booleans and control flow.
   Only wave ballot/active-mask data keeps a mask type.
4. **Matrix operations.** Cooperative matrix when the target profile supports
   the shape and layout; otherwise a diagnostic. No scalar expansion in version 1.
5. **FP8/FP4.** FP8 emits native SPIR-V float8 when the profile advertises
   `SPV_EXT_float8`, otherwise a defined integer emulation. FP4 always uses
   packed integer emulation.
6. **Intel `SEND`.** Known message families are lowered to dedicated handlers.
   Unknown messages are diagnosed with the message descriptor.
7. **Scoreboard waits.** Dropped from the IL. SSA ordering plus explicit
   barriers and fences is the contract.
