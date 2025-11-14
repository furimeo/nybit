// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>

bool nylink_apply_relocations_internal(Nylink_Context *ctx) {
    if (!ctx) return false;
    if (!ctx->is_laid_out) {
        nylink_diag_add(ctx, "relocation error: layout must be performed before relocations", nullptr, nullptr);
        return false;
    }

    bool success = true;

    for (size_t r = 0; r < ctx->relocation_count; r++) {
        const Nylink_Relocation *reloc = &ctx->relocations[r];

        if (reloc->sec_id >= ctx->section_count) {
            nylink_diag_add(ctx, "invalid relocation: target section index out of bounds", nullptr, nullptr);
            success = false;
            continue;
        }

        const Nylink_Section *in_sec = &ctx->sections[reloc->sec_id];
        uint32_t out_idx = ctx->sec_layouts[reloc->sec_id].out_sec_idx;
        Nylink_Output_Section *out_sec = &ctx->out_sections[out_idx];

        uint64_t target_sec_offset = ctx->sec_layouts[reloc->sec_id].offset_in_out_sec + reloc->offset;
        uint64_t place_va = ctx->sec_layouts[reloc->sec_id].va + reloc->offset;

        /* Resolve target symbol final VA */
        uint64_t sym_va = 0;
        const char *sym_name = "<unnamed>";

        if (reloc->sym_id < ctx->symbol_count) {
            const Nylink_Symbol *sym = &ctx->symbols[reloc->sym_id];
            if (sym->name) sym_name = sym->name;

            if (sym->is_defined) {
                sym_va = ctx->resolved_symbols[sym->id].final_va;
            } else {
                /* Try finding resolved global / weak symbol with matching name */
                const Nylink_Symbol *found = nylink_find_symbol(ctx, sym->name);
                if (found && found->is_defined) {
                    sym_va = ctx->resolved_symbols[found->id].final_va;
                } else {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "relocation against unresolved symbol: %s", sym_name);
                    nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                    success = false;
                    continue;
                }
            }
        } else {
            nylink_diag_add(ctx, "invalid relocation: symbol index out of bounds", in_sec->name, nullptr);
            success = false;
            continue;
        }

        if (reloc->type == NYLINK_RELOC_NONE) {
            continue;
        }

        if (reloc->type == NYLINK_RELOC_X86_64_64) {
            if (target_sec_offset + 8 > out_sec->data_capacity || target_sec_offset + 8 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t val = sym_va + reloc->addend;
            uint8_t *ptr = out_sec->data + target_sec_offset;
            ptr[0] = (uint8_t)(val & 0xFF);
            ptr[1] = (uint8_t)((val >> 8) & 0xFF);
            ptr[2] = (uint8_t)((val >> 16) & 0xFF);
            ptr[3] = (uint8_t)((val >> 24) & 0xFF);
            ptr[4] = (uint8_t)((val >> 32) & 0xFF);
            ptr[5] = (uint8_t)((val >> 40) & 0xFF);
            ptr[6] = (uint8_t)((val >> 48) & 0xFF);
            ptr[7] = (uint8_t)((val >> 56) & 0xFF);
        } else if (reloc->type == NYLINK_RELOC_X86_64_PC32 || reloc->type == NYLINK_RELOC_X86_64_PLT32) {
            if (target_sec_offset + 4 > out_sec->data_capacity || target_sec_offset + 4 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            int64_t val = (int64_t)(sym_va + reloc->addend) - (int64_t)place_va;
            if (val < (int64_t)INT32_MIN || val > (int64_t)INT32_MAX) {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation overflow: PC32 value %lld out of 32-bit range for symbol '%s'", (long long)val, sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint32_t val32 = (uint32_t)(int32_t)val;
            uint8_t *ptr = out_sec->data + target_sec_offset;
            ptr[0] = (uint8_t)(val32 & 0xFF);
            ptr[1] = (uint8_t)((val32 >> 8) & 0xFF);
            ptr[2] = (uint8_t)((val32 >> 16) & 0xFF);
            ptr[3] = (uint8_t)((val32 >> 24) & 0xFF);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unsupported relocation type: %d", (int)reloc->type);
            nylink_diag_add(ctx, msg, in_sec->name, sym_name);
            success = false;
        }
    }

    if (success) {
        ctx->relocations_applied = true;
    }
    return success;
}
