# Divergence testing

## 1. What counts as divergence

A divergence is any observable difference between the source program and the
recompiled program: a final register or buffer value, a NaN payload, a control
flow decision, a memory access that traps, or a crash. The pipeline is:

```
Decode -> Translate -> CFA -> SSA -> Optimize -> Lift to SPIR-V
```

Each arrow is a place where semantics can be lost, so each stage needs an
independent check. A test that only compares the IL interpreter with itself is
circular; the oracles below are ordered by independence, not by convenience.

## 2. Oracle ladder

### T0 — structural

Cheap, runs on every commit:

- decode: opcode, operand class, instruction length, literal consumption;
- translate: IL verifier, register-slot map, source pc on every instruction;
- CFA: blocks, edges, dominators, natural loops, reachability;
- SSA: verifier (definition dominance, phi arity, phi predecessors);
- optimize: verifier after every pass, pass determinism;
- emit: `spirv-val` against Vulkan 1.3;
- determinism: the same input hashes to the same IL and SPIR-V words.

T0 catches malformed modules, not wrong arithmetic.

### T1 — hand-checked micro-fixtures

One instruction or idiom per fixture, assembled for gfx1200 with `llvm-mc` and
checked into `tests/corpus/gfx1200`. Each fixture has:

- `.s` source and `.hsaco` binary;
- a manifest: stage, workgroup size, buffers, expected register/memory state;
- the expected values written down from the ISA reference, not from the
  recompiler.

The IL interpreter (`IL::Interpret`) executes the translated module before SSA
and the optimized module after SSA, so both forms are checked against the same
expectation. `Module::registerSlots` maps `s0`, `v1`, `scc` to their slots.

### T2 — independent ISA model

A small per-lane interpreter that decodes gfx instructions and executes them
directly, without the IL, the lifter, or the optimizer. It is written from the
ISA XML and the PPP, and shares no code with `GfxLifter`.

- Generate random straight-line and structured programs from an instruction
  grammar (ALU, compares, branches, loops, private memory, eventually LDS).
- Run the ISA model and the IL pipeline on the same program with the same
  initial registers and inputs.
- Compare final register slots and memory.

This is the first oracle that can catch a wrong translation. It should be
deliberately dumb: no SSA, no optimizations, no instruction scheduling.

### T3 — SPIR-V execution

Compile the emitted module and run it on a GPU or a CPU Vulkan implementation.

- Compute kernels write results into a storage buffer (test ABI, see §5).
- Runners: AMD proprietary Vulkan driver on the RX 9060 XT, lavapipe, and
  optionally RADV (Steam Deck / WSL).
- Compare the buffer contents with T2's memory image.

This catches SSA construction, spilling, dispatcher lowering, and emitter
bugs. It does not catch a lift bug that T2 shares, so T2 remains the reference.

### T4 — native ISA execution

Run the original code object on the RX 9060 XT and compare with T3. Two routes:

1. **Hand-written fixtures** with a complete AMDHSA code object (kernel
   descriptor, kernarg segment) loaded through HIP. Our current `.text`-only
   fixtures are not enough for this; the fixture builder must emit full code
   objects (ROCm `clang`/`hipcc` on a minimal kernel).
2. **Captured shaders** from a game or benchmark, replayed through the native
   driver. This is the realistic corpus but needs the capture path and
   descriptor ABI.

T4 is the only oracle that includes the exact wave scheduling, `EXEC`
divergence, and memory model of the hardware. It is also the most expensive and
the last to be enabled.

### Driver compiler round trip (implemented)

`SR.ISAProbe` compiles the emitted SPIR-V with the installed Vulkan driver
through `VK_AMD_shader_info` and prints the ISA the driver produced. This is the
first half of T4 without needing a full native loader: it proves the driver
accepts the module and shows what it lowers it to.

`tests/roundtrip.ps1` runs the full loop for every generation and fixture:

```
.hsaco -> shader-recomp -> .spv -> SR.ISAProbe -> driver ISA
.hsaco -> llvm-objdump -> original ISA
```

and checks that every opcode class used by the source appears in the driver
ISA. Current result for the loop fixture:

| Target | Driver ISA instructions | Original ISA instructions | Verdict |
|---|---:|---:|---|
| gfx10 | 29 | 7 | structural-ok |
| gfx1013 | 29 | 7 | structural-ok |
| gfx1030 | 29 | 7 | structural-ok |
| gfx1100 | 29 | 7 | structural-ok |
| gfx1150 | 29 | 7 | structural-ok |
| gfx1200 | 29 | 7 | structural-ok |

The 29-vs-7 gap is the dispatcher lowering: correct, but not the most efficient
form. [Review 0001](reviews/0001-dispatch-instruction-overhead.md) contains the
instruction-by-instruction comparison, the category breakdown, and the
structured-emission plan. `tests/isa_compare.ps1` regenerates that report.
Structured emission for reducible natural loops is the next step; the
structural check confirms the semantics survive, while exact losslessness still
needs execution (T3/T4).

The AMD LLVM reverse translator (`amd-llvm-spirv -r`) is not usable here: it
targets OpenCL and drops `GLCompute` entry points. The vendor driver is the
compiler that matters for the target anyway.

## 3. Comparison policy

| Category | Policy |
|---|---|
| Integer, bitwise, shifts, compares | bit-exact |
| Control flow, memory addresses, atomics | exact |
| Float add/sub/mul/fma on the same width | bit-exact where the ISA is IEEE and the emitter has `NoContraction`; otherwise ULP-bounded |
| Approximate ops (MUFU, RCP, RSQ, transcendentals) | ULP/absolute-error bound recorded per intrinsic |
| NaN | compare payload policy is explicit: `-nan` variants and sign of NaN are either required or ignored per fixture |
| Denormals | fixture declares KEEP/FLUSH modes; the comparator uses the declared mode |
| Uninitialized registers | undefined; the fixture must initialize every value it compares |

The comparator returns the first difference with its source register, `pc`,
and stage. A difference without an attributed `pc` is a harness bug.

## 4. Harness shape

```
tests/corpus/gfx1200/<name>.s          assembly source
tests/corpus/gfx1200/<name>.hsaco      assembled binary
tests/corpus/gfx1200/<name>.json       manifest: arch, stage, workgroup,
                                       inputs (seed + ranges), outputs,
                                       expected mode (exact/ulp)
```

Components:

- **Builder**: assembles `.s` with the ROCm `llvm-mc` (or DXC/hipcc for full
  code objects), records the hash, regenerates the manifest inputs.
- **Executor A (IL)**: `IL::Interpret` on the translated module and on the
  optimized SSA module.
- **Executor B (ISA model)**: T2's independent interpreter.
- **Executor C (SPIR-V)**: a Vulkan compute runner. It creates a buffer, binds
  it at the test ABI's set/binding, dispatches the workgroup, reads back.
- **Executor D (native)**: HIP module load + launch of the original code
  object.
- **Comparator**: category-aware, reports `(stage, value, expected, actual,
  pc, seed)`.

Start with A vs C for the fixtures we can already build, then add B, then D.

## 5. Test ABI and instrumentation

The recompiler has no descriptor model yet, so the harness needs a deliberate,
narrow ABI:

- One storage buffer at `set = 0, binding = 0`, an array of `u32`.
- A fixture kernel writes its results at byte offsets given by the manifest.
- `global_store_b32` lowers to an access chain into that buffer for this test
  profile only. This is **not** general shader semantics; the CLI flag
  `--test-abi` selects it and the manifest records it.

Instrumentation, independent of the ABI:

- `Module::registerSlots` already maps source registers to IL slots.
- A debug pass can append stores of selected slots to the output buffer at
  `s_endpgm`, giving a full register snapshot without changing arithmetic.
- Block checkpoints: store selected slots at the end of every IL block. The
  comparator then reports the first block where states diverge, which usually
  identifies the instruction family at fault.
- SSA values that were spilled for the dispatcher are visible in the same way;
  the snapshot is taken before the dispatcher's final branch.

## 6. Input generation

- Seeded PRNG, one seed per fixture per run; failing seeds are printed and
  replayable.
- Values per type: `0`, `-0`, `±1`, `±2`, powers of two, max/min finite,
  `inf`, quiet/signalling NaN, denormals, random bit patterns.
- Buffers: sizes around the workgroup size and buffer bounds; offsets that are
  aligned and misaligned; small and large.
- Control-flow programs: loop trip counts 0, 1, 2, N, and values that select
  each branch; later, values that make `EXEC` diverge.
- Fuzzing: mutate the instruction list and the inputs, not the bytes, so a
  failure is minimizable at the ISA level. The existing byte-level reader
  fuzzer remains for the parsing layer.

## 7. Minimization and attribution

- Input minimization: delta-debug the input tuple (buffer values, loop count)
  while preserving the divergence.
- Program minimization: delete instructions/blocks while preserving it.
- Attribution: run with block checkpoints; report the first checkpoint and the
  defining instruction's `pc`/opcode.
- Reproduce artifact: a single `.json` with seed, binary hash, IL dump, SPIR-V
  hash, and the first difference.

## 8. CI tiers

| Tier | Runs | Contents |
|---|---|---|
| PR | every commit, minutes | T0, T1, IL interpretation, 100 random T2 programs |
| Nightly | GPU machine | T3 on AMD + lavapipe, T2 corpus, opcode coverage |
| Release | lab machine | T4 native HIP, driver matrix, long fuzz runs |

Metrics: instructions covered (decoder table entries exercised), blocks
covered per fixture, divergence count by category, first-divergence stage, and
time to first divergence. Gates: zero T0/T1 failures; zero integer/control
divergences in T2/T3; float divergences only inside the declared policy.

## 9. Current status

Implemented:

- IL verifier and deterministic dump;
- CFA with dominators, dominance frontier and natural loops;
- SSA construction (alloca promotion, phi placement, renaming);
- DCE and unreachable-block removal;
- IL interpreter (`IL::Interpret`) with register slots;
- multi-block SPIR-V emission with SSA spilling in the dispatcher;
- `spirv-val` on every emitted module;
- per-generation fixtures (gfx10, gfx1013, gfx1030, gfx1100, gfx1150,
  gfx1200) checked at T1 through the full pipeline;
- `SR.ISAProbe` plus `tests/roundtrip.ps1` for the driver compiler round trip.

Next:

1. Full-code-object fixture builder (HIP-visible kernels) for T4.
2. Independent ISA interpreter (T2).
3. Vulkan compute runner with the test ABI (T3).
4. Structured emission for reducible loops and ifs, replacing the dispatcher
   for the common case.
5. Checkpoint instrumentation and the comparator report.
6. Random program generator for ALU + control + private memory.
7. First coverage report over the RDNA1–4 decoder table.
