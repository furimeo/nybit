// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

static inline void bitset_set(uint64_t *bits, size_t words_per_block, Ny_Block_ID blk, Ny_Value_ID val) {
    size_t b = (size_t)blk;
    size_t v = (size_t)val;
    size_t word_idx = b * words_per_block + (v / 64);
    bits[word_idx] |= ((uint64_t)1 << (v % 64));
}

static inline bool bitset_test(const uint64_t *bits, size_t words_per_block, Ny_Block_ID blk, Ny_Value_ID val) {
    size_t b = (size_t)blk;
    size_t v = (size_t)val;
    size_t word_idx = b * words_per_block + (v / 64);
    return (bits[word_idx] & ((uint64_t)1 << (v % 64))) != 0;
}

void ny_liveness_init(Ny_Liveness_Info *liv, const Ny_Function *fn) {
    memset(liv, 0, sizeof(*liv));
    size_t b_count = fn->block_count;
    size_t v_count = fn->val_count;

    if (b_count == 0) {
        return;
    }

    size_t words = (v_count + 63) / 64;
    if (words == 0) words = 1;

    size_t total_words = b_count * words;
    liv->num_blocks = b_count;
    liv->words_per_block = words;
    liv->total_words = total_words;

    liv->live_in = (uint64_t *)ny_alloc(sizeof(uint64_t) * total_words);
    liv->live_out = (uint64_t *)ny_alloc(sizeof(uint64_t) * total_words);
    memset(liv->live_in, 0, sizeof(uint64_t) * total_words);
    memset(liv->live_out, 0, sizeof(uint64_t) * total_words);

    uint64_t *use_gen = (uint64_t *)ny_alloc(sizeof(uint64_t) * total_words);
    uint64_t *def_kill = (uint64_t *)ny_alloc(sizeof(uint64_t) * total_words);
    uint64_t *phi_defs = (uint64_t *)ny_alloc(sizeof(uint64_t) * total_words);
    memset(use_gen, 0, sizeof(uint64_t) * total_words);
    memset(def_kill, 0, sizeof(uint64_t) * total_words);
    memset(phi_defs, 0, sizeof(uint64_t) * total_words);

    for (size_t b = 0; b < b_count; b++) {
        Ny_Block_ID b_id = (Ny_Block_ID)b;
        const Ny_Block *blk = ny_function_get_block(fn, b_id);
        if (blk == nullptr) continue;

        Ny_Inst_ID inst_id = blk->first_inst;
        while (inst_id != NY_INVALID_INST) {
            const Ny_Instruction *inst = ny_function_get_instruction(fn, inst_id);
            if (inst == nullptr || inst->opcode == NY_OPCODE_NONE) {
                if (inst != nullptr) inst_id = inst->next;
                else break;
                continue;
            }

            if (inst->opcode == NY_OPCODE_PHI) {
                if (inst->result != NY_INVALID_VALUE && (size_t)inst->result < v_count) {
                    bitset_set(phi_defs, words, b_id, inst->result);
                    bitset_set(def_kill, words, b_id, inst->result);
                }
            } else {
                const Ny_Operand *ops = ny_function_get_operands(fn, inst);
                for (size_t op_i = 0; op_i < inst->op_count; op_i++) {
                    if (ops[op_i].kind == NY_OP_VALUE && (size_t)ops[op_i].val < v_count) {
                        if (!bitset_test(def_kill, words, b_id, ops[op_i].val)) {
                            bitset_set(use_gen, words, b_id, ops[op_i].val);
                        }
                    }
                }
                if (inst->result != NY_INVALID_VALUE && (size_t)inst->result < v_count) {
                    bitset_set(def_kill, words, b_id, inst->result);
                }
            }
            inst_id = inst->next;
        }
    }

    uint64_t *cur_out = (uint64_t *)ny_alloc(sizeof(uint64_t) * words);

    bool changed = true;
    while (changed) {
        changed = false;
        for (ptrdiff_t b = (ptrdiff_t)b_count - 1; b >= 0; b--) {
            Ny_Block_ID b_id = (Ny_Block_ID)b;
            const Ny_Block *blk = ny_function_get_block(fn, b_id);
            if (blk == nullptr) continue;

            size_t b_offset = (size_t)b * words;
            memset(cur_out, 0, sizeof(uint64_t) * words);

            for (size_t s_idx = 0; s_idx < blk->succ_count; s_idx++) {
                Ny_Block_ID succ_id = blk->succs[s_idx];
                size_t s = (size_t)succ_id;
                if (s >= b_count) continue;
                size_t s_offset = s * words;

                for (size_t w = 0; w < words; w++) {
                    uint64_t in_val = liv->live_in[s_offset + w];
                    uint64_t pdef = phi_defs[s_offset + w];
                    cur_out[w] |= (in_val & ~pdef);
                }

                const Ny_Block *s_blk = ny_function_get_block(fn, succ_id);
                if (s_blk != nullptr) {
                    Ny_Inst_ID phi_id = s_blk->first_inst;
                    while (phi_id != NY_INVALID_INST) {
                        const Ny_Instruction *phi_inst = ny_function_get_instruction(fn, phi_id);
                        if (phi_inst == nullptr || phi_inst->opcode != NY_OPCODE_PHI) break;

                        const Ny_Operand *phi_ops = ny_function_get_operands(fn, phi_inst);
                        for (size_t k = 0; k < phi_inst->op_count; k += 2) {
                            if (k + 1 < phi_inst->op_count && phi_ops[k + 1].kind == NY_OP_BLOCK && phi_ops[k + 1].blk == b_id) {
                                if (phi_ops[k].kind == NY_OP_VALUE && (size_t)phi_ops[k].val < v_count) {
                                    size_t v = (size_t)phi_ops[k].val;
                                    cur_out[v / 64] |= ((uint64_t)1 << (v % 64));
                                }
                            }
                        }
                        phi_id = phi_inst->next;
                    }
                }
            }

            for (size_t w = 0; w < words; w++) {
                liv->live_out[b_offset + w] = cur_out[w];
            }

            for (size_t w = 0; w < words; w++) {
                uint64_t old_in = liv->live_in[b_offset + w];
                uint64_t kill = def_kill[b_offset + w];
                uint64_t gen = use_gen[b_offset + w];
                uint64_t new_in = gen | (cur_out[w] & ~kill);
                if (new_in != old_in) {
                    liv->live_in[b_offset + w] = new_in;
                    changed = true;
                }
            }
        }
    }

    ny_free(cur_out, sizeof(uint64_t) * words);
    ny_free(phi_defs, sizeof(uint64_t) * total_words);
    ny_free(def_kill, sizeof(uint64_t) * total_words);
    ny_free(use_gen, sizeof(uint64_t) * total_words);
}

void ny_liveness_destroy(Ny_Liveness_Info *liv) {
    if (liv->live_in != nullptr) {
        ny_free(liv->live_in, sizeof(uint64_t) * liv->total_words);
    }
    if (liv->live_out != nullptr) {
        ny_free(liv->live_out, sizeof(uint64_t) * liv->total_words);
    }
    memset(liv, 0, sizeof(*liv));
}

[[nodiscard]] bool ny_liveness_is_live_in(const Ny_Liveness_Info *liv, Ny_Block_ID blk, Ny_Value_ID val) {
    size_t b = (size_t)blk;
    size_t v = (size_t)val;
    if (b >= liv->num_blocks || liv->words_per_block == 0) return false;
    size_t word_idx = b * liv->words_per_block + (v / 64);
    if (word_idx >= liv->total_words) return false;
    return (liv->live_in[word_idx] & ((uint64_t)1 << (v % 64))) != 0;
}

[[nodiscard]] bool ny_liveness_is_live_out(const Ny_Liveness_Info *liv, Ny_Block_ID blk, Ny_Value_ID val) {
    size_t b = (size_t)blk;
    size_t v = (size_t)val;
    if (b >= liv->num_blocks || liv->words_per_block == 0) return false;
    size_t word_idx = b * liv->words_per_block + (v / 64);
    if (word_idx >= liv->total_words) return false;
    return (liv->live_out[word_idx] & ((uint64_t)1 << (v % 64))) != 0;
}

const uint64_t *ny_liveness_get_live_in_slice(const Ny_Liveness_Info *liv, Ny_Block_ID blk, size_t *out_words) {
    size_t b = (size_t)blk;
    if (b >= liv->num_blocks || liv->words_per_block == 0) {
        if (out_words != nullptr) *out_words = 0;
        return nullptr;
    }
    if (out_words != nullptr) *out_words = liv->words_per_block;
    return &liv->live_in[b * liv->words_per_block];
}

const uint64_t *ny_liveness_get_live_out_slice(const Ny_Liveness_Info *liv, Ny_Block_ID blk, size_t *out_words) {
    size_t b = (size_t)blk;
    if (b >= liv->num_blocks || liv->words_per_block == 0) {
        if (out_words != nullptr) *out_words = 0;
        return nullptr;
    }
    if (out_words != nullptr) *out_words = liv->words_per_block;
    return &liv->live_out[b * liv->words_per_block];
}
