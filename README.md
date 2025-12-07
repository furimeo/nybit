# Nybit

Nybit is a lightweight, low-level compiler infrastructure, JIT runtime, and linker written in C23 with zero external toolchain dependencies and zero memory leaks.

## Architecture & Subsystems

The toolchain is divided into three core subsystems:

- **`nygen` (Compiler Core & Code Generation)**:
  - Textual and binary Nybit Intermediate Representation (NYIR) with full serialization roundtripping.
  - SSA-based optimization pipeline: GVN, constant folding, dead code elimination, CFG simplification, and canonicalization.
  - Instruction selection and linear scan register allocation for x86-64 and AArch64.
  - Relocatable object generation for ELF64 and Windows COFF AMD64 with DWARF and CodeView debug foundations.
- **`nyjit` (In-Memory JIT Engine)**:
  - Native in-memory execution with memory protection (`rwx` transitions), symbol resolution, and host interop.
- **`nylink` (Native Linker)**:
  - Standalone linker emitting static executables, position-independent executables (PIE), and shared libraries (`.so` / `.dll`).
  - Transitive lazy member extraction from Unix `.a` archives and Windows `.lib` static libraries.
  - Bit-for-bit deterministic binary emission.

## Target Architectures

- **x86-64**:
  - System V AMD64 ABI (Linux)
  - Microsoft x64 ABI (Windows)
  - Full support for 64-bit integer, floating point, stack arguments, callee-saved registers, and frame unwinding.
- **AArch64**:
  - AAPCS64 standard ABI (Linux ELF)
  - Homogeneous Floating-point Aggregates (HFA) and small struct register packing
  - DWARF CFI/FDE unwind table generation
  - Runtime validation via native ARM64 or QEMU user-mode runner

## Building & Testing

### Requirements
- C23-compliant C compiler (`gcc` >= 14 or `clang` >= 18)

### Linux
```bash
# Build and run test suite
./build.sh --run-tests

# Run performance benchmarks
./build.sh --run-bench

# Package distribution archive
./build.sh --package
```

### Windows (PowerShell)
```powershell
# Build and run test suite
.\build.ps1 -RunTests

# Run performance benchmarks
.\build.ps1 -RunBench

# Package distribution archive
.\build.ps1 -Package
```

### CMake
```bash
cmake -B build
cmake --build build
ctest --test-dir build
```

## Quality Assurance

- **Zero Memory Leaks**: Monitored via internal memory tracking wrapper across every test case and pass pipeline.
- **Deterministic Emission**: All object, executable, and shared library outputs are verified byte-for-byte identical across runs.
- **Continuous Integration**: GitHub Actions testing matrix covering Linux x86-64 (native & QEMU AArch64), Linux ARM64 (native runner), and Windows x86-64.

## License

Mozilla Public License 2.0 (MPL-2.0). See [LICENSE](LICENSE) for details.
