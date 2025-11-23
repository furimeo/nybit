// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>

#define R_X86_64_64 1
#define R_X86_64_GLOB_DAT 6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE 8

#define R_AARCH64_ABS64 257
#define R_AARCH64_GLOB_DAT 1025
#define R_AARCH64_JUMP_SLOT 1026
#define R_AARCH64_RELATIVE 1027

#define EM_X86_64 62
#define EM_AARCH64 183

static void write_word64(uint8_t *ptr, uint64_t val) {
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)((val >> 8) & 0xFF);
    ptr[2] = (uint8_t)((val >> 16) & 0xFF);
    ptr[3] = (uint8_t)((val >> 24) & 0xFF);
    ptr[4] = (uint8_t)((val >> 32) & 0xFF);
    ptr[5] = (uint8_t)((val >> 40) & 0xFF);
    ptr[6] = (uint8_t)((val >> 48) & 0xFF);
    ptr[7] = (uint8_t)((val >> 56) & 0xFF);
}

static void write_disp32(uint8_t *ptr, uint32_t val) {
    ptr[0] = (uint8_t)(val & 0xFF);
    ptr[1] = (uint8_t)((val >> 8) & 0xFF);
    ptr[2] = (uint8_t)((val >> 16) & 0xFF);
    ptr[3] = (uint8_t)((val >> 24) & 0xFF);
}

static bool push_rela_dyn(uint8_t *rela_dyn_data, size_t capacity, size_t index, uint64_t r_offset, uint64_t r_info, int64_t r_addend) {
    size_t off = index * 24;
    if (off + 24 > capacity) return false;
    memcpy(rela_dyn_data + off, &r_offset, 8);
    memcpy(rela_dyn_data + off + 8, &r_info, 8);
    memcpy(rela_dyn_data + off + 16, &r_addend, 8);
    return true;
}

bool nylink_apply_relocations_internal(Nylink_Context *ctx) {
    if (!ctx) return false;
    if (!ctx->is_laid_out) {
        nylink_diag_add(ctx, "relocation error: layout must be performed before relocations", nullptr, nullptr);
        return false;
    }

    bool success = true;
    size_t rela_dyn_idx = 0;

    uint32_t *import_dynsym_idx = nullptr;
    if (ctx->import_count > 0) {
        import_dynsym_idx = (uint32_t *)ny_alloc_zero(ctx->import_count * sizeof(uint32_t));
        for (size_t i = 0; i < ctx->import_count; i++) {
            const char *name = ctx->symbols[ctx->import_sym_ids[i]].name;
            for (size_t d = 1; d < ctx->dynsym_count; d++) {
                if (strcmp(ctx->symbols[ctx->dynsym_sym_ids[d]].name, name) == 0) {
                    import_dynsym_idx[i] = (uint32_t)d;
                    break;
                }
            }
        }
    }

    if (ctx->uses_dynamic) {
        if (ctx->rela_dyn_file_size > 0 && !ctx->rela_dyn_data) {
            ctx->rela_dyn_data_capacity = ctx->rela_dyn_file_size;
            ctx->rela_dyn_data = (uint8_t *)ny_alloc_zero(ctx->rela_dyn_data_capacity);
        }
        if (ctx->got_file_size > 0 && !ctx->got_data) {
            ctx->got_data_capacity = ctx->got_file_size;
            ctx->got_data = (uint8_t *)ny_alloc_zero(ctx->got_data_capacity);
        }
        if (ctx->rela_plt_file_size > 0 && !ctx->rela_plt_data) {
            ctx->rela_plt_data_capacity = ctx->rela_plt_file_size;
            ctx->rela_plt_data = (uint8_t *)ny_alloc_zero(ctx->rela_plt_data_capacity);
        }
        if (ctx->got_plt_file_size > 0 && !ctx->got_plt_data) {
            ctx->got_plt_data_capacity = ctx->got_plt_file_size;
            ctx->got_plt_data = (uint8_t *)ny_alloc_zero(ctx->got_plt_data_capacity);

            uint64_t *got_plt_words = (uint64_t *)ctx->got_plt_data;
            got_plt_words[0] = ctx->dynamic_va;
            got_plt_words[1] = 0;
            got_plt_words[2] = 0;
        }
        if (ctx->plt_file_size > 0 && !ctx->plt_data) {
            ctx->plt_data_capacity = ctx->plt_file_size;
            ctx->plt_data = (uint8_t *)ny_alloc_zero(ctx->plt_data_capacity);

            if (ctx->machine == EM_AARCH64) {
                uint8_t *p0 = ctx->plt_data;
                memset(p0, 0, 32);
            } else {
                uint8_t *p0 = ctx->plt_data;
                int32_t disp1 = (int32_t)((int64_t)(ctx->got_plt_va + 8) - (int64_t)(ctx->plt_va + 6));
                int32_t disp2 = (int32_t)((int64_t)(ctx->got_plt_va + 16) - (int64_t)(ctx->plt_va + 12));

                p0[0] = 0xFF; p0[1] = 0x35;
                memcpy(&p0[2], &disp1, 4);
                p0[6] = 0xFF; p0[7] = 0x25;
                memcpy(&p0[8], &disp2, 4);
                p0[12] = 0x0F; p0[13] = 0x1F; p0[14] = 0x44; p0[15] = 0x00;
            }
        }

        for (size_t i = 0; i < ctx->import_count; i++) {
            uint32_t dyn_idx = import_dynsym_idx[i];

            uint32_t plt_idx = ctx->import_plt_idx[i];
            if (plt_idx != UINT32_MAX && ctx->plt_data && ctx->got_plt_data && ctx->rela_plt_data) {
                if (ctx->machine == EM_AARCH64) {
                    uint64_t entry_va = ctx->plt_va + 32 + (uint64_t)plt_idx * 16;
                    uint64_t slot_va = ctx->got_plt_va + 24 + (uint64_t)plt_idx * 8;

                    ((uint64_t *)ctx->got_plt_data)[3 + plt_idx] = entry_va;

                    uint8_t *pe = ctx->plt_data + 32 + (size_t)plt_idx * 16;
                    int64_t disp = (int64_t)slot_va - (int64_t)entry_va;
                    int64_t page_diff = (disp & ~0xFFFULL) >> 12;
                    uint32_t immlo = (uint32_t)(page_diff & 0x3);
                    uint32_t immhi = (uint32_t)((page_diff >> 2) & 0x7FFFF);

                    uint32_t adrp = 0x90000011u | (immlo << 29) | (immhi << 5);
                    memcpy(pe, &adrp, 4);

                    uint32_t lo12 = (uint32_t)(slot_va & 0xFFF) / 8;
                    uint32_t ldr = 0xF9400211u | (lo12 << 10);
                    memcpy(pe + 4, &ldr, 4);

                    uint32_t br = 0xD61F0220;
                    memcpy(pe + 8, &br, 4);

                    memset(pe + 12, 0, 4);

                    size_t rela_off = (size_t)plt_idx * 24;
                    uint64_t r_offset = slot_va;
                    uint64_t r_info = ((uint64_t)dyn_idx << 32) | R_AARCH64_JUMP_SLOT;
                    int64_t zero = 0;
                    if (rela_off + 24 > ctx->rela_plt_data_capacity) {
                        nylink_diag_add(ctx, "internal error: .rela.plt index overflow", nullptr, nullptr);
                        if (import_dynsym_idx) {
                            ny_free(import_dynsym_idx, ctx->import_count * sizeof(uint32_t));
                        }
                        return false;
                    }
                    memcpy(ctx->rela_plt_data + rela_off, &r_offset, 8);
                    memcpy(ctx->rela_plt_data + rela_off + 8, &r_info, 8);
                    memcpy(ctx->rela_plt_data + rela_off + 16, &zero, 8);
                } else {
                    uint64_t entry_va = ctx->plt_va + 16 + (uint64_t)plt_idx * 16;
                    uint64_t slot_va = ctx->got_plt_va + 24 + (uint64_t)plt_idx * 8;

                    ((uint64_t *)ctx->got_plt_data)[3 + plt_idx] = entry_va + 6;

                    uint8_t *pe = ctx->plt_data + 16 + (size_t)plt_idx * 16;
                    int32_t jmp_disp = (int32_t)((int64_t)slot_va - (int64_t)(entry_va + 6));
                    int32_t plt0_disp = (int32_t)((int64_t)ctx->plt_va - (int64_t)(entry_va + 16));

                    pe[0] = 0xFF; pe[1] = 0x25;
                    memcpy(&pe[2], &jmp_disp, 4);
                    pe[6] = 0x68;
                    uint32_t reloc_index = plt_idx;
                    memcpy(&pe[7], &reloc_index, 4);
                    pe[11] = 0xE9;
                    memcpy(&pe[12], &plt0_disp, 4);

                    size_t rela_off = (size_t)plt_idx * 24;
                    uint64_t r_offset = slot_va;
                    uint64_t r_info = ((uint64_t)dyn_idx << 32) | R_X86_64_JUMP_SLOT;
                    int64_t zero = 0;
                    if (rela_off + 24 > ctx->rela_plt_data_capacity) {
                        nylink_diag_add(ctx, "internal error: .rela.plt index overflow", nullptr, nullptr);
                        if (import_dynsym_idx) {
                            ny_free(import_dynsym_idx, ctx->import_count * sizeof(uint32_t));
                        }
                        return false;
                    }
                    memcpy(ctx->rela_plt_data + rela_off, &r_offset, 8);
                    memcpy(ctx->rela_plt_data + rela_off + 8, &r_info, 8);
                    memcpy(ctx->rela_plt_data + rela_off + 16, &zero, 8);
                }
            }

            uint32_t got_idx = ctx->import_got_idx[i];
            if (got_idx != UINT32_MAX && ctx->rela_dyn_data) {
                uint64_t slot_va = ctx->got_va + (uint64_t)got_idx * 8;
                uint64_t r_info;
                if (ctx->machine == EM_AARCH64) {
                    r_info = ((uint64_t)dyn_idx << 32) | R_AARCH64_GLOB_DAT;
                } else {
                    r_info = ((uint64_t)dyn_idx << 32) | R_X86_64_GLOB_DAT;
                }
                if (!push_rela_dyn(ctx->rela_dyn_data, ctx->rela_dyn_data_capacity, rela_dyn_idx++, slot_va, r_info, 0)) {
                    nylink_diag_add(ctx, "internal error: .rela.dyn index overflow", nullptr, nullptr);
                    if (import_dynsym_idx) {
                        ny_free(import_dynsym_idx, ctx->import_count * sizeof(uint32_t));
                    }
                    return false;
                }
            }
        }
    }

    for (size_t r = 0; r < ctx->relocation_count; r++) {
        const Nylink_Relocation *reloc = &ctx->relocations[r];

        if (reloc->sec_id >= ctx->section_count) {
            nylink_diag_add(ctx, "invalid relocation: target section index out of bounds", nullptr, nullptr);
            success = false;
            continue;
        }

        if (reloc->type == NYLINK_RELOC_NONE) {
            continue;
        }

        const Nylink_Section *in_sec = &ctx->sections[reloc->sec_id];
        uint32_t out_idx = ctx->sec_layouts[reloc->sec_id].out_sec_idx;
        Nylink_Output_Section *out_sec = &ctx->out_sections[out_idx];

        uint64_t target_sec_offset = ctx->sec_layouts[reloc->sec_id].offset_in_out_sec + reloc->offset;
        uint64_t place_va = ctx->sec_layouts[reloc->sec_id].va + reloc->offset;

        uint8_t plan = ctx->reloc_plans ? ctx->reloc_plans[r] : NYLINK_PLAN_STATIC;
        uint32_t slot = ctx->reloc_plan_slots ? ctx->reloc_plan_slots[r] : UINT32_MAX;

        if (reloc->sym_id >= ctx->symbol_count) {
            nylink_diag_add(ctx, "invalid relocation: symbol index out of bounds", in_sec->name, nullptr);
            success = false;
            continue;
        }

        const Nylink_Symbol *sym = &ctx->symbols[reloc->sym_id];
        const Nylink_Resolved_Sym *state = &ctx->resolved_symbols[reloc->sym_id];
        const char *sym_name = sym->name ? sym->name : "<unnamed>";

        if (reloc->type == NYLINK_RELOC_X86_64_64) {
            if (sym->is_pe_refptr_redirect) continue;

            if (target_sec_offset + 8 > out_sec->data_capacity || target_sec_offset + 8 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t val = 0;
            if (plan == NYLINK_PLAN_DYN_ABS64) {
                val = (uint64_t)reloc->addend;
                uint64_t r_info = ((uint64_t)import_dynsym_idx[slot] << 32) | R_X86_64_64;
                if (!push_rela_dyn(ctx->rela_dyn_data, ctx->rela_dyn_data_capacity, rela_dyn_idx++, place_va, r_info, reloc->addend)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "dynamic relocation overflow: symbol '%s'", sym_name);
                    nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                    success = false;
                    continue;
                }
            } else {
                val = state->final_va + (uint64_t)reloc->addend;
                if (plan == NYLINK_PLAN_RELATIVE) {
                    if (!push_rela_dyn(ctx->rela_dyn_data, ctx->rela_dyn_data_capacity, rela_dyn_idx++, place_va, R_X86_64_RELATIVE, (int64_t)val)) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "dynamic relocation overflow: symbol '%s'", sym_name);
                        nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                        success = false;
                        continue;
                    }
                } else if (ctx->target_format == NYLINK_TARGET_PE) {
                    /* Base relocation RVA: place_va - image_base */
                    uint32_t fixup_rva = (uint32_t)(place_va - ctx->image_base);
                    ny_buf_grow((void **)&ctx->pe_base_relocs, &ctx->pe_base_reloc_capacity, ctx->pe_base_reloc_count, sizeof(uint32_t));
                    ctx->pe_base_relocs[ctx->pe_base_reloc_count++] = fixup_rva;
                }
            }
            write_word64(out_sec->data + target_sec_offset, val);
        } else if (reloc->type == NYLINK_RELOC_X86_64_PC32 || reloc->type == NYLINK_RELOC_X86_64_PLT32) {
            if (target_sec_offset + 4 > out_sec->data_capacity || target_sec_offset + 4 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t target_va;
            if (plan == NYLINK_PLAN_PLT_CALL) {
                target_va = ctx->plt_va + 16 + (uint64_t)slot * 16;
            } else if (plan == NYLINK_PLAN_GOT_LOAD) {
                target_va = ctx->got_va + (uint64_t)slot * 8;
            } else if (state->is_defined && !state->is_dynamic) {
                target_va = state->final_va;
            } else if (state->is_dynamic && state->final_va != 0) {
                /* PE import thunk: layout pre-assigned a thunk VA; resolve statically */
                target_va = state->final_va;
            } else if (!state->is_defined && sym->binding == NYLINK_SYM_WEAK) {
                target_va = 0;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation against unresolved symbol: %s", sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            int64_t val = (int64_t)(target_va + (uint64_t)reloc->addend) - (int64_t)place_va;
            if (val < (int64_t)INT32_MIN || val > (int64_t)INT32_MAX) {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation overflow: PC32 value %lld out of 32-bit range for symbol '%s'", (long long)val, sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            write_disp32(out_sec->data + target_sec_offset, (uint32_t)(int32_t)val);
        } else if (reloc->type == NYLINK_RELOC_X86_64_GOTPCREL) {
            if (target_sec_offset + 4 > out_sec->data_capacity || target_sec_offset + 4 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t target_va = 0;
            if (plan == NYLINK_PLAN_GOT_LOAD) {
                target_va = ctx->got_va + (uint64_t)slot * 8;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "internal error: unexpected plan for GOTPCREL relocation against '%s'", sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            int64_t val = (int64_t)(target_va + (uint64_t)reloc->addend) - (int64_t)place_va;
            if (val < (int64_t)INT32_MIN || val > (int64_t)INT32_MAX) {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation overflow: GOTPCREL value %lld out of 32-bit range for symbol '%s'", (long long)val, sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            write_disp32(out_sec->data + target_sec_offset, (uint32_t)(int32_t)val);
        } else if (reloc->type == NYLINK_RELOC_AARCH64_ABS64) {
            if (target_sec_offset + 8 > out_sec->data_capacity || target_sec_offset + 8 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t val = 0;
            if (plan == NYLINK_PLAN_DYN_ABS64) {
                val = (uint64_t)reloc->addend;
                uint64_t r_info = ((uint64_t)import_dynsym_idx[slot] << 32) | R_AARCH64_ABS64;
                if (!push_rela_dyn(ctx->rela_dyn_data, ctx->rela_dyn_data_capacity, rela_dyn_idx++, place_va, r_info, reloc->addend)) {
                    char msg[256];
                    snprintf(msg, sizeof(msg), "dynamic relocation overflow: symbol '%s'", sym_name);
                    nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                    success = false;
                    continue;
                }
            } else {
                val = state->final_va + (uint64_t)reloc->addend;
                if (plan == NYLINK_PLAN_RELATIVE) {
                    if (!push_rela_dyn(ctx->rela_dyn_data, ctx->rela_dyn_data_capacity, rela_dyn_idx++, place_va, R_AARCH64_RELATIVE, (int64_t)val)) {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "dynamic relocation overflow: symbol '%s'", sym_name);
                        nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                        success = false;
                        continue;
                    }
                }
            }
            write_word64(out_sec->data + target_sec_offset, val);
        } else if (reloc->type == NYLINK_RELOC_AARCH64_CALL26 ||
                   reloc->type == NYLINK_RELOC_AARCH64_JUMP26) {
            if (target_sec_offset + 4 > out_sec->data_capacity || target_sec_offset + 4 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t target_va;
            if (plan == NYLINK_PLAN_PLT_CALL) {
                target_va = ctx->plt_va + 32 + (uint64_t)slot * 16;
            } else if (state->is_defined && !state->is_dynamic) {
                target_va = state->final_va;
            } else if (!state->is_defined && sym->binding == NYLINK_SYM_WEAK) {
                target_va = 0;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation against unresolved symbol: %s", sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            int64_t val = (int64_t)(target_va + (uint64_t)reloc->addend) - (int64_t)place_va;
            int64_t imm26 = val >> 2;
            if (imm26 < -(1LL << 25) || imm26 >= (1LL << 25)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation overflow: CALL26/JUMP26 offset %lld out of ±128MB range for symbol '%s'", (long long)val, sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint32_t word = (uint32_t)out_sec->data[target_sec_offset]
                | ((uint32_t)out_sec->data[target_sec_offset + 1] << 8)
                | ((uint32_t)out_sec->data[target_sec_offset + 2] << 16)
                | ((uint32_t)out_sec->data[target_sec_offset + 3] << 24);
            word = (word & ~0x03FFFFFFu) | ((uint32_t)imm26 & 0x03FFFFFFu);
            write_disp32(out_sec->data + target_sec_offset, word);
        } else if (reloc->type == NYLINK_RELOC_AARCH64_ADR_PREL_PG_HI21) {
            if (target_sec_offset + 4 > out_sec->data_capacity || target_sec_offset + 4 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t target_va;
            if (plan == NYLINK_PLAN_GOT_LOAD) {
                target_va = ctx->got_va + (uint64_t)slot * 8;
            } else if (state->is_defined && !state->is_dynamic) {
                target_va = state->final_va;
            } else if (!state->is_defined && sym->binding == NYLINK_SYM_WEAK) {
                target_va = 0;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation against unresolved symbol: %s", sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t S = target_va + (uint64_t)reloc->addend;
            uint64_t P = place_va;
            int64_t page_diff = (int64_t)((S & ~0xFFFULL) - (P & ~0xFFFULL));
            int64_t imm = page_diff >> 12;
            if (imm < -(1LL << 20) || imm >= (1LL << 20)) {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation overflow: ADRP page offset %lld out of ±4GB range for symbol '%s'", (long long)page_diff, sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint32_t word = (uint32_t)out_sec->data[target_sec_offset]
                | ((uint32_t)out_sec->data[target_sec_offset + 1] << 8)
                | ((uint32_t)out_sec->data[target_sec_offset + 2] << 16)
                | ((uint32_t)out_sec->data[target_sec_offset + 3] << 24);
            uint32_t immlo = (uint32_t)imm & 0x3;
            uint32_t immhi = (uint32_t)((imm >> 2) & 0x7FFFF);
            word = (word & ~0x9000001Fu) | (immlo << 29) | (immhi << 5);
            write_disp32(out_sec->data + target_sec_offset, word);
        } else if (reloc->type == NYLINK_RELOC_AARCH64_ADD_ABS_LO12_NC) {
            if (target_sec_offset + 4 > out_sec->data_capacity || target_sec_offset + 4 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t target_va;
            if (plan == NYLINK_PLAN_GOT_LOAD) {
                target_va = ctx->got_va + (uint64_t)slot * 8;
            } else if (state->is_defined && !state->is_dynamic) {
                target_va = state->final_va;
            } else if (!state->is_defined && sym->binding == NYLINK_SYM_WEAK) {
                target_va = 0;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation against unresolved symbol: %s", sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t S = target_va + (uint64_t)reloc->addend;
            uint32_t imm12 = (uint32_t)(S & 0xFFF);

            uint32_t word = (uint32_t)out_sec->data[target_sec_offset]
                | ((uint32_t)out_sec->data[target_sec_offset + 1] << 8)
                | ((uint32_t)out_sec->data[target_sec_offset + 2] << 16)
                | ((uint32_t)out_sec->data[target_sec_offset + 3] << 24);
            word = (word & ~0x3FFC00u) | (imm12 << 10);
            write_disp32(out_sec->data + target_sec_offset, word);
        } else if (reloc->type == NYLINK_RELOC_AARCH64_LDST64_ABS_LO12_NC ||
                   reloc->type == NYLINK_RELOC_AARCH64_LDST32_ABS_LO12_NC) {
            if (target_sec_offset + 4 > out_sec->data_capacity || target_sec_offset + 4 > out_sec->file_size) {
                nylink_diag_add(ctx, "relocation write out of bounds", in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t target_va;
            if (plan == NYLINK_PLAN_GOT_LOAD) {
                target_va = ctx->got_va + (uint64_t)slot * 8;
            } else if (state->is_defined && !state->is_dynamic) {
                target_va = state->final_va;
            } else if (!state->is_defined && sym->binding == NYLINK_SYM_WEAK) {
                target_va = 0;
            } else {
                char msg[256];
                snprintf(msg, sizeof(msg), "relocation against unresolved symbol: %s", sym_name);
                nylink_diag_add(ctx, msg, in_sec->name, sym_name);
                success = false;
                continue;
            }

            uint64_t S = target_va + (uint64_t)reloc->addend;
            uint32_t scale = (reloc->type == NYLINK_RELOC_AARCH64_LDST64_ABS_LO12_NC) ? 8 : 4;
            uint32_t imm12 = (uint32_t)((S & 0xFFF) / scale);

            uint32_t word = (uint32_t)out_sec->data[target_sec_offset]
                | ((uint32_t)out_sec->data[target_sec_offset + 1] << 8)
                | ((uint32_t)out_sec->data[target_sec_offset + 2] << 16)
                | ((uint32_t)out_sec->data[target_sec_offset + 3] << 24);
            word = (word & ~0x3FFC00u) | (imm12 << 10);
            write_disp32(out_sec->data + target_sec_offset, word);
        } else {
            char msg[256];
            snprintf(msg, sizeof(msg), "unsupported relocation type: %d", (int)reloc->type);
            nylink_diag_add(ctx, msg, in_sec->name, sym_name);
            success = false;
        }
    }

    if (import_dynsym_idx) {
        ny_free(import_dynsym_idx, ctx->import_count * sizeof(uint32_t));
    }

    if (success) {
        ctx->relocations_applied = true;
    }
    return success;
}
