# Shader Recompiler

Shader recompiler is a project meant to convert already compiled GPU assembly into SPIR-V bytecode in order to allow the
translation of kernels and shaders through architectures.

The pipeline is simple:
- You first write a decoder for the custom binary format the architecture uses, implementing the base architecture family (example: RDNA, which has many revisions, such as RDNA2,RDNA3,RDNA3.5,RDNA4, ...) and whichever extensions it may need
- You then translate the ISA into the custom IL format (if the ISA requires a brand new IL opcode, you must implement it and then appropriately translate the IL instruction into SPIR-V, including the creation of brand new tests for it)
- Create new test samples that exercise the new ISA and binary file
- Take the SPIR-V, compile it to the target architecture, and then feed it back through the recompiler. If it outputs equal code behaviourally then it is properly implemented, else it must be revised.

## Pipeline

```
Decode -> Translate -> CFA -> SSA -> Optimize -> Lift to SPIR-V
```

`./shader-recomp.exe --arch TARGET_ARCHITECTURE --dump-il --spirv <OUTPUT_SPIRV> <INPUT_BINARY>` runs every stage and validates the result with
SPIRV-Tools against Vulkan 1.3.

## Development

### Build

```powershell
cmake --preset Debug
cmake --build --preset Debug
ctest --test-dir cmake-build-debug --output-on-failure
```

#### Available targets for interaction

| Target     | Role                   |
|------------|------------------------|
| `SR.CLI`   | Comamnd line interface |
| `SR.Tests` | Tests                  |

## Testing divergences

The project requires numerous testing and evaluation to obtain the proper shader back from the binary file. During the translation of the ISA to the IL or the IL to the SPIR-V IR there may be mistakes within the implementations. For this you must test and probe the shaders in case of mistakes and errors.

The oracle ladder and harness design are in
[`docs/DIVERGENCE_TESTING.md`](docs/DIVERGENCE_TESTING.md).

`SR.ISAProbe` sends an emitted module through the installed Vulkan driver and prints the ISA it produced.
`tests/roundtrip.ps1` runs the whole loop for every gfx generation: ISA -> IL -> SPIR-V -> driver compiler -> ISA, and
checks that the source opcode classes survive.

A set of shaders with pre-compiled builds and disassembled samples live in [`tests/corpus`](tests/corpus/README.md) for this purpose.
