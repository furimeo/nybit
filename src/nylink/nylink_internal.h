// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYLINK_INTERNAL_H
#define NYLINK_INTERNAL_H

#include "nylink/nylink.h"
#include "nybit/support.h"

typedef struct Nylink_Object {
    char *name;
    Nylink_Format format;
    const uint8_t *data;
    size_t size;
    uint32_t first_sec_idx;
    uint32_t sec_count;
    uint32_t first_sym_idx;
    uint32_t sym_count;
} Nylink_Object;

typedef struct Nylink_Output_Section {
    Nylink_Sec_Kind kind;
    const char *name;
    uint64_t va;
    uint64_t file_offset;
    uint64_t file_size;
    uint64_t mem_size;
    uint32_t align;
    uint8_t *data;
    size_t data_capacity;
} Nylink_Output_Section;

typedef struct Nylink_Input_Sec_Layout {
    uint32_t out_sec_idx;
    uint64_t offset_in_out_sec;
    uint64_t va;
} Nylink_Input_Sec_Layout;

typedef struct Nylink_Resolved_Sym {
    uint32_t sym_id;
    uint64_t final_va;
    bool is_defined;
} Nylink_Resolved_Sym;

typedef struct Nylink_Archive_Member {
    char *name;
    size_t header_offset;
    size_t data_offset;
    size_t size;
    bool is_extracted;
} Nylink_Archive_Member;

typedef struct Nylink_Archive_Symbol {
    char *name;
    size_t member_offset;
} Nylink_Archive_Symbol;

typedef struct Nylink_Archive {
    char *name;
    const uint8_t *data;
    size_t size;

    Nylink_Archive_Member *members;
    size_t member_count;
    size_t member_capacity;

    Nylink_Archive_Symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;

    const char *long_names;
    size_t long_names_size;
} Nylink_Archive;

struct Nylink_Context {
    Nylink_Object *objects;
    size_t object_count;
    size_t object_capacity;

    Nylink_Archive *archives;
    size_t archive_count;
    size_t archive_capacity;

    Nylink_Section *sections;
    size_t section_count;
    size_t section_capacity;

    Nylink_Input_Sec_Layout *sec_layouts;

    Nylink_Symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;

    Nylink_Resolved_Sym *resolved_symbols;

    Nylink_Relocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;

    Nylink_Diagnostic *diagnostics;
    size_t diagnostic_count;
    size_t diagnostic_capacity;

    Nylink_Output_Section out_sections[4]; /* 0: text, 1: rodata, 2: data, 3: bss */
    bool is_laid_out;
    bool relocations_applied;
    uint64_t entry_point_va;
    uint64_t image_base;
    uint64_t total_file_size;

    /* Dynamic linking support (ELF ET_DYN) */
    bool is_shared;
    char *soname;
    char **needed_libs;
    size_t needed_lib_count;

    uint32_t *dynsym_sym_ids;     /* maps dynsym index (1..n) to ctx->symbols index */
    size_t dynsym_count;          /* total dynsym entries including index 0 (NULL) */
    size_t dynsym_capacity;

    uint8_t *dynstr_data;
    size_t dynstr_size;
    size_t dynstr_capacity;

    uint64_t dynsym_va;
    uint64_t dynsym_file_offset;
    size_t dynsym_file_size;

    uint64_t dynstr_va;
    uint64_t dynstr_file_offset;
    size_t dynstr_file_size;

    uint64_t dynamic_va;
    uint64_t dynamic_file_offset;
    size_t dynamic_file_size;

    uint64_t rela_dyn_va;
    uint64_t rela_dyn_file_offset;
    size_t rela_dyn_file_size;

    uint64_t got_va;
    uint64_t got_file_offset;
    size_t got_file_size;

    uint8_t *dynamic_data;
    size_t dynamic_data_capacity;

    uint8_t *rela_dyn_data;
    size_t rela_dyn_data_capacity;
    size_t rela_dyn_count;

    uint8_t *got_data;
    size_t got_data_capacity;
    size_t got_entry_count;

    bool has_error;
};

void nylink_diag_add(Nylink_Context *ctx, const char *msg, const char *obj_name, const char *sym_or_sec);

bool nylink_read_elf64(Nylink_Context *ctx, uint32_t obj_idx);
bool nylink_read_coff(Nylink_Context *ctx, uint32_t obj_idx);

bool nylink_read_archive(Nylink_Context *ctx, uint32_t arch_idx);
bool nylink_extract_needed_archive_members(Nylink_Context *ctx);

bool nylink_layout_internal(Nylink_Context *ctx, const Nylink_Config *cfg);
bool nylink_apply_relocations_internal(Nylink_Context *ctx);
bool nylink_write_elf_executable(Nylink_Context *ctx, const char *out_path, const Nylink_Config *cfg);
bool nylink_write_pe_executable(Nylink_Context *ctx, const char *out_path, const Nylink_Config *cfg);

#endif
