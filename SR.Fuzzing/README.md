# SR.Fuzzing

Reader and parser fuzzing harness.

## Modes

- **libFuzzer** (`SR_USE_LIBFUZZER=ON`, Clang GNU front-end): `fuzz_reader.cpp`
  exposes `LLVMFuzzerTestOneInput` and the `SR.Fuzzing` target is linked with
  `-fsanitize=fuzzer`. The project libraries are compiled with
  `-fsanitize=fuzzer-no-link`, so coverage includes the readers.
- **Smoke harness** (fallback, or `SR_USE_LIBFUZZER=OFF`): `smoke_reader.cpp`
  mutates every seed deterministically. It needs no fuzzer runtime and is useful
  on MSVC.

Both call `MasterReader::Read` on arbitrary bytes. A reader must return a
`Diagnostic`; crashing, hanging or reading out of bounds is a bug.

## Build

```powershell
cmake --preset Fuzzing
cmake --build --preset Fuzzing --target SR.Fuzzing
```

`Fuzzing` enables `BUILD_FUZZING` and `SR_USE_LIBFUZZER` and uses the static
CRT, which the prebuilt libFuzzer runtime requires on Windows. ASan/UBSan are
left off in that preset because the prebuilt fuzzer runtime's CRT/sanitizer
annotations do not match them; use a Linux fuzzing build or a matching
libFuzzer runtime for sanitizer fuzzing. The smoke harness can still be built
with `Debug-ASan`/`Debug-UBSan` by setting `BUILD_FUZZING=ON` and
`SR_USE_LIBFUZZER=OFF`.

## Run

libFuzzer:

```powershell
bin\fuzzing\SR.Fuzzing.exe tests\corpus\compiled\dxbc tests\corpus\compiled\dxil tests\corpus\spirv tests\corpus\vendor
```

Smoke harness:

```powershell
bin\fuzzing\SR.Fuzzing.exe --iterations 1000 tests\corpus
```

The smoke harness reads every `.dxbc`, `.dxil`, `.spv`, `.cubin` and `.hsaco`
under the supplied directory.

## Adding seeds

Drop a valid file into the existing corpus directories. For a new format, add
the reader and a small valid seed; libFuzzer mutates from the magic/header and
the smoke harness always starts from the unmodified file.
