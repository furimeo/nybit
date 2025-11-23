// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/regalloc.h"
#include "nybit/target_aarch64.h"
#include <string.h>
#include <stdio.h>

static inline void bitset_set(uint64_t *bits, size_t words_per_block, size_t blk, size_t val) {
    size_t idx = blk * words_per_block + (val / 64);
    bits[idx] |= ((uint64_t)1 << (val % 64));
}

static inline bool bitset_test(const uint64_t *bits, size_t words_per_block, size_t blk, size_t val) {
    size_t idx = blk * words_per_block + (val / 64);
    return (bits[idx] & ((uint64_t)1 << (val % 64))) != 0;
}

static const AArch64_Phys_Reg s_aapcs64_gpr_caller_saved[] = {
    AARCH64_X9, AARCH64_X10, AARCH64_X11, AARCH64_X12,
    AARCH64_X13, AARCH64_X14, AARCH64_X15, AARCH64_X16,
    AARCH64_X17, AARCH64_X18, AARCH64_X0, AARCH64_X1,
    AARCH64_X2, AARCH64_X3, AARCH64_X4, AARCH64_X5,
    AARCH64_X6, AARCH64_X7, AARCH64_X8
};

static const AArch64_Phys_Reg s_aapcs64_gpr_callee_saved[] = {
    AARCH64_X19, AARCH64_X20, AARCH64_X21, AARCH64_X22,
    AARCH64_X23, AARCH64_X24, AARCH64_X25, AARCH64_X26,
    AARCH64_X27, AARCH64_X28
};

static const AArch64_Phys_Reg s_aapcs64_fp_caller_saved[] = {
    AARCH64_V0, AARCH64_V1, AARCH64_V2, AARCH64_V3,
    AARCH64_V4, AARCH64_V5, AARCH64_V6, AARCH64_V7,
    AARCH64_V16, AARCH64_V17, AARCH64_V18, AARCH64_V19,
    AARCH64_V20, AARCH64_V21, AARCH64_V22, AARCH64_V23,
    AARCH64_V24, AARCH64_V25, AARCH64_V26, AARCH64_V27,
    AARCH64_V28, AARCH64_V29, AARCH64_V30, AARCH64_V31
};

static const AArch64_Phys_Reg s_aapcs64_fp_callee_saved[] = {
    AARCH64_V8, AARCH64_V9, AARCH64_V10, AARCH64_V11,
    AARCH64_V12, AARCH64_V13, AARCH64_V14, AARCH64_V15
};

static bool is_reg_available(uint64_t *free_mask, AArch64_Phys_Reg reg) {
    return (free_mask[reg / 64] & ((uint64_t)1 << (reg % 64))) != 0;
}

static void free_reg(uint64_t *free_mask, AArch64_Phys_Reg reg) {
    free_mask[reg / 64] |= ((uint64_t)1 << (reg % 64));
}

static void take_reg(uint64_t *free_mask, AArch64_Phys_Reg reg) {
    free_mask[reg / 64] &= ~((uint64_t)1 << (reg % 64));
}

static int compare_interval_start(const void *a, const void *b) {
    const Ny_Live_Interval *const *ia = (const Ny_Live_Interval *const *)a;
    const Ny_Live_Interval *const *ib = (const Ny_Live_Interval *const *)b;
    if ((*ia)->start_idx != (*ib)->start_idx) {
        return (*ia)->start_idx < (*ib)->start_idx ? -1 : 1;
    }
    return (*ia)->vreg < (*ib)->vreg ? -1 : 1;
}

bool aarch64_regalloc_run_impl(Ny_Machine_Function *fn, Ny_Target_ABI abi, Ny_RegAlloc_Result *out_res, Ny_Diagnostic_List *diags) {
    (void)abi;
    (void)diags;
    if (out_res) {
        memset(out_res, 0, sizeof(*out_res));
    }

    if (fn->vreg_count == 0) {
        return true;
    }

    size_t v_count = fn->vreg_count;
    size_t b_count = fn->block_count;

    Ny_Live_Interval *intervals = (Ny_Live_Interval *)ny_alloc_zero(v_count * sizeof(Ny_Live_Interval));
    for (size_t i = 0; i < v_count; i++) {
        intervals[i].vreg = (uint32_t)i;
        intervals[i].start_idx = UINT32_MAX;
        intervals[i].end_idx = 0;
        intervals[i].reg_class = fn->vreg_classes[i];
        intervals[i].is_spilled = false;
        intervals[i].crosses_call = false;
        intervals[i].assigned_phys = AARCH64_NO_REG;
        intervals[i].stack_slot = NY_INVALID_SLOT;
    }

    uint32_t *blk_start = (uint32_t *)ny_alloc(b_count * sizeof(uint32_t));
    uint32_t *blk_end = (uint32_t *)ny_alloc(b_count * sizeof(uint32_t));

    uint32_t current_inst_idx = 0;
    for (size_t b = 0; b < b_count; b++) {
        Ny_Machine_Block *blk = &fn->blocks[b];
        blk_start[b] = current_inst_idx;

        Ny_Inst_ID curr = blk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Machine_Instruction *inst = &fn->instructions[curr];
            current_inst_idx++;
            curr = inst->next;
        }
        blk_end[b] = current_inst_idx > 0 ? current_inst_idx - 1 : 0;
    }

    size_t words = (v_count + 63) / 64;
    if (words == 0) words = 1;
    size_t total_words = b_count * words;

    uint64_t *use_gen = (uint64_t *)ny_alloc_zero(total_words * sizeof(uint64_t));
    uint64_t *def_kill = (uint64_t *)ny_alloc_zero(total_words * sizeof(uint64_t));
    uint64_t *live_in = (uint64_t *)ny_alloc_zero(total_words * sizeof(uint64_t));
    uint64_t *live_out = (uint64_t *)ny_alloc_zero(total_words * sizeof(uint64_t));

    size_t call_capacity = 16;
    size_t call_count = 0;
    uint32_t *call_sites = (uint32_t *)ny_alloc(call_capacity * sizeof(uint32_t));

    current_inst_idx = 0;
    for (size_t b = 0; b < b_count; b++) {
        Ny_Machine_Block *blk = &fn->blocks[b];
        Ny_Inst_ID curr = blk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Machine_Instruction *inst = &fn->instructions[curr];
            if (inst->opcode == NY_MOPC_CALL) {
                if (call_count >= call_capacity) {
                    size_t new_cap = call_capacity * 2;
                    call_sites = (uint32_t *)ny_realloc(call_sites, call_capacity * sizeof(uint32_t), new_cap * sizeof(uint32_t));
                    call_capacity = new_cap;
                }
                call_sites[call_count++] = current_inst_idx;
            }

            Ny_Machine_Operand *ops = ny_mfunc_get_operands(fn, inst);

            for (size_t op_i = 0; op_i < inst->op_count; op_i++) {
                if (ops[op_i].kind == NY_MOP_KIND_REG && ops[op_i].reg.is_virtual) {
                    uint32_t v = ops[op_i].reg.id;
                    if (v < v_count) {
                        if (!bitset_test(def_kill, words, b, v)) {
                            bitset_set(use_gen, words, b, v);
                        }
                        if (intervals[v].start_idx > current_inst_idx) {
                            intervals[v].start_idx = current_inst_idx;
                        }
                        if (intervals[v].end_idx < current_inst_idx) {
                            intervals[v].end_idx = current_inst_idx;
                        }
                    }
                } else if (ops[op_i].kind == NY_MOP_KIND_MEM) {
                    if (ny_mreg_is_valid(ops[op_i].mem.base) && ops[op_i].mem.base.is_virtual) {
                        uint32_t v = ops[op_i].mem.base.id;
                        if (v < v_count) {
                            if (!bitset_test(def_kill, words, b, v)) {
                                bitset_set(use_gen, words, b, v);
                            }
                            if (intervals[v].start_idx > current_inst_idx) {
                                intervals[v].start_idx = current_inst_idx;
                            }
                            if (intervals[v].end_idx < current_inst_idx) {
                                intervals[v].end_idx = current_inst_idx;
                            }
                        }
                    }
                    if (ny_mreg_is_valid(ops[op_i].mem.index) && ops[op_i].mem.index.is_virtual) {
                        uint32_t v = ops[op_i].mem.index.id;
                        if (v < v_count) {
                            if (!bitset_test(def_kill, words, b, v)) {
                                bitset_set(use_gen, words, b, v);
                            }
                            if (intervals[v].start_idx > current_inst_idx) {
                                intervals[v].start_idx = current_inst_idx;
                            }
                            if (intervals[v].end_idx < current_inst_idx) {
                                intervals[v].end_idx = current_inst_idx;
                            }
                        }
                    }
                }
            }

            if (ny_mreg_is_valid(inst->def_reg) && inst->def_reg.is_virtual) {
                uint32_t v = inst->def_reg.id;
                if (v < v_count) {
                    bitset_set(def_kill, words, b, v);
                    if (intervals[v].start_idx > current_inst_idx) {
                        intervals[v].start_idx = current_inst_idx;
                    }
                    if (intervals[v].end_idx < current_inst_idx) {
                        intervals[v].end_idx = current_inst_idx;
                    }
                }
            }

            current_inst_idx++;
            curr = inst->next;
        }
    }

    for (size_t p = 0; p < fn->param_count; p++) {
        if (fn->param_regs[p].is_virtual) {
            uint32_t v = fn->param_regs[p].id;
            if (v < v_count) {
                intervals[v].start_idx = 0;
            }
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (ssize_t b = (ssize_t)b_count - 1; b >= 0; b--) {
            Ny_Machine_Block *blk = &fn->blocks[b];
            for (size_t s = 0; s < blk->succ_count; s++) {
                Ny_Block_ID succ = blk->succs[s];
                if (succ < b_count) {
                    for (size_t w = 0; w < words; w++) {
                        live_out[b * words + w] |= live_in[succ * words + w];
                    }
                }
            }

            for (size_t w = 0; w < words; w++) {
                uint64_t old_in = live_in[b * words + w];
                uint64_t new_in = use_gen[b * words + w] | (live_out[b * words + w] & ~def_kill[b * words + w]);
                if (new_in != old_in) {
                    live_in[b * words + w] = new_in;
                    changed = true;
                }
            }
        }
    }

    for (size_t b = 0; b < b_count; b++) {
        for (size_t v = 0; v < v_count; v++) {
            if (bitset_test(live_in, words, b, v)) {
                if (intervals[v].start_idx > blk_start[b]) {
                    intervals[v].start_idx = blk_start[b];
                }
                if (intervals[v].end_idx < blk_start[b]) {
                    intervals[v].end_idx = blk_start[b];
                }
            }
            if (bitset_test(live_out, words, b, v)) {
                if (intervals[v].end_idx < blk_end[b]) {
                    intervals[v].end_idx = blk_end[b];
                }
            }
        }
    }

    for (size_t c = 0; c < call_count; c++) {
        uint32_t call_idx = call_sites[c];
        for (size_t v = 0; v < v_count; v++) {
            if (intervals[v].start_idx <= call_idx && intervals[v].end_idx >= call_idx) {
                intervals[v].crosses_call = true;
            }
        }
    }
    ny_free(call_sites, call_capacity * sizeof(uint32_t));

    for (size_t v = 0; v < v_count; v++) {
        if (intervals[v].start_idx == UINT32_MAX) {
            intervals[v].start_idx = 0;
            intervals[v].end_idx = 0;
        }
    }

    ny_free(use_gen, total_words * sizeof(uint64_t));
    ny_free(def_kill, total_words * sizeof(uint64_t));
    ny_free(live_in, total_words * sizeof(uint64_t));
    ny_free(live_out, total_words * sizeof(uint64_t));
    ny_free(blk_start, b_count * sizeof(uint32_t));
    ny_free(blk_end, b_count * sizeof(uint32_t));

    const AArch64_Phys_Reg *caller_saved = s_aapcs64_gpr_caller_saved;
    size_t caller_saved_count = sizeof(s_aapcs64_gpr_caller_saved) / sizeof(s_aapcs64_gpr_caller_saved[0]);

    const AArch64_Phys_Reg *callee_saved = s_aapcs64_gpr_callee_saved;
    size_t callee_saved_count = sizeof(s_aapcs64_gpr_callee_saved) / sizeof(s_aapcs64_gpr_callee_saved[0]);

    const AArch64_Phys_Reg *fp_caller_saved = s_aapcs64_fp_caller_saved;
    size_t fp_caller_saved_count = sizeof(s_aapcs64_fp_caller_saved) / sizeof(s_aapcs64_fp_caller_saved[0]);

    const AArch64_Phys_Reg *fp_callee_saved = s_aapcs64_fp_callee_saved;
    size_t fp_callee_saved_count = sizeof(s_aapcs64_fp_callee_saved) / sizeof(s_aapcs64_fp_callee_saved[0]);

    uint64_t free_mask[2] = {0, 0};
    for (size_t i = 0; i < caller_saved_count; i++) free_reg(free_mask, caller_saved[i]);
    for (size_t i = 0; i < callee_saved_count; i++) free_reg(free_mask, callee_saved[i]);
    for (size_t i = 0; i < fp_caller_saved_count; i++) free_reg(free_mask, fp_caller_saved[i]);
    for (size_t i = 0; i < fp_callee_saved_count; i++) free_reg(free_mask, fp_callee_saved[i]);

    Ny_Live_Interval **sorted_intervals = (Ny_Live_Interval **)ny_alloc(v_count * sizeof(Ny_Live_Interval *));
    for (size_t i = 0; i < v_count; i++) {
        sorted_intervals[i] = &intervals[i];
    }
    qsort(sorted_intervals, v_count, sizeof(Ny_Live_Interval *), compare_interval_start);

    Ny_Live_Interval **active = (Ny_Live_Interval **)ny_alloc(v_count * sizeof(Ny_Live_Interval *));
    size_t active_count = 0;
    uint64_t used_callee_saved_mask[2] = {0, 0};
    size_t spilled_count = 0;

    for (size_t i = 0; i < v_count; i++) {
        Ny_Live_Interval *cur = sorted_intervals[i];

        size_t new_active_count = 0;
        for (size_t a = 0; a < active_count; a++) {
            if (active[a]->end_idx < cur->start_idx) {
                free_reg(free_mask, (AArch64_Phys_Reg)active[a]->assigned_phys);
            } else {
                active[new_active_count++] = active[a];
            }
        }
        active_count = new_active_count;

        AArch64_Phys_Reg chosen_reg = AARCH64_NO_REG;
        bool is_fp = (cur->reg_class == NY_REG_CLASS_FP32 || cur->reg_class == NY_REG_CLASS_FP64);

        if (is_fp) {
            if (!cur->crosses_call) {
                for (size_t c = 0; c < fp_caller_saved_count; c++) {
                    if (is_reg_available(free_mask, fp_caller_saved[c])) {
                        chosen_reg = fp_caller_saved[c];
                        break;
                    }
                }
            }
            if (chosen_reg == AARCH64_NO_REG) {
                for (size_t c = 0; c < fp_callee_saved_count; c++) {
                    if (is_reg_available(free_mask, fp_callee_saved[c])) {
                        chosen_reg = fp_callee_saved[c];
                        break;
                    }
                }
            }
            if (chosen_reg == AARCH64_NO_REG) {
                for (size_t c = 0; c < fp_caller_saved_count; c++) {
                    if (is_reg_available(free_mask, fp_caller_saved[c])) {
                        chosen_reg = fp_caller_saved[c];
                        break;
                    }
                }
            }
        } else if (cur->crosses_call) {
            for (size_t c = 0; c < callee_saved_count; c++) {
                if (is_reg_available(free_mask, callee_saved[c])) {
                    chosen_reg = callee_saved[c];
                    break;
                }
            }
            if (chosen_reg == AARCH64_NO_REG) {
                for (size_t c = 0; c < caller_saved_count; c++) {
                    if (is_reg_available(free_mask, caller_saved[c])) {
                        chosen_reg = caller_saved[c];
                        break;
                    }
                }
            }
        } else {
            for (size_t c = 0; c < caller_saved_count; c++) {
                if (is_reg_available(free_mask, caller_saved[c])) {
                    chosen_reg = caller_saved[c];
                    break;
                }
            }
            if (chosen_reg == AARCH64_NO_REG) {
                for (size_t c = 0; c < callee_saved_count; c++) {
                    if (is_reg_available(free_mask, callee_saved[c])) {
                        chosen_reg = callee_saved[c];
                        break;
                    }
                }
            }
        }

        if (chosen_reg != AARCH64_NO_REG) {
            cur->assigned_phys = (uint8_t)chosen_reg;
            take_reg(free_mask, chosen_reg);

            if (aarch64_abi_is_callee_saved(abi, chosen_reg)) {
                used_callee_saved_mask[chosen_reg / 64] |= ((uint64_t)1 << (chosen_reg % 64));
            }

            size_t insert_pos = active_count;
            while (insert_pos > 0 && active[insert_pos - 1]->end_idx > cur->end_idx) {
                active[insert_pos] = active[insert_pos - 1];
                insert_pos--;
            }
            active[insert_pos] = cur;
            active_count++;
        } else {
            size_t victim_idx = SIZE_MAX;
            for (ssize_t a = (ssize_t)active_count - 1; a >= 0; a--) {
                bool victim_is_fp = (active[a]->reg_class == NY_REG_CLASS_FP32 || active[a]->reg_class == NY_REG_CLASS_FP64);
                if (victim_is_fp == is_fp && active[a]->end_idx > cur->end_idx) {
                    victim_idx = (size_t)a;
                    break;
                }
            }

            if (victim_idx != SIZE_MAX) {
                Ny_Live_Interval *victim = active[victim_idx];
                cur->assigned_phys = victim->assigned_phys;
                victim->assigned_phys = AARCH64_NO_REG;
                victim->is_spilled = true;
                spilled_count++;

                for (size_t a = victim_idx; a + 1 < active_count; a++) {
                    active[a] = active[a + 1];
                }
                active_count--;

                size_t insert_pos = active_count;
                while (insert_pos > 0 && active[insert_pos - 1]->end_idx > cur->end_idx) {
                    active[insert_pos] = active[insert_pos - 1];
                    insert_pos--;
                }
                active[insert_pos] = cur;
                active_count++;
            } else {
                cur->is_spilled = true;
                cur->assigned_phys = AARCH64_NO_REG;
                spilled_count++;
            }
        }
    }

    ny_free(sorted_intervals, v_count * sizeof(Ny_Live_Interval *));
    ny_free(active, v_count * sizeof(Ny_Live_Interval *));

    for (size_t v = 0; v < v_count; v++) {
        if (intervals[v].is_spilled) {
            uint32_t sz = (intervals[v].reg_class == NY_REG_CLASS_GPR32 || intervals[v].reg_class == NY_REG_CLASS_FP32) ? 4 : 8;
            intervals[v].stack_slot = ny_mfunc_create_stack_slot(fn, sz, sz);
        }
    }

    for (size_t b = 0; b < fn->block_count; b++) {
        Ny_Inst_ID curr = fn->blocks[b].first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Inst_ID next_inst = fn->instructions[curr].next;

            uint16_t op_count = fn->instructions[curr].op_count;
            size_t fp_spill_idx = 0;
            size_t gpr_spill_idx = 0;
            Ny_Machine_Operand *ops = ny_mfunc_get_operands(fn, &fn->instructions[curr]);
            for (size_t op_i = 0; op_i < op_count; op_i++) {
                if (ops[op_i].kind == NY_MOP_KIND_REG && ops[op_i].reg.is_virtual) {
                    uint32_t v = ops[op_i].reg.id;
                    if (intervals[v].is_spilled) {
                        bool is_fp = (intervals[v].reg_class == NY_REG_CLASS_FP32 || intervals[v].reg_class == NY_REG_CLASS_FP64);
                        AArch64_Phys_Reg scratch_phys;
                        if (is_fp) {
                            scratch_phys = AARCH64_V0;
                            fp_spill_idx++;
                        } else {
                            scratch_phys = (gpr_spill_idx == 0) ? AARCH64_X9 : AARCH64_X10;
                            gpr_spill_idx++;
                        }
                        Ny_Machine_Reg scratch_reg = ny_mreg_preg(scratch_phys, (Ny_Reg_Class)intervals[v].reg_class);
                        Ny_Machine_Operand load_op;
                        memset(&load_op, 0, sizeof(load_op));
                        load_op.kind = NY_MOP_KIND_MEM;
                        load_op.mem.stack_slot = intervals[v].stack_slot;

                        ny_mfunc_insert_before(fn, curr, NY_MOPC_LOAD, scratch_reg, &load_op, 1, 0);

                        ops = ny_mfunc_get_operands(fn, &fn->instructions[curr]);
                        ops[op_i].reg = scratch_reg;
                    } else {
                        ops[op_i].reg = ny_mreg_preg(intervals[v].assigned_phys, (Ny_Reg_Class)intervals[v].reg_class);
                    }
                } else if (ops[op_i].kind == NY_MOP_KIND_MEM) {
                    if (ny_mreg_is_valid(ops[op_i].mem.base) && ops[op_i].mem.base.is_virtual) {
                        uint32_t v = ops[op_i].mem.base.id;
                        if (intervals[v].is_spilled) {
                            Ny_Machine_Reg scratch_reg = ny_mreg_preg(AARCH64_X9, (Ny_Reg_Class)intervals[v].reg_class);
                            Ny_Machine_Operand load_op;
                            memset(&load_op, 0, sizeof(load_op));
                            load_op.kind = NY_MOP_KIND_MEM;
                            load_op.mem.stack_slot = intervals[v].stack_slot;

                            ny_mfunc_insert_before(fn, curr, NY_MOPC_LOAD, scratch_reg, &load_op, 1, 0);

                            ops = ny_mfunc_get_operands(fn, &fn->instructions[curr]);
                            ops[op_i].mem.base = scratch_reg;
                        } else {
                            ops[op_i].mem.base = ny_mreg_preg(intervals[v].assigned_phys, (Ny_Reg_Class)intervals[v].reg_class);
                        }
                    }
                }
            }

            if (ny_mreg_is_valid(fn->instructions[curr].def_reg) && fn->instructions[curr].def_reg.is_virtual) {
                uint32_t v = fn->instructions[curr].def_reg.id;
                if (intervals[v].is_spilled) {
                    bool is_fp = (intervals[v].reg_class == NY_REG_CLASS_FP32 || intervals[v].reg_class == NY_REG_CLASS_FP64);
                    AArch64_Phys_Reg scratch_phys = is_fp ? AARCH64_V0 : AARCH64_X9;
                    Ny_Machine_Reg scratch_reg = ny_mreg_preg(scratch_phys, (Ny_Reg_Class)intervals[v].reg_class);
                    fn->instructions[curr].def_reg = scratch_reg;

                    Ny_Machine_Operand store_ops[2];
                    memset(&store_ops, 0, sizeof(store_ops));
                    store_ops[0].kind = NY_MOP_KIND_MEM;
                    store_ops[0].mem.stack_slot = intervals[v].stack_slot;
                    store_ops[1] = ny_mop_reg(scratch_reg);

                    ny_mfunc_insert_after(fn, curr, NY_MOPC_STORE, (Ny_Machine_Reg){0}, store_ops, 2, 0);
                } else {
                    fn->instructions[curr].def_reg = ny_mreg_preg(intervals[v].assigned_phys, (Ny_Reg_Class)intervals[v].reg_class);
                }
            }

            curr = next_inst;
        }
    }

    for (size_t p = 0; p < fn->param_count; p++) {
        if (fn->param_regs[p].is_virtual) {
            uint32_t v = fn->param_regs[p].id;
            if (intervals[v].is_spilled) {
                bool is_fp = (intervals[v].reg_class == NY_REG_CLASS_FP32 || intervals[v].reg_class == NY_REG_CLASS_FP64);
                AArch64_Phys_Reg scratch_phys = is_fp ? AARCH64_V0 : AARCH64_X9;
                fn->param_regs[p] = ny_mreg_preg(scratch_phys, (Ny_Reg_Class)intervals[v].reg_class);
            } else {
                fn->param_regs[p] = ny_mreg_preg(intervals[v].assigned_phys, (Ny_Reg_Class)intervals[v].reg_class);
            }
        }
    }

    ny_free(intervals, v_count * sizeof(Ny_Live_Interval));

    if (out_res) {
        out_res->used_callee_saved_mask = used_callee_saved_mask[0] | (used_callee_saved_mask[1] << 32);
        out_res->spilled_count = spilled_count;
    }

    return true;
}
