// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nyjit/nyjit.h"
#include "nybit/support.h"

#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

typedef struct {
    void *ptr;
    size_t size;
} Nyjit_Page_Alloc;

typedef struct {
    char *name;
    void *addr;
} Nyjit_Export;

struct Nyjit_Module {
    Nyjit_Config config;
    Nyjit_Page_Alloc *allocations;
    size_t allocation_count;
    size_t allocation_capacity;
    Nyjit_Export *exports;
    size_t export_count;
    size_t export_capacity;
};

void nyjit_config_init(Nyjit_Config *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
}

Nyjit_Module *nyjit_create(const Nyjit_Config *config) {
    Nyjit_Module *jit = (Nyjit_Module *)ny_alloc_zero(sizeof(Nyjit_Module));
    if (config) {
        jit->config = *config;
    } else {
        nyjit_config_init(&jit->config);
    }
    return jit;
}

static void free_pages(void *ptr, size_t size) {
    if (!ptr || size == 0) return;
#if defined(_WIN32)
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    munmap(ptr, size);
#endif
}

static void *alloc_pages(size_t size) {
    if (size == 0) return nullptr;
#if defined(_WIN32)
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void *p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
#endif
}

typedef enum {
    NYJIT_PROT_RW,
    NYJIT_PROT_RO,
    NYJIT_PROT_RX,
} Nyjit_Protection;

static bool protect_pages(void *ptr, size_t size, Nyjit_Protection prot) {
    if (!ptr || size == 0) return true;
#if defined(_WIN32)
    DWORD old_prot;
    DWORD win_prot = PAGE_READWRITE;
    if (prot == NYJIT_PROT_RO) win_prot = PAGE_READONLY;
    else if (prot == NYJIT_PROT_RX) win_prot = PAGE_EXECUTE_READ;
    return VirtualProtect(ptr, size, win_prot, &old_prot) != 0;
#else
    int posix_prot = PROT_READ | PROT_WRITE;
    if (prot == NYJIT_PROT_RO) posix_prot = PROT_READ;
    else if (prot == NYJIT_PROT_RX) posix_prot = PROT_READ | PROT_EXEC;
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

static void add_export(Nyjit_Module *jit, const char *name, void *addr) {
    ny_buf_grow((void **)&jit->exports, &jit->export_capacity, jit->export_count, sizeof(Nyjit_Export));
    size_t len = strlen(name);
    char *dup = (char *)ny_alloc(len + 1);
    memcpy(dup, name, len + 1);
    jit->exports[jit->export_count].name = dup;
    jit->exports[jit->export_count].addr = addr;
    jit->export_count++;
}

static const void *resolve_external(Nyjit_Module *jit, const char *name) {
    for (size_t i = 0; i < jit->config.symbol_count; i++) {
        if (jit->config.symbols[i].name && strcmp(jit->config.symbols[i].name, name) == 0) {
            return jit->config.symbols[i].addr;
        }
    }
    if (jit->config.resolver) {
        return jit->config.resolver(name, jit->config.resolver_user_data);
    }
    return nullptr;
}

static size_t align_up(size_t val, size_t align) {
    if (align <= 1) return val;
    size_t rem = val % align;
    return rem == 0 ? val : val + (align - rem);
}

static bool is_internal_symbol(const Nygen_Encoded_Module *mod, const char *name) {
    for (size_t i = 0; i < mod->symbol_count; i++) {
        if (strcmp(mod->symbols[i].name, name) == 0) return true;
    }
    return false;
}

static const void *resolve_internal(const Nygen_Encoded_Module *mod, const char *name,
                                     uint8_t *text_base, uint8_t *rodata_base,
                                     uint8_t *data_base, uint8_t *bss_base) {
    for (size_t i = 0; i < mod->symbol_count; i++) {
        const Nygen_JIT_Symbol *sym = &mod->symbols[i];
        if (strcmp(sym->name, name) != 0) continue;
        switch (sym->kind) {
        case NYGEN_SYM_FUNCTION: return text_base + sym->offset;
        case NYGEN_SYM_RODATA:   return rodata_base + sym->offset;
        case NYGEN_SYM_DATA:     return data_base + sym->offset;
        case NYGEN_SYM_BSS:      return bss_base + sym->offset;
        }
    }
    return nullptr;
}

static void add_diag(Nygen_Diagnostic **out_diags, size_t *out_count, const char *msg) {
    if (!out_diags || !out_count) return;
    size_t old = *out_count;
    size_t new_count = old + 1;
    *out_diags = (Nygen_Diagnostic *)ny_realloc(*out_diags, old * sizeof(Nygen_Diagnostic), new_count * sizeof(Nygen_Diagnostic));
    size_t len = strlen(msg);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, msg, len + 1);
    (*out_diags)[old].message = copy;
    (*out_diags)[old].line = 0;
    (*out_diags)[old].col = 0;
    *out_count = new_count;
}

bool nyjit_link(Nyjit_Module *jit, const Nygen_Encoded_Module *mod,
                Nygen_Diagnostic **out_diags, size_t *out_diag_count) {
    if (!jit || !mod) return false;
    if (out_diags) *out_diags = nullptr;
    if (out_diag_count) *out_diag_count = 0;

    size_t page_size = 4096;
    size_t text_size = mod->text_size;
    size_t rodata_size = mod->rodata_size;
    size_t data_size = mod->data_size;
    size_t bss_size = mod->bss_size;

    size_t external_call_count = 0;
    bool has_aarch64_relocs = false;
    for (size_t r = 0; r < mod->reloc_count; r++) {
        Nygen_Reloc_Kind rk = mod->relocs[r].kind;
        if (rk == NYGEN_RELOC_CALL_REL32 &&
            !is_internal_symbol(mod, mod->relocs[r].symbol_name)) {
            external_call_count++;
        }
        if (rk == NYGEN_RELOC_AARCH64_CALL26 &&
            !is_internal_symbol(mod, mod->relocs[r].symbol_name)) {
            external_call_count++;
        }
        if (rk >= NYGEN_RELOC_AARCH64_CALL26) {
            has_aarch64_relocs = true;
        }
    }
    size_t trampoline_entry_size = has_aarch64_relocs ? 20 : 12;
    size_t trampoline_area = external_call_count * trampoline_entry_size;
    size_t text_aligned = align_up(text_size + trampoline_area, page_size);
    size_t rodata_aligned = align_up(rodata_size, page_size);
    size_t data_aligned = align_up(data_size + bss_size, page_size);

    size_t total_payload = text_aligned + rodata_aligned + data_aligned;
    size_t total_alloc_size = align_up(total_payload > 0 ? total_payload : page_size, page_size);

    uint8_t *base_ptr = (uint8_t *)alloc_pages(total_alloc_size);
    if (!base_ptr) {
        add_diag(out_diags, out_diag_count, "failed to allocate native memory for JIT execution");
        return false;
    }
    memset(base_ptr, 0, total_alloc_size);

    uint8_t *text_base = base_ptr;
    uint8_t *rodata_base = text_base + text_aligned;
    uint8_t *data_base = rodata_base + rodata_aligned;
    uint8_t *bss_base = data_base + data_size;

    if (text_size > 0) memcpy(text_base, mod->text, text_size);
    if (rodata_size > 0) memcpy(rodata_base, mod->rodata, rodata_size);
    if (data_size > 0) memcpy(data_base, mod->data, data_size);

    bool reloc_success = true;
    size_t trampoline_offset = text_size;

    for (size_t r = 0; r < mod->reloc_count; r++) {
        const Nygen_JIT_Reloc *reloc = &mod->relocs[r];
        if (reloc->code_offset + 4 > text_size) {
            add_diag(out_diags, out_diag_count, "JIT relocation offset out of bounds");
            reloc_success = false;
            break;
        }

        Nygen_Reloc_Kind rk = reloc->kind;
        bool is_x86 = (rk == NYGEN_RELOC_CALL_REL32 || rk == NYGEN_RELOC_GLOBAL_REL32);
        bool is_aarch64 = (rk == NYGEN_RELOC_AARCH64_CALL26 || rk == NYGEN_RELOC_AARCH64_ADRP ||
                           rk == NYGEN_RELOC_AARCH64_ADD_LO12 || rk == NYGEN_RELOC_AARCH64_LDST_LO12);
        if (!is_x86 && !is_aarch64) {
            add_diag(out_diags, out_diag_count, "unsupported relocation kind in JIT");
            reloc_success = false;
            break;
        }

        const char *sym_name = reloc->symbol_name;
        const void *target_addr = nullptr;
        bool is_external = false;

        target_addr = resolve_internal(mod, sym_name, text_base, rodata_base, data_base, bss_base);
        if (!target_addr) {
            target_addr = resolve_external(jit, sym_name);
            if (target_addr) is_external = true;
        }

        if (!target_addr) {
            char err_buf[256];
            snprintf(err_buf, sizeof(err_buf), "unresolved symbol '%s' during JIT linking", sym_name);
            add_diag(out_diags, out_diag_count, err_buf);
            reloc_success = false;
            break;
        }

        uint8_t *patch_site = text_base + reloc->code_offset;

        if (is_aarch64) {
            uint64_t S = (uint64_t)(uintptr_t)target_addr + (uint64_t)reloc->addend;
            uint64_t P = (uint64_t)(uintptr_t)patch_site;

            if (rk == NYGEN_RELOC_AARCH64_CALL26) {
                int64_t disp64 = (int64_t)(S - P);
                int64_t imm26 = disp64 >> 2;
                if (imm26 < -(1LL << 25) || imm26 >= (1LL << 25)) {
                    if (is_external) {
                        if (trampoline_offset + trampoline_entry_size > text_size + trampoline_area) {
                            add_diag(out_diags, out_diag_count, "JIT trampoline space exhausted");
                            reloc_success = false;
                            break;
                        }
                        uint8_t *tramp = text_base + trampoline_offset;
                        uint32_t ldr = 0x58000050u;
                        memcpy(tramp, &ldr, 4);
                        memcpy(tramp + 4, &S, 8);
                        uint32_t br = 0xD61F0200;
                        memcpy(tramp + 12, &br, 4);
                        memset(tramp + 16, 0, 4);
                        trampoline_offset += trampoline_entry_size;

                        int64_t tramp_disp = (int64_t)((uintptr_t)tramp - P);
                        int64_t tramp_imm26 = tramp_disp >> 2;
                        uint32_t word = (uint32_t)patch_site[0]
                            | ((uint32_t)patch_site[1] << 8)
                            | ((uint32_t)patch_site[2] << 16)
                            | ((uint32_t)patch_site[3] << 24);
                        word = (word & ~0x03FFFFFFu) | ((uint32_t)tramp_imm26 & 0x03FFFFFFu);
                        memcpy(patch_site, &word, 4);
                        continue;
                    }
                    char err_buf[256];
                    snprintf(err_buf, sizeof(err_buf), "symbol '%s' address exceeds AArch64 BL ±128MB range", sym_name);
                    add_diag(out_diags, out_diag_count, err_buf);
                    reloc_success = false;
                    break;
                }
                uint32_t word = (uint32_t)patch_site[0]
                    | ((uint32_t)patch_site[1] << 8)
                    | ((uint32_t)patch_site[2] << 16)
                    | ((uint32_t)patch_site[3] << 24);
                word = (word & ~0x03FFFFFFu) | ((uint32_t)imm26 & 0x03FFFFFFu);
                memcpy(patch_site, &word, 4);
            } else if (rk == NYGEN_RELOC_AARCH64_ADRP) {
                int64_t page_diff = (int64_t)((S & ~0xFFFULL) - (P & ~0xFFFULL));
                int64_t imm = page_diff >> 12;
                uint32_t word = (uint32_t)patch_site[0]
                    | ((uint32_t)patch_site[1] << 8)
                    | ((uint32_t)patch_site[2] << 16)
                    | ((uint32_t)patch_site[3] << 24);
                uint32_t immlo = (uint32_t)(imm & 0x3);
                uint32_t immhi = (uint32_t)((imm >> 2) & 0x7FFFF);
                word = (word & ~0x9000001Fu) | (immlo << 29) | (immhi << 5);
                memcpy(patch_site, &word, 4);
            } else if (rk == NYGEN_RELOC_AARCH64_ADD_LO12) {
                uint32_t imm12 = (uint32_t)(S & 0xFFF);
                uint32_t word = (uint32_t)patch_site[0]
                    | ((uint32_t)patch_site[1] << 8)
                    | ((uint32_t)patch_site[2] << 16)
                    | ((uint32_t)patch_site[3] << 24);
                word = (word & ~0x3FFC00u) | (imm12 << 10);
                memcpy(patch_site, &word, 4);
            } else if (rk == NYGEN_RELOC_AARCH64_LDST_LO12) {
                uint32_t word = (uint32_t)patch_site[0]
                    | ((uint32_t)patch_site[1] << 8)
                    | ((uint32_t)patch_site[2] << 16)
                    | ((uint32_t)patch_site[3] << 24);
                uint32_t scale = (word & 0x40000000u) ? 8 : 4;
                uint32_t imm12 = (uint32_t)((S & 0xFFF) / scale);
                word = (word & ~0x3FFC00u) | (imm12 << 10);
                memcpy(patch_site, &word, 4);
            }
        } else {
            uint8_t *next_inst = patch_site + 4;
            int64_t disp64;

            if (is_external && rk == NYGEN_RELOC_CALL_REL32) {
                if (trampoline_offset + trampoline_entry_size > text_size + trampoline_area) {
                    add_diag(out_diags, out_diag_count, "JIT trampoline space exhausted");
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
                trampoline_offset += trampoline_entry_size;
                disp64 = (int64_t)((intptr_t)tramp - (intptr_t)next_inst);
            } else {
                disp64 = (int64_t)((intptr_t)target_addr + reloc->addend - (intptr_t)next_inst);
            }

            if (disp64 < (int64_t)INT32_MIN || disp64 > (int64_t)INT32_MAX) {
                char err_buf[256];
                snprintf(err_buf, sizeof(err_buf), "symbol '%s' address exceeds 32-bit relative displacement limit", sym_name);
                add_diag(out_diags, out_diag_count, err_buf);
                reloc_success = false;
                break;
            }

            int32_t disp32 = (int32_t)disp64;
            patch_site[0] = (uint8_t)(disp32 & 0xFF);
            patch_site[1] = (uint8_t)((disp32 >> 8) & 0xFF);
            patch_site[2] = (uint8_t)((disp32 >> 16) & 0xFF);
            patch_site[3] = (uint8_t)((disp32 >> 24) & 0xFF);
        }
    }

    if (!reloc_success) {
        free_pages(base_ptr, total_alloc_size);
        return false;
    }

    if (text_aligned > 0) {
        if (!protect_pages(text_base, text_aligned, NYJIT_PROT_RX)) {
            add_diag(out_diags, out_diag_count, "failed to apply executable page protection");
            free_pages(base_ptr, total_alloc_size);
            return false;
        }
        flush_cache(text_base, text_aligned);
    }
    if (rodata_aligned > 0) {
        protect_pages(rodata_base, rodata_aligned, NYJIT_PROT_RO);
    }

    ny_buf_grow((void **)&jit->allocations, &jit->allocation_capacity, jit->allocation_count, sizeof(Nyjit_Page_Alloc));
    jit->allocations[jit->allocation_count].ptr = base_ptr;
    jit->allocations[jit->allocation_count].size = total_alloc_size;
    jit->allocation_count++;

    for (size_t i = 0; i < mod->symbol_count; i++) {
        const Nygen_JIT_Symbol *sym = &mod->symbols[i];
        void *addr = nullptr;
        switch (sym->kind) {
        case NYGEN_SYM_FUNCTION: addr = text_base + sym->offset; break;
        case NYGEN_SYM_RODATA:   addr = rodata_base + sym->offset; break;
        case NYGEN_SYM_DATA:     addr = data_base + sym->offset; break;
        case NYGEN_SYM_BSS:      addr = bss_base + sym->offset; break;
        }
        if (addr) add_export(jit, sym->name, addr);
    }

    return true;
}

void nyjit_diagnostics_destroy(Nygen_Diagnostic *diags, size_t diag_count) {
    if (!diags || diag_count == 0) return;
    for (size_t i = 0; i < diag_count; i++) {
        if (diags[i].message) {
            ny_free((void *)diags[i].message, strlen(diags[i].message) + 1);
        }
    }
    ny_free(diags, diag_count * sizeof(Nygen_Diagnostic));
}

void *nyjit_lookup(Nyjit_Module *jit, const char *symbol_name) {
    if (!jit || !symbol_name) return nullptr;
    for (size_t i = 0; i < jit->export_count; i++) {
        if (strcmp(jit->exports[i].name, symbol_name) == 0) {
            return jit->exports[i].addr;
        }
    }
    return nullptr;
}

void nyjit_destroy(Nyjit_Module *jit) {
    if (!jit) return;
    for (size_t i = 0; i < jit->allocation_count; i++) {
        free_pages(jit->allocations[i].ptr, jit->allocations[i].size);
    }
    if (jit->allocations) {
        ny_free(jit->allocations, jit->allocation_capacity * sizeof(Nyjit_Page_Alloc));
    }
    for (size_t i = 0; i < jit->export_count; i++) {
        if (jit->exports[i].name) {
            ny_free(jit->exports[i].name, strlen(jit->exports[i].name) + 1);
        }
    }
    if (jit->exports) {
        ny_free(jit->exports, jit->export_capacity * sizeof(Nyjit_Export));
    }
    ny_free(jit, sizeof(Nyjit_Module));
}
