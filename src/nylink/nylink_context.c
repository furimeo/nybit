// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>

void nylink_diag_add(Nylink_Context *ctx, const char *msg, const char *obj_name, const char *sym_or_sec) {
    if (!ctx) return;
    ctx->has_error = true;

    ny_buf_grow((void **)&ctx->diagnostics, &ctx->diagnostic_capacity, ctx->diagnostic_count, sizeof(Nylink_Diagnostic));
    Nylink_Diagnostic *diag = &ctx->diagnostics[ctx->diagnostic_count++];

    diag->message = nullptr;
    diag->object_name = nullptr;
    diag->symbol_or_section = nullptr;

    if (msg) {
        size_t len = strlen(msg);
        diag->message = (char *)ny_alloc(len + 1);
        memcpy(diag->message, msg, len + 1);
    }
    if (obj_name) {
        size_t len = strlen(obj_name);
        diag->object_name = (char *)ny_alloc(len + 1);
        memcpy(diag->object_name, obj_name, len + 1);
    }
    if (sym_or_sec) {
        size_t len = strlen(sym_or_sec);
        diag->symbol_or_section = (char *)ny_alloc(len + 1);
        memcpy(diag->symbol_or_section, sym_or_sec, len + 1);
    }
}

Nylink_Context *nylink_context_create(void) {
    Nylink_Context *ctx = (Nylink_Context *)ny_alloc_zero(sizeof(Nylink_Context));
    return ctx;
}

void nylink_context_destroy(Nylink_Context *ctx) {
    if (!ctx) return;

    for (size_t i = 0; i < ctx->object_count; i++) {
        Nylink_Object *obj = &ctx->objects[i];
        if (obj->name) {
            ny_free(obj->name, strlen(obj->name) + 1);
        }
    }
    if (ctx->objects) {
        ny_free(ctx->objects, ctx->object_capacity * sizeof(Nylink_Object));
    }

    for (size_t a = 0; a < ctx->archive_count; a++) {
        Nylink_Archive *arch = &ctx->archives[a];
        if (arch->name) {
            ny_free(arch->name, strlen(arch->name) + 1);
        }
        for (size_t m = 0; m < arch->member_count; m++) {
            if (arch->members[m].name) {
                ny_free(arch->members[m].name, strlen(arch->members[m].name) + 1);
            }
        }
        if (arch->members) {
            ny_free(arch->members, arch->member_capacity * sizeof(Nylink_Archive_Member));
        }
        for (size_t s = 0; s < arch->symbol_count; s++) {
            if (arch->symbols[s].name) {
                ny_free(arch->symbols[s].name, strlen(arch->symbols[s].name) + 1);
            }
        }
        if (arch->symbols) {
            ny_free(arch->symbols, arch->symbol_capacity * sizeof(Nylink_Archive_Symbol));
        }
    }
    if (ctx->archives) {
        ny_free(ctx->archives, ctx->archive_capacity * sizeof(Nylink_Archive));
    }

    for (size_t i = 0; i < ctx->section_count; i++) {
        Nylink_Section *sec = &ctx->sections[i];
        if (sec->name) {
            ny_free(sec->name, strlen(sec->name) + 1);
        }
    }
    if (ctx->sections) {
        ny_free(ctx->sections, ctx->section_capacity * sizeof(Nylink_Section));
    }
    if (ctx->sec_layouts) {
        ny_free(ctx->sec_layouts, ctx->section_count * sizeof(Nylink_Input_Sec_Layout));
    }

    for (size_t i = 0; i < ctx->symbol_count; i++) {
        Nylink_Symbol *sym = &ctx->symbols[i];
        if (sym->name) {
            ny_free(sym->name, strlen(sym->name) + 1);
        }
    }
    if (ctx->symbols) {
        ny_free(ctx->symbols, ctx->symbol_capacity * sizeof(Nylink_Symbol));
    }
    if (ctx->resolved_symbols) {
        ny_free(ctx->resolved_symbols, ctx->symbol_count * sizeof(Nylink_Resolved_Sym));
    }

    for (size_t i = 0; i < 4; i++) {
        if (ctx->out_sections[i].data) {
            ny_free(ctx->out_sections[i].data, ctx->out_sections[i].data_capacity);
        }
    }

    if (ctx->relocations) {
        ny_free(ctx->relocations, ctx->relocation_capacity * sizeof(Nylink_Relocation));
    }

    for (size_t i = 0; i < ctx->diagnostic_count; i++) {
        Nylink_Diagnostic *diag = &ctx->diagnostics[i];
        if (diag->message) {
            ny_free(diag->message, strlen(diag->message) + 1);
        }
        if (diag->object_name) {
            ny_free(diag->object_name, strlen(diag->object_name) + 1);
        }
        if (diag->symbol_or_section) {
            ny_free(diag->symbol_or_section, strlen(diag->symbol_or_section) + 1);
        }
    }
    if (ctx->diagnostics) {
        ny_free(ctx->diagnostics, ctx->diagnostic_capacity * sizeof(Nylink_Diagnostic));
    }

    if (ctx->soname) {
        ny_free(ctx->soname, strlen(ctx->soname) + 1);
    }
    if (ctx->rpath) {
        ny_free(ctx->rpath, strlen(ctx->rpath) + 1);
    }
    for (size_t i = 0; i < ctx->needed_lib_count; i++) {
        if (ctx->needed_libs[i]) {
            ny_free(ctx->needed_libs[i], strlen(ctx->needed_libs[i]) + 1);
        }
    }
    if (ctx->needed_libs) {
        ny_free(ctx->needed_libs, ctx->needed_lib_capacity * sizeof(char *));
    }

    if (ctx->reloc_plans) {
        ny_free(ctx->reloc_plans, ctx->relocation_count);
    }
    if (ctx->reloc_plan_slots) {
        ny_free(ctx->reloc_plan_slots, ctx->relocation_count * sizeof(uint32_t));
    }
    if (ctx->import_sym_ids) {
        ny_free(ctx->import_sym_ids, ctx->import_capacity * sizeof(uint32_t));
    }
    if (ctx->import_plt_idx) {
        ny_free(ctx->import_plt_idx, ctx->import_capacity * sizeof(uint32_t));
    }
    if (ctx->import_got_idx) {
        ny_free(ctx->import_got_idx, ctx->import_capacity * sizeof(uint32_t));
    }

    if (ctx->dynsym_sym_ids) {
        ny_free(ctx->dynsym_sym_ids, ctx->dynsym_capacity * sizeof(uint32_t));
    }
    if (ctx->dynstr_data) {
        ny_free(ctx->dynstr_data, ctx->dynstr_capacity);
    }
    if (ctx->dynamic_data) {
        ny_free(ctx->dynamic_data, ctx->dynamic_data_capacity);
    }
    if (ctx->rela_dyn_data) {
        ny_free(ctx->rela_dyn_data, ctx->rela_dyn_data_capacity);
    }
    if (ctx->got_data) {
        ny_free(ctx->got_data, ctx->got_data_capacity);
    }
    if (ctx->plt_data) {
        ny_free(ctx->plt_data, ctx->plt_data_capacity);
    }
    if (ctx->got_plt_data) {
        ny_free(ctx->got_plt_data, ctx->got_plt_data_capacity);
    }
    if (ctx->rela_plt_data) {
        ny_free(ctx->rela_plt_data, ctx->rela_plt_data_capacity);
    }
    if (ctx->hash_data) {
        ny_free(ctx->hash_data, ctx->hash_data_capacity);
    }
    if (ctx->dynamic_linker) {
        ny_free(ctx->dynamic_linker, strlen(ctx->dynamic_linker) + 1);
    }

    if (ctx->pe_exports) {
        for (size_t i = 0; i < ctx->pe_export_count; i++) {
            if (ctx->pe_exports[i]) {
                ny_free(ctx->pe_exports[i], strlen(ctx->pe_exports[i]) + 1);
            }
        }
        ny_free(ctx->pe_exports, ctx->pe_export_capacity * sizeof(char *));
    }

    if (ctx->pe_base_relocs) {
        ny_free(ctx->pe_base_relocs, ctx->pe_base_reloc_capacity * sizeof(uint32_t));
    }

    if (ctx->pe_dll_names) {
        for (size_t i = 0; i < ctx->pe_dll_count; i++) {
            if (ctx->pe_dll_names[i]) {
                ny_free(ctx->pe_dll_names[i], strlen(ctx->pe_dll_names[i]) + 1);
            }
        }
        ny_free(ctx->pe_dll_names, ctx->pe_dll_capacity * sizeof(char *));
    }

    if (ctx->pe_imp_sym_names) {
        for (size_t i = 0; i < ctx->pe_imp_count; i++) {
            if (ctx->pe_imp_sym_names[i]) {
                ny_free(ctx->pe_imp_sym_names[i], strlen(ctx->pe_imp_sym_names[i]) + 1);
            }
        }
        ny_free(ctx->pe_imp_sym_names, ctx->pe_imp_capacity * sizeof(char *));
    }
    if (ctx->pe_imp_dll_indices) {
        ny_free(ctx->pe_imp_dll_indices, ctx->pe_imp_capacity * sizeof(uint32_t));
    }
    if (ctx->pe_imp_sym_ids) {
        ny_free(ctx->pe_imp_sym_ids, ctx->pe_imp_capacity * sizeof(uint32_t));
    }
    if (ctx->pe_imp_iat_rvas) {
        ny_free(ctx->pe_imp_iat_rvas, ctx->pe_imp_capacity * sizeof(uint32_t));
    }
    if (ctx->pe_imp_thunk_rvas) {
        ny_free(ctx->pe_imp_thunk_rvas, ctx->pe_imp_capacity * sizeof(uint32_t));
    }

    ny_free(ctx, sizeof(Nylink_Context));
}

void nylink_context_set_shared(Nylink_Context *ctx, bool is_shared) {
    if (ctx) {
        ctx->is_shared = is_shared;
        ctx->output_mode = is_shared ? NYLINK_OUTPUT_SHARED : NYLINK_OUTPUT_EXECUTABLE;
    }
}

void nylink_context_set_output_mode(Nylink_Context *ctx, Nylink_Output_Mode mode) {
    if (ctx) {
        ctx->output_mode = mode;
        ctx->is_shared = (mode == NYLINK_OUTPUT_SHARED || mode == NYLINK_OUTPUT_PIE || mode == NYLINK_OUTPUT_DLL);
    }
}

bool nylink_add_export(Nylink_Context *ctx, const char *symbol_name) {
    if (!ctx || !symbol_name || symbol_name[0] == '\0') return false;
    for (size_t i = 0; i < ctx->pe_export_count; i++) {
        if (strcmp(ctx->pe_exports[i], symbol_name) == 0) {
            return true;
        }
    }
    ny_buf_grow((void **)&ctx->pe_exports, &ctx->pe_export_capacity, ctx->pe_export_count, sizeof(char *));
    size_t len = strlen(symbol_name);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, symbol_name, len + 1);
    ctx->pe_exports[ctx->pe_export_count++] = copy;
    return true;
}

bool nylink_add_object(Nylink_Context *ctx, const char *name, const uint8_t *data, size_t size) {
    if (!ctx) return false;
    const char *obj_name = name ? name : "<unnamed>";

    if (!data || size < 4) {
        nylink_diag_add(ctx, "corrupt or truncated object: insufficient size", obj_name, nullptr);
        return false;
    }

    ny_buf_grow((void **)&ctx->objects, &ctx->object_capacity, ctx->object_count, sizeof(Nylink_Object));
    uint32_t obj_idx = (uint32_t)ctx->object_count++;
    Nylink_Object *obj = &ctx->objects[obj_idx];
    memset(obj, 0, sizeof(Nylink_Object));

    size_t name_len = strlen(obj_name);
    obj->name = (char *)ny_alloc(name_len + 1);
    memcpy(obj->name, obj_name, name_len + 1);
    obj->data = data;
    obj->size = size;

    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        if (obj_idx > 0 && ctx->objects[0].format != NYLINK_FORMAT_ELF64) {
            nylink_diag_add(ctx, "format mismatch: cannot mix ELF object with non-ELF objects in same link context", obj->name, nullptr);
            return false;
        }
        obj->format = NYLINK_FORMAT_ELF64;
        return nylink_read_elf64(ctx, obj_idx);
    }

    uint16_t machine = (uint16_t)(data[0] | (data[1] << 8));
    if (machine == 0x8664) {
        if (obj_idx > 0 && ctx->objects[0].format != NYLINK_FORMAT_COFF) {
            nylink_diag_add(ctx, "format mismatch: cannot mix COFF object with non-COFF objects in same link context", obj->name, nullptr);
            return false;
        }
        obj->format = NYLINK_FORMAT_COFF;
        return nylink_read_coff(ctx, obj_idx);
    }

    obj->format = NYLINK_FORMAT_UNKNOWN;
    nylink_diag_add(ctx, "unsupported or unrecognized object format", obj->name, nullptr);
    return false;
}

bool nylink_add_archive(Nylink_Context *ctx, const char *name, const uint8_t *data, size_t size) {
    if (!ctx) return false;
    const char *arch_name = name ? name : "<unnamed_archive>";

    if (!data || size < 8) {
        nylink_diag_add(ctx, "corrupt or truncated archive: insufficient size", arch_name, nullptr);
        return false;
    }

    ny_buf_grow((void **)&ctx->archives, &ctx->archive_capacity, ctx->archive_count, sizeof(Nylink_Archive));
    uint32_t arch_idx = (uint32_t)ctx->archive_count++;
    Nylink_Archive *arch = &ctx->archives[arch_idx];
    memset(arch, 0, sizeof(Nylink_Archive));

    size_t name_len = strlen(arch_name);
    arch->name = (char *)ny_alloc(name_len + 1);
    memcpy(arch->name, arch_name, name_len + 1);
    arch->data = data;
    arch->size = size;

    return nylink_read_archive(ctx, arch_idx);
}

bool nylink_resolve_symbols(Nylink_Context *ctx) {
    if (!ctx) return false;

    if (!nylink_extract_needed_archive_members(ctx)) {
        return false;
    }

    typedef struct Resolved_Ref {
        const char *name;
        uint32_t sym_id;
        bool is_defined;
        Nylink_Sym_Binding binding;
        uint32_t obj_index;
    } Resolved_Ref;

    size_t ref_count = 0;
    size_t ref_capacity = 0;
    Resolved_Ref *refs = nullptr;
    bool success = true;

    for (size_t i = 0; i < ctx->symbol_count; i++) {
        Nylink_Symbol *sym = &ctx->symbols[i];
        if (sym->binding == NYLINK_SYM_LOCAL) {
            continue;
        }

        if (!sym->name || sym->name[0] == '\0') {
            continue;
        }

        int match_idx = -1;
        for (size_t r = 0; r < ref_count; r++) {
            if (strcmp(refs[r].name, sym->name) == 0) {
                match_idx = (int)r;
                break;
            }
        }

        if (match_idx < 0) {
            ny_buf_grow((void **)&refs, &ref_capacity, ref_count, sizeof(Resolved_Ref));
            Resolved_Ref *r = &refs[ref_count++];
            r->name = sym->name;
            r->sym_id = sym->id;
            r->is_defined = sym->is_defined;
            r->binding = sym->binding;
            r->obj_index = sym->obj_index;
            continue;
        }

        Resolved_Ref *curr = &refs[match_idx];
        const Nylink_Symbol *curr_sym = &ctx->symbols[curr->sym_id];

        if (!curr->is_defined) {
            if (sym->is_defined) {
                curr->sym_id = sym->id;
                curr->is_defined = true;
                curr->binding = sym->binding;
                curr->obj_index = sym->obj_index;
            }
        } else {
            if (sym->is_defined) {
                if (curr_sym->is_dynamic && !sym->is_dynamic) {
                    /* Static definition overrides dynamic definition from .so */
                    curr->sym_id = sym->id;
                    curr->is_defined = true;
                    curr->binding = sym->binding;
                    curr->obj_index = sym->obj_index;
                } else if (!curr_sym->is_dynamic && sym->is_dynamic) {
                    /* Keep static definition */
                } else if (curr_sym->is_dynamic && sym->is_dynamic) {
                    /* Both are from .so libraries: first one wins, not a duplicate error */
                } else if (curr->binding == NYLINK_SYM_GLOBAL && sym->binding == NYLINK_SYM_GLOBAL) {
                    const char *obj1 = curr->obj_index < ctx->object_count ? ctx->objects[curr->obj_index].name : "<unknown>";
                    const char *obj2 = sym->obj_index < ctx->object_count ? ctx->objects[sym->obj_index].name : "<unknown>";
                    char msg[256];
                    snprintf(msg, sizeof(msg), "duplicate symbol definition: %s defined in %s and %s", sym->name, obj1, obj2);
                    nylink_diag_add(ctx, msg, obj2, sym->name);
                    success = false;
                } else if (curr->binding == NYLINK_SYM_WEAK && sym->binding == NYLINK_SYM_GLOBAL) {
                    curr->sym_id = sym->id;
                    curr->is_defined = true;
                    curr->binding = sym->binding;
                    curr->obj_index = sym->obj_index;
                }
            }
        }
    }

    for (size_t r = 0; r < ref_count; r++) {
        if (!refs[r].is_defined) {
            if (ctx->is_shared || refs[r].binding == NYLINK_SYM_WEAK) {
                /* Undefined externals resolve at runtime in shared/PIE output;
                   weak undefined references resolve to 0 everywhere */
                continue;
            }
            const char *obj_name = refs[r].obj_index < ctx->object_count ? ctx->objects[refs[r].obj_index].name : "<unknown>";
            char msg[256];
            snprintf(msg, sizeof(msg), "undefined symbol: %s", refs[r].name);
            nylink_diag_add(ctx, msg, obj_name, refs[r].name);
            success = false;
        }
    }

    if (success && ctx->symbol_count > 0 && !ctx->resolved_symbols) {
        ctx->resolved_symbols = (Nylink_Resolved_Sym *)ny_alloc_zero(ctx->symbol_count * sizeof(Nylink_Resolved_Sym));
        for (size_t i = 0; i < ctx->symbol_count; i++) {
            ctx->resolved_symbols[i].sym_id = (uint32_t)i;
            ctx->resolved_symbols[i].is_defined = ctx->symbols[i].is_defined;
            ctx->resolved_symbols[i].is_dynamic = ctx->symbols[i].is_dynamic;
        }

        for (size_t i = 0; i < ctx->symbol_count; i++) {
            const Nylink_Symbol *sym = &ctx->symbols[i];
            if (sym->binding == NYLINK_SYM_LOCAL || !sym->name || sym->name[0] == '\0') {
                continue;
            }
            for (size_t r = 0; r < ref_count; r++) {
                if (strcmp(refs[r].name, sym->name) != 0) continue;
                ctx->resolved_symbols[i].is_defined = refs[r].is_defined;
                ctx->resolved_symbols[i].is_dynamic = refs[r].is_defined && ctx->symbols[refs[r].sym_id].is_dynamic;
                break;
            }
        }
    }

    if (refs) {
        ny_free(refs, ref_capacity * sizeof(Resolved_Ref));
    }

    if (!success) {
        ctx->has_error = true;
    }
    return success;
}

bool nylink_layout(Nylink_Context *ctx, const Nylink_Config *cfg) {
    if (!ctx) return false;
    return nylink_layout_internal(ctx, cfg);
}

bool nylink_apply_relocations(Nylink_Context *ctx) {
    if (!ctx) return false;
    return nylink_apply_relocations_internal(ctx);
}

bool nylink_write_executable(Nylink_Context *ctx, const char *out_path, const Nylink_Config *cfg) {
    if (!ctx || !out_path) return false;
    Nylink_Target_Format fmt = cfg ? cfg->target_format : NYLINK_TARGET_ELF64;
    if (fmt == NYLINK_TARGET_PE) {
        return nylink_write_pe_executable(ctx, out_path, cfg);
    } else {
        return nylink_write_elf_executable(ctx, out_path, cfg);
    }
}

size_t nylink_get_object_count(const Nylink_Context *ctx) {
    return ctx ? ctx->object_count : 0;
}

const char *nylink_get_object_name(const Nylink_Context *ctx, size_t index) {
    if (!ctx || index >= ctx->object_count) return nullptr;
    return ctx->objects[index].name;
}

size_t nylink_get_section_count(const Nylink_Context *ctx) {
    return ctx ? ctx->section_count : 0;
}

const Nylink_Section *nylink_get_section(const Nylink_Context *ctx, size_t index) {
    if (!ctx || index >= ctx->section_count) return nullptr;
    return &ctx->sections[index];
}

uint64_t nylink_section_get_va(const Nylink_Context *ctx, uint32_t sec_id) {
    if (!ctx || !ctx->sec_layouts || sec_id >= ctx->section_count) return 0;
    return ctx->sec_layouts[sec_id].va;
}

size_t nylink_get_symbol_count(const Nylink_Context *ctx) {
    return ctx ? ctx->symbol_count : 0;
}

const Nylink_Symbol *nylink_get_symbol(const Nylink_Context *ctx, size_t index) {
    if (!ctx || index >= ctx->symbol_count) return nullptr;
    return &ctx->symbols[index];
}

const Nylink_Symbol *nylink_find_symbol(const Nylink_Context *ctx, const char *name) {
    if (!ctx || !name) return nullptr;

    const Nylink_Symbol *candidate = nullptr;
    for (size_t i = 0; i < ctx->symbol_count; i++) {
        const Nylink_Symbol *sym = &ctx->symbols[i];
        if (sym->name && strcmp(sym->name, name) == 0) {
            if (sym->is_defined && !sym->is_dynamic && sym->binding == NYLINK_SYM_GLOBAL) {
                return sym;
            }
            if (sym->is_defined && !sym->is_dynamic && sym->binding == NYLINK_SYM_WEAK) {
                if (!candidate || !candidate->is_defined || candidate->is_dynamic) {
                    candidate = sym;
                }
            } else if (sym->is_defined && sym->is_dynamic) {
                if (!candidate || !candidate->is_defined) {
                    candidate = sym;
                }
            } else if (!candidate) {
                candidate = sym;
            }
        }
    }
    return candidate;
}

uint64_t nylink_symbol_get_final_va(const Nylink_Context *ctx, uint32_t sym_id) {
    if (!ctx || !ctx->resolved_symbols || sym_id >= ctx->symbol_count) return 0;
    return ctx->resolved_symbols[sym_id].final_va;
}

size_t nylink_get_relocation_count(const Nylink_Context *ctx) {
    return ctx ? ctx->relocation_count : 0;
}

const Nylink_Relocation *nylink_get_relocation(const Nylink_Context *ctx, size_t index) {
    if (!ctx || index >= ctx->relocation_count) return nullptr;
    return &ctx->relocations[index];
}

size_t nylink_get_diagnostic_count(const Nylink_Context *ctx) {
    return ctx ? ctx->diagnostic_count : 0;
}

const Nylink_Diagnostic *nylink_get_diagnostic(const Nylink_Context *ctx, size_t index) {
    if (!ctx || index >= ctx->diagnostic_count) return nullptr;
    return &ctx->diagnostics[index];
}

bool nylink_has_errors(const Nylink_Context *ctx) {
    return ctx ? ctx->has_error : false;
}

