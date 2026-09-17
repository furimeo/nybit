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

## Target Support Matrix

| Target Architecture | Target Format | Status | Runtime Validation |
| :--- | :--- | :--- | :--- |
| **x86-64 Linux** | ELF64 Static / PIE / Dynamic (`.so`) | Supported | Native Linux / WSL |
| **x86-64 Windows** | PE32+ (AMD64) EXE / DLL | Supported | Native Windows Win32 |
| **AArch64 Linux** | ELF64 Static / PIE / Dynamic (`.so`) | Supported | Native ARM64 runner / QEMU user-mode |
| **AArch64 Windows** | PE/COFF AMD64 | Unsupported | N/A |

### ABI & Architecture Highlights
- **x86-64**: System V AMD64 and Microsoft x64 calling conventions with complete stack alignment, register spills, floating point arguments, and callee-saved restoration.
- **AAPCS64 Aggregate ABI**: Complete standard aggregate classification (`aarch64_abi_classify_aggregate`) supporting:
  - Small composites (<= 16 bytes) packed into 1-2 General Purpose Registers (GPRs).
  - Homogeneous Floating-point Aggregates (HFA) of up to 4 uniform float/double members passed in V0-V3.
  - Large aggregates (> 16 bytes or > 4 float members) passed via indirect pointer / sret buffer (X8 register).
  - Mixed scalar, aggregate, and stack outgoing arguments under high register pressure.
- **Unwind & Debug Foundation**: DWARF CIE/FDE unwind table emission (`.eh_frame`) and line/type tables (`.debug_line`, `.debug_info`, `.debug_abbrev`) on ELF; CodeView debug tables on Windows COFF.
- **In-Memory JIT**: Instruction cache flushing (`__builtin___clear_cache` / `FlushInstructionCache`), page protection transitions (`rwx`), and BL/ADRP relocation fixups across both x86-64 and AArch64.

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

## Quality Assurance & Non-Goals

- **Zero Memory Leaks**: Monitored via internal memory tracking wrapper (`ny_alloc`, `ny_free`) across every test case and pass pipeline. Leak checks are strictly verified on all platforms.
- **Deterministic Emission**: All object, executable, and shared library outputs are verified byte-for-byte identical across repeated runs.
- **Continuous Integration**: GitHub Actions testing matrix covering Linux x86-64 (native runner & QEMU fallback), Linux ARM64 (native runner `ubuntu-24.04-arm`), Windows x86-64 (MSYS2 UCRT64), and automated release packaging.
- **Known Limitations & Core V1 Freeze**: Advanced dynamic linking features (such as GNU hash, RELR packed relocations, thread-local storage / TLS, IFUNC, symbol versioning, lazy binding, copy relocations, linker scripts, and LTO) are explicit non-goals for Core V1.

## Documentation

- [NyIR Reference](docs/nyir.md) — complete syntax, instruction set, and examples
- [Linker Driver](docs/linker.md) — `nybit link` command syntax and ELF/PE emission

## License

Mozilla Public License 2.0 (MPL-2.0). See [LICENSE](LICENSE) for details.
