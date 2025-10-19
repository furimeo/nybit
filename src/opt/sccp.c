// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    LAT_TOP = 0,
    LAT_CONSTANT,
    LAT_BOTTOM,
} Lat_Kind;

typedef struct {
    Lat_Kind kind;
    bool is_float;
    int64_t int_val;
    double flt_val;
} Lat_Val;

static inline Lat_Val lat_constant_int(int64_t val) {
    Lat_Val v;
    v.kind = LAT_CONSTANT;
    v.is_float = false;
    v.int_val = val;
    v.flt_val = 0.0;
    return v;
}

static inline Lat_Val lat_constant_float(double val) {
    Lat_Val v;
    v.kind = LAT_CONSTANT;
    v.is_float = true;
    v.int_val = 0;
    v.flt_val = val;
    return v;
}

static inline Lat_Val lat_bottom(void) {
    Lat_Val v;
    memset(&v, 0, sizeof(v));
    v.kind = LAT_BOTTOM;
    return v;
}

static inline Lat_Val lat_top(void) {
    Lat_Val v;
    memset(&v, 0, sizeof(v));
    v.kind = LAT_TOP;
    return v;
}

static inline Lat_Val lat_meet(Lat_Val a, Lat_Val b) {
    if (a.kind == LAT_TOP) return b;
    if (b.kind == LAT_TOP) return a;
    if (a.kind == LAT_BOTTOM || b.kind == LAT_BOTTOM) return lat_bottom();

    if (a.is_float != b.is_float) return lat_bottom();
    if (a.is_float) {
        return (a.flt_val == b.flt_val) ? a : lat_bottom();
    } else {
        return (a.int_val == b.int_val) ? a : lat_bottom();
    }
}

static inline bool lat_equal(Lat_Val a, Lat_Val b) {
    if (a.kind != b.kind) return false;
    if (a.kind != LAT_CONSTANT) return true;
    if (a.is_float != b.is_float) return false;
    return a.is_float ? (a.flt_val == b.flt_val) : (a.int_val == b.int_val);
}

typedef struct {
    Ny_Block_ID from;
    Ny_Block_ID to;
} SCCP_Edge;

static bool is_edge_exec(const SCCP_Edge *edges, size_t count, Ny_Block_ID from, Ny_Block_ID to) {
    for (size_t i = 0; i < count; i++) {
        if (edges[i].from == from && edges[i].to == to) return true;
    }
    return false;
}

static bool mark_edge_exec(SCCP_Edge **edges, size_t *count, size_t *cap, Ny_Block_ID from, Ny_Block_ID to) {
    if (is_edge_exec(*edges, *count, from, to)) return false;
    ny_buf_grow((void **)edges, cap, *count, sizeof(SCCP_Edge));
    (*edges)[*count].from = from;
    (*edges)[*count].to = to;
    (*count)++;
    return true;
}

static Lat_Val get_operand_lat(const Ny_Function *fn, const Lat_Val *lat, size_t num_values, Ny_Operand op) {
    (void)fn;
    if (op.kind == NY_OP_IMM_INT) return lat_constant_int(op.imm_int);
    if (op.kind == NY_OP_IMM_FLOAT) return lat_constant_float(op.imm_float);
    if (op.kind == NY_OP_VALUE) {
        size_t v_idx = (size_t)op.val;
        if (v_idx < num_values) return lat[v_idx];
    }
    return lat_bottom();
}

static Lat_Val eval_instruction(const Ny_Function *fn, const Ny_Instruction *inst, const Lat_Val *lat, size_t num_values,
                                const SCCP_Edge *edges, size_t edge_count) {
    if (inst->opcode == NY_OPCODE_CONST) {
        const Ny_Operand *ops = ny_function_get_operands(fn, inst);
        if (inst->op_count == 1) {
            if (ops[0].kind == NY_OP_IMM_INT) return lat_constant_int(ops[0].imm_int);
            if (ops[0].kind == NY_OP_IMM_FLOAT) return lat_constant_float(ops[0].imm_float);
        }
        return lat_bottom();
    }

    const Ny_Value *res_val = ny_function_get_value(fn, inst->result);
    if (res_val == nullptr) return lat_bottom();
    uint32_t width = ny_opt_get_type_bit_width(res_val->type);

    const Ny_Operand *ops = ny_function_get_operands(fn, inst);
    size_t op_cnt = inst->op_count;

    if (inst->opcode == NY_OPCODE_PHI) {
        Lat_Val result = lat_top();
        for (size_t i = 0; i < op_cnt; i += 2) {
            if (i + 1 < op_cnt && ops[i + 1].kind == NY_OP_BLOCK) {
                Ny_Block_ID pred_blk = ops[i + 1].blk;
                if (is_edge_exec(edges, edge_count, pred_blk, inst->block)) {
                    Lat_Val op_lat = get_operand_lat(fn, lat, num_values, ops[i]);
                    result = lat_meet(result, op_lat);
                }
            }
        }
        return result;
    }

    if (op_cnt == 1) {
        Lat_Val op0 = get_operand_lat(fn, lat, num_values, ops[0]);
        if (op0.kind == LAT_BOTTOM) return lat_bottom();
        if (op0.kind == LAT_TOP) return lat_top();

        if (!op0.is_float) {
            int64_t res = 0;
            if (ny_fold_int_unary((Ny_Opcode)inst->opcode, op0.int_val, width, &res)) {
                return lat_constant_int(res);
            }
        } else {
            double res = 0.0;
            if (ny_fold_float_unary((Ny_Opcode)inst->opcode, op0.flt_val, &res)) {
                return lat_constant_float(res);
            }
        }
    }

    if (op_cnt == 2) {
        Lat_Val op0 = get_operand_lat(fn, lat, num_values, ops[0]);
        Lat_Val op1 = get_operand_lat(fn, lat, num_values, ops[1]);

        if (op0.kind == LAT_BOTTOM || op1.kind == LAT_BOTTOM) return lat_bottom();
        if (op0.kind == LAT_TOP || op1.kind == LAT_TOP) return lat_top();

        if (!op0.is_float && !op1.is_float) {
            const Ny_Value *op0_val = (ops[0].kind == NY_OP_VALUE) ? ny_function_get_value(fn, ops[0].val) : nullptr;
            uint32_t cmp_w = (op0_val != nullptr) ? ny_opt_get_type_bit_width(op0_val->type) : width;

            int64_t res_bin = 0;
            if (ny_fold_int_binary((Ny_Opcode)inst->opcode, op0.int_val, op1.int_val, width, &res_bin)) {
                return lat_constant_int(res_bin);
            }
            int64_t res_cmp = 0;
            if (ny_fold_int_cmp((Ny_Opcode)inst->opcode, op0.int_val, op1.int_val, cmp_w, &res_cmp)) {
                return lat_constant_int(res_cmp);
            }
        } else if (op0.is_float && op1.is_float) {
            double res_flt = 0.0;
            if (ny_fold_float_binary((Ny_Opcode)inst->opcode, op0.flt_val, op1.flt_val, &res_flt)) {
                return lat_constant_float(res_flt);
            }
            int64_t res_fcmp = 0;
            if (ny_fold_float_cmp((Ny_Opcode)inst->opcode, op0.flt_val, op1.flt_val, &res_fcmp)) {
                return lat_constant_int(res_fcmp);
            }
        }
    }

    if (inst->opcode == NY_OPCODE_SELECT && op_cnt == 3) {
        Lat_Val cond_lat = get_operand_lat(fn, lat, num_values, ops[0]);
        if (cond_lat.kind == LAT_CONSTANT) {
            return (cond_lat.int_val != 0) ? get_operand_lat(fn, lat, num_values, ops[1])
                                           : get_operand_lat(fn, lat, num_values, ops[2]);
        }
        if (cond_lat.kind == LAT_BOTTOM) {
            Lat_Val t_lat = get_operand_lat(fn, lat, num_values, ops[1]);
            Lat_Val f_lat = get_operand_lat(fn, lat, num_values, ops[2]);
            return lat_meet(t_lat, f_lat);
        }
        return lat_top();
    }

    return lat_bottom();
}

static void eval_terminator(const Ny_Function *fn, Ny_Block_ID blk_id, const Lat_Val *lat, size_t num_values,
                            SCCP_Edge **edges, size_t *edge_count, size_t *edge_cap,
                            Ny_Block_ID **cfg_wl, size_t *cfg_count, size_t *cfg_cap,
                            bool *block_exec, size_t num_blocks) {
    const Ny_Block *blk = ny_function_get_block(fn, blk_id);
    if (blk == nullptr || blk->last_inst == NY_INVALID_INST) return;

    const Ny_Instruction *term = ny_function_get_instruction(fn, blk->last_inst);
    if (term == nullptr) return;

    if (term->opcode == NY_OPCODE_BRANCH) {
        const Ny_Operand *ops = ny_function_get_operands(fn, term);
        if (term->op_count == 1 && ops[0].kind == NY_OP_BLOCK) {
            Ny_Block_ID target = ops[0].blk;
            if (mark_edge_exec(edges, edge_count, edge_cap, blk_id, target)) {
                size_t t_idx = (size_t)target;
                if (t_idx < num_blocks) {
                    block_exec[t_idx] = true;
                    ny_buf_grow((void **)cfg_wl, cfg_cap, *cfg_count, sizeof(Ny_Block_ID));
                    (*cfg_wl)[(*cfg_count)++] = target;
                }
            }
        }
    } else if (term->opcode == NY_OPCODE_BRANCH_IF) {
        const Ny_Operand *ops = ny_function_get_operands(fn, term);
        if (term->op_count == 3 && ops[1].kind == NY_OP_BLOCK && ops[2].kind == NY_OP_BLOCK) {
            Lat_Val cond_lat = get_operand_lat(fn, lat, num_values, ops[0]);
            Ny_Block_ID true_blk = ops[1].blk;
            Ny_Block_ID false_blk = ops[2].blk;

            if (cond_lat.kind == LAT_CONSTANT) {
                Ny_Block_ID target = (cond_lat.int_val != 0) ? true_blk : false_blk;
                if (mark_edge_exec(edges, edge_count, edge_cap, blk_id, target)) {
                    size_t t_idx = (size_t)target;
                    if (t_idx < num_blocks) {
                        block_exec[t_idx] = true;
                        ny_buf_grow((void **)cfg_wl, cfg_cap, *cfg_count, sizeof(Ny_Block_ID));
                        (*cfg_wl)[(*cfg_count)++] = target;
                    }
                }
            } else if (cond_lat.kind == LAT_BOTTOM) {
                Ny_Block_ID targets[2] = { true_blk, false_blk };
                for (int i = 0; i < 2; i++) {
                    Ny_Block_ID target = targets[i];
                    if (mark_edge_exec(edges, edge_count, edge_cap, blk_id, target)) {
                        size_t t_idx = (size_t)target;
                        if (t_idx < num_blocks) {
                            block_exec[t_idx] = true;
                            ny_buf_grow((void **)cfg_wl, cfg_cap, *cfg_count, sizeof(Ny_Block_ID));
                            (*cfg_wl)[(*cfg_count)++] = target;
                        }
                    }
                }
            }
        }
    }
}

bool ny_opt_pass_sccp(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am) {
    (void)mod;
    size_t num_values = fn->val_count;
    size_t num_blocks = fn->block_count;
    if (num_blocks == 0 || fn->entry_block == NY_INVALID_BLOCK) return false;

    Ny_Use_Def local_ud;
    Ny_Use_Def *ud_ptr = nullptr;
    if (am != nullptr) {
        ud_ptr = ny_analysis_get_use_def(am);
    } else {
        ny_use_def_init(&local_ud, fn);
        ud_ptr = &local_ud;
    }

    Lat_Val *lat = (Lat_Val *)ny_alloc(sizeof(Lat_Val) * num_values);
    bool *block_exec = (bool *)ny_alloc(sizeof(bool) * num_blocks);
    memset(block_exec, 0, sizeof(bool) * num_blocks);

    SCCP_Edge *exec_edges = nullptr;
    size_t exec_edge_count = 0;
    size_t exec_edge_cap = 0;

    for (size_t i = 0; i < num_values; i++) {
        const Ny_Value *v = ny_function_get_value(fn, (Ny_Value_ID)i);
        if (v == nullptr) {
            lat[i] = lat_top();
            continue;
        }

        if (v->kind == NY_VAL_ARGUMENT) {
            lat[i] = lat_bottom();
        } else if (v->kind == NY_VAL_INSTRUCTION && v->def != NY_INVALID_INST) {
            const Ny_Instruction *inst = ny_function_get_instruction(fn, v->def);
            if (inst != nullptr && inst->opcode == NY_OPCODE_CONST) {
                const Ny_Operand *ops = ny_function_get_operands(fn, inst);
                if (inst->op_count == 1) {
                    if (ops[0].kind == NY_OP_IMM_INT) {
                        lat[i] = lat_constant_int(ops[0].imm_int);
                    } else if (ops[0].kind == NY_OP_IMM_FLOAT) {
                        lat[i] = lat_constant_float(ops[0].imm_float);
                    } else {
                        lat[i] = lat_bottom();
                    }
                } else {
                    lat[i] = lat_bottom();
                }
            } else {
                lat[i] = lat_top();
            }
        } else {
            lat[i] = lat_top();
        }
    }

    Ny_Block_ID *cfg_worklist = nullptr;
    size_t cfg_wl_count = 0;
    size_t cfg_wl_cap = 0;

    Ny_Value_ID *ssa_worklist = nullptr;
    size_t ssa_wl_count = 0;
    size_t ssa_wl_cap = 0;

    block_exec[fn->entry_block] = true;
    ny_buf_grow((void **)&cfg_worklist, &cfg_wl_cap, cfg_wl_count, sizeof(Ny_Block_ID));
    cfg_worklist[cfg_wl_count++] = fn->entry_block;

    while (cfg_wl_count > 0 || ssa_wl_count > 0) {
        if (cfg_wl_count > 0) {
            Ny_Block_ID blk_id = cfg_worklist[--cfg_wl_count];
            const Ny_Block *blk = ny_function_get_block(fn, blk_id);
            if (blk == nullptr) continue;

            Ny_Inst_ID curr = blk->first_inst;
            while (curr != NY_INVALID_INST) {
                const Ny_Instruction *inst = ny_function_get_instruction(fn, curr);
                if (inst == nullptr) break;

                if (inst->result != NY_INVALID_VALUE) {
                    size_t r_idx = (size_t)inst->result;
                    Lat_Val old_val = lat[r_idx];
                    Lat_Val new_val = eval_instruction(fn, inst, lat, num_values, exec_edges, exec_edge_count);
                    if (!lat_equal(new_val, old_val)) {
                        lat[r_idx] = new_val;
                        ny_buf_grow((void **)&ssa_worklist, &ssa_wl_cap, ssa_wl_count, sizeof(Ny_Value_ID));
                        ssa_worklist[ssa_wl_count++] = inst->result;
                    }
                }
                curr = inst->next;
            }

            eval_terminator(fn, blk_id, lat, num_values,
                            &exec_edges, &exec_edge_count, &exec_edge_cap,
                            &cfg_worklist, &cfg_wl_count, &cfg_wl_cap,
                            block_exec, num_blocks);
        } else if (ssa_wl_count > 0) {
            Ny_Value_ID v_id = ssa_worklist[--ssa_wl_count];
            size_t use_cnt = 0;
            const Ny_Use *uses = ny_use_def_get_uses(ud_ptr, v_id, &use_cnt);

            for (size_t u_i = 0; u_i < use_cnt; u_i++) {
                const Ny_Instruction *user_inst = ny_function_get_instruction(fn, uses[u_i].inst);
                if (user_inst == nullptr) continue;

                size_t b_idx = (size_t)user_inst->block;
                if (b_idx < num_blocks && block_exec[b_idx]) {
                    if (user_inst->result != NY_INVALID_VALUE) {
                        size_t r_idx = (size_t)user_inst->result;
                        Lat_Val old_val = lat[r_idx];
                        Lat_Val new_val = eval_instruction(fn, user_inst, lat, num_values, exec_edges, exec_edge_count);
                        if (!lat_equal(new_val, old_val)) {
                            lat[r_idx] = new_val;
                            ny_buf_grow((void **)&ssa_worklist, &ssa_wl_cap, ssa_wl_count, sizeof(Ny_Value_ID));
                            ssa_worklist[ssa_wl_count++] = user_inst->result;
                        }
                    }
                    if (ny_opcode_is_terminator((Ny_Opcode)user_inst->opcode)) {
                        eval_terminator(fn, user_inst->block, lat, num_values,
                                        &exec_edges, &exec_edge_count, &exec_edge_cap,
                                        &cfg_worklist, &cfg_wl_count, &cfg_wl_cap,
                                        block_exec, num_blocks);
                    }
                }
            }
        }
    }

    bool changed = false;
    bool cfg_changed = false;

    for (size_t i = 0; i < fn->inst_count; i++) {
        Ny_Instruction *inst = &fn->instructions[i];
        if (inst->opcode == NY_OPCODE_NONE || inst->opcode == NY_OPCODE_CONST || inst->result == NY_INVALID_VALUE) {
            continue;
        }
        size_t b_idx = (size_t)inst->block;
        if (b_idx >= num_blocks || !block_exec[b_idx]) continue;

        size_t r_idx = (size_t)inst->result;
        if (r_idx < num_values && lat[r_idx].kind == LAT_CONSTANT) {
            inst->opcode = NY_OPCODE_CONST;
            if (lat[r_idx].is_float) {
                ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_float(lat[r_idx].flt_val));
            } else {
                ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(lat[r_idx].int_val));
            }
            inst->op_count = 1;
            changed = true;
        }
    }

    for (size_t blk_idx = 0; blk_idx < num_blocks; blk_idx++) {
        if (!block_exec[blk_idx]) continue;
        Ny_Block_ID b_id = (Ny_Block_ID)blk_idx;
        Ny_Block *blk = ny_function_get_block(fn, b_id);
        if (blk == nullptr || blk->last_inst == NY_INVALID_INST) continue;

        Ny_Instruction *term = ny_function_get_instruction(fn, blk->last_inst);
        if (term != nullptr && term->opcode == NY_OPCODE_BRANCH_IF) {
            const Ny_Operand *ops = ny_function_get_operands(fn, term);
            if (term->op_count == 3 && ops[1].kind == NY_OP_BLOCK && ops[2].kind == NY_OP_BLOCK) {
                Lat_Val cond_lat = get_operand_lat(fn, lat, num_values, ops[0]);
                if (cond_lat.kind == LAT_CONSTANT) {
                    Ny_Block_ID taken_blk = (cond_lat.int_val != 0) ? ops[1].blk : ops[2].blk;
                    Ny_Block_ID dead_blk  = (cond_lat.int_val != 0) ? ops[2].blk : ops[1].blk;

                    term->opcode = NY_OPCODE_BRANCH;
                    ny_instruction_replace_operand(fn, term->id, 0, ny_operand_block(taken_blk));
                    term->op_count = 1;

                    Ny_Block *dead_blk_ptr = ny_function_get_block(fn, dead_blk);
                    if (dead_blk_ptr != nullptr) {
                        ny_block_remove_edge(blk, dead_blk_ptr);
                        ny_function_remove_phi_incoming(fn, dead_blk, b_id);
                    }

                    changed = true;
                    cfg_changed = true;
                }
            }
        }
    }

    if (exec_edges != nullptr) ny_free(exec_edges, sizeof(SCCP_Edge) * exec_edge_cap);
    if (cfg_worklist != nullptr) ny_free(cfg_worklist, sizeof(Ny_Block_ID) * cfg_wl_cap);
    if (ssa_worklist != nullptr) ny_free(ssa_worklist, sizeof(Ny_Value_ID) * ssa_wl_cap);
    ny_free(block_exec, sizeof(bool) * num_blocks);
    ny_free(lat, sizeof(Lat_Val) * num_values);

    if (am == nullptr) {
        ny_use_def_destroy(&local_ud);
    } else {
        if (cfg_changed) {
            ny_analysis_invalidate_cfg(am);
        } else if (changed) {
            ny_analysis_invalidate_use_def(am);
        }
    }

    return changed;
}
