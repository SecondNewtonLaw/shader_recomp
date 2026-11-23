# shader_recomp architecture (v1)

Status: final design, no implementation. The IL specification is
[IL_ARCHITECTURE.md](IL_ARCHITECTURE.md).

## 1. Design goal

Adding an ISA should mean writing an architecture definition, not a front end.

The framework provides everything that is not architecture-specific:

- container and instruction decoding driven by a schema
- basic-block construction and the CFG
- control-flow analysis (CFA): reachability, dominators, loops, irreducibility
- SSA construction from a physical register file
- the common IL, its verifier and its dump
- structurization and SPIR-V emission

An architecture module supplies data: opcode encodings, operand schemas, register
layout, lowering rules, resource and signature conventions. Small hooks exist
for irregular encodings, but a well-formed ISA should not need C++ per opcode.

## 2. Pipeline

```
                 ARCHITECTURE DEFINITION (per ISA, declarative)
                 +--------------------------------------------+
                 | decoder schema                             |
                 | register file layout                       |
                 | opcode table: encoding + lowering           |
                 | resource / signature / calling convention   |
                 +--------------------------------------------+
                                   |
 bytes                             v
   |                    +-------------------------+
   +------------------> |  Binary Reader (common) |  decode tokens/words,
                        +-------------------------+  validate container
                                   |
                                   | DecodedShader: instruction stream + labels
                                   v
                        +-------------------------+
                        |  Decode (per ISA)       |  opcode + operands
                        +-------------------------+
                                   |
                                   v
                        +-------------------------+
                        |  Translate (per ISA)    |  register-transfer IL
                        +-------------------------+
                                   |
                                   v
                        +-------------------------+
                        |  CFA (common)           |  blocks, edges, dominators,
                        |                         |  dominance frontiers, loops
                        +-------------------------+
                                   |
                                   v
                        +-------------------------+
                        |  SSA (common)           |  alloca promotion, phis,
                        |                         |  renaming
                        +-------------------------+
                                   |
                                   v
                        +-------------------------+
                        |  Optimize (common)      |  DCE, unreachable blocks,
                        |                         |  folding
                        +-------------------------+
                                   |
                                   | COMMON_SHADER::Module (typed SSA IL)
                                   v
                        +-------------------------+
                        |  Lift to SPIR-V (common)|  structure or spill,
                        |                         |  emit, validate
                        +-------------------------+
                                   |
                                   v
                              SPIR-V words
```

Per-ISA code lives in exactly two places:

1. the architecture definition consumed by the reader;
2. the lowering entries consumed by the remapper.

Both are tables. Neither contains CFG, SSA or SPIR-V logic.

## 3. The architecture definition

```cpp
namespace ShaderRecomp {

struct Architecture {
    std::string_view name;              // "dxbc", "dxil", "gcn", "sass"
    std::string_view displayName;

    DecoderSchema decoder;              // token stream or fixed-word encoding
    RegisterFileLayout registers;       // classes, widths, aliasing, initial state
    std::span<const OpcodeSpec> opcodes;
    std::span<const SignatureRule> signatures;
    std::span<const ResourceRule> resources;
    CallingConvention callingConvention;
    ControlFlowRules control;           // branch/call/return/barrier forms
    StageRules stages;                  // how the entry stage is recovered

    std::span<const ArchitectureHook> hooks; // optional, see below
};

std::span<const Architecture> AllArchitectures();

} // namespace ShaderRecomp
```

### Decoder schema

Two schemas cover most shipped formats:

- `TokenStream`: a 32-bit opcode token, optional extended tokens, then operands.
  Operand forms declare their own encoding (register token with modifiers,
  immediate, resource, label).
- `FixedWord`: fixed-width words with bitfields; instruction length is derived
  from the opcode.

Anything else (variable-length prefixes, inline literals, block payloads) is
described with operand forms rather than a new decoder. A hook is available when
a format genuinely cannot be expressed; the hook returns operands, not IL.

### Opcode specification

```cpp
struct OpcodeSpec {
    OpcodeId id;                 // allocated in the architecture's namespace
    std::string_view mnemonic;
    Encoding encoding;           // pattern, length, operand forms
    OperandSchema operands;      // kinds, widths, read/write, masks
    Lowering lowering;           // Expand template | Intrinsic | Control
    Effect effects;              // memory, barrier, wave, status, side effects
    Flags flags;                 // predicated, saturating, writes status, ...
};
```

`Lowering` is data:

- `Expand`: an ordered template of IL operations, with operand bindings. Covers
  arithmetic, bit operations, comparisons, conversions, selects and most moves.
- `Intrinsic`: a named IL intrinsic plus a result/type rule. Used when the
  operation has no direct IL equivalent.
- `Control`: branch, conditional branch, call, return, barrier, terminate. The
  remapper turns these into CFG edges and IL terminators; the descriptor only
  says which operands are targets and what condition applies.
- `Custom`: a named hook for the rare instruction whose expansion depends on
  values the template cannot express.

A table entry is small. A hypothetical fixed-word ISA:

```cpp
constexpr OpcodeSpec kAdd = {
    .id = 0x02,
    .mnemonic = "add",
    .encoding = { .pattern = 0x02, .length = Fixed32, .forms = { DestReg, SrcReg, SrcReg } },
    .operands = { .dest = WriteMask::Xyzw, .src = { ReadWhole, ReadWhole } },
    .lowering = ExpandIAdd(),
    .effects = Effect::None,
    .flags = Flag::WritesStatus,
};
```

The remapper reads `lowering`, binds the decoded operands, and emits IL. The
architecture author never writes a switch statement.

### Lowering rules

Every opcode must have one of:

- an `Expand` template that produces IL;
- an `Intrinsic` with a defined result type and semantics;
- a `Control` kind that the CFG builder understands;
- a `Custom` hook, registered by name.

A validation pass runs when an architecture is registered: every opcode has a
lowering, operand counts match, control opcodes declare targets, expansions
reference known IL opcodes. Misconfiguration is a startup error, not a runtime
surprise on a customer shader.

## 4. COMMON_SHADER: the malleable IL

The concrete model, instruction set, attributes, intrinsics and lowering stages
are specified in [IL_ARCHITECTURE.md](IL_ARCHITECTURE.md). This section is the
summary.

The IL is open for extension without editing the core.

### Form

```
Module      stage, entry, types, constants, resources, signatures, functions
Function    blocks, values, debug names, pc range
Block       instructions + exactly one terminator, predecessors/successors
Instruction opcode id, optional typed result, typed operands, source pc
Value       type, definition, uses
```

Instructions are generic containers:

```cpp
struct Instruction {
    OpcodeId op;
    TypeId type;                 // invalid for void / control ops
    SmallVector<ValueId> operands;
    SmallVector<BlockId> targets;   // for control ops
    SourceLocation source;
};
```

### Opcode registry

Opcodes are registered, not hard-coded in a global enum:

```cpp
struct OpcodeInfo {
    OpcodeId id;
    std::string_view name;       // dump name, e.g. "iadd", "wave_ballot"
    OpcodeCategory category;     // Core | Control | Intrinsic | Isa
    Arity arity;
    ResultKind result;           // None | Value | Multi
    Effect effects;
    LoweringTarget target;       // canonical IL op, or SPIR-V intrinsic
};
```

Core opcodes are a reserved range; intrinsics and ISA-local opcodes are
allocated into namespaces. Adding an operation means adding a registry entry and
an emitter mapping, not editing every switch in the codebase.

"Malleable" means three concrete things:

- an ISA opcode can be introduced as an intrinsic before it has a canonical
  lowering, then rewritten later without touching the decoder;
- IL-to-IL rewrites match on opcode ids and can replace one op with any
  sequence, including new ops;
- the dump, verifier and emitter read opcode metadata, so a new op appears in
  all three immediately.

### Types and values

- Scalars: bool, i8/i16/i32/i64, f16/f32/f64, plus vendor 8/16-bit float types
  as explicit types, not opcode variants.
- Composites: fixed vectors (any width), matrices, arrays, structs, pointers,
  images, samplers, sampled images.
- Integer signedness is explicit.
- Partial-register writes are normalized at definition: a masked write becomes
  extract/insert or a vector select producing a new SSA value. Phi nodes never
  need per-component tracking.
- Predication lowers to `select` when it only picks a value, and to control flow
  when it guards effects. Guards are eliminated before SSA.
- Execution masks never become values. They become per-invocation predicates
  and CFG edges; only wave operations return mask-typed data. Uniformity is a
  hint, not a type.

### Control flow

The IL carries a plain CFG, not structured control flow. Block, edge and
terminator rules:

- exactly one terminator per block;
- every branch target is a block;
- unreachable blocks are preserved until an explicit cleanup pass;
- loops and irreducible regions are represented structurally in the CFG and
  recovered later by the structurizer.

## 5. CFA and CFG services

Provided by the framework and used by the generic remapper:

| Service | Output |
|---|---|
| Block construction | leaders, blocks, edges from branch/call/return descriptors |
| Reachability | live blocks, dead blocks, entry closure |
| Dominators | dominator tree, immediate dominators, dominance frontiers |
| Post-dominators | post-dominator tree, join points |
| Loop analysis | natural loops, headers, latches, preheaders, exits |
| SCC / irreducibility | strongly connected components, irreducible regions |
| SSA construction | reaching definitions over the register file, phi insertion, renaming |
| Use/def chains | per-value uses, per-block defs/lives |

The remapper only orchestrates: decode, build CFG, run SSA, bind opcode
templates. Algorithms live in one place, are tested independently, and improve
every ISA at once.

## 6. ISARemapper

The remapper is common code with a per-ISA opcode table:

1. Take the instruction stream and labels from the reader.
2. Build blocks and the CFG using `ControlFlowRules`.
3. Run CFA.
4. Build SSA over `RegisterFileLayout`, honoring write masks and predication.
5. For each instruction, look up `OpcodeSpec`, bind operands, and emit the
   lowering template or intrinsic.
6. Resolve resources and signatures through the architecture rules.
7. Verify the module, then return it with diagnostics attached to source pcs.

Failure modes are diagnostics: unknown opcode, operand form not supported for
this variant, unmapped intrinsic, budget exceeded. No instruction is skipped.

## 7. ISAReemitter

Common code, no ISA knowledge:

- structurize the CFG (selection/loop merges, continue targets, phi placement);
- canonicalize IL (optional small passes: constant folding, dead code, copy
  propagation);
- allocate IDs and deduplicate types/constants deterministically;
- apply decorations, execution modes and entry-point interface;
- select capabilities/extensions from the operations actually used;
- encode through the SPIR-V writer and validate the result.

Emitter mappings are table-driven from `OpcodeInfo`. An intrinsic either binds
to a SPIR-V opcode template or has an emitter hook; both are common code keyed
by intrinsic id, never by ISA.

## 8. Target graph

Current backbone:

```
SR.Common        IL, diagnostics, byte reader, verifier, dump, CFA/SSA helpers
SR.Frontend      generic reader + remapper, frontend interfaces
SR.Backend       structurizer + SPIR-V emitter + validation
SR.CLI           driver
SR.Tests         Catch2
SR.Fuzzing       optional reader/parser fuzzing harness (BUILD_FUZZING)
```

Per-ISA modules land as `SR.Arch.*` targets when their opcode tables are added:

```
SR.Arch.DXBC     decoder schema, opcode table, lowering rules
SR.Arch.DXIL     decoder schema, opcode table, lowering rules
SR.Arch.GCN      decoder schema, opcode table, lowering rules
SR.Arch.SASS     decoder schema, opcode table, lowering rules
```

Dependencies:

```
SR.Frontend  -> SR.Common
SR.Backend   -> SR.Common, SPIRV-Tools
SR.Arch.*    -> SR.Common (opcode ids, lowering templates)
SR.CLI       -> SR.Frontend, SR.Backend, every SR.Arch.*
SR.Tests     -> all of the above
SR.Fuzzing   -> SR.Frontend
```

Adding an ISA is one `SR.Arch.*` directory and one entry in the CLI's
architecture table. Nothing in `SR.Frontend`, `SR.Backend` or the IL changes.

## 9. Error handling and limits

- Malformed input, unsupported opcodes and unsupported intrinsics return
  `std::expected<T, Diagnostic>`.
- Internal invariant violations assert. A malformed module produced by the
  remapper is a bug, not an input error.
- Diagnostics carry a container offset or instruction pc and a stable code.
- Readers, CFA and the remapper enforce budgets: container size, instruction
  count, block count, value count, phi count, nesting depth.
- Endianness is resolved in the reader; IL values are host typed.
- Determinism: no unordered iteration in observable paths; resources sorted by
  binding; ids allocated in walk order. Same input, same bytes out.

## 10. Testing

| Layer | Evidence |
|---|---|
| Architecture definition | registration validates every opcode, operand schema and lowering |
| Binary Reader | real containers decode with expected metadata; hostile inputs rejected at the right offset |
| CFA | dominators, loops and SSA invariants tested on constructed CFGs with known answers |
| ISARemapper | golden IL dumps per ISA; verifier success; one test per lowering rule family |
| ISAReemitter | constructed modules emit and pass validation; golden disassembly |
| End to end | container -> SPIR-V -> validation; each failure has a precise diagnostic |

Every stage has a stable textual dump. Regression diffs are read per layer, and
a new ISA reuses the same dumping and comparison infrastructure.

## 11. Resolved decisions

1. **Descriptor form.** C++ `constexpr` tables. AMD tables are generated from
   the machine-readable XML, NVIDIA tables are hand-written from the published
   opcode list, Intel tables are generated from vISA/IGC.
2. **Hook budget.** Named hooks only for irregular encodings or lowering. No
   hook may build CFG, SSA or SPIR-V.
3. **Lowering coverage.** A core operation when the IL expresses the semantics
   directly; an intrinsic with a payload otherwise.
4. **Canonical op set.** The core set frozen in [IL_ARCHITECTURE.md](IL_ARCHITECTURE.md):
   unary/binary/ternary arithmetic, compare, select, convert, bitcast,
   extract/insert, shuffle, alloca, load/store/atomic, phi, call, kill,
   intrinsic and terminators. New behaviour goes through intrinsics, not new
   core concepts.
5. **Structurization.** Common emitter. Per-ISA convergence hints are optional
   annotations; irreducible and indirect-flow cases use dispatch flattening.
6. **Partial writes.** Whole-value SSA with `Bitcast`/`Insert` at the
   definition. No component-tracked phis.
7. **First architecture.** DXBC SM5, then DXIL. AMD GCN/RDNA and NVIDIA SASS
   follow the same architecture-definition interface.
8. **SPIR-V baseline.** SPIR-V 1.6, Vulkan 1.3. Capabilities are enabled on
   demand; a missing capability is a diagnostic, never a downgrade.
