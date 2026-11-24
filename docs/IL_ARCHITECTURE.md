# COMMON_SHADER IL architecture

Status: final, version 1.0. This is the normative IL specification. All design
questions are resolved. [ARCHITECTURE.md](ARCHITECTURE.md) is the high-level
pipeline and [IL_MAPPING.md](IL_MAPPING.md) is the quick mapping table.

The goal is a small, honest translation IR: rich enough to preserve shader
semantics, close enough to SPIR-V that emission is mechanical, and simple
enough that per-ISA work stays in tables.

In one paragraph: `COMMON_SHADER` is an SSA CFG of per-invocation scalar
operations. Execution masks are lowered to booleans and branches before SSA.
Vendor-specific behaviour is either a core operation or an intrinsic with a
payload. The emitter maps core ops directly and dispatches intrinsics to common
handlers. A per-ISA module is an opcode table that calls a lift builder; it
never builds CFG, SSA or SPIR-V itself.

Non-goals: there is no interpreter, no mandatory optimisation pipeline, no
SIMD-lane value model, no execution-mask values, and no per-ISA emitter code.

## 1. Principles

1. **The IL is SPMD, like SPIR-V.** One value per invocation. Wave width,
   exec size and warps are batching, not vector data. Real vectors exist only
   when the shader uses vectors.
2. **Exec masks are control flow.** AMD `EXEC`, Intel masks and NVIDIA
   predicates become per-invocation booleans and CFG edges. Only wave
   operations produce mask-typed data.
3. **Uniformity is an annotation.** It guides structure and is a correctness
   condition when a wave-uniform destination is guarded. It is not a separate
   type.
4. **The IL is data.** There is no interpreter. Opaque constructs are carried
   as intrinsics with payloads and lowered by the emitter or rejected.
5. **One semantic layer.** Vendor mnemonics do not appear in the IL. They map
   to core ops, intrinsics, or a control kind.
6. **Determinism.** IDs follow walk order; dumps are stable and diffable.

## 2. Where the IL sits

```
bytes
  -> decoder (architecture schema)
  -> DecodedProgram: instructions, labels, signatures, resources
  -> lift: virtual registers + guards (per-ISA opcode tables)
  -> lower predication: guards -> select / CFG
  -> build SSA: dominance-frontier phis
  -> verify
  -> COMMON_SHADER::Module (SSA, CFG, typed)
  -> structure: CFG -> SPIR-V regions
  -> emit: SPIR-V words
  -> validate
```

`COMMON_SHADER::Module` is the IL. The virtual-register form exists only inside
the lift pipeline; per-ISA lowering rules emit into a builder that hides it.

## 3. Module structure

```
Module
  Stage stage; entry function; execution modes
  TypeTable types; ConstantTable constants
  ResourceTable resources; SignatureTable inputs/outputs
  PayloadPool payloads
  FunctionTable functions

SignatureDecl
  Space space; uint32_t location; uint32_t component
  TypeId type; Interpolation interpolation
  bool patch; Builtin builtin        // builtin != None => no location

Function
  FunctionId id; std::string name
  std::vector<TypeId> params; std::vector<ValueId> paramValues
  std::vector<BlockId> blocks; BlockId entry
  TypeId returnType

Block
  std::vector<Instruction> body
  Terminator terminator
  std::vector<BlockId> preds, succs
  BlockHint hint                     // optional convergence hint

Value
  TypeId type
  Def def                            // instruction, constant, resource, parameter, undef
  std::vector<Use> uses
  bool uniform                       // lift-time uniformity fact
```

Inputs and outputs are declared in the signature table. `Load`/`Store` with
`Input`/`Output` spaces access them; interpolation, patch and builtin
decorations come from the declaration.

## 4. Types

```
TypeKind
  Void, Bool
  Int { width: 8|16|32|64, signed: bool }
  Float { format: F16|BF16|F32|F64|TF32|FP8_E4M3|FP8_E5M2|FP6|FP4 }
  Vector { element, count }          // genuine vector data
  Packed { element, count }          // low-precision lanes packed in one register
  Matrix { element, columns, rows }
  Array { element, count }
  Struct { members, packed }
  Pointer { space, pointee }
  Mask { width: 32|64 }              // wave ballot/active mask data
  Resource { kind, format, stride }
  Opaque { name }                    // URB, tensor memory, Intel state
```

Rules:

- `Mask` is only produced or consumed by wave intrinsics. It is never an
  execution mask.
- Address spaces: `Private`, `Workgroup`, `DeviceShared`, `Global`, `Constant`,
  `PushConstant`, `Input`, `Output`, `Image`, `Tensor`, `Opaque`.
- Signedness is part of the integer type. Op selection never guesses it.
- `Bool` is per-invocation. There is no lane-vector bool.
- `Packed` covers `v2f16`, `v2bf16`, `v4f8`, `v8i4` and similar. The emitter
  chooses SPIR-V vectors or integer bitcasts; the IL keeps the packing explicit.
- Samplers are `Resource` values with `kind = Sampler`; there is no separate
  sampler type.

## 5. Values and instructions

Every instruction has zero or more results, typed operands, attributes and a
source location:

```cpp
struct Instruction {
    Op op;
    uint16_t subop;                 // ALU/compare/convert subop, or intrinsic id
    SmallVector<ValueId, 2> results;
    SmallVector<ValueId, 4> operands;
    Attrs attrs;
    PayloadId payload;              // 0 = none
    uint32_t sourcePc;
    uint16_t sourceOpcode;          // diagnostics only
};
```

`Value` carries the type; an instruction result is never untyped. Multi-result
instructions are normal: the result order is fixed by the subop/intrinsic spec.

### Op set

| Op | Meaning |
|---|---|
| `Unary` | Neg, Not, FAbs, BitReverse, PopCount, Clz, Ctz, FindFirstSet, Sqrt |
| `Binary` | Integer/float arithmetic, logic, shifts, rotates, min/max, remainder |
| `Ternary` | Fma, IMad, 3-input min/max, clamp, lerp, funnel shift |
| `Compare` | Integer/float compare, ordered/unordered, returns `bool` |
| `Select` | Choose between two values |
| `Convert` | Type conversion with rounding/saturation attributes |
| `Bitcast` | Same-width reinterpretation |
| `Extract` / `Insert` | Composite/vector/sub-register access |
| `Shuffle` | Vector permute (register permute is an intrinsic) |
| `Alloca` | Function-local storage |
| `Load` / `Store` | Memory access |
| `Atomic` | Atomic/reduction with optional return |
| `Phi` | SSA merge |
| `Call` | Direct function call |
| `Kill` | Discard or demote to helper invocation |
| `Intrinsic` | Escape hatch, keyed by intrinsic id |
| `Nop` | No operation |

Constants and undefs are values (`Value::Def`), not instructions.

Terminators:

```
Br(target)
CondBr(condition, trueTarget, falseTarget, uniformHint)
Switch(value, defaultTarget, cases[], uniformHint)
IndirectBr(index, targets[], defaultTarget)     // Switch or dispatcher flattening
Ret(value?)
Unreachable
```

`Kill` with `demote=false` is emitted as the block terminator
(`OpTerminateInvocation` under the Vulkan profile, `OpKill` otherwise); the IL
terminator is unreachable and is not emitted. With `demote=true` it emits
`OpDemoteToHelperInvocation` and the block continues to its terminator.

## 6. Attributes

Attributes are semantic, not cosmetic:

```cpp
struct Attrs {
    Rounding rounding = Rounding::Default;   // RTNE, RTZ, RU, RD, STOCHASTIC
    Denorm denorm = Denorm::Default;         // KEEP, FLUSH_IN, FLUSH_OUT, BOTH
    bool saturate = false;
    bool approx = false;                     // approximate math is allowed
    MemoryOrder order = MemoryOrder::None;   // RELAXED, ACQUIRE, RELEASE, ACQ_REL
    MemoryScope scope = MemoryScope::None;   // WAVE, WORKGROUP, DEVICE, SYSTEM
    bool isVolatile = false;
    CachePolicy cache = CachePolicy::Default;
};
```

Uniformity is a property of values (`Value::uniform`) and branch terminators
(`CondBr::uniformHint`), not an instruction attribute.

Effects are a small bit set used by predication lowering and emission:
`None`, `ReadMemory`, `WriteMemory`, `Atomic`, `Barrier`, `Wave`, `Call`,
`Trap`. Only a pure instruction may be lowered to `Select`; every other effect
forces a guarded region.

`Alloca` carries an element type and element count. `Phi` carries a payload of
`(ValueId, BlockId)` incoming pairs. `Call` carries the callee `FunctionId`.
`Kill` uses `subop = KillDiscard | KillDemote`.

`MUL.FTZ.SAT` is one `Binary(FMul)` with `denorm=FLUSH_BOTH` and
`saturate=true`, not three opcodes. Operations the vendor documents as
approximate (`MUFU`, Intel `DIV`/`INV`/`RSQRT`/`SQRT`/`LOG`/`EXP`/`SIN`/`COS`)
set `approx=true`; IEEE-specified forms (`DIVM`, `SQRTM`, `RSQTM` and their
equivalents) leave it false and use precise sequences.

Floating-point lowering policy:

- Rounding defaults to round-to-nearest-even. RTZ/RU/RD conversions use
  explicit conversion sequences; stochastic rounding is diagnosed unless the
  target profile enables a stochastic-rounding helper.
- Denorm handling uses a module execution mode when every operation in the
  function shares the mode; otherwise it is emulated with bit masks on inputs
  and outputs.
- Saturate emits a clamp after the operation.
- Approximate operations are implementation-defined and may use extended
  instructions or arithmetic sequences, matching the source ISA's approximate
  results.
- NaN behaviour is encoded by the compare subop: ordered compares emit
  `OpFOrd*`, unordered compares emit `OpFUnord*`.
- Alternative (DX9-style) float mode is represented with per-operation
  saturation and special-value handling. If a module mixes incompatible modes,
  the unsupported combination is diagnosed.

## 7. Intrinsics

An intrinsic is a typed operation with a payload. Payloads live in a module
pool so instructions stay small and dumps stay readable.

```cpp
struct IntrinsicSpec {
    IntrinsicId id;
    std::string_view name;
    uint8_t minOperands, maxOperands;
    uint8_t minResults, maxResults;
    Effect effects;
    PayloadKind payload;
    bool requiresUniformControlFlow = false;
};
```

The emitter keeps its own handler table keyed by `IntrinsicId`; handlers are
not part of the IL data.

Built-in intrinsic families:

| Id | Payload | Used for |
|---|---|---|
| `WaveBallot` | none | compare mask -> `Mask` |
| `WaveVote` | `VoteMode` | any/all/uni vote |
| `WaveShuffle` | `ShuffleMode` | index/up/down/xor/butterfly |
| `WavePermute` | `PermuteMode` | DPP/cross-lane permute |
| `WaveMatch` | none | NVIDIA `MATCH` |
| `WaveReduce` / `WavePrefix` | `ReduceOp` | reductions and scans |
| `WaveLeader` | none | NVIDIA `ELECT` |
| `WaveActiveMask` | none | activemask |
| `MaskBit` | none | extract this invocation's bit from a `Mask` |
| `WaveLaneId` | none | lane/subgroup id |
| `Derivative` | `DerivativeMode` | ddx/ddy/fwidth, coarse/fine |
| `Quad` | `QuadMode` | quad broadcast/swap/vote |
| `TextureSample` | `SampleDesc` | sample/load/gather/query |
| `TextureStore` | `SampleDesc` | image/surface stores |
| `ImageAtomic` | `AtomicOp` | image atomics |
| `Mma` | `MmaDesc` | matrix multiply-accumulate |
| `TensorLoad` / `TensorStore` | `TensorDesc` | tensor memory movement |
| `RayQuery` | `RayQueryDesc` | BVH/ray traversal |
| `Send` | `SendDesc` | Intel shared-function messages |
| `SpecialReg` | `SpecialRegId` | thread/CTA/lane/clock/SM ids |
| `Transcendental` | `MathFn` | rcp, rsq, exp, log, sin, cos, tanh |
| `Division` | `DivMode` | integer/float division and remainder |
| `DotProduct` | `DotDesc` | DP4A/DP2/DP4/DP8 |
| `Sad` | none | sum of absolute differences |
| `Average` | `RoundMode` | integer average |
| `Bitfield` | `BitfieldDesc` | BFE/BFI/BMSK when not core |
| `BytePermute` | `PermuteDesc` | NVIDIA `PRMT`, AMD align/permute forms |
| `StochasticRound` | none | Intel `SRND` |
| `Compress` / `Decompress` | `CompressDesc` | Blackwell vector compression |
| `Barrier` | `BarrierDesc` | control barrier |
| `Fence` | `FenceDesc` | memory fence |
| `AsyncCopy` / `AsyncWait` | `AsyncDesc` | cp.async, LDGSTS, TMA, cluster copies |
| `ResourceFromValue` | `ResourceDesc` | dynamic descriptor/handle construction |
| `Sleep` / `Yield` | none | NANOSLEEP, S_SLEEP, YIELD |
| `DebugBreak` / `Trap` | none | trap/breakpoint |
| `StageInput` / `StageOutput` | `StageIoDesc` | interface variable read/write |
| `EmitVertex` / `EndPrimitive` | none | geometry output emission |

Known Intel `SEND` families (sampler, dataport, URB, gateway, LSC, barrier) get
dedicated payload kinds and handlers. Unknown messages are `SendDesc{Unknown}`
and the emitter rejects them with the message descriptor.

`Sleep`, `Yield`, debug markers, lifetime markers and file/line intrinsics are
scheduling or debug hints. Their handlers emit nothing.

### 7a. Geometry

A geometry stage is one invocation that reads its input primitives and emits
vertices. The IL spells that with the same `StageOutput` write a vertex stage
uses, then `EmitVertex` to publish the current vertex, and `EndPrimitive` to
close the strip:

```
StageOutput out loc 0.0 = value   ; the vertex being built
EmitVertex                        ; publish it
EndPrimitive                      ; close the strip
```

Geometry outputs are the plain per-vertex variables every stage uses, not
arrays: `OpEmitVertex` captures the current values, so the next vertex simply
overwrites them. (Arrayed geometry outputs are rejected by the AMD driver;
glslang emits the plain form too.)

Geometry *inputs* are the opposite: the primitive is an array over its
vertices, so `StageInput` carries the input vertex as its only operand and the
emitter indexes the input array with it. The RDNA input ring names that vertex
in the byte address (`vertex * 256`) and one output register in the immediate
offset, position first and then one four-component location per register.

A shader that publishes vertices by message - the NGG form, `ds_write` into
the vertex's slot followed by `s_sendmsg GS_OP_EMIT` - stages them instead:
`geometry.stage-store` writes one component of slot *k* and
`geometry.stage-load` reads it back when the message publishes the slot. The
staging is a Function-local array of `outputVertices * outputRegisters * 4`
floats, so it is per invocation; the slot a message publishes is an internal
counter, and `GS_OP_EMIT_CUT` also ends the strip. Both intrinsics have no
effect outside the emitter's staging, and the interpreter keeps the staged
dwords so an emitted vertex can be asserted.

Pipeline state that no instruction carries lives in the execution modes:
`inputTopology`, `outputTopology` and `outputVertices`. The emitter turns them
into `Triangles`/`InputLines`/..., `OutputTriangleStrip`/... and
`OutputVertices N`; the lifter takes them from `LiftOptions`, because a raw
code object does not say what the pipeline feeds it. The source pc and opcode
are attached to every intrinsic.

The RDNA form this maps from: `exp pos0..pos3` and `exp param<n>` write one
vertex's outputs, and the `pos0` export that begins the next vertex closes the
previous one; `exp prim` (target 20) closes the last vertex and ends the
primitive; `s_endpgm` flushes whichever of the two is still open. That is the
legacy export path. NGG (`s_sendmsg GS_ALLOC_REQ`) is still a diagnostic:
its vertex data lives in an allocated ring that the wave reads with `ds_*`, not
in the export path.

## 8. Memory and resources

Addresses are byte-based and space-qualified:

```cpp
struct Address {
    Space space;
    ValueId base;        // pointer value or resource handle
    ValueId byteOffset;
    TypeId pointee;
    uint32_t alignment;  // 0 = unknown
};
```

Static resources are module declarations:

```cpp
struct ResourceDecl {
    ResourceId id;
    ResourceKind kind;   // Buffer, Image, Sampler, Tensor, PushConstant, Opaque
    uint32_t set, binding;
    Format format;
    uint32_t stride;
    TypeId element;      // scalar storage element; u32 for untyped buffers
};
```

A `Resource`-typed value with `Def::Resource(resourceId)` refers to a
declaration. `ResourceFromValue` constructs a handle from a raw 64-bit device
address; every other dynamic descriptor value is diagnosed.

Access policy (normative):

- `Load` / `Store` take an address, an access type and width, and attributes.
- `Atomic` takes an address, an operation, a value, an optional comparator, and
  order/scope attributes; the result is optional.
- The IL uses byte offsets. A resource declaration names its scalar storage
  element (`element`: u8/u16/u32/u64/f16/f32/f64; u32 for untyped buffers) and
  stride. Composite resources are read as sequences of scalar elements.
- The emitter converts a byte access to element accesses:
  `elementIndex = byteOffset / sizeof(element)`,
  `shift = byteOffset % sizeof(element)`. Values wider than one element are
  assembled from consecutive elements. Stores are read-modify-write for
  partially covered elements.
- No access assumes more alignment than `Address.alignment` or the declared
  element alignment. Misaligned access is lowered with element-neighbour loads
  and bit operations; a misaligned typed `OpLoad`/`OpStore` is never emitted.
- Atomics require the byte offset to be a multiple of the element size and the
  element to be a 32- or 64-bit integer/float. Otherwise the instruction is
  rejected with a diagnostic.
- Raw pointer access uses `Pointer{space}` values. `Global` uses
  `PhysicalStorageBuffer`; `Private`, `Workgroup`, `Constant`, `Input` and
  `Output` use their logical storage classes. `DeviceShared` maps to `Global`
  with a dedicated buffer; if the target cannot provide it, the access is
  rejected.
- Non-atomic accesses with `order != None` are emitted as a relaxed access
  surrounded by `Fence` with the requested order and scope.
- `CachePolicy` is a performance hint and is dropped. `isVolatile` is emitted.
  Cache-invalidate instructions (`S_DCACHE_INV`, `S_GL1_INV`) become a
  device-scope fence; performance-monitor triggers and debug markers are
  dropped.
- Bounds are not checked. Out-of-bounds behaviour is undefined in the source
  ISA and remains undefined in the output.
- Static resource bindings are supported. `ResourceFromValue` is accepted only
  for a raw 64-bit device address under the `PhysicalStorageBuffer` capability.
  Bindless descriptor arrays and arbitrary 128-bit descriptor arithmetic are
  rejected with a diagnostic.
- Scoreboard waits (`S_WAITCNT`, `DEPBAR`, `WAIT`) are not represented. SSA
  ordering plus barriers/fences is the contract.
- Async copy produces a token; `AsyncWait` consumes it. Barriers and fences are
  separate intrinsics with explicit scope and order.

## 9. Predication, SSA and structuring

### 9.1 Lift builder

Per-ISA lowering rules never touch CFG, SSA, guards or SPIR-V. They call a
builder that owns the register file and the current guard:

```
LiftBuilder
  ValueId readReg(RegClass, index, Type)          // emits extract for sub-registers
  void    writeReg(RegClass, index, Value, uint32_t writeMask)  // emits insert
  ValueId unary(Op, Value)
  ValueId binary(Op, Value, Value, Attrs)
  ValueId ternary(Op, Value, Value, Value, Attrs)
  ValueId compare(Op, Value, Value)
  ValueId convert(Type, Value, Attrs)
  ValueId intrinsic(IntrinsicId, operands, PayloadId)
  void    setGuard(ValueId bool)                  // current execution predicate
  void    branch(BlockId target)
  void    condBranch(BranchKind, ValueId cond, BlockId t, BlockId f)
  BlockId newBlock()
  BlockId currentBlock()
  void    diagnostic(Code, pc, args...)
```

`BranchKind` is `Uniform`, `AnyMask` or `ExecMask`; it tells the builder how to
turn a mask condition into a bool. The builder records the
current guard and emits the virtual-register form. Everything after that
(predication, SSA, structuring) is common and invisible to the ISA table.

### 9.2 Lift form

The lifter tracks, per block, a per-invocation active predicate (`exec`) as a
boolean expression. Every emitted operation may carry a `guard`:

- AMD: `EXEC` is the guard; `S_AND_SAVEEXEC` updates it and saves the old one;
  `S_CBRANCH_EXECZ` is a branch on the current guard.
- NVIDIA: `@P` is the guard; `BSSY`/`BSYNC` are structure hints.
- Intel: predicate + mask control is the guard; `NoMask` means true.

Control-register writes that select floating-point modes (`S_SETREG` on the AMD
MODE register) update the lifter's rounding/denorm attributes for subsequent
instructions and are not emitted. Reads become `SpecialReg`; unknown control
registers are diagnosed.

Branch conditions are classified by what they test, not by their mnemonic:

| Condition source | IL branch |
|---|---|
| scalar compare / SCC / uniform predicate | `CondBr` with `uniformHint = true` |
| wave mask "any/none" (AMD `VCC`, a ballot) | `CondBr` on `WaveVote(Any/All, p)`, uniform |
| execution mask (AMD `EXEC`, Intel mask, NVIDIA `@P`) | `CondBr` on the per-invocation guard, varying |

The classification is part of the architecture's `Control` lowering data. The
lifter must not treat `S_CBRANCH_VCCNZ` and `S_CBRANCH_EXECZ` as the same
shape: the first is a wave-uniform branch that leaves `EXEC` unchanged, the
second is the execution mask itself.

A scalar result derived from a mask test (`SCC = new mask != 0`, `VCC != 0`) is
materialized as `WaveVote(Any/All, maskBool)` and is marked uniform.

Only structured divergence is accepted. The lifter recognizes the
saveexec/restore pattern around a branch, including nested forms. An EXEC update
that does not correspond to a structured divergence pattern, or that re-enables
lanes across unrelated blocks, is diagnosed rather than guessed.

Guards exist only in the lift form.

### 9.3 Lower predication (before SSA)

Run on virtual registers, before SSA construction:

- **Pure instruction with guard `g`**: emit the operation unguarded, then
  `result = select g, new, previousValue(destination)`. Multi-result ops select
  each result. If there is no previous value, use `Undef`.
- **Effectful instruction with guard `g`**: split the block into
  `CondBr g, body, cont`; emit the instruction in `body`; `Br cont`. Memory,
  atomics, barriers, traps, kills and calls are effectful. Consecutive
  effectful instructions with the same guard share one region.
- **Guarded terminator**: fold the guard into the condition.
- **Mask used as data**: if an execution mask escapes into arithmetic or
  memory, materialize it with `WaveBallot(guard)` and convert it back with
  `MaskBit` where a per-invocation boolean is needed. This is the only path from
  an execution mask to a value, and materialization is only legal at a uniform
  point; the emitter rejects it under a varying guard.
- **Uniform destinations** may only be guarded by a uniform condition. A
  varying guard on a wave-uniform result is a lift error, not a select.

After this pass there are no guards in the IL.

### 9.4 SSA construction

Standard dominance-frontier construction over the virtual-register CFG:

- insert phis where a register is live across a merge;
- rename definitions and uses;
- partial writes are already whole-register values (see 9.5);
- uninitialized reads become `Undef` and produce a diagnostic;
- `Alloca` values are pointers and are not SSA-renamed.

### 9.5 Partial writes

A write to part of a register is lowered at the definition:

- 16-bit half, byte, SDWA select/merge, Intel sub-element: `Bitcast` to the
  small type, `Insert` into the whole-register value;
- vector component write: `Insert` into a vector value;
- packed result: the producer emits the packed type directly.

Phi nodes therefore only ever merge whole values. There is no component-wise
SSA and no per-lane SSA.

Indexed register access (`V_MOVRELS`, `V_MOVRELD`, `V_SWAPREL`, address-register
operands) cannot be represented as direct SSA. The lifter materializes the
affected register class as a function-local `Alloca` array and rewrites the
access to load/store. A register range that cannot be bounded is diagnosed.

### 9.6 Structuring

The IL is an unstructured CFG. The structurizer produces SPIR-V structured
regions:

1. compute dominators, post-dominators and the loop forest;
2. classify each block as selection, loop header, loop body, switch, or plain;
3. emit `OpSelectionMerge` / `OpLoopMerge` regions, continue targets and merge
   blocks;
4. lower `CondBr` to selection or loop back edges; `Switch` to `OpSwitch`;
5. `Kill` becomes `OpTerminateInvocation` (Vulkan profile), `OpKill`, or
   `OpDemoteToHelperInvocation`.

Indirect branches and irreducible CFGs share one fallback, dispatch flattening:

- If an `IndirectBr` has a finite, known target set, emit `OpSwitch` directly.
  Indirect targets are translated through the function's block-address map; a
  target outside the function is diagnosed.
- Otherwise, flatten the containing region (or the whole function) into a
  dispatcher loop. The loop state is an i32 block id in function-local storage;
  the loop body is an `OpSwitch` on that state. Each case executes one original
  block, then stores the successor id and branches to the loop continue.
  Exits branch out of the loop; the switch default branches to an `Unreachable`
  block.
- An irreducible SCC is flattened the same way. Reducible regions keep normal
  structured emission.
- Before flattening, every SSA value live across a dispatch edge is spilled to
  a function-local slot (store at the definition's block exit, load at the case
  entry). Phis wholly inside the flattened region are converted to slots.

No CFG shape is rejected. Every function is emittable.

Uniformity-sensitive operations are checked here: SPIR-V requires barriers and
some group/wave operations to execute in uniform control flow. The IL marks
these intrinsics with `requiresUniformControlFlow`; the structurizer rejects
them when the enclosing guard is varying instead of emitting invalid SPIR-V.

### 9.7 Calls

- Direct calls become IL functions and `OpFunctionCall`.
- Arguments and results are passed by value; pointer arguments are allowed.
- Indirect calls are rejected with a diagnostic: SPIR-V has no function
  pointers.
- Recursion, including mutual recursion, is rejected. The verifier checks that
  the call graph is acyclic.
- A call guarded by a varying predicate is wrapped in a region by predication
  lowering, like any other effectful instruction.

## 10. Emission

For each function:

1. emit `OpVariable` for parameters stored in memory and for `Alloca` values;
2. emit types, constants and resource variables in deterministic order;
3. walk blocks in RPO; emit instructions in order;
4. lower intrinsics through the handler table;
5. emit decorations and the entry-point interface;
6. validate with SPIRV-Tools against the Vulkan 1.3 environment; fail the
   pipeline if validation fails.

### Target profile

The emitter is parameterised by one `TargetProfile`. The default profile is:

```
SPIR-V 1.6, Vulkan 1.3
memory model: Logical GLSL450
subgroup size: declared by the module or the default 32
capabilities: enabled on demand from the types and operations actually used
float16/float64/int8/int16/int64: available
storageBuffer8BitAccess/16BitAccess: available
physicalStorageBuffer: available
cooperativeMatrix: available for the shapes listed by the profile
float8: available if the driver advertises SPV_EXT_float8, otherwise emulated
rayQuery: available if the driver advertises SPV_KHR_ray_query
optional helpers: stochasticRounding, vectorCompression, tensorMemory
                   (off unless the profile explicitly enables them)
```

The emitter never guesses. If a used type or intrinsic needs a capability the
profile lacks, emission fails with a diagnostic naming the capability and the
source pc. FP4 has no standard SPIR-V type and is always emulated with packed
integer operations.

### Execution modes and interface

- Workgroup size, subgroup size, early-fragment-tests, depth modes, origin,
  output vertices/primitives, patch size and derivative group size come from
  module metadata and are emitted as `OpExecutionMode`.
- Inputs and outputs are declared from the signature table with location,
  component, interpolation and patch decorations, or as builtins.
- Resources are declared with descriptor set and binding from the resource
  table.
- Debug information is limited to names and source pc in version 1. No
  non-semantic debug extension instructions are emitted.

### Memory helper

Every `Load`, `Store` and `Atomic` goes through one emitter helper that
implements the byte-offset to element-index conversion, misalignment handling,
volatility, ordered-access fences and atomic alignment checks from section 8.

Handlers are common code keyed by intrinsic id. A handler may emit multiple
SPIR-V instructions, call an extended instruction, or expand a pattern. It never
contains an ISA name or an ISA opcode.

Per-ISA recipes may exist for intrinsics that cannot be normalized. A recipe is
data: an ordered list of `Emit(opcode, operands...)`, `Extract`, `Insert`,
`CallHelper`. The emitter interprets it; adding an ISA does not touch common
code.

## 11. Verification

The verifier runs after SSA and before structuring:

- every value has one definition and all uses are dominated;
- operand and result types match the op/intrinsic spec;
- phis are at block starts and have one incoming per predecessor;
- every block has exactly one terminator;
- branch targets exist and dominate rules hold;
- no guard remains;
- `Mask` values only flow through wave intrinsics;
- resources and address spaces are legal for the access op;
- intrinsic arity and payload kind match;
- multi-result arity matches;
- direct call targets exist and the call graph is acyclic;
- `IndirectBr` has a resolved target set or is marked for dispatch flattening;
- atomics use an element-sized, naturally aligned address;
- `Kill(demote=false)` is the last instruction in its block; its block
  terminator is replaced by the termination instruction, and it is only legal in
  a fragment stage;
- control barriers are only legal in execution models that permit them;
- entry function exists with a valid stage and signature;
- IDs and tables are deterministic.

The emitter performs the final capability check against `TargetProfile` and the
structurizer performs the uniform-control-flow check; neither can turn a
verified module into invalid SPIR-V without reporting a diagnostic.

## 12. Worked examples

### AMD vector add under EXEC

```
V_ADD_F32 v0, v1, v2        ; exec = E
```

Lift:

```
t   = FAdd v1, v2           ; guarded by E
v0  = Select E, t, old_v0
```

If `E` is statically true (common case after `S_MOV EXEC, -1`), the select
disappears.

### AMD divergent branch

```
V_CMP_LT_F32       vcc, v1, v2
S_AND_SAVEEXEC_B64 vcc
S_CBRANCH_EXECZ    target
```

Lift:

```
p = FCompare(LessThan, v1, v2)     ; per-invocation bool
e = And(exec, p)                   ; per-invocation guard
CondBr !e, target, fallthrough     ; varying
```

A bare `S_CBRANCH_VCCNZ` is different: it is a wave-uniform branch on
`WaveVote(Any, p)` and leaves `EXEC` unchanged. Mapping it to a per-invocation
branch would incorrectly skip lanes whose bit is clear.

### Intel predicated add

```
ADD r1, r2, r3 {M1} P0
```

Lift:

```
g = P0
t = IAdd r2, r3
r1 = Select g, t, old_r1
```

### NVIDIA predicated FFMA

```
@P0 FFMA R4, R5, R6, R7
```

Lift:

```
g = P0
t = Fma R5, R6, R7
R4 = Select g, t, old_R4
```

### Sub-register write

```
MOV r0.w, r1        ; or SDWA / 16-bit form
```

Lift:

```
t = Bitcast<i16> r1
r0 = Insert<i32> old_r0, t, lane 1
```

### Texture sample

```
IMAGE_SAMPLE v[0:3], v1, s[0:7], t[0:15]
```

Lift:

```
res = Resource(image0, format)
smp = Resource(sampler0)
v   = Intrinsic(TextureSample, {res, smp, coord, ...}, SampleDesc{2D, array=false, lod=implicit})
```

### Matrix multiply

```
V_WMMA_F32_16X16X16_F16 v[0:7], v[8:11], v[12:15], v[16:23]
```

Lift:

```
Intrinsic(Mma, {a[0..3], b[0..3], c[0..7]}, MmaDesc{16,16,16, f16, f32, layout})
```

The emitter maps to cooperative matrix if the fragment layout matches;
otherwise it reports an unsupported layout.

## 13. Implementation order

| Phase | Scope |
|---|---|
| 1 | Scalars, bools, CFG, predication, SSA, simple global/private/workgroup memory, branches |
| 2 | Wave intrinsics, first complete ISA front end, resources and descriptors |
| 3 | Textures, fixed-function messages, atomics with scope/order |
| 4 | Matrix/tensor ops, FP8/FP4, async copies |

The IL shape does not change between phases. Phases add intrinsics and handlers,
never new core concepts.

## 14. Resolved decisions

Everything below is settled. There is no deferred design question.

1. **IR form.** SSA, unstructured CFG, per-invocation scalar values, a small
   core op set, and intrinsics with payloads.
2. **Execution masks.** Lowered to per-invocation booleans and control flow
   before SSA. No execution-mask values exist; only wave ballot/active-mask
   data uses the `Mask` type.
3. **Uniformity.** A lift-time annotation on values and branch terminators. A
   wave-uniform destination may not be guarded by a varying condition; that is
   a lift error.
4. **Guard lowering.** Before SSA. Pure instructions become `Select`; effectful
   instructions become guarded regions. Guards never reach the SSA module.
5. **SSA construction.** Dominance-frontier phis over whole-register values.
   Partial writes are lowered to `Bitcast`/`Insert` at the definition.
6. **Memory.** Byte offsets in the IL; typed-element emission with element
   index and intra-element shift. Misaligned accesses use element-neighbour
   loads and bit operations. Atomics require natural alignment and element
   size. Out-of-bounds remains undefined. `DeviceShared` maps to `Global` with
   a dedicated buffer; if unavailable the access is diagnosed.
7. **Dynamic descriptors.** Static logical bindings are the supported model.
   `ResourceFromValue` accepts only a raw 64-bit device address under
   `PhysicalStorageBuffer`. Bindless descriptor arrays and arbitrary 128-bit
   descriptor arithmetic are diagnosed.
8. **Calls.** Direct calls become functions and `OpFunctionCall`. Indirect
   calls are diagnosed. Recursion, including mutual recursion, is rejected and
   the call graph is verified acyclic.
9. **Indirect branches.** A finite, known target set becomes `OpSwitch`.
   Anything else is flattened into the dispatcher loop described in 9.6.
10. **Irreducible CFGs.** Flattened into the same dispatcher loop. Values live
    across dispatch edges are spilled to function-local storage. No CFG shape
    is rejected.
11. **Wave and barrier uniformity.** Intrinsics marked
    `requiresUniformControlFlow` are rejected under a varying guard.
12. **Matrix operations.** Cooperative matrix only for shapes and layouts the
    target profile supports. An unsupported layout is diagnosed; there is no
    scalar expansion in version 1.
13. **FP8 and FP4.** FP8 emits native SPIR-V float8 when the profile advertises
    `SPV_EXT_float8`, otherwise a defined integer emulation. FP4 has no standard
    type and is always emulated with packed integer operations.
14. **Intel `SEND`.** Known families (sampler, dataport, URB, gateway, LSC,
    barrier) have dedicated handlers. Unknown messages are diagnosed with the
    message descriptor.
15. **Stochastic rounding, vector compression, tensor memory, ray queries.**
    Emitted only when the target profile provides a lowering (`SRND` helper,
    packed-integer emulation, `SPV_KHR_ray_query`). Otherwise they are diagnosed.
16. **Memory model and ordering.** `Logical GLSL450`. Ordered non-atomic
    accesses are emitted as a relaxed access surrounded by fences. Cache
    policies are performance hints and are dropped. Volatile is preserved.
    Scoreboard waits are dropped in favour of SSA ordering and explicit
    barriers/fences.
17. **SPIR-V baseline.** SPIR-V 1.6, Vulkan 1.3. Capabilities are enabled on
    demand; a missing capability fails emission with a diagnostic.
18. **Debug information.** Names and source pc only. No non-semantic debug
    extension instructions in version 1.
19. **Structurization.** Common emitter. Per-ISA convergence annotations are
    hints; they never change the output's validity.
20. **Descriptor form.** C++ `constexpr` tables. AMD is generated from the
    machine-readable XML, NVIDIA is hand-written from the published opcode
    list, Intel is generated from vISA/IGC.
21. **Hook budget.** Named hooks only for irregular encodings or lowering. No
    hook may build CFG, SSA or SPIR-V.
22. **Lowering coverage.** A core op when SPIR-V expresses the semantics
    directly; an intrinsic with a payload otherwise.
23. **Opcode tables.** Compositional axes (`base op x type x encoding form x
    modifiers`), not one row per mnemonic.
24. **Public surface.** Internal libraries. No installed headers or ABI
    promises in version 1.
25. **Tests.** Small checked-in corpus per ISA, generated golden dumps, no
    network access in tests.
26. **First front end.** DXBC SM5, then DXIL.
27. **Stage scope.** The architecture covers every SPIR-V execution model.
    Implementation order is compute, vertex and fragment first, then geometry,
    tessellation, mesh/task and ray stages. A stage whose signatures or
    intrinsics are not implemented is diagnosed; no wrong output is emitted.
28. **Failure policy.** Every unsupported construct produces a diagnostic with
    the source pc and opcode. Silent approximation or silent dropping is never
    allowed.

## 15. Correctness argument

The translation is sound because each step preserves observable behaviour for
defined source programs, and undefined cases are diagnosed:

- **Invocations.** One source invocation maps to one SPIR-V invocation. Values
  are per-invocation scalars; wave width never becomes vector data.
- **Divergence.** Execution masks become per-invocation predicates and
  structured control flow. Active lanes execute the same operations; inactive
  lanes take the skip path. The resulting control flow preserves each
  invocation's sequence of effects.
- **Wave operations.** Ballot, vote, shuffle, match, reduction and leader
  election map to SPIR-V group operations over the same active set. Mask data is
  preserved as `Mask`.
- **Memory.** Byte address, width, space, order and scope are preserved.
  Misaligned accesses are lowered without violating SPIR-V alignment; atomics
  keep their element type and natural alignment. Out-of-bounds remains undefined
  exactly as in the source ISA.
- **Arithmetic.** Wrapping, saturation, rounding, denorm and NaN policies are
  explicit attributes. Approximate operations are allowed to differ only to the
  degree the source ISA allows.
- **Control flow.** Reducible CFGs are structured directly. Irreducible CFGs and
  unresolved indirect branches are flattened into a dispatcher with explicit
  state and spilled values; the block graph is preserved.
- **Resources.** Static descriptor bindings map to module declarations. Dynamic
  raw addresses map to physical storage buffer pointers under the profile
  capability. Bindless descriptor arithmetic is diagnosed, never guessed.
- **Failure.** Anything outside the supported subset is a diagnostic with source
  pc and opcode. The tool never silently emits a module it cannot justify.

Source programs that rely on undefined behaviour may observe different results;
that is the only allowed divergence from the source ISA.
