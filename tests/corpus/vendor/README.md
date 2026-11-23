# Vendor container fixtures

These fixtures exercise the SPIR-V, NVIDIA cubin and AMDGPU code-object readers.

- `spirv/simple.spv` is compiled from `spirv/simple.comp` with
  `glslangValidator -V`.
- `vendor/minimal.cubin` and `vendor/minimal.hsaco` are **synthetic container
  fixtures**: a small ELF object from Clang with `e_machine` patched to
  `EM_CUDA` (190) and `EM_AMDGPU` (224). They validate container/section parsing,
  not device instruction semantics. Real cubin/HSACO artifacts replace them as
  soon as the CUDA/ROCm toolchains are available.

Regenerate:

```powershell
glslangValidator -V -S comp spirv/simple.comp -o spirv/simple.spv
clang --target=x86_64-unknown-linux-gnu -c vendor/minimal.c -o vendor/minimal.o
# patch ELF e_machine (offset 18, little-endian) to 190 for .cubin and 224 for .hsaco
```
