// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nygen/nygen.h"
#include "nybit/support.h"
#include "nybit/ir.h"
#include "nybit/parser.h"
#include "nybit/opt.h"
#include "nybit/analysis.h"
#include "nybit/machine.h"
#include "nybit/regalloc.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include "nybit/x86_encode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef struct JIT_Exported_Symbol {
    char *name;
    void *addr;
} JIT_Exported_Symbol;

typedef struct JIT_Page_Allocation {
    void *ptr;
    size_t size;
} JIT_Page_Allocation;

struct Ny_JIT_Engine {
    Ny_JIT_Config config;

    JIT_Page_Allocation *allocations;
    size_t allocation_count;
    size_t allocation_capacity;

    JIT_Exported_Symbol *exports;
    size_t export_count;
    size_t export_capacity;
};

void ny_jit_config_init(Ny_JIT_Config *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->opt_level = NYGEN_OPT_O1;
}

Ny_JIT_Engine *ny_jit_create(const Ny_JIT_Config *config) {
    Ny_JIT_Engine *jit = (Ny_JIT_Engine *)ny_alloc_zero(sizeof(Ny_JIT_Engine));
    if (config) {
        jit->config = *config;
    } else {
        ny_jit_config_init(&jit->config);
    }
    return jit;
}

static void free_page_allocation(void *ptr, size_t size) {
    if (!ptr || size == 0) return;
#if defined(_WIN32)
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

static void *allocate_pages(size_t size) {
    if (size == 0) return NULL;
#if defined(_WIN32)
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
#endif
}

typedef enum {
    JIT_PROT_READWRITE,
    JIT_PROT_READONLY,
    JIT_PROT_EXECUTE_READ,
} JIT_Protection;

static bool protect_pages(void *ptr, size_t size, JIT_Protection prot) {
    if (!ptr || size == 0) return true;
#if defined(_WIN32)
    DWORD old_prot;
    DWORD win_prot = PAGE_READWRITE;
    if (prot == JIT_PROT_READONLY) win_prot = PAGE_READONLY;
    else if (prot == JIT_PROT_EXECUTE_READ) win_prot = PAGE_EXECUTE_READ;
    return VirtualProtect(ptr, size, win_prot, &old_prot) != 0;
#else
    int posix_prot = PROT_READ | PROT_WRITE;
    if (prot == JIT_PROT_READONLY) posix_prot = PROT_READ;
    else if (prot == JIT_PROT_EXECUTE_READ) posix_prot = PROT_READ | PROT_EXEC;
    return mprotect(ptr, size, posix_prot) == 0;
#endif
}

static void flush_cache(void *ptr, size_t size) {
#if defined(_WIN32)
    FlushInstructionCache(GetCurrentProcess(), ptr, size);
#else
    __builtin___clear_cache((char *)ptr, (char *)ptr + size);
#endif
}

static void jit_add_export(Ny_JIT_Engine *jit, const char *name, void *addr) {
    ny_buf_grow((void **)&jit->exports, &jit->export_capacity, jit->export_count, sizeof(JIT_Exported_Symbol));
    size_t len = strlen(name);
    char *dup = (char *)ny_alloc(len + 1);
    memcpy(dup, name, len + 1);
    jit->exports[jit->export_count].name = dup;
    jit->exports[jit->export_count].addr = addr;
    jit->export_count++;
}

static const void *resolve_external_symbol(Ny_JIT_Engine *jit, const char *name) {
    for (size_t i = 0; i < jit->config.symbol_count; i++) {
        if (jit->config.symbols[i].name && strcmp(jit->config.symbols[i].name, name) == 0) {
            return jit->config.symbols[i].addr;
        }
    }
    if (jit->config.resolver) {
        return jit->config.resolver(name, jit->config.resolver_user_data);
    }
    return NULL;
}

static size_t align_up(size_t val, size_t align) {
    if (align <= 1) return val;
    size_t rem = val % align;
    return rem == 0 ? val : val + (align - rem);
}

static void add_diagnostic(Nygen_Diagnostic **out_diags, size_t *out_count, const char *msg, uint32_t line, uint32_t col) {
    if (!out_diags || !out_count) return;
    size_t old_count = *out_count;
    size_t new_count = old_count + 1;
    *out_diags = (Nygen_Diagnostic *)ny_realloc(*out_diags, old_count * sizeof(Nygen_Diagnostic), new_count * sizeof(Nygen_Diagnostic));
    size_t len = strlen(msg);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, msg, len + 1);
    (*out_diags)[old_count].message = copy;
    (*out_diags)[old_count].line = line;
    (*out_diags)[old_count].col = col;
    *out_count = new_count;
}

static bool is_internal_symbol(const X86_Encoded_Module *emod, Ny_String name) {
    for (size_t i = 0; i < emod->function_count; i++) {
        if (ny_str_eq(emod->functions[i].name, name)) return true;
    }
    for (size_t g = 0; g < emod->global_count; g++) {
        if (ny_str_eq(emod->globals[g].name, name)) return true;
    }
    return false;
}

bool ny_jit_compile(Ny_JIT_Engine *jit, const char *source_text, size_t source_len, Nygen_Diagnostic **out_diags, size_t *out_diag_count) {
    if (!jit || !source_text || source_len == 0) return false;

    if (out_diags) *out_diags = NULL;
    if (out_diag_count) *out_diag_count = 0;

    Ny_Context ctx;
    ny_context_init(&ctx, "jit_module");

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, source_text, source_len, &ctx.arena);
    bool parsed = ny_parse_module(&parser);
    if (!parsed) {
        for (size_t i = 0; i < parser.diag_count; i++) {
            add_diagnostic(out_diags, out_diag_count, parser.diagnostics[i].message, parser.diagnostics[i].line, parser.diagnostics[i].col);
        }
        ny_parser_destroy(&parser);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_parser_destroy(&parser);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_validate_module(&ctx.module, &val_diags);
    if (!valid) {
        for (size_t i = 0; i < val_diags.count; i++) {
            add_diagnostic(out_diags, out_diag_count, val_diags.items[i].message, 0, 0);
        }
        ny_diagnostic_list_destroy(&val_diags);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&val_diags);

    if (jit->config.opt_level != NYGEN_OPT_O0) {
        Ny_Opt_Level opt_lvl = (jit->config.opt_level == NYGEN_OPT_O2) ? NY_OPT_O2 : NY_OPT_O1;
        ny_opt_run_module_pipeline(&ctx.module, opt_lvl);
    }

    const Ny_Target *target = jit->config.target_triple ? ny_target_find(jit->config.target_triple) : ny_target_get_default();
    if (!target) {
        add_diagnostic(out_diags, out_diag_count, "target not found for JIT compilation", 0, 0);
        ny_context_destroy(&ctx);
        return false;
    }

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List mir_diags;
    ny_diagnostic_list_init(&mir_diags);
    bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &mir_diags);
    if (!mir_ok) {
        for (size_t i = 0; i < mir_diags.count; i++) {
            add_diagnostic(out_diags, out_diag_count, mir_diags.items[i].message, 0, 0);
        }
        ny_diagnostic_list_destroy(&mir_diags);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&mir_diags);

    for (size_t f = 0; f < mmod.function_count; f++) {
        Ny_Diagnostic_List ra_diags;
        ny_diagnostic_list_init(&ra_diags);
        bool ra_ok = ny_regalloc_run(&mmod.functions[f], target->abi, nullptr, &ra_diags);
        if (!ra_ok) {
            for (size_t i = 0; i < ra_diags.count; i++) {
                add_diagnostic(out_diags, out_diag_count, ra_diags.items[i].message, 0, 0);
            }
            ny_diagnostic_list_destroy(&ra_diags);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            return false;
        }
        ny_diagnostic_list_destroy(&ra_diags);
    }

    X86_Module xmod;
    Ny_Diagnostic_List x86_diags;
    ny_diagnostic_list_init(&x86_diags);
    bool x86_ok = x86_lower_machine_mod(target, &mmod, &xmod, &x86_diags);
    if (!x86_ok) {
        for (size_t i = 0; i < x86_diags.count; i++) {
            add_diagnostic(out_diags, out_diag_count, x86_diags.items[i].message, 0, 0);
        }
        ny_diagnostic_list_destroy(&x86_diags);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&x86_diags);

    X86_Encoded_Module emod;
    Ny_Diagnostic_List enc_diags;
    ny_diagnostic_list_init(&enc_diags);
    bool enc_ok = x86_encode_module(&emod, &xmod, &enc_diags);
    if (!enc_ok) {
        for (size_t i = 0; i < enc_diags.count; i++) {
            add_diagnostic(out_diags, out_diag_count, enc_diags.items[i].message, 0, 0);
        }
        ny_diagnostic_list_destroy(&enc_diags);
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&enc_diags);

    /* Section layout calculations */
    size_t page_size = 4096;
    size_t text_size = emod.text_section.count;
    size_t rodata_size = emod.rodata_section.count;
    size_t data_size = emod.data_section.count;
    size_t bss_size = emod.bss_size;

    size_t external_call_count = 0;
    for (size_t r = 0; r < emod.text_section.reloc_count; r++) {
        const X86_Relocation *reloc = &emod.text_section.relocs[r];
        if (reloc->kind == X86_FIXUP_CALL_REL32 && !is_internal_symbol(&emod, reloc->symbol_name)) {
            external_call_count++;
        }
    }
    size_t trampoline_area = external_call_count * 12;
    size_t text_aligned = align_up(text_size + trampoline_area, page_size);
    size_t rodata_aligned = align_up(rodata_size, page_size);
    size_t data_aligned = align_up(data_size + bss_size, page_size);

    size_t total_payload = text_aligned + rodata_aligned + data_aligned;
    size_t total_alloc_size = align_up(total_payload > 0 ? total_payload : page_size, page_size);

    uint8_t *base_ptr = (uint8_t *)allocate_pages(total_alloc_size);
    if (!base_ptr) {
        add_diagnostic(out_diags, out_diag_count, "failed to allocate native memory for JIT execution", 0, 0);
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    memset(base_ptr, 0, total_alloc_size);

    uint8_t *text_base = base_ptr;
    uint8_t *rodata_base = text_base + text_aligned;
    uint8_t *data_base = rodata_base + rodata_aligned;
    uint8_t *bss_base = data_base + data_size;

    if (text_size > 0) {
        memcpy(text_base, emod.text_section.bytes, text_size);
    }
    if (rodata_size > 0) {
        memcpy(rodata_base, emod.rodata_section.bytes, rodata_size);
    }
    if (data_size > 0) {
        memcpy(data_base, emod.data_section.bytes, data_size);
    }

    /* Relocation resolution */
    bool reloc_success = true;
    size_t trampoline_offset = text_size;
    for (size_t r = 0; r < emod.text_section.reloc_count; r++) {
        const X86_Relocation *reloc = &emod.text_section.relocs[r];
        if (reloc->code_offset + 4 > text_size) {
            add_diagnostic(out_diags, out_diag_count, "JIT relocation offset out of bounds", 0, 0);
            reloc_success = false;
            break;
        }
        if (reloc->kind != X86_FIXUP_CALL_REL32 && reloc->kind != X86_FIXUP_GLOBAL_REL32) {
            add_diagnostic(out_diags, out_diag_count, "unsupported relocation kind in JIT", 0, 0);
            reloc_success = false;
            break;
        }

        char sym_name[128];
        snprintf(sym_name, sizeof(sym_name), "%.*s", (int)reloc->symbol_name.len, reloc->symbol_name.data);

        const void *target_addr = NULL;
        bool is_external = false;

        for (size_t i = 0; i < emod.function_count; i++) {
            if (ny_str_eq(emod.functions[i].name, reloc->symbol_name)) {
                target_addr = text_base + emod.functions[i].offset;
                break;
            }
        }

        if (!target_addr) {
            for (size_t g = 0; g < emod.global_count; g++) {
                if (ny_str_eq(emod.globals[g].name, reloc->symbol_name)) {
                    const X86_Encoded_Global *eg = &emod.globals[g];
                    if (eg->kind == NY_GLOBAL_CONST) target_addr = rodata_base + eg->offset;
                    else if (eg->kind == NY_GLOBAL_DATA) target_addr = data_base + eg->offset;
                    else if (eg->kind == NY_GLOBAL_BSS) target_addr = bss_base + eg->offset;
                    break;
                }
            }
        }

        if (!target_addr) {
            target_addr = resolve_external_symbol(jit, sym_name);
            if (target_addr) is_external = true;
        }

        if (!target_addr) {
            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf), "unresolved symbol '%s' during JIT linking", sym_name);
            add_diagnostic(out_diags, out_diag_count, err_buf, 0, 0);
            reloc_success = false;
            break;
        }

        uint8_t *patch_site = text_base + reloc->code_offset;
        uint8_t *next_inst = patch_site + 4;
        int64_t disp64;

        if (is_external && reloc->kind == X86_FIXUP_CALL_REL32) {
            if (trampoline_offset + 12 > text_size + trampoline_area) {
                add_diagnostic(out_diags, out_diag_count, "JIT trampoline space exhausted", 0, 0);
                reloc_success = false;
                break;
            }
            uint8_t *tramp = text_base + trampoline_offset;
            uint64_t abs_addr = (uint64_t)(uintptr_t)target_addr + (uint64_t)reloc->addend;
            tramp[0] = 0x48;
            tramp[1] = 0xB8;
            for (size_t i = 0; i < 8; i++) {
                tramp[2 + i] = (uint8_t)((abs_addr >> (i * 8)) & 0xFF);
            }
            tramp[10] = 0xFF;
            tramp[11] = 0xE0;
            trampoline_offset += 12;
            disp64 = (int64_t)((intptr_t)tramp - (intptr_t)next_inst);
        } else {
            disp64 = (int64_t)((intptr_t)target_addr + reloc->addend - (intptr_t)next_inst);
        }

        if (disp64 < (int64_t)INT32_MIN || disp64 > (int64_t)INT32_MAX) {
            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf), "symbol '%s' address exceeds 32-bit relative displacement limit", sym_name);
            add_diagnostic(out_diags, out_diag_count, err_buf, 0, 0);
            reloc_success = false;
            break;
        }

        int32_t disp32 = (int32_t)disp64;
        patch_site[0] = (uint8_t)(disp32 & 0xFF);
        patch_site[1] = (uint8_t)((disp32 >> 8) & 0xFF);
        patch_site[2] = (uint8_t)((disp32 >> 16) & 0xFF);
        patch_site[3] = (uint8_t)((disp32 >> 24) & 0xFF);
    }

    if (!reloc_success) {
        free_page_allocation(base_ptr, total_alloc_size);
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }

    /* Finalize memory protection: .text -> RX, .rodata -> R, .data/.bss -> RW */
    if (text_aligned > 0) {
        if (!protect_pages(text_base, text_aligned, JIT_PROT_EXECUTE_READ)) {
            add_diagnostic(out_diags, out_diag_count, "failed to apply executable page protection", 0, 0);
            free_page_allocation(base_ptr, total_alloc_size);
            x86_encoded_mod_destroy(&emod);
            x86_mod_destroy(&xmod);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            return false;
        }
        flush_cache(text_base, text_aligned);
    }
    if (rodata_aligned > 0) {
        protect_pages(rodata_base, rodata_aligned, JIT_PROT_READONLY);
    }

    ny_buf_grow((void **)&jit->allocations, &jit->allocation_capacity, jit->allocation_count, sizeof(JIT_Page_Allocation));
    jit->allocations[jit->allocation_count].ptr = base_ptr;
    jit->allocations[jit->allocation_count].size = total_alloc_size;
    jit->allocation_count++;

    for (size_t i = 0; i < emod.function_count; i++) {
        const X86_Function_Code *fn = &emod.functions[i];
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)fn->name.len, fn->name.data);
        jit_add_export(jit, name_buf, text_base + fn->offset);
    }

    for (size_t g = 0; g < emod.global_count; g++) {
        const X86_Encoded_Global *eg = &emod.globals[g];
        char name_buf[128];
        snprintf(name_buf, sizeof(name_buf), "%.*s", (int)eg->name.len, eg->name.data);
        void *addr = NULL;
        if (eg->kind == NY_GLOBAL_CONST) addr = rodata_base + eg->offset;
        else if (eg->kind == NY_GLOBAL_DATA) addr = data_base + eg->offset;
        else if (eg->kind == NY_GLOBAL_BSS) addr = bss_base + eg->offset;
        if (addr) jit_add_export(jit, name_buf, addr);
    }

    x86_encoded_mod_destroy(&emod);
    x86_mod_destroy(&xmod);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
    return true;
}

void ny_jit_diagnostics_destroy(Nygen_Diagnostic *diags, size_t diag_count) {
    if (!diags || diag_count == 0) return;
    for (size_t i = 0; i < diag_count; i++) {
        if (diags[i].message) {
            ny_free((void *)diags[i].message, strlen(diags[i].message) + 1);
        }
    }
    ny_free(diags, diag_count * sizeof(Nygen_Diagnostic));
}

void *ny_jit_lookup(Ny_JIT_Engine *jit, const char *symbol_name) {
    if (!jit || !symbol_name) return NULL;
    for (size_t i = 0; i < jit->export_count; i++) {
        if (strcmp(jit->exports[i].name, symbol_name) == 0) {
            return jit->exports[i].addr;
        }
    }
    return NULL;
}

void ny_jit_destroy(Ny_JIT_Engine *jit) {
    if (!jit) return;
    for (size_t i = 0; i < jit->allocation_count; i++) {
        free_page_allocation(jit->allocations[i].ptr, jit->allocations[i].size);
    }
    if (jit->allocations) {
        ny_free(jit->allocations, jit->allocation_capacity * sizeof(JIT_Page_Allocation));
    }
    for (size_t i = 0; i < jit->export_count; i++) {
        if (jit->exports[i].name) {
            ny_free(jit->exports[i].name, strlen(jit->exports[i].name) + 1);
        }
    }
    if (jit->exports) {
        ny_free(jit->exports, jit->export_capacity * sizeof(JIT_Exported_Symbol));
    }
    ny_free(jit, sizeof(Ny_JIT_Engine));
}
