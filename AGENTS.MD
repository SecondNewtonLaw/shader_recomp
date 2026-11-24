# shader_recomp — agentic guidance

You are an agent working on a shader recompiler project.

The intention of this project is to take in a compiled binary, such as `dxil`, `dxbc`, `spv`, among others...; translating the instructions inside the container into a custom Intermediate Representation, where analysis and optimizations are applied (if necessary), and later the custom IR gets lowered into proper SPIR-V IL.

This allows for the recovery of shaders compiled to explicit machine code back into SPIR-V, where they can be re-targetted into almost any architecture that the SPIR-V compiler supports.

### Commits
When creating commits, make sure you use conventional commits with an atomic shape, one commit per change in order to allow for revert to work as expected. For example: `feat:` for new features or changes, `fix:` for fixes, `docs:` for documentation, `breaking change:` for breaking changes, ...; you may apply a scope, such as `feat(IR): map [OPCODE NAME] into [SPIR-V OPCODE NAME]`, where the change is scoped to a specific section of the project. You may use agents for agent related notes (such as this file), Tests for integration tests, Fuzzing, Deserialization, et cetera.

Given that you're an agent acting on behalf of another user, you must **explicitly** express it in the commit messages, pr content or issue messages at the tail of the message with a footer such as:

```
---
Submitted by [MODEL NAME] in [HARNESS NAME]
```

This way agentic changes can be attributed to the agent, not to the human.

### Maintain a clean workspace
During agentic sessions it is common that you create numerous intermediate files, intermediate new targets, among other things. You must clear your workspace after the work is done and provide the user with a summary of the changes.

### Tests
Tests in this codebase are not to be unit tests—rather integration tests. Integration tests evaluate the entire pipeline, allowing us to catch issues with the combination of everything rather than per-unit, if a unit has a hidden bug, but the unit test does not exercise the edge case due to whichever the case may be we will never catch it. Always prefer integration tests over unit tests.

### Architecture

The compiler is a pipeline with one responsibility per stage. Keep those boundaries; when a change crosses one, prefer adding data to a table over adding a special case in common code.

1. **Decode.** `SR.Frontend` parses the container (`DXBC`, `DXIL`, `SPIR-V`, `cubin`, `HSACO`) and the per-ISA decoder turns bytes into instructions, operands, labels and source `pc`. Decoding knows the encoding, not the meaning. Unsupported encodings are diagnostics, not guesses.
2. **Translate.** The per-ISA translator lowers instructions into the lift form of `COMMON_SHADER`: typed values, register slots (`alloca` plus load/store), compares, branches and intrinsics. Basic blocks are discovered here from branch targets and fallthroughs; CFA consumes them. Translation does not know about SPIR-V and does not build SSA.
3. **CFA.** `SR.Common` computes edges, reverse postorder, dominators, dominance frontiers and natural loops. This is analysis only; it does not rewrite the module.
4. **SSA.** Register slots are promoted to SSA values: phis are placed at the iterated dominance frontier, definitions are renamed, and the load/store form disappears. A register that escapes or is indexed remains an alloca.
5. **Optimize.** Dead pure instructions and unreachable blocks run here, on the SSA module; constant folding is the next pass in this stage. Optimizations are ISA-neutral; they never look at an opcode table.
6. **Lift to SPIR-V.** `SR.Backend` consumes the optimized SSA module and emits SPIR-V 1.6 for Vulkan 1.3. Multi-block functions currently go through the dispatcher fallback, which spills values across dispatch edges; structured emission for reducible loops and ifs is the tracked next step. Every module is validated with SPIRV-Tools, and executed on the detected GPU when the fixture has an observable effect.

Per-ISA differences live in data. Every generation builds on top of the one before it: a base table for the root architecture, and a delta for each later generation. Keep generation checks out of the passes and the emitter; a table entry should carry the difference.

Correctness is tied to the machine, not to a named architecture. Before you claim a shader is correct, enumerate the GPU that can actually execute compute and record what you found: `vulkaninfo --summary`, `SR.ISAProbe` or `SR.VkRun` for Vulkan, `amdgpu-arch` or the vendor equivalent for the driver stack. Use that device and the corpus generation that matches it for execution and ISA comparison. If no usable GPU is present, fall back to the IL interpreter and structural checks, and say explicitly that execution was not verified. Never hard-code the maintainer's GPU, its architecture, or its driver into tests, docs, or code; the device is an environment fact, discovered at run time.

Expand other ISAs by reusing the same IL and emitter and proving that their instructions translate; do not fork the pipeline per architecture. Everything unsupported fails with a diagnostic carrying the source `pc` and opcode; silent approximation or dropping is a bug.

Tests are integration tests. A unit existing and passing proves little; the pipeline and the shader behavior are the contract. Prefer a small checked-in fixture, the IL interpreter, and a GPU run over a mock. The divergence ladder and the round-trip tools are in `docs/DIVERGENCE_TESTING.md`; the instruction coverage report is generated from `tools/coverage.py`.

### File map

Start with `docs/ARCHITECTURE.md` and `docs/IL_ARCHITECTURE.md`; they are the normative documents. The rest of the map is ordered by what you are likely to need.

| Path | What it is |
|---|---|
| `CMakeLists.txt` | Targets, static CRT, Clang flags, Vulkan tool discovery, fuzzing options |
| `CMakePresets.json` | Debug, Release, ASan, UBSan, Fuzzing |
| `cmake/` | CPM bootstrap, ccache, compiler detection, dependency wiring |
| `docs/ARCHITECTURE.md` | High-level pipeline and stage contracts |
| `docs/IL_ARCHITECTURE.md` | Normative `COMMON_SHADER` spec: types, ops, intrinsics, SSA, structuring |
| `docs/IL_MAPPING.md` | Vendor construct to IL to SPIR-V quick table |
| `docs/ISA_SURVEY.md` | NVIDIA/AMD/Intel common constraints and differences |
| `docs/GFX_ISA_LINEAGE.md` | Base-plus-delta decoder model, measured generation diffs |
| `docs/DIVERGENCE_TESTING.md` | Oracle ladder T0-T4, test ABI, comparison policy |
| `docs/reviews/0001-dispatch-instruction-overhead.md` | Why the loop emits 29 instead of 7 instructions |
| `docs/XBOX_SERIES_X.md` | Xbox Series X|S container format, execution model and the roadmap to SPIR-V |
| `docs/coverage.html` | Generated instruction coverage graph |
| `SR.Common/include/SR/Common/IL/` | IL headers: `Types`, `Instruction`, `Intrinsics`, `Module`, `Builder`, `Analysis`, `Passes`, `Interpreter`, `Verifier`, `Dump` |
| `SR.Common/src/IL/` | IL implementations, passes and the reference interpreter |
| `SR.Common/include/SR/Common/Diagnostic.hpp` | `Result`, `Status`, diagnostics; every failure path uses it |
| `SR.Common/include/SR/Common/ByteReader.hpp` | Bounds-checked byte reader used by all readers |
| `SR.Frontend/include/SR/Frontend/Frontend.hpp` | `IBinaryReader` and `IIsaRemapper` contracts |
| `SR.Frontend/include/SR/Frontend/DecodedShader.hpp` | Decoded instruction stream and metadata |
| `SR.Frontend/include/SR/Frontend/MasterReader.hpp` | Reader registry and probe order |
| `SR.Frontend/src/Container/` | DXBC and ELF containers, and the Xbox Series X|S XDAT/XBCH/FSSM reader |
| `SR.Frontend/src/Readers/` | DXBC, DXIL, SPIR-V, cubin, HSACO and Xbox container readers |
| `SR.Frontend/include/SR/Frontend/Arch/Gfx/GfxIsa.hpp` | gfx generations, encodings, opcodes, operands, `Decode` |
| `SR.Frontend/src/Arch/Gfx/GfxIsa.cpp` | Lineage tables and instruction decoding |
| `SR.Frontend/include/SR/Frontend/Arch/Gfx/GfxLifter.hpp` | `Lift`, `LiftOptions`, `GfxRemapper` |
| `SR.Frontend/src/Arch/Gfx/GfxLifter.cpp` | Block discovery, register slots, branches, SCC, test ABI store |
| `SR.Backend/include/SR/Backend/Reemitter.hpp` | `Reemit`, `Emission`, target profile |
| `SR.Backend/src/SpirvEmitter.cpp` | SPIR-V types, SSA spilling, dispatcher, storage buffer, validation |
| `SR.CLI/main.cpp` | `--inspect`, `--arch`, `--spirv`, `--dump-il`, `--test-abi` |
| `SR.Tools/IsaProbe.cpp` | `VK_AMD_shader_info` disassembly: SPIR-V to driver ISA |
| `SR.Tools/VkRun.cpp` | Test-ABI compute dispatch and buffer readback |
| `SR.Tests/GfxTests.cpp` | Decode, lineage, loop, store and end-to-end pipeline tests |
| `SR.Tests/CorpusReaderTests.cpp` | DXBC/DXIL corpus reads |
| `SR.Fuzzing/` | libFuzzer entry point and deterministic smoke harness |
| `tests/corpus/gfx<generation>/` | Real `llvm-mc` fixtures per generation; use the directory matching the detected GPU |
| `tests/corpus/build_gfx.ps1` | Rebuild the gfx fixtures |
| `tests/roundtrip.ps1` | ISA -> IL -> SPIR-V -> driver ISA structural check |
| `tests/sweep.ps1` | Parallel translate-all, driver-probe and draw sweep with a pass/fail table |
| `tests/isa_compare.ps1` | Numbered instruction-stream comparison and category counts |
| `tools/coverage.py` | Coverage graph generator from the AMD XML and the tables |
| `COMMIT_PLAN.md` | Draft commit stack, waiting for the commit guidelines |

### Working on an instruction

1. Add the encoding/opcode record to the right generation table in `SR.Frontend/src/Arch/Gfx/GfxIsa.cpp`; if a later generation renumbers it, add the removal and the new entry in that delta.
2. Add or reuse a canonical `Gfx::Op` in `GfxIsa.hpp`; keep the vendor mnemonic out of the IL.
3. Add the lift rule in `GfxLifter.cpp`; return a diagnostic with the source `pc` for anything not implemented.
4. If the rule introduces a new IL op or intrinsic, update the verifier, the interpreter, the dumper and the emitter table in the same change.
5. Add a small fixture under `tests/corpus/gfx<generation>/` for the detected GPU (or the generation you are implementing), regenerate with `tests/corpus/build_gfx.ps1`, and extend `SR.Tests/GfxTests.cpp`.
6. Run the pipeline, `spirv-val`, the interpreter, and the GPU runner when the fixture has an observable effect. Regenerate the coverage graph last.

