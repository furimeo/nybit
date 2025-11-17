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
    size_t dyn_reloc_idx = 0;
    size_t got_slot_idx = 0;

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

        uint64_t sym_va = 0;
        const char *sym_name = "<unnamed>";

        bool is_sym_def = false;
        if (reloc->sym_id < ctx->symbol_count) {
            const Nylink_Symbol *sym = &ctx->symbols[reloc->sym_id];
            if (sym->name) sym_name = sym->name;

            if (ctx->resolved_symbols && ctx->resolved_symbols[sym->id].is_defined) {
                sym_va = ctx->resolved_symbols[sym->id].final_va;
                is_sym_def = true;
            } else if (ctx->is_shared) {
                /* In shared mode, unresolved symbols will be resolved dynamically via GOT */
                is_sym_def = false;
                sym_va = 0;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation against unresolved symbol: %s", sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
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

            /* In shared mode, record R_X86_64_RELATIVE dynamic relocation */
            if (ctx->is_shared) {
                if (!ctx->rela_dyn_data) {
                    ctx->rela_dyn_data_capacity = ctx->rela_dyn_count * 24;
                    if (ctx->rela_dyn_data_capacity == 0) ctx->rela_dyn_data_capacity = 24;
                    ctx->rela_dyn_data = (uint8_t *)ny_alloc_zero(ctx->rela_dyn_data_capacity);
                }
                /* Elf64_Rela: r_offset (place_va), r_info (ELF64_R_INFO(0, R_X86_64_RELATIVE=8)), r_addend (val) */
                size_t off = dyn_reloc_idx * 24;
                if (off + 24 <= ctx->rela_dyn_data_capacity) {
                    uint64_t r_offset = place_va;
                    uint64_t r_info = 8ULL; /* R_X86_64_RELATIVE = 8, sym = 0 */
                    int64_t r_addend = (int64_t)val;
                    memcpy(ctx->rela_dyn_data + off, &r_offset, 8);
                    memcpy(ctx->rela_dyn_data + off + 8, &r_info, 8);
                    memcpy(ctx->rela_dyn_data + off + 16, &r_addend, 8);
                    dyn_reloc_idx++;
                }
            }
        } else if (reloc->type == NYLINK_RELOC_X86_64_PC32 || reloc->type == NYLINK_RELOC_X86_64_PLT32) {
            if (target_sec_offset + 4 > out_sec->data_capacity || target_sec_offset + 4 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t target_va = sym_va;
            if (ctx->is_shared && !is_sym_def) {
                /* Target is undefined external symbol: route through GOT */
                if (!ctx->got_data) {
                    ctx->got_data_capacity = ctx->got_entry_count * 8;
                    if (ctx->got_data_capacity == 0) ctx->got_data_capacity = 8;
                    ctx->got_data = (uint8_t *)ny_alloc_zero(ctx->got_data_capacity);
                }

                uint64_t got_slot_va = ctx->got_va + got_slot_idx * 8;
                target_va = got_slot_va;

                /* Emit dynamic relocation R_X86_64_GLOB_DAT (type 6) for this GOT entry */
                if (!ctx->rela_dyn_data) {
                    ctx->rela_dyn_data_capacity = ctx->rela_dyn_count * 24;
                    if (ctx->rela_dyn_data_capacity == 0) ctx->rela_dyn_data_capacity = 24;
                    ctx->rela_dyn_data = (uint8_t *)ny_alloc_zero(ctx->rela_dyn_data_capacity);
                }

                /* Find dynsym index */
                uint32_t dyn_sym_idx = 0;
                for (size_t d = 1; d < ctx->dynsym_count; d++) {
                    uint32_t s_id = ctx->dynsym_sym_ids[d];
                    if (strcmp(ctx->symbols[s_id].name, sym_name) == 0) {
                        dyn_sym_idx = (uint32_t)d;
                        break;
                    }
                }

                size_t off = dyn_reloc_idx * 24;
                if (off + 24 <= ctx->rela_dyn_data_capacity) {
                    uint64_t r_offset = got_slot_va;
                    uint64_t r_info = ((uint64_t)dyn_sym_idx << 32) | 6ULL; /* R_X86_64_GLOB_DAT = 6 */
                    int64_t r_addend = 0;
                    memcpy(ctx->rela_dyn_data + off, &r_offset, 8);
                    memcpy(ctx->rela_dyn_data + off + 8, &r_info, 8);
                    memcpy(ctx->rela_dyn_data + off + 16, &r_addend, 8);
                    dyn_reloc_idx++;
                }
                got_slot_idx++;
            }

            int64_t val = (int64_t)(target_va + reloc->addend) - (int64_t)place_va;
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
