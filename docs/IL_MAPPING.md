# Mapping vendor ISAs into COMMON_SHADER, simply

Status: quick mapping reference. The authoritative model lives in
[IL_ARCHITECTURE.md](IL_ARCHITECTURE.md); this file is the one-page table.

The IL is a translation record, not a machine that executes. That changes the
design in three useful ways:

- correctness is defined by the SPIR-V that comes out, not by the IL;
- no evaluator, interpreter or mandatory optimisation pass is needed;
- constructs that are awkward to model can stay opaque until emission and be
  lowered there, or be rejected with a diagnostic.

The target is SPIR-V, and SPIR-V is SPMD: one invocation, structured control
flow, scalar values. If the IL is also SPMD, most of the vendor complexity
disappears at the boundary instead of being carried through the IR.

## 1. The three simplifications

### 1.1 Lanes are invocations, not vectors

A VGPR, GPR or GRF element is one value per invocation. The 32-wide wave, the
8/16/32 Intel exec size and the NVIDIA warp are how hardware batches invocations;
they are not data parallelism inside one invocation.

So the IL has no "lane vector" type. It has ordinary scalar values, plus real
shader vectors (`float4`, `v2f16`, `v4f8`) when the source actually uses them.
A SIMD instruction becomes a per-invocation scalar op; Intel regions are a
register-addressing detail resolved during decode.

This removes the need for a mask type, lane-indexed value access and vectorised
SSA.

### 1.2 Execution masks become control flow

AMD `EXEC`, Intel `EM`/predicates and NVIDIA `@P` are execution masks, not data.
SPIR-V has no per-instruction lane mask, so they cannot survive into the output.

Map them as follows:

- a compare that produces a mask becomes a per-invocation **bool**;
- a predicated instruction carries a `guard` bool;
- a branch controlled by a mask becomes a normal `CondBr` on the per-invocation
  predicate (AMD `S_CBRANCH_EXECZ` is already "skip if my bit is clear");
- `saveexec`/`restoreexec` patterns become nested `if`s during structuring;
- NVIDIA `BSSY`/`BSYNC` become structure hints, not instructions;
- Intel mask control becomes the guard on the instruction.

The final IL contains bools and CFG edges, no masks. Wave-level masks that are
genuinely *data* (`ballot`, `activemask`) are the exception: those are values
returned by wave intrinsics and are typed `Mask`.

Uniform vs varying is then an **annotation** on a branch or value. It guides
structure and gates the rule that a wave-uniform destination may not be guarded
by a varying condition.

### 1.3 Opaque until emission

An instruction the front end cannot express in core ops becomes an intrinsic
with a small payload, not a new IL concept:

```
Intrinsic { id: Mma, operands: [...], payload: TileLayout{...} }
Intrinsic { id: TextureSample, operands: [...], payload: SampleDesc{...} }
Intrinsic { id: Send, operands: [...], payload: SendDesc{...} }
```

The IL stores what it knows and defers the rest. The emitter either has a
handler for that id or reports the instruction as unsupported, with the source
pc and opcode.

## 2. Minimal node set

The IL is deliberately close to SPIR-V:

```
Module      types, constants, resources, signatures, functions

Function    blocks, values

Block       instructions, terminator, optional convergence hint

Instruction op, subop, result(s), type, operands, attributes, source location

Terminator  Br, CondBr (uniform flag), Switch, IndirectBr, Ret, Unreachable

Value       type, definition, uses (uniform is an attribute, not a type)
```

Core node families:

| Family | Nodes |
|---|---|
| Constants | value definitions (`Constant`, `Undef`) |
| Arithmetic | `Unary`, `Binary`, `Ternary` with a subop (`IAdd`, `FAdd`, `Fma`, `FMin`, `FMax`, `Shift`, `Bitfield`, ...) |
| Compare/select | `Compare` (ordered/unordered), `Select` |
| Conversion | `Convert` with rounding and saturation attributes |
| Composite | `Bitcast`, `Extract`, `Insert`, `Shuffle`, `Construct` |
| Memory | `Load`, `Store`, `Atomic`, `Alloca` |
| Control | terminators, `Phi`, `Call` |
| Escapes | `Intrinsic` (wave, texture, MMA, send, barrier, fence, async, special register, transcendentals) |

Everything a vendor does must be expressible as one of those, or as an
intrinsic with a payload. There is no third category.

## 3. Source construct -> IL -> SPIR-V

| Source construct | IL | SPIR-V emission |
|---|---|---|
| Scalar ALU (AMD SALU, NVIDIA `U*`, Intel scalar) | `Binary`/`Ternary`, marked uniform | ordinary op |
| Vector ALU (VGPR/GPR/GRF) | scalar `Binary`/`Ternary` | ordinary op |
| `V_CMP`, `ISETP`, `CMP` | `Compare` returning `bool` | `OpSLessThan` etc. |
| `V_CNDMASK`, `SEL`, `FSEL` | `Select` | `OpSelect` |
| Predicated instruction (`@P`, Intel predicate) | `Instruction.guard = bool` | `OpSelect` for pure results, `if` for effects |
| `S_CBRANCH_EXECZ`, `@P BRA`, `JMP` | `CondBr` on the per-invocation bool | `OpBranchConditional` |
| `S_CBRANCH_VCCNZ`, ballot-any branch | `CondBr` on `WaveVote(Any, p)`, uniform | `OpBranchConditional` |
| `S_AND_SAVEEXEC` / restore pattern | nested blocks after structuring | structured `if` |
| `BSSY` / `BSYNC` | block convergence hint | nothing |
| Packed half / 16-bit write | `Bitcast` + `Insert` on i32 | `OpBitcast`, shift/or, `OpCompositeInsert` |
| Intel region read/write | scalar loads/stores at the resolved element offset | ordinary access chain |
| `LDG`/`GLOBAL_LOAD`/`LSC_UNTYPED` | `Load(space=Global, handle?, offset, width, attrs)` | `OpLoad` + access chain |
| Buffer/image descriptor (`V#`, `T#`, `desc[UR]`) | resource handle in the module table; access by handle + offset | variable + access chain |
| `ATOM`/`DS_ADD`/`LSC_*` atomic | `Atomic(op, address, value, order, scope, returns)` | `OpAtomic*` |
| `TEX`/`IMAGE_SAMPLE`/`SAMPLE` | `Intrinsic(TextureSample, payload)` | `OpImageSample*` |
| `HMMA`/`WMMA`/`DPAS` | `Intrinsic(Mma, payload: tile layout)` | cooperative matrix or expansion |
| `SEND`/`RAW_SEND` | `Intrinsic(Send, payload: message kind)` | known helper; unknown = diagnostic |
| `S_WAITCNT`/`DEPBAR`/`WAIT` | dropped | nothing (SSA already orders values) |
| `BAR`/`FENCE`/`SBARRIER` | `Barrier`/`Fence` intrinsic | `OpControlBarrier`, `OpMemoryBarrier` |
| `S2R`/`S_GETREG`/`CS2R` | `Intrinsic(SpecialReg, payload: id)` | builtin load or helper |
| `SHFL`/`V_READLANE`/DPP | `Intrinsic(WaveShuffle, payload: mode)` | `OpGroupNonUniformShuffle*` |
| `VOTE`/`MATCH`/`BALLOT` | `Intrinsic(WaveVote/Match/Ballot)` | `OpGroupNonUniform*` |
| `MUFU`/`V_RCP`/`INV`/`RSQRT` | `Intrinsic(Transcendental, payload: fn, precision)` | `OpExtInst` or arithmetic sequence |
| `DIV`/`MOD` | `Intrinsic(Division, payload: signed/rounding)` | arithmetic sequence or helper |
| `KILL`/`DISCARD` | `Intrinsic(Kill)` | `OpKill` / `OpDemoteToHelperInvocation` |
| `BRA`/`BRX`/`SWITCHJMP` | `CondBr` / `Switch` / `IndirectBr` | `OpBranchConditional` / `OpSwitch`; otherwise dispatcher flattening |
| `CALL`/`RET` | `Call` terminator + function | `OpFunctionCall` |
| `EXP`/`RT_WRITE`/`URB_WRITE` | `Store` to an `Output` signature, or `Intrinsic(Send)` for URB | builtin/output store / helper |

## 4. Attributes instead of variants

Vendor modifiers are attributes on a node, not separate opcodes:

```
Attrs {
  rounding:   RTNE | RTZ | RU | RD | STOCHASTIC
  denorm:     KEEP | FLUSH_IN | FLUSH_OUT | FLUSH_BOTH
  saturate:   bool
  approx:     bool
  order:      RELAXED | ACQUIRE | RELEASE | ACQ_REL
  scope:      WAVE | WORKGROUP | DEVICE | SYSTEM
  volatile:   bool
}
```

Uniformity is a property of values and branch terminators, not an attribute.

`OpAttrs` is interpreted by the emitter, which either selects a SPIR-V
construct or calls a shared helper. The IL never branches on ISA.

## 5. Per-ISA data, not per-ISA code

The opcode table stays declarative:

```
OpcodeSpec {
  id, mnemonic
  encoding: pattern, length, operand forms
  operands: kinds, widths, read/write, masks
  lower:    Core(subop, type rule, attrs) | Intrinsic(id, payload rule) | Control(kind)
  effects:  memory, barrier, wave, status
}
```

Compositional axes keep the table small:

- base operation x type suffix (`_F16`, `_F32`, `_I32`, `_U8`, ...);
- base operation x encoding form (VOP2/VOP3/DPP/SDWA, uniform `U*`, ...);
- base operation x modifier set (neg, abs, clamp, transpose, ...).

AMD ships exactly these axes in its machine-readable XML, so its table can be
generated from the XML plus a semantic overlay. NVIDIA has no machine-readable
spec, so its table is hand-written against the published opcode list. Intel's
vISA documents and IGC headers can generate the base table, with `SEND`
messages handled as payloads.

## 6. Emission model

```
for each function:
  build SPIR-V types, constants, values, blocks
  structurize the CFG (natural regions, dispatch flattening as fallback)
  for each block in RPO:
    for each instruction:
      if core op:      emit mapped SPIR-V op
      if intrinsic:    call registered handler(id, payload, operands)
    emit the terminator
  emit decorations, interface and execution modes
  validate the module
```

Guards are already gone by this point: predication lowering runs before SSA, so
the emitter only sees `Select` and ordinary control flow.

Handlers are common code keyed by intrinsic id. They may emit several SPIR-V
instructions, call extended instructions, or expand loops. They never contain
an ISA name or an ISA opcode.

A per-ISA recipe is only needed for intrinsics that cannot be normalized. Keep
those recipes as data in the architecture table, for example an ordered list of
`Emit(opcode, dst, src...)`, `Extract`, `Insert`, `CallHelper`. The emitter
interprets that list; adding an ISA does not touch common code.

## 7. What this buys

- No mask arithmetic, lane vectors or SIMD execution model in the IL.
- Uniformity is a single annotation, not a type system.
- Partial writes are two nodes (`Bitcast`, `Insert`), not component-tracking SSA.
- Descriptors are a resource table plus an offset, not pointer arithmetic.
- Scoreboard waits disappear; only real barriers and fences survive.
- Matrices, textures, and Intel messages are payloads on intrinsics.
- An unsupported instruction is a diagnostic, never silently wrong output.

## 8. Resolved decisions

1. **Indirect branches.** A finite, known target set becomes `OpSwitch`.
   Anything else is flattened into a dispatcher loop with spilled values.
2. **Guarded effects.** Pure instructions become `Select`; effectful
   instructions become a region. Every op and intrinsic carries an effect bit
   set, so the classification is mechanical.
3. **Intel `SEND`.** Known message families have dedicated handlers. Unknown
   messages are diagnosed with the message descriptor.
4. **Matrices.** Cooperative matrix only for shapes and layouts the target
   profile supports; otherwise a diagnostic.
5. **FP8/FP4.** FP8 is native when `SPV_EXT_float8` is available, otherwise a
   defined integer emulation. FP4 is always packed-integer emulation.
6. **Uniformity.** Lift-time annotation from register class. A wave-uniform
   destination guarded by a varying condition is a lift error, not a select.
7. **Resource access.** Byte offsets in the IL; typed-element emission with
   element index and intra-element shift. Atomics require natural alignment.
8. **Dynamic descriptors.** Static logical bindings only. `ResourceFromValue`
   accepts a raw 64-bit device address under `PhysicalStorageBuffer`; bindless
   descriptor arrays are diagnosed.
