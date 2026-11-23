# Shader Recompiler test corpus

HLSL sources and compiled artifacts used to exercise the recompiler pipeline.
The sources are the human-readable contract; the compiled DXBC/DXIL files are the
actual inputs the front ends will consume.

```
tests/corpus/
  manifest.json          target list, profiles and feature tags
  sources/               HLSL sources
  compiled/
    dxil/                DXIL containers (checked in)
    dxil-asm/            DXIL disassembly (checked in)
    dxbc/                DXBC SM5 containers (checked in)
    dxbc-asm/            DXBC disassembly (checked in)
    index.json           artifact hashes and toolchain versions
  scripts/
    build_corpus.ps1     regenerate compiled artifacts
    validate_corpus.ps1  check manifest, sources, artifacts and hashes
```

## Categories

| Directory | What it covers |
|---|---|
| `basic` | scalar/vector ALU, control flow, functions and structs |
| `memory` | global/shared memory, barriers, atomics, append/consume, typed buffers |
| `wave` | ballot, vote, shuffle, reductions, prefix operations |
| `texture` | sampling, gather, comparison, load/store, derivatives |
| `graphics` | vertex, pixel and geometry stages |
| `mesh` | mesh and amplification shaders |
| `wmma` | wave/thread-group matrix multiply, FP16/FP8/INT8, outer product |
| `raytracing` | DXR shaders, inline RayQuery, software BLAS/TLAS builders and traversal |

`spirv/` and `vendor/` hold reader fixtures: a GLSL-to-SPIR-V module and
synthetic ELF containers for the NVIDIA cubin and AMDGPU HSACO readers. The
vendor README explains what is real and what is synthetic.

`gfx10/`, `gfx1013/`, `gfx1030/`, `gfx1100/`, `gfx1150/` and `gfx1200/` hold
real AMDGPU code objects assembled from `*.s` with ROCm's `llvm-mc`, one
directory per generation. They exercise decode → translate → CFA → SSA →
optimize → SPIR-V end to end, and `build_gfx.ps1` regenerates all of them.

Round-trip checks live in `tests/roundtrip.ps1`: they recompile each fixture to
SPIR-V, compile that SPIR-V with the installed Vulkan driver through
`SR.ISAProbe`, and compare the driver's ISA against the original.

`raytracing/raygen.hlsl` traces into a DXR `RaytracingAccelerationStructure`
(the TLAS). BLAS/TLAS are normally built by the API, so the corpus also contains
software `blas_build`, `tlas_build` and `tlas_traverse` compute shaders that
exercise the same data structures, bounds math and atomics directly in a shader.
`fixtures/scene.json` is the deterministic test scene: two BLAS (a triangle quad
and a procedural sphere AABB) and three TLAS instances. It is the input contract
for host-side acceleration-structure tests.

The corpus targets DXBC SM5 and DXIL, and now also has AMDGPU ISA fixtures for
RDNA1 through RDNA4. Vendor ISA corpora for SASS and Gen/Xe will be added
alongside their front ends; the source layout and manifest are ready for them.

## Toolchain

- `dxc` for DXIL. WMMA sources use the SM 6.10 `dx::linalg` header and require
  DXC 1.10+ with `include/hlsl/dx/linalg.h`.
- `fxc` for DXBC SM5 where the source is SM5-compatible.
- The checked-in artifacts record the tool versions used. Tests never download
  or compile anything; `validate_corpus.ps1` only hashes files.

## Build

```powershell
# DXIL with a full DXC install (including dx/linalg.h for WMMA)
powershell -ExecutionPolicy Bypass -File scripts/build_corpus.ps1 -Dxc C:\tools\dxc\dxc.exe

# Both DXIL and DXBC; fxc is discovered under Windows Kits
powershell -ExecutionPolicy Bypass -File scripts/build_corpus.ps1

# Rebuild everything from scratch. Requires a DXC 1.10+ install with
# include/hlsl/dx/linalg.h, otherwise WMMA outputs are skipped.
powershell -ExecutionPolicy Bypass -File scripts/build_corpus.ps1 -Clean

# Only one format
powershell -ExecutionPolicy Bypass -File scripts/build_corpus.ps1 -SkipDxbc
powershell -ExecutionPolicy Bypass -File scripts/build_corpus.ps1 -SkipDxil
```

Partial builds rewrite `compiled/index.json` with only the built targets. Run a
full build before committing or validating the corpus.

## Validate

```powershell
powershell -ExecutionPolicy Bypass -File scripts/validate_corpus.ps1
```

Validation fails on a missing source, a missing artifact, a stale hash, or a
manifest entry without an output.

## Adding a target

1. Add the HLSL file under `sources/<category>/`.
2. Add an entry to `manifest.json` with `name`, `source`, `entry`, profiles and
   feature tags.
3. Run `build_corpus.ps1` and commit the source, artifacts, disassembly and
   `compiled/index.json`.

Prefer small, focused shaders over large samples. Each target should isolate one
feature family so a recompiler regression points at one stage. Cross-target
duplication is acceptable when it keeps the failing feature obvious.
