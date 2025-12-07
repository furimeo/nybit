# Nybit Linker Driver (`nybit link`)

The `nybit` compiler driver includes a built-in static linker driver (`nybit link`) driven directly by `nylink.lib`. It links native object files and static archives into standalone executables without invoking external linkers.

## Command Syntax

```bash
nybit link <inputs...> [-o <output>] [--target=<target>] [-L<dir>] [-l<name>] [--entry=<symbol>] [--base=<address>]
```

### Options

- `<inputs...>`: List of input files in linker command line order. Inputs can be:
  - ELF64 (x86-64 and AArch64) relocatable objects (`.o`)
  - Windows COFF AMD64 relocatable objects (`.obj`)
  - Unix `ar` archives (`.a`)
  - Windows static libraries (`.lib`)
  Input formats are detected automatically by inspecting file signatures (`!<arch>\n`, ELF header, COFF machine `0x8664`).
- `-o <output>`: Destination output path. Defaults to `a.out` (on Unix) or `a.exe` (on Windows).
- `--target=<target>`: Select output executable or shared object format. Supported targets:
  - `elf64` / `elf64-x86-64`: Linux x86-64 executable or shared object.
  - `elf64-aarch64`: Linux AArch64 executable or shared object.
  - `pe` / `pe-x86-64`: Windows PE32+ (AMD64) executable or DLL.
  Defaults to the host system format if omitted.
- `--shared`: Emit shared library / dynamic library (`.so` or `.dll`).
- `--pie`: Emit position-independent executable (PIE).
- `-L<dir>`: Add `<dir>` to the library search path.
- `-l<name>`: Search for library in directories specified by `-L`.
- `--entry=<symbol>`: Specify the entry point symbol.
- `--base=<address>`: Base address for executable image.

## Features

- Static executable emission: ELF64 (x86-64, AArch64) and Windows PE32+ (AMD64).
- Dynamic / shared library emission: ELF64 `.so` with dynamic symbol table, hash tables, and PLT/GOT relocations; Windows PE DLL with export directory and base relocations (`.reloc`).
- Position-independent executables (PIE) with full ASLR compatibility.
- Lazy member extraction from static archives with fixpoint transitive dependency resolution.
- Bit-for-bit deterministic binary emission.
- Atomic file replacement for safe builds.
