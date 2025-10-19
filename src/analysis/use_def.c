// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

void ny_use_def_init(Ny_Use_Def *ud, const Ny_Function *fn) {
    memset(ud, 0, sizeof(*ud));
    size_t n = fn->val_count;
    ud->val_count = n;

    if (n == 0) {
        return;
    }

    ud->def_inst_count = n;
    ud->def_inst = (Ny_Inst_ID *)ny_alloc(sizeof(Ny_Inst_ID) * n);
    ud->use_start = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
    ud->use_count = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);

    for (size_t i = 0; i < n; i++) {
        ud->def_inst[i] = NY_INVALID_INST;
    }

    uint32_t *temp_counts = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
    memset(temp_counts, 0, sizeof(uint32_t) * n);

    for (size_t i = 0; i < fn->inst_count; i++) {
        const Ny_Instruction *inst = &fn->instructions[i];
        if (inst->opcode == NY_OPCODE_NONE) continue;

        if (inst->result != NY_INVALID_VALUE) {
            size_t r_idx = (size_t)inst->result;
            if (r_idx < n) {
                ud->def_inst[r_idx] = inst->id;
            }
        }

        const Ny_Operand *ops = ny_function_get_operands(fn, inst);
        for (size_t op_i = 0; op_i < inst->op_count; op_i++) {
            if (ops[op_i].kind == NY_OP_VALUE) {
                size_t v_idx = (size_t)ops[op_i].val;
                if (v_idx < n) {
                    temp_counts[v_idx]++;
                }
            }
        }
    }

    size_t total_uses = 0;
    for (size_t i = 0; i < n; i++) {
        ud->use_start[i] = (uint32_t)total_uses;
        ud->use_count[i] = temp_counts[i];
        total_uses += temp_counts[i];
    }

    ud->total_uses = total_uses;
    if (total_uses > 0) {
        ud->uses = (Ny_Use *)ny_alloc(sizeof(Ny_Use) * total_uses);
        uint32_t *cursor = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
        memset(cursor, 0, sizeof(uint32_t) * n);

        for (size_t i = 0; i < fn->inst_count; i++) {
            const Ny_Instruction *inst = &fn->instructions[i];
            if (inst->opcode == NY_OPCODE_NONE) continue;

            const Ny_Operand *ops = ny_function_get_operands(fn, inst);
            for (size_t op_i = 0; op_i < inst->op_count; op_i++) {
                if (ops[op_i].kind == NY_OP_VALUE) {
                    size_t v_idx = (size_t)ops[op_i].val;
                    if (v_idx < n) {
                        size_t pos = ud->use_start[v_idx] + cursor[v_idx];
                        ud->uses[pos].inst = inst->id;
                        ud->uses[pos].op_idx = (uint16_t)op_i;
                        cursor[v_idx]++;
                    }
                }
            }
        }

        ny_free(cursor, sizeof(uint32_t) * n);
    }

    ny_free(temp_counts, sizeof(uint32_t) * n);
}

void ny_use_def_destroy(Ny_Use_Def *ud) {
    if (ud->def_inst != nullptr) {
        ny_free(ud->def_inst, sizeof(Ny_Inst_ID) * ud->def_inst_count);
    }
    if (ud->use_start != nullptr) {
        ny_free(ud->use_start, sizeof(uint32_t) * ud->val_count);
    }
    if (ud->use_count != nullptr) {
        ny_free(ud->use_count, sizeof(uint32_t) * ud->val_count);
    }
    if (ud->uses != nullptr) {
        ny_free(ud->uses, sizeof(Ny_Use) * ud->total_uses);
    }
    memset(ud, 0, sizeof(*ud));
}

[[nodiscard]] Ny_Inst_ID ny_use_def_get_def(const Ny_Use_Def *ud, Ny_Value_ID val) {
    size_t idx = (size_t)val;
    return idx < ud->def_inst_count ? ud->def_inst[idx] : NY_INVALID_INST;
}

const Ny_Use *ny_use_def_get_uses(const Ny_Use_Def *ud, Ny_Value_ID val, size_t *out_count) {
    size_t idx = (size_t)val;
    if (idx >= ud->val_count || ud->use_count == nullptr) {
        if (out_count != nullptr) *out_count = 0;
        return nullptr;
    }
    uint32_t count = ud->use_count[idx];
    if (count == 0) {
        if (out_count != nullptr) *out_count = 0;
        return nullptr;
    }
    if (out_count != nullptr) *out_count = count;
    return &ud->uses[ud->use_start[idx]];
}

[[nodiscard]] uint32_t ny_use_def_use_count(const Ny_Use_Def *ud, Ny_Value_ID val) {
    size_t idx = (size_t)val;
    return idx < ud->val_count && ud->use_count != nullptr ? ud->use_count[idx] : 0;
}

[[nodiscard]] bool ny_use_def_has_uses(const Ny_Use_Def *ud, Ny_Value_ID val) {
    return ny_use_def_use_count(ud, val) > 0;
}

[[nodiscard]] bool ny_use_def_has_single_use(const Ny_Use_Def *ud, Ny_Value_ID val) {
    return ny_use_def_use_count(ud, val) == 1;
}
