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

static uint32_t elf_sysv_hash(const char *name) {
    uint32_t h = 0, g = 0;
    while (*name) {
        h = (h << 4) + (uint8_t)(*name++);
        g = h & 0xf0000000;
        if (g) h ^= (g >> 24);
        h &= ~g;
    }
    return h;
}

static void dynstr_append(Nylink_Context *ctx, const char *str, size_t len) {
    if (ctx->dynstr_size + len + 1 > ctx->dynstr_capacity) {
        size_t ncap = (ctx->dynstr_size + len + 1) * 2;
        ctx->dynstr_data = (uint8_t *)ny_realloc(ctx->dynstr_data, ctx->dynstr_capacity, ncap);
        ctx->dynstr_capacity = ncap;
    }
    memcpy(ctx->dynstr_data + ctx->dynstr_size, str, len + 1);
    ctx->dynstr_size += len + 1;
}

static bool dynstr_contains(Nylink_Context *ctx, const char *str, size_t len, uint32_t *offset) {
    for (size_t p = 1; p + len < ctx->dynstr_size; p++) {
        if (ctx->dynstr_data[p - 1] == '\0' &&
            memcmp(ctx->dynstr_data + p, str, len) == 0 &&
            ctx->dynstr_data[p + len] == '\0') {
            *offset = (uint32_t)p;
            return true;
        }
    }
    return false;
}

static void dynstr_intern(Nylink_Context *ctx, const char *str, size_t len) {
    uint32_t offset = 0;
    if (dynstr_contains(ctx, str, len, &offset)) return;
    dynstr_append(ctx, str, len);
}

static void needed_lib_add(Nylink_Context *ctx, const char *lib) {
    for (size_t n = 0; n < ctx->needed_lib_count; n++) {
        if (strcmp(ctx->needed_libs[n], lib) == 0) return;
    }
    size_t len = strlen(lib);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, lib, len + 1);
    ny_buf_grow((void **)&ctx->needed_libs, &ctx->needed_lib_capacity, ctx->needed_lib_count, sizeof(char *));
    ctx->needed_libs[ctx->needed_lib_count++] = copy;
}

static void dynsym_reserve(Nylink_Context *ctx, size_t extra) {
    if (ctx->dynsym_count + extra <= ctx->dynsym_capacity) return;
    size_t ncap = ctx->dynsym_capacity * 2;
    while (ncap < ctx->dynsym_count + extra) ncap *= 2;
    ctx->dynsym_sym_ids = (uint32_t *)ny_realloc(ctx->dynsym_sym_ids,
                                                 ctx->dynsym_capacity * sizeof(uint32_t),
                                                 ncap * sizeof(uint32_t));
    ctx->dynsym_capacity = ncap;
}

static void dynsym_push(Nylink_Context *ctx, uint32_t sym_id) {
    dynsym_reserve(ctx, 1);
    ctx->dynsym_sym_ids[ctx->dynsym_count++] = sym_id;
}

static bool dynsym_has_name(Nylink_Context *ctx, const char *name) {
    for (size_t d = 1; d < ctx->dynsym_count; d++) {
        if (strcmp(ctx->symbols[ctx->dynsym_sym_ids[d]].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static size_t import_find(Nylink_Context *ctx, const char *name) {
    for (size_t i = 0; i < ctx->import_count; i++) {
        if (strcmp(ctx->symbols[ctx->import_sym_ids[i]].name, name) == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

static size_t import_intern(Nylink_Context *ctx, uint32_t sym_id) {
    const char *name = ctx->symbols[sym_id].name;
    size_t existing = import_find(ctx, name);
    if (existing != SIZE_MAX) return existing;

    /* Prefer the defining .so entry when present: it carries the real
       STT_FUNC/STT_OBJECT/STT_NOTYPE binding info instead of a plain reference */
    for (size_t s = 0; s < ctx->symbol_count; s++) {
        const Nylink_Symbol *sym = &ctx->symbols[s];
        if (sym->is_dynamic && sym->is_defined && sym->name && strcmp(sym->name, name) == 0) {
            sym_id = (uint32_t)s;
            break;
        }
    }

    if (ctx->import_count >= ctx->import_capacity) {
        size_t ncap = ctx->import_capacity * 2;
        if (ncap == 0) ncap = 8;
        ctx->import_sym_ids = (uint32_t *)ny_realloc(ctx->import_sym_ids,
                                                     ctx->import_capacity * sizeof(uint32_t),
                                                     ncap * sizeof(uint32_t));
        ctx->import_plt_idx = (uint32_t *)ny_realloc(ctx->import_plt_idx,
                                                     ctx->import_capacity * sizeof(uint32_t),
                                                     ncap * sizeof(uint32_t));
        ctx->import_got_idx = (uint32_t *)ny_realloc(ctx->import_got_idx,
                                                     ctx->import_capacity * sizeof(uint32_t),
                                                     ncap * sizeof(uint32_t));
        ctx->import_capacity = ncap;
    }
    size_t idx = ctx->import_count++;
    ctx->import_sym_ids[idx] = sym_id;
    ctx->import_plt_idx[idx] = UINT32_MAX;
    ctx->import_got_idx[idx] = UINT32_MAX;
    return idx;
}

static bool reloc_target_is_writable(Nylink_Context *ctx, const Nylink_Relocation *reloc) {
    if (!ctx->sec_layouts || reloc->sec_id >= ctx->section_count) return false;
    return ctx->sec_layouts[reloc->sec_id].out_sec_idx == 2;
}

static void pe_append_to_section(Nylink_Output_Section *sec, const void *data, size_t size) {
    if (size == 0) return;
    size_t needed = (size_t)sec->file_size + size;
    if (needed > sec->data_capacity) {
        size_t ncap = sec->data_capacity ? sec->data_capacity * 2 : 256;
        while (ncap < needed) ncap *= 2;
        sec->data = (uint8_t *)ny_realloc(sec->data, sec->data_capacity, ncap);
        memset(sec->data + sec->data_capacity, 0, ncap - sec->data_capacity);
        sec->data_capacity = ncap;
    }
    if (data) {
        memcpy(sec->data + sec->file_size, data, size);
    } else {
        memset(sec->data + sec->file_size, 0, size);
    }
    sec->file_size += size;
    sec->mem_size = sec->file_size;
}

static void pe_pad_section(Nylink_Output_Section *sec, size_t align) {
    if (align <= 1) return;
    size_t rem = (size_t)sec->file_size % align;
    if (rem != 0) {
        size_t pad = align - rem;
        pe_append_to_section(sec, nullptr, pad);
    }
}

static int pe_export_name_cmp(const void *a, const void *b) {
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;
    return strcmp(sa, sb);
}

bool nylink_layout_internal(Nylink_Context *ctx, const Nylink_Config *cfg) {
    if (!ctx) return false;
    bool overflow = false;

    bool is_elf_target = !cfg || cfg->target_format == NYLINK_TARGET_ELF64;
    ctx->target_format = cfg ? cfg->target_format : NYLINK_TARGET_ELF64;

    if (cfg && cfg->target_format == NYLINK_TARGET_PE) {
        for (size_t i = 0; i < ctx->object_count; i++) {
            if (ctx->objects[i].format != NYLINK_FORMAT_COFF) {
                nylink_diag_add(ctx, "target mismatch: cannot emit PE executable from ELF objects", ctx->objects[i].name, nullptr);
                return false;
            }
        }
    } else if (cfg && cfg->target_format == NYLINK_TARGET_ELF64) {
        for (size_t i = 0; i < ctx->object_count; i++) {
            if (ctx->objects[i].format != NYLINK_FORMAT_ELF64) {
                nylink_diag_add(ctx, "target mismatch: cannot emit ELF executable from COFF objects", ctx->objects[i].name, nullptr);
                return false;
            }
        }
    }

    if (cfg && (cfg->output_mode == NYLINK_OUTPUT_SHARED || cfg->output_mode == NYLINK_OUTPUT_PIE || cfg->output_mode == NYLINK_OUTPUT_DLL)) {
        ctx->is_shared = (cfg->output_mode == NYLINK_OUTPUT_SHARED || cfg->output_mode == NYLINK_OUTPUT_DLL);
        ctx->output_mode = cfg->output_mode;
    }
    if (cfg && cfg->exports && cfg->export_count > 0 && ctx->pe_export_count == 0) {
        for (size_t e = 0; e < cfg->export_count; e++) {
            if (cfg->exports[e]) {
                nylink_add_export(ctx, cfg->exports[e]);
            }
        }
    }
    if (cfg && cfg->soname && !ctx->soname &&
        (cfg->output_mode == NYLINK_OUTPUT_SHARED || cfg->output_mode == NYLINK_OUTPUT_DLL)) {
        size_t slen = strlen(cfg->soname);
        ctx->soname = (char *)ny_alloc(slen + 1);
        memcpy(ctx->soname, cfg->soname, slen + 1);
    }
    if (cfg && cfg->dynamic_linker && !ctx->dynamic_linker) {
        size_t dlen = strlen(cfg->dynamic_linker);
        ctx->dynamic_linker = (char *)ny_alloc(dlen + 1);
        memcpy(ctx->dynamic_linker, cfg->dynamic_linker, dlen + 1);
    }
    if (cfg && cfg->rpath && !ctx->rpath) {
        size_t rlen = strlen(cfg->rpath);
        ctx->rpath = (char *)ny_alloc(rlen + 1);
        memcpy(ctx->rpath, cfg->rpath, rlen + 1);
    }
    if (cfg) {
        for (size_t i = 0; i < cfg->needed_lib_count; i++) {
            if (cfg->needed_libs[i]) {
                needed_lib_add(ctx, cfg->needed_libs[i]);
            }
        }
    }

    ctx->uses_dynamic = ctx->is_shared || ctx->needed_lib_count > 0 || (cfg && cfg->soname) || (ctx->dynamic_linker != nullptr) || (cfg && cfg->dynamic_linker != nullptr);
    if (!ctx->uses_dynamic) {
        for (size_t i = 0; i < ctx->object_count; i++) {
            if (ctx->objects[i].is_shared_input) {
                ctx->uses_dynamic = true;
                break;
            }
        }
    }

    if (is_elf_target && ctx->uses_dynamic && ctx->output_mode != NYLINK_OUTPUT_SHARED && !ctx->dynamic_linker) {
        const char *default_interp;
        if (ctx->machine == 183) {
            default_interp = "/lib/ld-linux-aarch64.so.1";
        } else {
            default_interp = "/lib64/ld-linux-x86-64.so.2";
        }
        size_t dlen = strlen(default_interp);
        ctx->dynamic_linker = (char *)ny_alloc(dlen + 1);
        memcpy(ctx->dynamic_linker, default_interp, dlen + 1);
    }

    if (!ctx->is_laid_out) {
        ctx->out_sections[0] = (Nylink_Output_Section){ .kind = NYLINK_SEC_TEXT, .name = ".text", .align = 16 };
        ctx->out_sections[1] = (Nylink_Output_Section){ .kind = NYLINK_SEC_RODATA, .name = ".rodata", .align = 16 };
        ctx->out_sections[2] = (Nylink_Output_Section){ .kind = NYLINK_SEC_DATA, .name = ".data", .align = 16 };
        ctx->out_sections[3] = (Nylink_Output_Section){ .kind = NYLINK_SEC_BSS, .name = ".bss", .align = 16 };
    }

    if (ctx->section_count > 0 && !ctx->sec_layouts) {
        ctx->sec_layouts = (Nylink_Input_Sec_Layout *)ny_alloc_zero(ctx->section_count * sizeof(Nylink_Input_Sec_Layout));
    }

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

    if (ctx->symbol_count > 0 && !ctx->resolved_symbols) {
        ctx->resolved_symbols = (Nylink_Resolved_Sym *)ny_alloc_zero(ctx->symbol_count * sizeof(Nylink_Resolved_Sym));
        for (size_t i = 0; i < ctx->symbol_count; i++) {
            ctx->resolved_symbols[i].sym_id = (uint32_t)i;
            ctx->resolved_symbols[i].is_defined = ctx->symbols[i].is_defined;
            ctx->resolved_symbols[i].is_dynamic = ctx->symbols[i].is_dynamic;
        }
    }

    size_t plt_count = 0;
    size_t got_data_count = 0;
    size_t rela_dyn_count = 0;

    if (ctx->uses_dynamic && is_elf_target) {
        if (!ctx->reloc_plans && ctx->relocation_count > 0) {
            ctx->reloc_plans = (uint8_t *)ny_alloc_zero(ctx->relocation_count);
            ctx->reloc_plan_slots = (uint32_t *)ny_alloc_zero(ctx->relocation_count * sizeof(uint32_t));
        }

        for (size_t r = 0; r < ctx->relocation_count; r++) {
            const Nylink_Relocation *reloc = &ctx->relocations[r];
            ctx->reloc_plans[r] = NYLINK_PLAN_STATIC;
            ctx->reloc_plan_slots[r] = UINT32_MAX;

            if (reloc->sym_id >= ctx->symbol_count) continue;
            const Nylink_Symbol *sym = &ctx->symbols[reloc->sym_id];
            const Nylink_Resolved_Sym *state = &ctx->resolved_symbols[reloc->sym_id];

            bool dynamic_ref = state->is_dynamic ||
                               (!state->is_defined && ctx->output_mode == NYLINK_OUTPUT_SHARED);
            bool weak_undef = !state->is_defined && sym->binding == NYLINK_SYM_WEAK;

            if (weak_undef) {
                if (reloc->type == NYLINK_RELOC_X86_64_64 || reloc->type == NYLINK_RELOC_AARCH64_ABS64) {
                    size_t weak_imp = import_intern(ctx, reloc->sym_id);
                    ctx->reloc_plans[r] = NYLINK_PLAN_DYN_ABS64;
                    ctx->reloc_plan_slots[r] = (uint32_t)weak_imp;
                    rela_dyn_count++;
                }
                continue;
            }

            if (!dynamic_ref) {
                if (reloc->type == NYLINK_RELOC_X86_64_64 || reloc->type == NYLINK_RELOC_AARCH64_ABS64) {
                    ctx->reloc_plans[r] = NYLINK_PLAN_RELATIVE;
                    rela_dyn_count++;
                }
                continue;
            }

            size_t imp = import_intern(ctx, reloc->sym_id);

            if (reloc->type == NYLINK_RELOC_X86_64_64 || reloc->type == NYLINK_RELOC_AARCH64_ABS64) {
                const Nylink_Section *in_sec = &ctx->sections[reloc->sec_id];
                if (!reloc_target_is_writable(ctx, reloc)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "dynamic relocation against '%s' in non-writable section '%s': text relocations are not supported",
                             sym->name, in_sec->name);
                    nylink_diag_add(ctx, msg, in_sec->name, sym->name);
                    return false;
                }
                ctx->reloc_plans[r] = NYLINK_PLAN_DYN_ABS64;
                ctx->reloc_plan_slots[r] = (uint32_t)imp;
                rela_dyn_count++;
                continue;
            }

            if (reloc->type == NYLINK_RELOC_X86_64_GOTPCREL ||
                reloc->type == NYLINK_RELOC_AARCH64_ADR_PREL_PG_HI21 ||
                reloc->type == NYLINK_RELOC_AARCH64_ADD_ABS_LO12_NC ||
                reloc->type == NYLINK_RELOC_AARCH64_LDST64_ABS_LO12_NC ||
                reloc->type == NYLINK_RELOC_AARCH64_LDST32_ABS_LO12_NC) {
                if (ctx->import_got_idx[imp] == UINT32_MAX) {
                    ctx->import_got_idx[imp] = (uint32_t)got_data_count++;
                    rela_dyn_count++;
                }
                ctx->reloc_plans[r] = NYLINK_PLAN_GOT_LOAD;
                ctx->reloc_plan_slots[r] = ctx->import_got_idx[imp];
                continue;
            }

            if (reloc->type == NYLINK_RELOC_X86_64_PC32 || reloc->type == NYLINK_RELOC_X86_64_PLT32) {
                bool want_plt = (reloc->type == NYLINK_RELOC_X86_64_PLT32) || sym->is_function;
                if (want_plt) {
                    if (ctx->import_plt_idx[imp] == UINT32_MAX) {
                        ctx->import_plt_idx[imp] = (uint32_t)plt_count++;
                    }
                    ctx->reloc_plans[r] = NYLINK_PLAN_PLT_CALL;
                    ctx->reloc_plan_slots[r] = ctx->import_plt_idx[imp];
                } else {
                    const Nylink_Section *in_sec = &ctx->sections[reloc->sec_id];
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "relocation R_X86_64_PC32 against dynamic data symbol '%s' in section '%s': copy relocations are not supported; recompile with -fPIC/-fPIE",
                             sym->name, in_sec->name);
                    nylink_diag_add(ctx, msg, in_sec->name, sym->name);
                    return false;
                }
            }

            if (reloc->type == NYLINK_RELOC_AARCH64_CALL26 || reloc->type == NYLINK_RELOC_AARCH64_JUMP26) {
                if (ctx->import_plt_idx[imp] == UINT32_MAX) {
                    ctx->import_plt_idx[imp] = (uint32_t)plt_count++;
                }
                ctx->reloc_plans[r] = NYLINK_PLAN_PLT_CALL;
                ctx->reloc_plan_slots[r] = ctx->import_plt_idx[imp];
            }
        }
    }

    uint64_t base_va = 0;
    uint64_t page_size = 0x1000;
    uint64_t file_align = 0x1000;
    uint64_t header_file_size = 0x1000;

    if (cfg && cfg->target_format == NYLINK_TARGET_PE) {
        if (cfg->base_address != 0) {
            base_va = cfg->base_address;
        } else if (ctx->output_mode == NYLINK_OUTPUT_DLL) {
            base_va = 0x180000000ULL;
        } else {
            base_va = 0x140000000ULL;
        }
        page_size = 0x1000;
        file_align = 0x200;
        header_file_size = 0x400;
    } else {
        if (ctx->is_shared) {
            base_va = (cfg && cfg->base_address != 0) ? cfg->base_address : 0x0ULL;
        } else {
            base_va = (cfg && cfg->base_address != 0) ? cfg->base_address : 0x400000ULL;
        }
    }

    ctx->image_base = base_va;

    uint64_t current_rva = page_size;
    uint64_t current_file_off = header_file_size;

    if (cfg && cfg->target_format == NYLINK_TARGET_PE) {
        /* Windows PE Dynamic Linking: DLL Export Directory */
        if (ctx->pe_export_count > 0) {
            /* Sort exports lexicographically for binary search */
            qsort(ctx->pe_exports, ctx->pe_export_count, sizeof(char *), pe_export_name_cmp);

            Nylink_Output_Section *sec_rdata = &ctx->out_sections[1];
            pe_pad_section(sec_rdata, 4);

            uint32_t exp_sec_offset = (uint32_t)sec_rdata->file_size;
            uint32_t num_exports = (uint32_t)ctx->pe_export_count;

            const char *out_dll_name = ctx->soname ? ctx->soname : (cfg->soname ? cfg->soname : "output.dll");
            size_t dll_name_len = strlen(out_dll_name);

            size_t names_total_len = 0;
            for (size_t e = 0; e < ctx->pe_export_count; e++) {
                names_total_len += strlen(ctx->pe_exports[e]) + 1;
            }

            size_t exp_total_bytes = 40 + (size_t)num_exports * 4 + (size_t)num_exports * 4 +
                                     (size_t)num_exports * 2 + dll_name_len + 1 + names_total_len;
            exp_total_bytes = (exp_total_bytes + 3) & ~3ULL;

            uint8_t *exp_buf = (uint8_t *)ny_alloc_zero(exp_total_bytes);

            uint32_t eat_off = 40;
            uint32_t npt_off = eat_off + num_exports * 4;
            uint32_t ot_off  = npt_off + num_exports * 4;
            uint32_t dll_name_off = ot_off + num_exports * 2;
            uint32_t names_off = dll_name_off + (uint32_t)dll_name_len + 1;

            uint32_t text_aligned_rva = (uint32_t)((ctx->out_sections[0].mem_size + page_size - 1) & ~(page_size - 1));
            uint32_t rdata_base_rva = (uint32_t)page_size + text_aligned_rva;
            uint32_t exp_base_rva = rdata_base_rva + exp_sec_offset;

            *(uint32_t *)(exp_buf + 12) = exp_base_rva + dll_name_off; /* Name RVA */
            *(uint32_t *)(exp_buf + 16) = 1;                           /* Ordinal Base = 1 */
            *(uint32_t *)(exp_buf + 20) = num_exports;                 /* NumberOfFunctions */
            *(uint32_t *)(exp_buf + 24) = num_exports;                 /* NumberOfNames */
            *(uint32_t *)(exp_buf + 28) = exp_base_rva + eat_off;      /* AddressOfFunctions */
            *(uint32_t *)(exp_buf + 32) = exp_base_rva + npt_off;      /* AddressOfNames */
            *(uint32_t *)(exp_buf + 36) = exp_base_rva + ot_off;       /* AddressOfNameOrdinals */

            memcpy(exp_buf + dll_name_off, out_dll_name, dll_name_len + 1);

            uint32_t cur_name_off = names_off;
            for (uint32_t e = 0; e < num_exports; e++) {
                const char *sname = ctx->pe_exports[e];
                size_t slen = strlen(sname);
                memcpy(exp_buf + cur_name_off, sname, slen + 1);

                *(uint32_t *)(exp_buf + npt_off + e * 4) = exp_base_rva + cur_name_off;
                *(uint16_t *)(exp_buf + ot_off + e * 2) = (uint16_t)e;

                const Nylink_Symbol *esym = nylink_find_symbol(ctx, sname);
                uint32_t sym_rva = 0;
                if (esym && esym->is_defined && esym->sec_id < ctx->section_count) {
                    uint32_t out_sec_idx = ctx->sec_layouts[esym->sec_id].out_sec_idx;
                    uint64_t in_sec_va_offset = ctx->sec_layouts[esym->sec_id].offset_in_out_sec + esym->value;
                    uint32_t sec_rva_start = (uint32_t)page_size;
                    if (out_sec_idx == 1) sec_rva_start += text_aligned_rva;
                    else if (out_sec_idx == 2) {
                        uint32_t rdata_aligned_rva = (uint32_t)((ctx->out_sections[1].mem_size + page_size - 1) & ~(page_size - 1));
                        sec_rva_start += text_aligned_rva + rdata_aligned_rva;
                    }
                    sym_rva = sec_rva_start + (uint32_t)in_sec_va_offset;
                }
                *(uint32_t *)(exp_buf + eat_off + e * 4) = sym_rva;

                cur_name_off += (uint32_t)(slen + 1);
            }

            pe_append_to_section(sec_rdata, exp_buf, exp_total_bytes);
            ny_free(exp_buf, exp_total_bytes);

            ctx->pe_export_va = base_va + exp_base_rva;
            ctx->pe_export_size = (uint32_t)exp_total_bytes;
        }

        /* Windows PE Dynamic Linking: Import Resolution & Tables */
        if (ctx->pe_imp_count > 0) {
            Nylink_Output_Section *sec_text = &ctx->out_sections[0];
            Nylink_Output_Section *sec_rdata = &ctx->out_sections[1];

            /* Function thunks in .text: FF 25 <disp32> */
            pe_pad_section(sec_text, 16);
            for (size_t i = 0; i < ctx->pe_imp_count; i++) {
                const char *sname = ctx->pe_imp_sym_names[i];
                const Nylink_Symbol *sym = nylink_find_symbol(ctx, sname);
                bool is_func = sym ? sym->is_function : true;

                if (is_func) {
                    pe_pad_section(sec_text, 4);
                    uint32_t thunk_off = (uint32_t)sec_text->file_size;
                    uint8_t thunk_code[6] = { 0xFF, 0x25, 0, 0, 0, 0 };
                    pe_append_to_section(sec_text, thunk_code, 6);
                    ctx->pe_imp_thunk_rvas[i] = (uint32_t)page_size + thunk_off;
                } else {
                    ctx->pe_imp_thunk_rvas[i] = 0;
                }
            }

            /* Import tables in .rdata */
            pe_pad_section(sec_rdata, 8);
            uint32_t iat_sec_off = (uint32_t)sec_rdata->file_size;

            uint32_t text_aligned_rva = (uint32_t)((ctx->out_sections[0].mem_size + page_size - 1) & ~(page_size - 1));
            uint32_t rdata_base_rva = (uint32_t)page_size + text_aligned_rva;
            uint32_t iat_base_rva = rdata_base_rva + iat_sec_off;

            size_t iat_total_entries = ctx->pe_imp_count + ctx->pe_dll_count;
            size_t iat_bytes = iat_total_entries * 8;
            size_t idt_bytes = (ctx->pe_dll_count + 1) * 20;
            size_t ilt_bytes = iat_bytes;

            size_t hint_names_bytes = 0;
            for (size_t i = 0; i < ctx->pe_imp_count; i++) {
                size_t nlen = strlen(ctx->pe_imp_sym_names[i]);
                size_t entry_len = 2 + nlen + 1;
                if (entry_len % 2 != 0) entry_len++;
                hint_names_bytes += entry_len;
            }

            size_t dll_names_total = 0;
            for (size_t d = 0; d < ctx->pe_dll_count; d++) {
                dll_names_total += strlen(ctx->pe_dll_names[d]) + 1;
            }

            size_t imp_total_bytes = iat_bytes + idt_bytes + ilt_bytes + hint_names_bytes + dll_names_total;
            imp_total_bytes = (imp_total_bytes + 7) & ~7ULL;

            uint8_t *imp_buf = (uint8_t *)ny_alloc_zero(imp_total_bytes);

            uint32_t idt_off = (uint32_t)iat_bytes;
            uint32_t ilt_off = idt_off + (uint32_t)idt_bytes;
            uint32_t hn_off  = ilt_off + (uint32_t)ilt_bytes;
            uint32_t dll_str_off = hn_off + (uint32_t)hint_names_bytes;

            uint32_t cur_iat_idx = 0;
            uint32_t cur_hn_off = hn_off;
            uint32_t cur_dll_str_off = dll_str_off;

            for (size_t d = 0; d < ctx->pe_dll_count; d++) {
                const char *dname = ctx->pe_dll_names[d];
                size_t dlen = strlen(dname);
                memcpy(imp_buf + cur_dll_str_off, dname, dlen + 1);
                uint32_t dll_name_rva = rdata_base_rva + iat_sec_off + cur_dll_str_off;
                cur_dll_str_off += (uint32_t)(dlen + 1);

                uint32_t dll_iat_start_rva = rdata_base_rva + iat_sec_off + cur_iat_idx * 8;
                uint32_t dll_ilt_start_rva = rdata_base_rva + iat_sec_off + ilt_off + cur_iat_idx * 8;

                uint32_t cur_idt_entry_off = idt_off + (uint32_t)d * 20;
                *(uint32_t *)(imp_buf + cur_idt_entry_off + 0) = dll_ilt_start_rva;
                *(uint32_t *)(imp_buf + cur_idt_entry_off + 12) = dll_name_rva;
                *(uint32_t *)(imp_buf + cur_idt_entry_off + 16) = dll_iat_start_rva;

                for (size_t i = 0; i < ctx->pe_imp_count; i++) {
                    if (ctx->pe_imp_dll_indices[i] != d) continue;

                    const char *sym_name = ctx->pe_imp_sym_names[i];
                    size_t slen = strlen(sym_name);
                    size_t entry_len = 2 + slen + 1;
                    if (entry_len % 2 != 0) entry_len++;

                    *(uint16_t *)(imp_buf + cur_hn_off) = 0;
                    memcpy(imp_buf + cur_hn_off + 2, sym_name, slen + 1);

                    uint32_t hn_rva = rdata_base_rva + iat_sec_off + cur_hn_off;
                    cur_hn_off += (uint32_t)entry_len;

                    *(uint64_t *)(imp_buf + cur_iat_idx * 8) = (uint64_t)hn_rva;
                    *(uint64_t *)(imp_buf + ilt_off + cur_iat_idx * 8) = (uint64_t)hn_rva;

                    ctx->pe_imp_iat_rvas[i] = rdata_base_rva + iat_sec_off + cur_iat_idx * 8;
                    cur_iat_idx++;
                }

                *(uint64_t *)(imp_buf + cur_iat_idx * 8) = 0;
                *(uint64_t *)(imp_buf + ilt_off + cur_iat_idx * 8) = 0;
                cur_iat_idx++;
            }

            pe_append_to_section(sec_rdata, imp_buf, imp_total_bytes);
            ny_free(imp_buf, imp_total_bytes);

            ctx->pe_iat_va = base_va + iat_base_rva;
            ctx->pe_iat_size = (uint32_t)iat_bytes;
            ctx->pe_import_va = base_va + rdata_base_rva + iat_sec_off + idt_off;
            ctx->pe_import_size = (uint32_t)idt_bytes;

            /* Update thunk jump offsets in .text */
            for (size_t i = 0; i < ctx->pe_imp_count; i++) {
                if (ctx->pe_imp_thunk_rvas[i] != 0) {
                    uint32_t thunk_off_in_text = ctx->pe_imp_thunk_rvas[i] - (uint32_t)page_size;
                    uint64_t thunk_va = base_va + ctx->pe_imp_thunk_rvas[i];
                    uint64_t iat_slot_va = base_va + ctx->pe_imp_iat_rvas[i];
                    int32_t disp = (int32_t)((int64_t)iat_slot_va - (int64_t)(thunk_va + 6));
                    memcpy(sec_text->data + thunk_off_in_text + 2, &disp, 4);
                }
            }

            /* Update resolved_symbols for imported symbols */
            for (size_t i = 0; i < ctx->pe_imp_count; i++) {
                const char *sname = ctx->pe_imp_sym_names[i];
                const Nylink_Symbol *sym = nylink_find_symbol(ctx, sname);
                if (sym) {
                    if (ctx->pe_imp_thunk_rvas[i] != 0) {
                        ctx->resolved_symbols[sym->id].final_va = base_va + ctx->pe_imp_thunk_rvas[i];
                    } else {
                        ctx->resolved_symbols[sym->id].final_va = base_va + ctx->pe_imp_iat_rvas[i];
                    }
                    ctx->resolved_symbols[sym->id].is_defined = true;
                    ctx->resolved_symbols[sym->id].is_dynamic = true;
                }

                char imp_name[512];
                snprintf(imp_name, sizeof(imp_name), "__imp_%s", sname);
                const Nylink_Symbol *isym = nylink_find_symbol(ctx, imp_name);
                if (isym) {
                    ctx->resolved_symbols[isym->id].final_va = base_va + ctx->pe_imp_iat_rvas[i];
                    ctx->resolved_symbols[isym->id].is_defined = true;
                    ctx->resolved_symbols[isym->id].is_dynamic = true;
                }
            }
        }
    }

    if (ctx->uses_dynamic && is_elf_target) {
        if (!ctx->dynstr_data) {
            ctx->dynstr_capacity = 256;
            ctx->dynstr_data = (uint8_t *)ny_alloc_zero(ctx->dynstr_capacity);
            ctx->dynstr_size = 1;
        }

        if (ctx->soname) {
            dynstr_intern(ctx, ctx->soname, strlen(ctx->soname));
        }
        if (ctx->rpath) {
            dynstr_intern(ctx, ctx->rpath, strlen(ctx->rpath));
        }
        for (size_t i = 0; i < ctx->needed_lib_count; i++) {
            if (ctx->needed_libs[i]) {
                dynstr_intern(ctx, ctx->needed_libs[i], strlen(ctx->needed_libs[i]));
            }
        }

        if (!ctx->dynsym_sym_ids) {
            ctx->dynsym_capacity = 32;
            ctx->dynsym_sym_ids = (uint32_t *)ny_alloc_zero(ctx->dynsym_capacity * sizeof(uint32_t));
            ctx->dynsym_count = 1;
        }

        bool export_all = (ctx->output_mode == NYLINK_OUTPUT_SHARED);
        if (export_all) {
            for (size_t s = 0; s < ctx->symbol_count; s++) {
                const Nylink_Symbol *sym = &ctx->symbols[s];
                if (!sym->name || sym->name[0] == '\0') continue;
                if (sym->binding == NYLINK_SYM_LOCAL) continue;
                if (sym->visibility != 0) continue;
                if (!sym->is_defined || sym->is_dynamic) continue;
                if (!ctx->resolved_symbols[s].is_defined || ctx->resolved_symbols[s].is_dynamic) continue;
                if (dynsym_has_name(ctx, sym->name)) continue;
                dynsym_push(ctx, (uint32_t)s);
                dynstr_intern(ctx, sym->name, strlen(sym->name));
            }
        }

        for (size_t i = 0; i < ctx->import_count; i++) {
            uint32_t sym_id = ctx->import_sym_ids[i];
            if (dynsym_has_name(ctx, ctx->symbols[sym_id].name)) continue;
            dynsym_push(ctx, sym_id);
            dynstr_intern(ctx, ctx->symbols[sym_id].name, strlen(ctx->symbols[sym_id].name));
        }

        ctx->dynsym_file_size = ctx->dynsym_count * 24;
        ctx->dynstr_file_size = ctx->dynstr_size;

        uint32_t nbucket = (ctx->dynsym_count > 1) ? (uint32_t)ctx->dynsym_count : 1;
        uint32_t nchain = (uint32_t)ctx->dynsym_count;
        ctx->hash_file_size = (2 + nbucket + nchain) * sizeof(uint32_t);
        if (ctx->hash_data) {
            ny_free(ctx->hash_data, ctx->hash_data_capacity);
        }
        ctx->hash_data_capacity = ctx->hash_file_size;
        ctx->hash_data = (uint8_t *)ny_alloc_zero(ctx->hash_data_capacity);

        uint32_t *hash_words = (uint32_t *)ctx->hash_data;
        hash_words[0] = nbucket;
        hash_words[1] = nchain;
        uint32_t *buckets = &hash_words[2];
        uint32_t *chains = &hash_words[2 + nbucket];

        for (uint32_t d = 1; d < (uint32_t)ctx->dynsym_count; d++) {
            uint32_t s_id = ctx->dynsym_sym_ids[d];
            const char *sym_name = ctx->symbols[s_id].name;
            uint32_t h = elf_sysv_hash(sym_name);
            uint32_t b = h % nbucket;
            chains[d] = buckets[b];
            buckets[b] = d;
        }

        ctx->plt_entry_count = plt_count;
        uint32_t plt0_size = (ctx->machine == 183) ? 32 : 16;
        ctx->plt_file_size = (plt_count > 0) ? (plt0_size + plt_count * 16) : 0;
        ctx->rela_plt_count = plt_count;
        ctx->rela_plt_file_size = plt_count * 24;
        ctx->got_plt_file_size = (plt_count > 0) ? ((3 + plt_count) * 8) : 0;

        ctx->got_entry_count = got_data_count;
        ctx->got_file_size = (got_data_count > 0) ? (got_data_count * 8) : 8;
        ctx->rela_dyn_count = rela_dyn_count;
        ctx->rela_dyn_file_size = rela_dyn_count * 24;

        size_t dynamic_entries = 8 + (ctx->soname ? 1 : 0) + (ctx->rpath ? 1 : 0) + ctx->needed_lib_count;
        if (ctx->dynamic_linker) dynamic_entries++;
        dynamic_entries++;
        if (ctx->rela_dyn_file_size > 0) dynamic_entries += 3;
        if (ctx->rela_plt_file_size > 0) dynamic_entries += 4;
        ctx->dynamic_file_size = dynamic_entries * 16;

        bool is_exec_output = (ctx->output_mode != NYLINK_OUTPUT_SHARED);
        if (is_exec_output && ctx->dynamic_linker) {
            ctx->interp_file_size = strlen(ctx->dynamic_linker) + 1;
            ctx->interp_va = base_va + current_rva;
            ctx->interp_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->interp_file_size, 8, &overflow);
            current_file_off += align_up_checked(ctx->interp_file_size, 8, &overflow);
        } else {
            ctx->interp_file_size = 0;
        }

        ctx->hash_va = base_va + current_rva;
        ctx->hash_file_offset = current_file_off;
        current_rva += align_up_checked(ctx->hash_file_size, 8, &overflow);
        current_file_off += align_up_checked(ctx->hash_file_size, 8, &overflow);

        ctx->dynsym_va = base_va + current_rva;
        ctx->dynsym_file_offset = current_file_off;
        current_rva += align_up_checked(ctx->dynsym_file_size, 8, &overflow);
        current_file_off += align_up_checked(ctx->dynsym_file_size, 8, &overflow);

        ctx->dynstr_va = base_va + current_rva;
        ctx->dynstr_file_offset = current_file_off;
        current_rva += align_up_checked(ctx->dynstr_file_size, 8, &overflow);
        current_file_off += align_up_checked(ctx->dynstr_file_size, 8, &overflow);

        if (ctx->rela_dyn_file_size > 0) {
            ctx->rela_dyn_va = base_va + current_rva;
            ctx->rela_dyn_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->rela_dyn_file_size, 8, &overflow);
            current_file_off += align_up_checked(ctx->rela_dyn_file_size, 8, &overflow);
        }

        if (ctx->rela_plt_file_size > 0) {
            ctx->rela_plt_va = base_va + current_rva;
            ctx->rela_plt_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->rela_plt_file_size, 8, &overflow);
            current_file_off += align_up_checked(ctx->rela_plt_file_size, 8, &overflow);
        }

        if (ctx->plt_file_size > 0) {
            current_rva = align_up_checked(current_rva, 16, &overflow);
            current_file_off = align_up_checked(current_file_off, 16, &overflow);
            ctx->plt_va = base_va + current_rva;
            ctx->plt_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->plt_file_size, 16, &overflow);
            current_file_off += align_up_checked(ctx->plt_file_size, 16, &overflow);
        }
    }

    for (size_t out_idx = 0; out_idx < 3; out_idx++) {
        Nylink_Output_Section *out_sec = &ctx->out_sections[out_idx];
        if (out_sec->mem_size == 0 && out_sec->file_size == 0) {
            out_sec->va = 0;
            out_sec->file_offset = 0;
            continue;
        }

        current_rva = align_up_checked(current_rva, page_size, &overflow);
        out_sec->va = base_va + current_rva;

        current_file_off = align_up_checked(current_file_off, file_align, &overflow);
        out_sec->file_offset = current_file_off;
        current_file_off += align_up_checked(out_sec->file_size, file_align, &overflow);

        current_rva += align_up_checked(out_sec->mem_size, page_size, &overflow);
    }

    /* RW tail: .data, .got, .dynamic stay inside RELRO; .got.plt must remain
       writable for the lazy resolver, so it starts on the page after RELRO;
       .bss is the only non-file-backed piece and therefore comes last */
    if (ctx->uses_dynamic && is_elf_target) {
        current_rva = align_up_checked(current_rva, page_size, &overflow);
        current_file_off = align_up_checked(current_file_off, page_size, &overflow);

        ctx->got_va = base_va + current_rva;
        ctx->got_file_offset = current_file_off;
        current_rva += align_up_checked(ctx->got_file_size, 8, &overflow);
        current_file_off += align_up_checked(ctx->got_file_size, 8, &overflow);

        ctx->dynamic_va = base_va + current_rva;
        ctx->dynamic_file_offset = current_file_off;
        current_rva += align_up_checked(ctx->dynamic_file_size, 8, &overflow);
        current_file_off += align_up_checked(ctx->dynamic_file_size, 8, &overflow);

        if (ctx->got_plt_file_size > 0) {
            current_rva = align_up_checked(current_rva, page_size, &overflow);
            current_file_off = align_up_checked(current_file_off, page_size, &overflow);
            ctx->got_plt_va = base_va + current_rva;
            ctx->got_plt_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->got_plt_file_size, 8, &overflow);
            current_file_off += align_up_checked(ctx->got_plt_file_size, 8, &overflow);
        }
    }

    Nylink_Output_Section *sec_bss = &ctx->out_sections[3];
    if (sec_bss->mem_size > 0) {
        current_rva = align_up_checked(current_rva, page_size, &overflow);
        sec_bss->va = base_va + current_rva;
        sec_bss->file_offset = 0;
        current_rva += align_up_checked(sec_bss->mem_size, page_size, &overflow);
    }

    if (overflow) {
        nylink_diag_add(ctx, "layout error: address or file offset overflow", nullptr, nullptr);
        return false;
    }

    ctx->total_file_size = current_file_off;

    for (size_t in_idx = 0; in_idx < ctx->section_count; in_idx++) {
        uint32_t out_idx = ctx->sec_layouts[in_idx].out_sec_idx;
        Nylink_Output_Section *out_sec = &ctx->out_sections[out_idx];
        ctx->sec_layouts[in_idx].va = out_sec->va + ctx->sec_layouts[in_idx].offset_in_out_sec;
    }

    for (size_t s = 0; s < ctx->symbol_count; s++) {
        Nylink_Symbol *sym = &ctx->symbols[s];
        ctx->resolved_symbols[s].sym_id = sym->id;

        if (sym->is_defined && !sym->is_dynamic && sym->sec_id < ctx->section_count) {
            ctx->resolved_symbols[s].final_va = ctx->sec_layouts[sym->sec_id].va + sym->value;
        } else if (sym->is_dynamic && ctx->resolved_symbols[s].final_va != 0) {
            /* Keep pre-assigned VA (e.g. PE import IAT or function thunk) */
        } else {
            ctx->resolved_symbols[s].final_va = 0;
        }
    }

    for (size_t s = 0; s < ctx->symbol_count; s++) {
        Nylink_Symbol *sym = &ctx->symbols[s];
        if (!sym->name || sym->name[0] == '\0') continue;

        const Nylink_Symbol *def_sym = nylink_find_symbol(ctx, sym->name);
        if (def_sym && def_sym->is_defined) {
            if (!def_sym->is_dynamic) {
                ctx->resolved_symbols[s].final_va = ctx->resolved_symbols[def_sym->id].final_va;
                ctx->resolved_symbols[s].is_defined = true;
                ctx->resolved_symbols[s].is_dynamic = false;
            } else {
                if (ctx->resolved_symbols[def_sym->id].final_va != 0) {
                    ctx->resolved_symbols[s].final_va = ctx->resolved_symbols[def_sym->id].final_va;
                }
                ctx->resolved_symbols[s].is_defined = true;
                ctx->resolved_symbols[s].is_dynamic = true;
            }
        }
    }

    /* PE imported-data redirect: when an imported data symbol is referenced
       through a .refptr.<name> indirection (MinGW pattern), redirect the
       .refptr symbol's VA to the IAT slot. This makes the generated code
       load the value VA directly from the IAT instead of loading a pointer
       to the IAT and then loading from it (which would yield the low 32
       bits of the pointer, not the data). */
    if (ctx->target_format == NYLINK_TARGET_PE && ctx->pe_imp_count > 0) {
        for (size_t i = 0; i < ctx->pe_imp_count; i++) {
            if (ctx->pe_imp_thunk_rvas[i] != 0) continue; /* function import uses thunk */
            uint32_t iat_rva = ctx->pe_imp_iat_rvas[i];
            if (iat_rva == 0) continue;

            char refptr_name[512];
            snprintf(refptr_name, sizeof(refptr_name), ".refptr.%s", ctx->pe_imp_sym_names[i]);

            for (size_t s = 0; s < ctx->symbol_count; s++) {
                Nylink_Symbol *sym = &ctx->symbols[s];
                if (!sym->name || strcmp(sym->name, refptr_name) != 0) continue;
                if (!sym->is_defined || sym->is_dynamic) continue;

                ctx->resolved_symbols[s].final_va = base_va + iat_rva;
                ctx->resolved_symbols[s].is_defined = true;
                ctx->resolved_symbols[s].is_dynamic = true;
                sym->is_pe_refptr_redirect = true;
                break;
            }
        }
    }

    if (ctx->output_mode == NYLINK_OUTPUT_SHARED || ctx->output_mode == NYLINK_OUTPUT_DLL) {
        ctx->entry_point_va = 0;
        if (cfg && cfg->entry_point) {
            const Nylink_Symbol *entry_sym = nylink_find_symbol(ctx, cfg->entry_point);
            if (entry_sym && entry_sym->is_defined) {
                ctx->entry_point_va = ctx->resolved_symbols[entry_sym->id].final_va;
            }
        }
        ctx->is_laid_out = true;
        return true;
    }

    const char *entry_name = (cfg && cfg->entry_point) ? cfg->entry_point : "main";
    const Nylink_Symbol *entry_sym = nylink_find_symbol(ctx, entry_name);
    if (!entry_sym) {
        entry_sym = nylink_find_symbol(ctx, "_start");
    }

    if (!entry_sym || !entry_sym->is_defined) {
        char msg[256];
        snprintf(msg, sizeof(msg), "entry point '%s' not defined", entry_name);
        nylink_diag_add(ctx, msg, nullptr, entry_name);
        return false;
    }

    uint64_t entry_va = ctx->resolved_symbols[entry_sym->id].final_va;
    const Nylink_Output_Section *sec_text = &ctx->out_sections[0];
    if (sec_text->mem_size == 0 || entry_va < sec_text->va || entry_va >= (sec_text->va + sec_text->mem_size)) {
        char msg[256];
        snprintf(msg, sizeof(msg), "entry point '%s' (0x%llx) is outside .text section (0x%llx - 0x%llx)",
                 entry_name, (unsigned long long)entry_va,
                 (unsigned long long)sec_text->va, (unsigned long long)(sec_text->va + sec_text->mem_size));
        nylink_diag_add(ctx, msg, nullptr, entry_name);
        return false;
    }

    ctx->entry_point_va = entry_va;
    ctx->is_laid_out = true;
    return true;
}
