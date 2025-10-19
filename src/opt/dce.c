// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

static bool inst_has_side_effects(const Ny_Instruction *inst) {
    if (ny_opcode_is_terminator((Ny_Opcode)inst->opcode)) return true;
    if (inst->flags & NY_FLAG_VOLATILE) return true;

    switch (inst->opcode) {
        case NY_OPCODE_STORE:
        case NY_OPCODE_ATOMIC_STORE:
        case NY_OPCODE_ATOMIC_RMW:
        case NY_OPCODE_ATOMIC_CMPXCHG:
        case NY_OPCODE_FENCE:
        case NY_OPCODE_CALL:
        case NY_OPCODE_CALL_INDIRECT:
        case NY_OPCODE_TRAP:
            return true;
        default:
            return false;
    }
}

bool ny_opt_pass_dce(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am) {
    (void)mod;
    size_t num_values = fn->val_count;
    if (num_values == 0) return false;

    Ny_Use_Def local_ud;
    Ny_Use_Def *ud_ptr = nullptr;
    if (am != nullptr) {
        ud_ptr = ny_analysis_get_use_def(am);
    } else {
        ny_use_def_init(&local_ud, fn);
        ud_ptr = &local_ud;
    }

    uint32_t *active_use_count = (uint32_t *)ny_alloc(sizeof(uint32_t) * num_values);
    for (size_t i = 0; i < num_values; i++) {
        active_use_count[i] = ny_use_def_use_count(ud_ptr, (Ny_Value_ID)i);
    }

    Ny_Inst_ID *dead_worklist = nullptr;
    size_t dead_count = 0;
    size_t dead_cap = 0;

    for (size_t i = 0; i < fn->inst_count; i++) {
        const Ny_Instruction *inst = &fn->instructions[i];
        if (inst->opcode == NY_OPCODE_NONE) continue;
        if (inst->result == NY_INVALID_VALUE) continue;
        if (inst_has_side_effects(inst)) continue;

        size_t r_idx = (size_t)inst->result;
        if (r_idx < num_values && active_use_count[r_idx] == 0) {
            ny_buf_grow((void **)&dead_worklist, &dead_cap, dead_count, sizeof(Ny_Inst_ID));
            dead_worklist[dead_count++] = inst->id;
        }
    }

    bool changed = false;

    while (dead_count > 0) {
        Ny_Inst_ID inst_id = dead_worklist[--dead_count];
        const Ny_Instruction *inst = ny_function_get_instruction(fn, inst_id);
        if (inst == nullptr || inst->opcode == NY_OPCODE_NONE) continue;
        if (inst_has_side_effects(inst)) continue;

        const Ny_Operand *ops = ny_function_get_operands(fn, inst);
        for (size_t op_i = 0; op_i < inst->op_count; op_i++) {
            if (ops[op_i].kind == NY_OP_VALUE) {
                size_t v_idx = (size_t)ops[op_i].val;
                if (v_idx < num_values) {
                    if (active_use_count[v_idx] > 0) {
                        active_use_count[v_idx]--;
                        if (active_use_count[v_idx] == 0) {
                            Ny_Inst_ID def_id = ny_use_def_get_def(ud_ptr, ops[op_i].val);
                            if (def_id != NY_INVALID_INST) {
                                const Ny_Instruction *def_inst = ny_function_get_instruction(fn, def_id);
                                if (def_inst != nullptr && !inst_has_side_effects(def_inst)) {
                                    ny_buf_grow((void **)&dead_worklist, &dead_cap, dead_count, sizeof(Ny_Inst_ID));
                                    dead_worklist[dead_count++] = def_id;
                                }
                            }
                        }
                    }
                }
            }
        }

        ny_function_remove_instruction(fn, inst_id);
        changed = true;
    }

    if (dead_worklist != nullptr) ny_free(dead_worklist, sizeof(Ny_Inst_ID) * dead_cap);
    ny_free(active_use_count, sizeof(uint32_t) * num_values);

    if (am == nullptr) {
        ny_use_def_destroy(&local_ud);
    } else if (changed) {
        ny_analysis_invalidate_use_def(am);
    }

    return changed;
}
