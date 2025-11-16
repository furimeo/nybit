# Nybit Linker Driver (`nybit link`)

The `nybit` compiler driver includes a built-in static linker driver (`nybit link`) driven directly by `nylink.lib`. It links native object files and static archives into standalone executables without invoking external linkers.

## Command Syntax

```bash
nybit link <inputs...> [-o <output>] [--target=<target>] [-L<dir>] [-l<name>] [--entry=<symbol>] [--base=<address>]
```

### Options

- `<inputs...>`: List of input files in linker command line order. Inputs can be:
  - ELF64 x86-64 relocatable objects (`.o`)
  - Windows COFF AMD64 relocatable objects (`.obj`)
  - Unix `ar` archives (`.a`)
  - Windows static libraries (`.lib`)
  Input formats are detected automatically by inspecting file signatures (`!<arch>\n`, ELF header, COFF machine `0x8664`).
- `-o <output>`: Destination output path. Defaults to `a.out` (on Unix) or `a.exe` (on Windows).
- `--target=<target>`: Select output executable format. Supported targets:
  - `elf64` / `elf64-x86-64`: Static Linux `ET_EXEC` ELF64 executable.
  - `pe` / `pe-x86-64`: Static Windows PE32+ (AMD64) executable.
  Defaults to the host system format if omitted.
- `-L<dir>`: Add `<dir>` to the library search path.
- `-l<name>`: Search for static library in directories specified by `-L`:
  - For `elf64`: searches `lib<name>.a` then `<name>`.
  - For `pe-x86-64`: searches `<name>.lib`, `lib<name>.a`, then `<name>`.
- `--entry=<symbol>`: Specify the entry point symbol. Defaults to:
  - `_start` (or `main` if `_start` is absent) for ELF64.
  - `main` for PE AMD64.
- `--base=<address>`: Base address for executable image. Supports decimal and hexadecimal formats (`0x...`). Defaults to:
  - `0x400000` for ELF64.
  - `0x140000000` for PE AMD64.

## Archive Ingestion & Lazy Member Extraction

When static archives (`.a` or `.lib`) are passed to `nybit link`, members are extracted lazily:
- Only archive members that satisfy currently unresolved global or weak symbols are extracted.
- Members are extracted in cascading fixpoint loops to resolve multi-level and cyclic dependencies between members.
- Unused members are never parsed or emitted into the final executable.

## Determinism & Atomicity

- Executable output generation is bit-for-bit deterministic across runs.
- Output binaries are staged in a temporary file and atomically renamed upon success, ensuring failed link operations do not leave corrupted or partial binaries.

## Current Limitations

- Static linking only: shared libraries (`.so`, `.dll`), dynamic relocations, GOT/PLT, and imports/exports are not supported.
- System standard library paths (`/usr/lib`, Windows SDK paths) are not queried by default; all search paths must be supplied via `-L`.
