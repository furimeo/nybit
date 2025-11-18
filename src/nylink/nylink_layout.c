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

bool nylink_layout_internal(Nylink_Context *ctx, const Nylink_Config *cfg) {
    if (!ctx) return false;

    if (cfg && (cfg->output_mode == NYLINK_OUTPUT_SHARED || cfg->output_mode == NYLINK_OUTPUT_PIE)) {
        ctx->is_shared = true;
        ctx->output_mode = cfg->output_mode;
        if (cfg->soname && !ctx->soname) {
            size_t slen = strlen(cfg->soname);
            ctx->soname = (char *)ny_alloc(slen + 1);
            memcpy(ctx->soname, cfg->soname, slen + 1);
        }
        if (cfg->dynamic_linker && !ctx->dynamic_linker) {
            size_t dlen = strlen(cfg->dynamic_linker);
            ctx->dynamic_linker = (char *)ny_alloc(dlen + 1);
            memcpy(ctx->dynamic_linker, cfg->dynamic_linker, dlen + 1);
        }
        if (cfg->needed_lib_count > 0 && !ctx->needed_libs) {
            ctx->needed_lib_count = cfg->needed_lib_count;
            ctx->needed_libs = (char **)ny_alloc(ctx->needed_lib_count * sizeof(char *));
            for (size_t i = 0; i < ctx->needed_lib_count; i++) {
                size_t nlen = strlen(cfg->needed_libs[i]);
                ctx->needed_libs[i] = (char *)ny_alloc(nlen + 1);
                memcpy(ctx->needed_libs[i], cfg->needed_libs[i], nlen + 1);
            }
        }
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
        header_file_size = 0x400;
    } else {
        /* ELF64 */
        if (ctx->is_shared) {
            base_va = (cfg && cfg->base_address != 0) ? cfg->base_address : 0x0ULL;
        } else {
            base_va = (cfg && cfg->base_address != 0) ? cfg->base_address : 0x400000ULL;
        }
        page_size = 0x1000;
        file_align = 0x1000;
        header_file_size = 0x1000;
    }

    ctx->image_base = base_va;

    uint64_t current_rva = page_size;
    uint64_t current_file_off = header_file_size;

    /* If shared ELF64, layout dynamic read-only sections before .text in the RX/R segment or right after headers */
    if (ctx->is_shared && (!cfg || cfg->target_format == NYLINK_TARGET_ELF64)) {
        /* Construct .dynstr and .dynsym */
        /* Start dynstr with a null byte */
        if (!ctx->dynstr_data) {
            ctx->dynstr_capacity = 256;
            ctx->dynstr_data = (uint8_t *)ny_alloc_zero(ctx->dynstr_capacity);
            ctx->dynstr_size = 1; /* index 0 is '\0' */
        }

        /* Helper lambda-like macro to append string to dynstr */
        #define APPEND_DYNSTR(str) ({ \
            const char *_s = (str); \
            size_t _slen = strlen(_s) + 1; \
            if (ctx->dynstr_size + _slen > ctx->dynstr_capacity) { \
                size_t _ncap = (ctx->dynstr_size + _slen) * 2; \
                ctx->dynstr_data = (uint8_t *)ny_realloc(ctx->dynstr_data, ctx->dynstr_capacity, _ncap); \
                ctx->dynstr_capacity = _ncap; \
            } \
            uint32_t _off = (uint32_t)ctx->dynstr_size; \
            memcpy(ctx->dynstr_data + _off, _s, _slen); \
            ctx->dynstr_size += _slen; \
            _off; \
        })

        /* Append soname */
        if (ctx->soname) {
            APPEND_DYNSTR(ctx->soname);
        }

        /* Append needed libs */
        for (size_t i = 0; i < ctx->needed_lib_count; i++) {
            if (ctx->needed_libs[i]) {
                APPEND_DYNSTR(ctx->needed_libs[i]);
            }
        }

        /* Populate dynsym: index 0 is null symbol */
        if (!ctx->dynsym_sym_ids) {
            ctx->dynsym_capacity = 32;
            ctx->dynsym_sym_ids = (uint32_t *)ny_alloc_zero(ctx->dynsym_capacity * sizeof(uint32_t));
            ctx->dynsym_count = 1; /* Index 0 is NULL symbol */
        }

        /* Handle .interp for PIE */
        if (ctx->output_mode == NYLINK_OUTPUT_PIE) {
            const char *interp_path = ctx->dynamic_linker ? ctx->dynamic_linker : "/lib64/ld-linux-x86-64.so.2";
            ctx->interp_file_size = strlen(interp_path) + 1;
            ctx->interp_va = base_va + current_rva;
            ctx->interp_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->interp_file_size, 8, &overflow);
            current_file_off += align_up_checked(ctx->interp_file_size, 8, &overflow);
        }

        /* Collect exported symbols and external references into dynsym */
        for (size_t s = 0; s < ctx->symbol_count; s++) {
            Nylink_Symbol *sym = &ctx->symbols[s];
            if (!sym->name || sym->name[0] == '\0') continue;
            if (sym->binding == NYLINK_SYM_LOCAL) continue;
            if (sym->visibility != 0) continue; /* hidden symbols not exported */

            /* Check if already in dynsym by name */
            bool exists = false;
            for (size_t d = 1; d < ctx->dynsym_count; d++) {
                uint32_t existing_sym_id = ctx->dynsym_sym_ids[d];
                if (strcmp(ctx->symbols[existing_sym_id].name, sym->name) == 0) {
                    exists = true;
                    break;
                }
            }
            if (exists) continue;

            if (ctx->dynsym_count >= ctx->dynsym_capacity) {
                size_t new_cap = ctx->dynsym_capacity * 2;
                ctx->dynsym_sym_ids = (uint32_t *)ny_realloc(ctx->dynsym_sym_ids, ctx->dynsym_capacity * sizeof(uint32_t), new_cap * sizeof(uint32_t));
                ctx->dynsym_capacity = new_cap;
            }
            ctx->dynsym_sym_ids[ctx->dynsym_count++] = (uint32_t)s;
            APPEND_DYNSTR(sym->name);
        }

        #undef APPEND_DYNSTR

        /* Compute size of dynamic sections */
        ctx->dynsym_file_size = ctx->dynsym_count * 24; /* sizeof(Elf64_Sym) = 24 */
        ctx->dynstr_file_size = ctx->dynstr_size;

        /* Build SysV .hash table */
        uint32_t nbucket = (ctx->dynsym_count > 1) ? (uint32_t)ctx->dynsym_count : 1;
        uint32_t nchain = (uint32_t)ctx->dynsym_count;
        ctx->hash_file_size = (2 + nbucket + nchain) * sizeof(uint32_t);
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

        /* Scan relocations to identify:
           1. PLT calls (PLT32 / PC32 to function dynamic symbol)
           2. GOT data imports (PLT32 / PC32 to non-function dynamic symbol)
           3. Relative relocations (R_X86_64_64)
        */
        size_t dynamic_reloc_count = 0;
        size_t plt_count = 0;
        size_t got_data_count = 0;

        for (size_t r = 0; r < ctx->relocation_count; r++) {
            const Nylink_Relocation *reloc = &ctx->relocations[r];
            if (reloc->sym_id < ctx->symbol_count) {
                const Nylink_Symbol *sym = &ctx->symbols[reloc->sym_id];
                const Nylink_Symbol *def = nylink_find_symbol(ctx, sym->name);
                bool is_def = (def && def->is_defined);

                if (reloc->type == NYLINK_RELOC_X86_64_64) {
                    dynamic_reloc_count++;
                } else if (reloc->type == NYLINK_RELOC_X86_64_PC32 || reloc->type == NYLINK_RELOC_X86_64_PLT32) {
                    if (!is_def || (def && def->is_dynamic)) {
                        bool is_func = (def ? def->is_function : sym->is_function);
                        if (is_func || reloc->type == NYLINK_RELOC_X86_64_PLT32) {
                            plt_count++;
                        } else {
                            got_data_count++;
                            dynamic_reloc_count++;
                        }
                    }
                }
            }
        }

        ctx->plt_entry_count = plt_count;
        ctx->plt_file_size = (plt_count > 0) ? (16 + plt_count * 16) : 0; /* PLT0 + N * PLT[i] */
        ctx->rela_plt_count = plt_count;
        ctx->rela_plt_file_size = plt_count * 24; /* sizeof(Elf64_Rela) = 24 */
        ctx->got_plt_file_size = (plt_count > 0) ? ((3 + plt_count) * 8) : 0; /* GOT_PLT[0..2] + N slots */

        ctx->got_entry_count = got_data_count;
        /* Always emit at least a minimal .got in shared/PIE mode */
        ctx->got_file_size = (got_data_count == 0) ? 8 : got_data_count * 8;
        ctx->rela_dyn_count = dynamic_reloc_count;
        ctx->rela_dyn_file_size = dynamic_reloc_count * 24;

        /* .dynamic size:
           DT_HASH, DT_STRTAB, DT_STRSZ, DT_SYMTAB, DT_SYMENT
           DT_SONAME (optional), DT_NEEDED * N
           DT_RELA, DT_RELASZ, DT_RELAENT (if rela_dyn)
           DT_PLTGOT, DT_PLTRELSZ, DT_PLTREL, DT_JMPREL (if rela_plt)
           DT_NULL
        */
        size_t dynamic_entries = 6 + (ctx->soname ? 1 : 0) + ctx->needed_lib_count;
        if (ctx->rela_dyn_file_size > 0) dynamic_entries += 3;
        if (ctx->rela_plt_file_size > 0) dynamic_entries += 4;
        ctx->dynamic_file_size = dynamic_entries * 16; /* sizeof(Elf64_Dyn) = 16 */

        /* Place .hash, .dynsym, .dynstr, .rela.dyn, .rela.plt in read-only segment before .text */
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
            /* .plt is executable code, align to 16 bytes */
            current_rva = align_up_checked(current_rva, 16, &overflow);
            current_file_off = align_up_checked(current_file_off, 16, &overflow);
            ctx->plt_va = base_va + current_rva;
            ctx->plt_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->plt_file_size, 16, &overflow);
            current_file_off += align_up_checked(ctx->plt_file_size, 16, &overflow);
        }

        /* Pad to page boundary before .text */
        current_rva = align_up_checked(current_rva, page_size, &overflow);
        current_file_off = align_up_checked(current_file_off, file_align, &overflow);
    }

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

    /* In shared/PIE mode, place .got, .got.plt and .dynamic in data segment */
    if (ctx->is_shared && (!cfg || cfg->target_format == NYLINK_TARGET_ELF64)) {
        if (ctx->got_file_size > 0) {
            ctx->got_va = base_va + current_rva;
            ctx->got_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->got_file_size, 8, &overflow);
            current_file_off += align_up_checked(ctx->got_file_size, 8, &overflow);
        }
        if (ctx->got_plt_file_size > 0) {
            ctx->got_plt_va = base_va + current_rva;
            ctx->got_plt_file_offset = current_file_off;
            current_rva += align_up_checked(ctx->got_plt_file_size, 8, &overflow);
            current_file_off += align_up_checked(ctx->got_plt_file_size, 8, &overflow);
        }
        ctx->dynamic_va = base_va + current_rva;
        ctx->dynamic_file_offset = current_file_off;
        current_rva += align_up_checked(ctx->dynamic_file_size, 8, &overflow);
        current_file_off += align_up_checked(ctx->dynamic_file_size, 8, &overflow);
    }

    if (overflow) {
        nylink_diag_add(ctx, "layout error: address or file offset overflow", nullptr, nullptr);
        return false;
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

    /* Assign VAs to directly defined symbols */
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

    /* Propagate final_va to undefined references from resolved global/weak definitions */
    for (size_t s = 0; s < ctx->symbol_count; s++) {
        Nylink_Symbol *sym = &ctx->symbols[s];
        if (!sym->is_defined && sym->name && sym->name[0] != '\0') {
            const Nylink_Symbol *def_sym = nylink_find_symbol(ctx, sym->name);
            if (def_sym && def_sym->is_defined) {
                ctx->resolved_symbols[s].final_va = ctx->resolved_symbols[def_sym->id].final_va;
                ctx->resolved_symbols[s].is_defined = true;
            }
        }
    }

    /* Phase 4: Resolve entry point */
    if (ctx->output_mode == NYLINK_OUTPUT_SHARED) {
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
