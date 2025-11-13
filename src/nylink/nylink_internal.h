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

struct Nylink_Context {
    Nylink_Object *objects;
    size_t object_count;
    size_t object_capacity;

    Nylink_Section *sections;
    size_t section_count;
    size_t section_capacity;

    Nylink_Symbol *symbols;
    size_t symbol_count;
    size_t symbol_capacity;

    Nylink_Relocation *relocations;
    size_t relocation_count;
    size_t relocation_capacity;

    Nylink_Diagnostic *diagnostics;
    size_t diagnostic_count;
    size_t diagnostic_capacity;

    bool has_error;
};

void nylink_diag_add(Nylink_Context *ctx, const char *msg, const char *obj_name, const char *sym_or_sec);

bool nylink_read_elf64(Nylink_Context *ctx, uint32_t obj_idx);
bool nylink_read_coff(Nylink_Context *ctx, uint32_t obj_idx);

#endif
