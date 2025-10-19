// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

static bool get_copy_source(const Ny_Function *fn, const Ny_Instruction *inst, Ny_Value_ID *out_src) {
    if (inst->result == NY_INVALID_VALUE) return false;
    const Ny_Operand *ops = ny_function_get_operands(fn, inst);
    size_t op_cnt = inst->op_count;
    const Ny_Value *res_val = ny_function_get_value(fn, inst->result);
    if (res_val == nullptr) return false;

    if (inst->opcode == NY_OPCODE_PHI && op_cnt >= 2) {
        Ny_Value_ID first_val = NY_INVALID_VALUE;
        for (size_t i = 0; i < op_cnt; i += 2) {
            if (ops[i].kind == NY_OP_VALUE) {
                Ny_Value_ID val = ops[i].val;
                if (val == inst->result) continue;
                if (first_val == NY_INVALID_VALUE) {
                    first_val = val;
                } else if (first_val != val) {
                    return false;
                }
            } else {
                return false;
            }
        }
        if (first_val != NY_INVALID_VALUE) {
            if (out_src) *out_src = first_val;
            return true;
        }
    }

    if (inst->opcode == NY_OPCODE_SELECT && op_cnt == 3) {
        if (ops[1].kind == NY_OP_VALUE && ops[2].kind == NY_OP_VALUE && ops[1].val == ops[2].val) {
            if (out_src) *out_src = ops[1].val;
            return true;
        }
    }

    switch (inst->opcode) {
        case NY_OPCODE_BITCAST:
        case NY_OPCODE_CAST:
            if (op_cnt >= 1 && ops[0].kind == NY_OP_VALUE) {
                const Ny_Value *src_v = ny_function_get_value(fn, ops[0].val);
                if (src_v != nullptr && src_v->type == res_val->type) {
                    if (out_src) *out_src = ops[0].val;
                    return true;
                }
            }
            break;
        case NY_OPCODE_TRUNCATE:
        case NY_OPCODE_SEXT:
        case NY_OPCODE_ZEXT:
            if (op_cnt >= 1 && ops[0].kind == NY_OP_VALUE) {
                const Ny_Value *src_v = ny_function_get_value(fn, ops[0].val);
                if (src_v != nullptr && ny_opt_get_type_bit_width(src_v->type) == ny_opt_get_type_bit_width(res_val->type)) {
                    if (out_src) *out_src = ops[0].val;
                    return true;
                }
            }
            break;
        default:
            break;
    }

    return false;
}

static Ny_Value_ID resolve_copy(const Ny_Value_ID *copy_map, size_t num_values, Ny_Value_ID v) {
    Ny_Value_ID curr = v;
    int visited = 0;
    const int max_depth = 64;

    while (visited < max_depth) {
        size_t c_idx = (size_t)curr;
        if (c_idx >= num_values) break;
        Ny_Value_ID next = copy_map[c_idx];
        if (next == NY_INVALID_VALUE || next == curr) break;
        curr = next;
        visited++;
    }
    return curr;
}

bool ny_opt_pass_copy_prop(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am) {
    (void)mod;
    size_t num_values = fn->val_count;
    if (num_values == 0) return false;

    Ny_Value_ID *copy_map = (Ny_Value_ID *)ny_alloc(sizeof(Ny_Value_ID) * num_values);
    for (size_t i = 0; i < num_values; i++) {
        copy_map[i] = NY_INVALID_VALUE;
    }

    for (size_t i = 0; i < fn->inst_count; i++) {
        const Ny_Instruction *inst = &fn->instructions[i];
        if (inst->opcode == NY_OPCODE_NONE) continue;
        Ny_Value_ID src = NY_INVALID_VALUE;
        if (get_copy_source(fn, inst, &src)) {
            copy_map[inst->result] = src;
        }
    }

    for (size_t i = 0; i < num_values; i++) {
        if (copy_map[i] != NY_INVALID_VALUE) {
            copy_map[i] = resolve_copy(copy_map, num_values, copy_map[i]);
        }
    }

    bool changed = false;

    for (size_t i = 0; i < fn->inst_count; i++) {
        Ny_Instruction *inst = &fn->instructions[i];
        if (inst->opcode == NY_OPCODE_NONE) continue;

        size_t r_idx = (size_t)inst->result;
        if (r_idx < num_values && copy_map[r_idx] != NY_INVALID_VALUE) {
            Ny_Value_ID target = copy_map[r_idx];
            ny_function_replace_all_uses(fn, inst->result, target);
            ny_function_remove_instruction(fn, inst->id);
            changed = true;
            continue;
        }

        const Ny_Operand *ops = ny_function_get_operands(fn, inst);
        for (size_t op_i = 0; op_i < inst->op_count; op_i++) {
            if (ops[op_i].kind == NY_OP_VALUE) {
                size_t v_idx = (size_t)ops[op_i].val;
                if (v_idx < num_values && copy_map[v_idx] != NY_INVALID_VALUE) {
                    ny_instruction_replace_operand(fn, inst->id, op_i, ny_operand_value(copy_map[v_idx]));
                    changed = true;
                }
            }
        }
    }

    ny_free(copy_map, sizeof(Ny_Value_ID) * num_values);

    if (changed && am != nullptr) {
        ny_analysis_invalidate_use_def(am);
    }

    return changed;
}
