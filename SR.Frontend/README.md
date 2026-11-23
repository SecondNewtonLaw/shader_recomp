# SR.Frontend

Binary readers and the format registry.

## Contract

`IBinaryReader` exposes:

- `Format()` and `Name()`
- `Probe(bytes)` — header check only
- `Read(bytes)` — returns `DecodedShader` or `Diagnostic`

`MasterReader` probes registered readers in order and dispatches to the first
match. DXIL is probed before DXBC because DXIL lives inside a DXBC container.

## Implemented formats

| Reader | Format | Coverage |
|---|---|---|
| `DxbcReader` | DXBC SM5 | container parts, program header, instruction token boundaries, extended and custom-data tokens |
| `DxilReader` | DXIL | container parts, `ProgramHeader`, `BitcodeHeader`, stage and shader-model extraction, bitcode payload |
| `SpirvReader` | SPIR-V (Vulkan/OpenGL) | five-word module header, instruction stream, entry point and stage |
| `CubinReader` | NVIDIA cubin | ELF container, `.text` functions, raw 128-bit SASS words |
| `HsacoReader` | AMDGPU HSACO | ELF container, `.text` words; gfx10–gfx12 decode/lift in `Arch/Gfx` |

DXBC instructions currently expose the opcode id plus the raw token sequence.
SASS and GCN/RDNA currently expose raw instruction words; operand/opcode
semantics are the remapper's job. The raw data is the stable input for the ISA
descriptor tables.

## AMD GFX decode and lift

`SR.Frontend::Gfx` contains a lineage decoder and a lifter:

- `GfxIsa.hpp` decodes SOP1/SOP2/SOPP/VOP1/VOP2/VOP3 for RDNA1 through RDNA4.
  RDNA1 is the base table; each later generation is a delta that adds, removes
  or renumbers entries. See [docs/GFX_ISA_LINEAGE.md](../docs/GFX_ISA_LINEAGE.md).
- `GfxLifter.hpp` lifts a straight-line `.text` payload into a COMMON_SHADER
  module. `GfxRemapper` is the `IIsaRemapper` implementation.

Currently lifted:

- scalar: `s_mov_b32`, `s_add_u32`, `s_sub_u32`, `s_and/or/xor_b32`,
  `s_lshl/lshr_b32`, `s_cselect_b32`, the `s_cmp_*` compares and
  `s_add_co_u32` / `s_sub_co_u32` with SCC;
- vector: `v_mov_b32`, `v_add/sub/mul_f32`, `v_and/or/xor_b32`, `v_fma_f32`;
- control: unconditional branches, SCC branches and loops; EXEC branches fold
  while no instruction writes `EXEC`;
- private register slots (alloca + load/store) and `s_endpgm`.

Everything else is diagnosed with the source pc: EXEC masks and saveexec, VCC,
memory and descriptors, barriers, inline modifiers, and the rest of the ISA.
Nothing is guessed or silently dropped.

After translation the common chain runs: CFA (edges, dominators, dominance
frontiers, natural loops), SSA (register-slot promotion, phi placement,
renaming), then optimize (dead code and unreachable blocks). Emission consumes
the SSA module; the dispatcher spills cross-block values and phis into function
variables before lowering to structured SPIR-V.

Fixture directories exist for `gfx10`, `gfx1013`, `gfx1030`, `gfx1100`,
`gfx1150` and `gfx1200`. Rebuild all of them with:

```powershell
powershell -ExecutionPolicy Bypass -File tests/corpus/build_gfx.ps1
```

Then run the full pipeline and the driver round trip:

```powershell
bin\debug\shader-recomp.exe --arch gfx1200 --dump-il --spirv out.spv `
        tests\corpus\gfx1200\loop.hsaco
bin\debug\SR.ISAProbe.exe out.spv --disassembly
powershell -ExecutionPolicy Bypass -File tests/roundtrip.ps1
```

`Reemit` validates the emitted module with SPIRV-Tools against Vulkan 1.3 before
returning it. `SR.ISAProbe` compiles it with the installed Vulkan driver through
`VK_AMD_shader_info`, which is the "SPIR-V -> compiler -> ISA" half of the
round-trip check.

Vulkan and OpenGL both consume SPIR-V; OpenGL has no portable compiled-shader
container. GLSL source is not a binary-reader target. CUDA cubin is the ELF
shader binary; fatbin is a wrapper around cubins/PTX and is not implemented yet.
PTX is a text ISA, not a binary container. ROCm shader binaries are AMDGPU ELF
code objects (HSACO).

## References used

- DirectX Container: <https://llvm.org/docs/DirectX/DXContainer.html> and
  `llvm/include/llvm/BinaryFormat/DXContainer.h`
- DXIL container structures: DXC `include/dxc/DxilContainer/DxilContainer.h`
- DXBC token format: `d3d11TokenizedProgramFormat.hpp` from the Windows Driver
  Kit (opcode token bits, extended token chain, custom-data blocks)
- SPIR-V: Khronos SPIR-V specification (five-word module header, instruction
  `word count | opcode`, `OpEntryPoint`)
- ELF containers: ELF gABI; LLVM `AMDGPUUsage` for AMDGPU code objects; CUDA
  binary utilities and `nvFatbin` for cubin/fatbin

## Planned readers

`FatbinReader` (CUDA fatbin wrapper) is the next CUDA addition. Full SASS and
GCN/RDNA opcode decoding lives in the ISA descriptor layer, not in the container
reader. Each format remains a separate `IBinaryReader`; `MasterReader` only needs
registration.
