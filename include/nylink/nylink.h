// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYLINK_H
#define NYLINK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Nylink_Format {
    NYLINK_FORMAT_UNKNOWN = 0,
    NYLINK_FORMAT_ELF64,
    NYLINK_FORMAT_COFF,
} Nylink_Format;

typedef enum Nylink_Sec_Kind {
    NYLINK_SEC_UNKNOWN = 0,
    NYLINK_SEC_TEXT,
    NYLINK_SEC_RODATA,
    NYLINK_SEC_DATA,
    NYLINK_SEC_BSS,
} Nylink_Sec_Kind;

typedef enum Nylink_Sym_Binding {
    NYLINK_SYM_LOCAL = 0,
    NYLINK_SYM_GLOBAL,
    NYLINK_SYM_WEAK,
} Nylink_Sym_Binding;

typedef enum Nylink_Reloc_Type {
    NYLINK_RELOC_NONE = 0,
    NYLINK_RELOC_X86_64_64,
    NYLINK_RELOC_X86_64_PC32,
    NYLINK_RELOC_X86_64_PLT32,
} Nylink_Reloc_Type;

typedef struct Nylink_Diagnostic {
    char *message;
    char *object_name;
    char *symbol_or_section;
} Nylink_Diagnostic;

typedef struct Nylink_Section {
    uint32_t id;
    uint32_t obj_index;
    Nylink_Sec_Kind kind;
    char *name;
    uint32_t align;
    uint32_t flags;
    size_t size;
    const uint8_t *data;
} Nylink_Section;

typedef struct Nylink_Symbol {
    uint32_t id;
    char *name;
    Nylink_Sym_Binding binding;
    bool is_defined;
    uint32_t sec_id;
    uint64_t value;
    size_t size;
    uint32_t obj_index;
} Nylink_Symbol;

typedef struct Nylink_Relocation {
    uint32_t sec_id;
    uint64_t offset;
    Nylink_Reloc_Type type;
    uint32_t sym_id;
    int64_t addend;
} Nylink_Relocation;

typedef enum Nylink_Target_Format {
    NYLINK_TARGET_ELF64 = 0,
    NYLINK_TARGET_PE,
} Nylink_Target_Format;

typedef struct Nylink_Config {
    Nylink_Target_Format target_format;
    uint64_t base_address;
    const char *entry_point;
} Nylink_Config;

typedef struct Nylink_Context Nylink_Context;

Nylink_Context *nylink_context_create(void);
void nylink_context_destroy(Nylink_Context *ctx);

bool nylink_add_object(Nylink_Context *ctx, const char *name, const uint8_t *data, size_t size);
bool nylink_resolve_symbols(Nylink_Context *ctx);
bool nylink_layout(Nylink_Context *ctx, const Nylink_Config *cfg);
bool nylink_apply_relocations(Nylink_Context *ctx);
bool nylink_write_executable(Nylink_Context *ctx, const char *out_path, const Nylink_Config *cfg);

size_t nylink_get_section_count(const Nylink_Context *ctx);
const Nylink_Section *nylink_get_section(const Nylink_Context *ctx, size_t index);
uint64_t nylink_section_get_va(const Nylink_Context *ctx, uint32_t sec_id);

size_t nylink_get_symbol_count(const Nylink_Context *ctx);
const Nylink_Symbol *nylink_get_symbol(const Nylink_Context *ctx, size_t index);
const Nylink_Symbol *nylink_find_symbol(const Nylink_Context *ctx, const char *name);
uint64_t nylink_symbol_get_final_va(const Nylink_Context *ctx, uint32_t sym_id);

size_t nylink_get_relocation_count(const Nylink_Context *ctx);
const Nylink_Relocation *nylink_get_relocation(const Nylink_Context *ctx, size_t index);

size_t nylink_get_diagnostic_count(const Nylink_Context *ctx);
const Nylink_Diagnostic *nylink_get_diagnostic(const Nylink_Context *ctx, size_t index);
bool nylink_has_errors(const Nylink_Context *ctx);

#ifdef __cplusplus
}
#endif

#endif
