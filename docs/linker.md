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

## Features & Capabilities

- **Static Executable Emission**:
  - ELF64 (x86-64 and AArch64) with automatic `_start` trampoline synthesis (BL main; exit_group syscall) when no `_start` symbol is present, or raw entry-point resolution via custom `--entry`.
  - Windows PE32+ (AMD64) with full Optional Header and section headers (`.text`, `.rdata`, `.data`, `.pdata`).
- **Dynamic & Shared Library Emission**:
  - ELF64 `.so` with dynamic symbol table (`.dynsym`), string table (`.dynstr`), hash table (`.hash`), `.dynamic` section, and dynamic relocations (`.rela.dyn`, `.rela.plt`).
  - Windows PE DLL with Export Directory Table (`.edata`), Export Address Table (EAT), Name Pointer Table, and base relocation fixups (`.reloc`).
- **Position-Independent Executables (PIE)**:
  - Full ASLR compatibility on ELF64 platforms with PT_INTERP program header and dynamic linker resolution (`/lib64/ld-linux-x86-64.so.2` on x86-64, `/lib/ld-linux-aarch64.so.1` on AArch64).
- **Import and Export Resolution**:
  - ELF: Dynamic symbol resolution with `-l` and `--needed`, `DT_NEEDED`, and `$ORIGIN` runpath support.
  - Windows PE: Import Directory Table (`.idata`), Import Lookup Table (ILT), Import Address Table (IAT) bound via delay/load thunks.
- **Relocations**:
  - x86-64: `R_X86_64_64`, `R_X86_64_PC32`, `R_X86_64_PLT32`, `R_X86_64_GOTPCREL`.
  - AArch64: `R_AARCH64_ADR_PREL_PG_HI21`, `R_AARCH64_ADD_ABS_LO12_NC`, `R_AARCH64_CALL26`, `R_AARCH64_JUMP26`, `R_AARCH64_LDST64_ABS_LO12_NC`, `R_AARCH64_LDST32_ABS_LO12_NC`.
  - Windows PE: `IMAGE_REL_AMD64_ADDR64`, `IMAGE_REL_AMD64_REL32`, `IMAGE_REL_BASED_DIR64`.
- **Static Archive Resolution**:
  - Lazy member extraction from static archives (`.a` / `.lib`) with fixpoint transitive dependency resolution.
- **Bit-for-Bit Determinism**:
  - Deterministic binary emission without timestamps or non-deterministic metadata.
- **Atomic Output Replacement**:
  - Outputs are written and swapped safely.
