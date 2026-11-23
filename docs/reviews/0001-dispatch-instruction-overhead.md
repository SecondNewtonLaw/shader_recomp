# Review 0001 — dispatcher instruction overhead

Status: accepted finding, action pending.
Fixture: `tests/corpus/gfx1200/loop.hsaco` (RDNA4, `s0 = 0..10` loop).
Pipeline: `Decode -> Translate -> CFA -> SSA -> Optimize -> SPIR-V`.
Comparison tooling: `tests/isa_compare.ps1`, `tests/roundtrip.ps1`,
`SR.ISAProbe`.

## Finding

The original ISA has 7 instructions. The ISA produced by the Vulkan driver from
our SPIR-V has 29. The conversion is structurally lossless (every source opcode
class survives), but it is not instruction-optimal. The overhead is in the
emitter, not in the IL: the SSA module for this loop is the 4-block phi form
shown below.

Original:

```asm
s_mov_b32 s0, 0
s_mov_b32 s1, 10
L:
  s_cmp_lt_u32 s0, s1
  s_cbranch_scc0 done
  s_add_co_u32 s0, s0, 1
  s_branch L
done:
  s_endpgm
```

Driver ISA (`SR.ISAProbe --disassembly`, gfx1200):

```asm
 1: s_mov_b32 s3, 0
 2: s_mov_b32 s2, 0
 3: s_branch _L14
 4: s_and_not1_b64 vcc, exec, s[0:1]
 5: s_cbranch_vccz _L70
 6: s_mov_b32 s5, s2
 7: s_mov_b32 s4, s3
 8: s_cmp_lt_i32 s2, 2
 9: s_mov_b64 s[0:1], -1
10: s_cbranch_scc0 _L48
11: s_mov_b32 s2, 1
12: s_cmp_lg_u32 s5, 1
13: s_mov_b32 s3, s5
14: s_cbranch_scc1 _L44
15: s_cmp_gt_u32 s4, 9
16: s_mov_b32 s3, s4
17: s_cselect_b32 s2, 3, 2
18: s_mov_b64 s[0:1], 0
19: s_delay_alu instid0(SALU_CYCLE_1)
20: s_and_not1_b64 vcc, exec, s[0:1]
21: s_cbranch_vccnz _L14
22: s_cmp_lt_i32 s5, 3
23: s_mov_b64 s[0:1], -1
24: s_cbranch_scc0 _Lc
25: s_mov_b32 s2, 1
26: s_add_co_i32 s3, s4, 1
27: s_mov_b64 s[0:1], 0
28: s_branch _Lc
29: s_endpgm
```

## Breakdown

| Category | Original | Driver | Delta |
|---|---:|---:|---:|
| arithmetic | 1 | 3 | +2 |
| compare | 1 | 4 | +3 |
| select | 0 | 1 | +1 |
| move | 2 | 8 | +6 |
| branch | 2 | 5 | +3 |
| convergence | 0 | 6 | +6 |
| scheduling | 0 | 1 | +1 |
| end | 1 | 1 | 0 |
| **total** | **7** | **29** | **+22** |

## Why 29

The IL is already compact:

```
block0: br block1
block1:
  %14 = phi [%0, block0] [%12, block2]
  %6  = cmp.ult %14, %10
  %9  = unary.not %6
  condbr uniform %9, block3, block2
block2:
  %12 = binary.iadd %14, %1
  br block1
block3: ret
```

The emitter lowers *every* multi-block function to a dispatcher, regardless of
shape:

1. a function-local `pc` variable and a spill variable for the phi;
2. a `while (true)` loop whose body loads `pc` and `OpSwitch`es over block IDs;
3. each case stores the successor ID and branches to the loop continue;
4. each conditional branch becomes a nested `OpSelectionMerge` with two stores;
5. every cross-block value and every phi is spilled to a function variable.

That is a correct, general fallback, but it turns a structured loop into an
interpreter of its own CFG. The driver then has to compile the interpreter:

- **No indirect branch on AMD.** `OpSwitch` is lowered to a chain of scalar
  compares and branches (`cmp s2, 2`, `cbranch`, `cselect`). The pc state
  occupies `s2..s5`, which accounts for most of the extra moves and compares.
- **Loop/switch convergence.** The nested loop + switch form makes the driver
  emit EXEC/VCC bookkeeping (`s_and_not1_b64 vcc, exec`, `s_cbranch_vccz/vccnz`,
  `s_mov_b64 s[0:1]`) even though our branches are scalar-uniform.
- **Scheduling.** `s_delay_alu` is added for the scalar pipeline.
- **The driver did partially recover the loop** (`s_add_co_i32 s3, s4, 1` and
  the counter compare `s_cmp_gt_u32 s4, 9`), but it cannot remove the dispatch
  state because the input is a state machine.

Nothing is lost semantically; the cost is the state machine itself.

## Remediation

Emit structured control flow directly for reducible CFGs:

1. **Natural loops** (`FunctionAnalysis::loops` already finds headers, blocks
   and latches): emit `OpLoopMerge`, a header block, a continue target, and a
   merge block; the loop-carried SSA values become `OpPhi` at the header.
2. **If/else diamonds**: emit `OpSelectionMerge` with direct
   `OpBranchConditional` and a merge block.
3. **Phi values**: emit `OpPhi` instead of spill variables when the target block
   is in the same structured construct.
4. **Keep the dispatcher** only for irreducible CFGs and unresolved indirect
   branches, as specified in `docs/IL_ARCHITECTURE.md` §9.6.

Expected result for this fixture: the driver should see a normal loop (phi,
compare, conditional branch, add, branch, end), i.e. roughly the original 7–9
instructions, with no pc variable and no switch.

## Acceptance criteria

- `tests/roundtrip.ps1` reports driver ISA counts within a small constant of the
  original for structured fixtures (proposed: `driver <= original + 2` for the
  loop fixture).
- No `pc`/switch/dispatch state in the driver ISA for reducible fixtures.
- The IL verifier and `spirv-val` remain clean.
- The IL interpreter and driver ISA agree at T3 once the test ABI exists.

Add the instruction-count comparison as a non-blocking report now and a gate
once structured emission lands.

## Action items

| Item | File |
|---|---|
| structured loop + selection emission | `SR.Backend/src/SpirvEmitter.cpp` |
| phi emission (no spill for structured cases) | same |
| structured-vs-dispatcher choice from CFA | same, using `IL::FunctionAnalysis` |
| instruction-count budget in round-trip | `tests/roundtrip.ps1` |
| regression fixture with nested if/loop | `tests/corpus/gfx1200/` |
