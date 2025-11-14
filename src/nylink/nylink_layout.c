// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>

static inline uint64_t align_up_checked(uint64_t val, uint64_t align, bool *overflow) {
    if (align <= 1) return val;
    uint64_t mask = align - 1;
    if (val > UINT64_MAX - mask) {
        *overflow = true;
        return val;
    }
    return (val + mask) & ~mask;
}

bool nylink_layout_internal(Nylink_Context *ctx, const Nylink_Config *cfg) {
    if (!ctx) return false;

    if (!ctx->is_laid_out) {
        /* Initialize the 4 output sections: .text, .rodata, .data, .bss */
        ctx->out_sections[0] = (Nylink_Output_Section){ .kind = NYLINK_SEC_TEXT, .name = ".text", .align = 16 };
        ctx->out_sections[1] = (Nylink_Output_Section){ .kind = NYLINK_SEC_RODATA, .name = ".rodata", .align = 16 };
        ctx->out_sections[2] = (Nylink_Output_Section){ .kind = NYLINK_SEC_DATA, .name = ".data", .align = 16 };
        ctx->out_sections[3] = (Nylink_Output_Section){ .kind = NYLINK_SEC_BSS, .name = ".bss", .align = 16 };
    }

    if (ctx->section_count > 0 && !ctx->sec_layouts) {
        ctx->sec_layouts = (Nylink_Input_Sec_Layout *)ny_alloc_zero(ctx->section_count * sizeof(Nylink_Input_Sec_Layout));
    }

    bool overflow = false;

    /* Phase 1: For each output section, concatenate corresponding input sections */
    for (size_t out_idx = 0; out_idx < 4; out_idx++) {
        Nylink_Output_Section *out_sec = &ctx->out_sections[out_idx];
        uint64_t current_offset = 0;
        uint32_t max_align = out_sec->align;

        for (size_t in_idx = 0; in_idx < ctx->section_count; in_idx++) {
            Nylink_Section *in_sec = &ctx->sections[in_idx];
            if (in_sec->kind != out_sec->kind) continue;

            uint32_t in_align = in_sec->align > 0 ? in_sec->align : 1;
            if (in_align > max_align) max_align = in_align;

            current_offset = align_up_checked(current_offset, in_align, &overflow);
            if (overflow) {
                nylink_diag_add(ctx, "layout error: integer overflow in section offset calculation", in_sec->name, nullptr);
                return false;
            }

            ctx->sec_layouts[in_idx].out_sec_idx = (uint32_t)out_idx;
            ctx->sec_layouts[in_idx].offset_in_out_sec = current_offset;

            if (in_sec->size > 0 && out_sec->kind != NYLINK_SEC_BSS) {
                uint64_t needed_cap = current_offset + in_sec->size;
                if (needed_cap > out_sec->data_capacity) {
                    size_t new_cap = (size_t)needed_cap;
                    if (new_cap < out_sec->data_capacity * 2) new_cap = out_sec->data_capacity * 2;
                    out_sec->data = (uint8_t *)ny_realloc(out_sec->data, out_sec->data_capacity, new_cap);
                    memset(out_sec->data + out_sec->data_capacity, 0, new_cap - out_sec->data_capacity);
                    out_sec->data_capacity = new_cap;
                }
                if (in_sec->data) {
                    memcpy(out_sec->data + current_offset, in_sec->data, in_sec->size);
                }
            }

            if (current_offset > UINT64_MAX - in_sec->size) {
                nylink_diag_add(ctx, "layout error: section size overflow", in_sec->name, nullptr);
                return false;
            }
            current_offset += in_sec->size;
        }

        out_sec->align = max_align;
        if (out_sec->kind == NYLINK_SEC_BSS) {
            out_sec->file_size = 0;
            out_sec->mem_size = current_offset;
        } else {
            out_sec->file_size = current_offset;
            out_sec->mem_size = current_offset;
        }
    }

    /* Phase 2: Compute Virtual Addresses and File Offsets based on Target Format */
    uint64_t base_va = 0;
    uint64_t page_size = 0x1000;
    uint64_t file_align = 0x1000;
    uint64_t header_file_size = 0x1000;

    if (cfg && cfg->target_format == NYLINK_TARGET_PE) {
        base_va = (cfg->base_address != 0) ? cfg->base_address : 0x140000000ULL;
        page_size = 0x1000;
        file_align = 0x200;
        header_file_size = 0x400; /* DOS header + PE headers fit comfortably within 0x400 */
    } else {
        /* ELF64 */
        base_va = (cfg && cfg->base_address != 0) ? cfg->base_address : 0x400000ULL;
        page_size = 0x1000;
        file_align = 0x1000;
        header_file_size = 0x1000;
    }

    ctx->image_base = base_va;

    uint64_t current_rva = page_size;
    uint64_t current_file_off = header_file_size;

    for (size_t out_idx = 0; out_idx < 4; out_idx++) {
        Nylink_Output_Section *out_sec = &ctx->out_sections[out_idx];
        if (out_sec->mem_size == 0) {
            out_sec->va = 0;
            out_sec->file_offset = 0;
            continue;
        }

        current_rva = align_up_checked(current_rva, page_size, &overflow);
        out_sec->va = base_va + current_rva;

        if (out_sec->kind != NYLINK_SEC_BSS) {
            current_file_off = align_up_checked(current_file_off, file_align, &overflow);
            out_sec->file_offset = current_file_off;
            current_file_off += align_up_checked(out_sec->file_size, file_align, &overflow);
        } else {
            out_sec->file_offset = 0;
        }

        current_rva += align_up_checked(out_sec->mem_size, page_size, &overflow);
    }

    ctx->total_file_size = current_file_off;

    /* Phase 3: Compute final VA for each input section and symbol */
    for (size_t in_idx = 0; in_idx < ctx->section_count; in_idx++) {
        uint32_t out_idx = ctx->sec_layouts[in_idx].out_sec_idx;
        Nylink_Output_Section *out_sec = &ctx->out_sections[out_idx];
        uint64_t in_va = out_sec->va + ctx->sec_layouts[in_idx].offset_in_out_sec;
        ctx->sec_layouts[in_idx].va = in_va;
    }

    if (ctx->symbol_count > 0 && !ctx->resolved_symbols) {
        ctx->resolved_symbols = (Nylink_Resolved_Sym *)ny_alloc_zero(ctx->symbol_count * sizeof(Nylink_Resolved_Sym));
    }

    for (size_t s = 0; s < ctx->symbol_count; s++) {
        Nylink_Symbol *sym = &ctx->symbols[s];
        ctx->resolved_symbols[s].sym_id = sym->id;
        ctx->resolved_symbols[s].is_defined = sym->is_defined;

        if (sym->is_defined && sym->sec_id < ctx->section_count) {
            uint64_t in_sec_va = ctx->sec_layouts[sym->sec_id].va;
            ctx->resolved_symbols[s].final_va = in_sec_va + sym->value;
        } else {
            ctx->resolved_symbols[s].final_va = 0;
        }
    }

    /* Phase 4: Resolve entry point */
    const char *entry_name = (cfg && cfg->entry_point) ? cfg->entry_point : "main";
    const Nylink_Symbol *entry_sym = nylink_find_symbol(ctx, entry_name);
    if (!entry_sym) {
        /* If "main" not found, check "_start" */
        entry_sym = nylink_find_symbol(ctx, "_start");
    }

    if (entry_sym && entry_sym->is_defined) {
        ctx->entry_point_va = ctx->resolved_symbols[entry_sym->id].final_va;
    } else {
        /* Entry point symbol undefined */
        char msg[256];
        snprintf(msg, sizeof(msg), "entry point '%s' not defined", entry_name);
        nylink_diag_add(ctx, msg, nullptr, entry_name);
        return false;
    }

    ctx->is_laid_out = true;
    return true;
}
