// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

static inline bool is_const_zero(const Ny_Function *fn, Ny_Operand op) {
    int64_t val = 0;
    if (ny_opt_get_operand_const_int(fn, op, &val) && val == 0) return true;
    if (op.kind == NY_OP_IMM_FLOAT && op.imm_float == 0.0) return true;
    return false;
}

static inline bool is_const_one(const Ny_Function *fn, Ny_Operand op) {
    int64_t val = 0;
    if (ny_opt_get_operand_const_int(fn, op, &val) && val == 1) return true;
    if (op.kind == NY_OP_IMM_FLOAT && op.imm_float == 1.0) return true;
    return false;
}

bool ny_canonicalize_instructions(Ny_Module *mod, Ny_Function *fn) {
    (void)mod;
    bool changed = false;

    for (size_t inst_idx = 0; inst_idx < fn->inst_count; inst_idx++) {
        Ny_Instruction *inst = &fn->instructions[inst_idx];
        if (inst->opcode == NY_OPCODE_NONE) continue;

        const Ny_Operand *operands = ny_function_get_operands(fn, inst);
        size_t op_cnt = inst->op_count;

        switch (inst->opcode) {
            case NY_OPCODE_CMP_GT_S:
                inst->opcode = NY_OPCODE_CMP_LT_S;
                if (op_cnt == 2) {
                    Ny_Operand tmp = operands[0];
                    ny_instruction_replace_operand(fn, inst->id, 0, operands[1]);
                    ny_instruction_replace_operand(fn, inst->id, 1, tmp);
                    changed = true;
                }
                break;
            case NY_OPCODE_CMP_GT_U:
                inst->opcode = NY_OPCODE_CMP_LT_U;
                if (op_cnt == 2) {
                    Ny_Operand tmp = operands[0];
                    ny_instruction_replace_operand(fn, inst->id, 0, operands[1]);
                    ny_instruction_replace_operand(fn, inst->id, 1, tmp);
                    changed = true;
                }
                break;
            case NY_OPCODE_CMP_GE_S:
                inst->opcode = NY_OPCODE_CMP_LE_S;
                if (op_cnt == 2) {
                    Ny_Operand tmp = operands[0];
                    ny_instruction_replace_operand(fn, inst->id, 0, operands[1]);
                    ny_instruction_replace_operand(fn, inst->id, 1, tmp);
                    changed = true;
                }
                break;
            case NY_OPCODE_CMP_GE_U:
                inst->opcode = NY_OPCODE_CMP_LE_U;
                if (op_cnt == 2) {
                    Ny_Operand tmp = operands[0];
                    ny_instruction_replace_operand(fn, inst->id, 0, operands[1]);
                    ny_instruction_replace_operand(fn, inst->id, 1, tmp);
                    changed = true;
                }
                break;
            case NY_OPCODE_FCMP_GT:
                inst->opcode = NY_OPCODE_FCMP_LT;
                if (op_cnt == 2) {
                    Ny_Operand tmp = operands[0];
                    ny_instruction_replace_operand(fn, inst->id, 0, operands[1]);
                    ny_instruction_replace_operand(fn, inst->id, 1, tmp);
                    changed = true;
                }
                break;
            case NY_OPCODE_FCMP_GE:
                inst->opcode = NY_OPCODE_FCMP_LE;
                if (op_cnt == 2) {
                    Ny_Operand tmp = operands[0];
                    ny_instruction_replace_operand(fn, inst->id, 0, operands[1]);
                    ny_instruction_replace_operand(fn, inst->id, 1, tmp);
                    changed = true;
                }
                break;
            default:
                break;
        }

        operands = ny_function_get_operands(fn, inst);
        op_cnt = inst->op_count;

        if (ny_opt_is_commutative((Ny_Opcode)inst->opcode) && op_cnt == 2) {
            Ny_Operand op0 = operands[0];
            Ny_Operand op1 = operands[1];
            bool should_swap = false;

            if ((op0.kind == NY_OP_IMM_INT || op0.kind == NY_OP_IMM_FLOAT) && op1.kind == NY_OP_VALUE) {
                should_swap = true;
            } else if (op0.kind == NY_OP_VALUE && op1.kind == NY_OP_VALUE) {
                int64_t dummy = 0;
                bool v0_const = ny_opt_get_operand_const_int(fn, op0, &dummy);
                bool v1_const = ny_opt_get_operand_const_int(fn, op1, &dummy);
                if (v0_const && !v1_const) {
                    should_swap = true;
                } else if (!v0_const && !v1_const && op0.val > op1.val) {
                    should_swap = true;
                }
            }

            if (should_swap) {
                ny_instruction_replace_operand(fn, inst->id, 0, op1);
                ny_instruction_replace_operand(fn, inst->id, 1, op0);
                operands = ny_function_get_operands(fn, inst);
                changed = true;
            }
        }

        if (inst->result != NY_INVALID_VALUE && op_cnt == 2) {
            Ny_Operand op0 = operands[0];
            Ny_Operand op1 = operands[1];

            switch (inst->opcode) {
                case NY_OPCODE_ADD:
                    if (op0.kind == NY_OP_VALUE && is_const_zero(fn, op1)) {
                        ny_function_replace_all_uses(fn, inst->result, op0.val);
                        ny_function_remove_instruction(fn, inst->id);
                        changed = true;
                        continue;
                    }
                    break;

                case NY_OPCODE_SUB:
                    if (op0.kind == NY_OP_VALUE && is_const_zero(fn, op1)) {
                        ny_function_replace_all_uses(fn, inst->result, op0.val);
                        ny_function_remove_instruction(fn, inst->id);
                        changed = true;
                        continue;
                    }
                    if (op0.kind == NY_OP_VALUE && op1.kind == NY_OP_VALUE && op0.val == op1.val) {
                        inst->opcode = NY_OPCODE_CONST;
                        ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(0));
                        inst->op_count = 1;
                        changed = true;
                        continue;
                    }
                    break;

                case NY_OPCODE_MUL:
                    if (op0.kind == NY_OP_VALUE && is_const_one(fn, op1)) {
                        ny_function_replace_all_uses(fn, inst->result, op0.val);
                        ny_function_remove_instruction(fn, inst->id);
                        changed = true;
                        continue;
                    }
                    break;

                case NY_OPCODE_OR:
                    if (op0.kind == NY_OP_VALUE && is_const_zero(fn, op1)) {
                        ny_function_replace_all_uses(fn, inst->result, op0.val);
                        ny_function_remove_instruction(fn, inst->id);
                        changed = true;
                        continue;
                    }
                    break;

                case NY_OPCODE_XOR:
                    if (op0.kind == NY_OP_VALUE && op1.kind == NY_OP_VALUE && op0.val == op1.val) {
                        inst->opcode = NY_OPCODE_CONST;
                        ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(0));
                        inst->op_count = 1;
                        changed = true;
                        continue;
                    }
                    break;

                default:
                    break;
            }
        }

        switch (inst->opcode) {
            case NY_OPCODE_CAST:
            case NY_OPCODE_BITCAST:
            case NY_OPCODE_SEXT:
            case NY_OPCODE_ZEXT:
            case NY_OPCODE_EXTEND:
            case NY_OPCODE_TRUNCATE:
                if (inst->result != NY_INVALID_VALUE && op_cnt == 1 && operands[0].kind == NY_OP_VALUE) {
                    Ny_Type_ID res_type = fn->values[inst->result].type;
                    Ny_Type_ID src_type = fn->values[operands[0].val].type;
                    if (res_type == src_type) {
                        ny_function_replace_all_uses(fn, inst->result, operands[0].val);
                        ny_function_remove_instruction(fn, inst->id);
                        changed = true;
                        continue;
                    }
                }
                break;
            default:
                break;
        }
    }

    return changed;
}

bool ny_canonicalize_cfg(Ny_Module *mod, Ny_Function *fn) {
    (void)mod;
    bool changed = false;

    for (size_t b_idx = 0; b_idx < fn->block_count; b_idx++) {
        Ny_Block *blk = &fn->blocks[b_idx];
        if (blk->first_inst == NY_INVALID_INST) continue;

        Ny_Inst_ID curr = blk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Instruction *inst = ny_function_get_instruction(fn, curr);
            if (inst == nullptr) break;
            Ny_Inst_ID next = inst->next;

            if (ny_opcode_is_terminator((Ny_Opcode)inst->opcode)) {
                Ny_Inst_ID dead = next;
                while (dead != NY_INVALID_INST) {
                    Ny_Inst_ID dead_next = fn->instructions[dead].next;
                    ny_function_remove_instruction(fn, dead);
                    dead = dead_next;
                    changed = true;
                }
                blk->last_inst = curr;
                inst->next = NY_INVALID_INST;
                break;
            }
            curr = next;
        }
    }

    for (size_t b_idx = 0; b_idx < fn->block_count; b_idx++) {
        Ny_Block *blk = &fn->blocks[b_idx];
        if (blk->last_inst == NY_INVALID_INST) continue;

        Ny_Instruction *term = ny_function_get_instruction(fn, blk->last_inst);
        if (term != nullptr && term->opcode == NY_OPCODE_BRANCH_IF) {
            const Ny_Operand *ops = ny_function_get_operands(fn, term);
            if (term->op_count == 3 && ops[1].kind == NY_OP_BLOCK && ops[2].kind == NY_OP_BLOCK && ops[1].blk == ops[2].blk) {
                Ny_Block_ID target = ops[1].blk;
                term->opcode = NY_OPCODE_BRANCH;
                ny_instruction_replace_operand(fn, term->id, 0, ny_operand_block(target));
                term->op_count = 1;

                if (blk->succ_count > 1) {
                    blk->succ_count = 1;
                    blk->succs[0] = target;
                }
                Ny_Block *tgt_blk = ny_function_get_block(fn, target);
                if (tgt_blk != nullptr) {
                    size_t pred_count = 0;
                    for (size_t i = 0; i < tgt_blk->pred_count; i++) {
                        if (tgt_blk->preds[i] == blk->id) {
                            pred_count++;
                            if (pred_count > 1) {
                                tgt_blk->preds[i] = tgt_blk->preds[tgt_blk->pred_count - 1];
                                tgt_blk->pred_count--;
                                i--;
                            }
                        }
                    }
                }
                changed = true;
            }
        }
    }

    for (size_t b_idx = 0; b_idx < fn->block_count; b_idx++) {
        Ny_Block *blk = &fn->blocks[b_idx];
        Ny_Inst_ID curr = blk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Instruction *inst = ny_function_get_instruction(fn, curr);
            if (inst == nullptr) break;
            Ny_Inst_ID next = inst->next;

            if (inst->opcode == NY_OPCODE_PHI && inst->result != NY_INVALID_VALUE) {
                const Ny_Operand *ops = ny_function_get_operands(fn, inst);
                if (inst->op_count >= 2) {
                    Ny_Value_ID first_val = ops[0].val;
                    bool all_same = true;
                    for (size_t i = 2; i < inst->op_count; i += 2) {
                        if (ops[i].kind == NY_OP_VALUE && ops[i].val != first_val) {
                            all_same = false;
                            break;
                        }
                    }
                    if (all_same) {
                        ny_function_replace_all_uses(fn, inst->result, first_val);
                        ny_function_remove_instruction(fn, inst->id);
                        changed = true;
                    }
                }
            }
            curr = next;
        }
    }

    size_t n_blocks = fn->block_count;
    if (fn->entry_block != NY_INVALID_BLOCK && n_blocks > 1) {
        bool *reachable = (bool *)ny_alloc(sizeof(bool) * n_blocks);
        memset(reachable, 0, sizeof(bool) * n_blocks);
        Ny_Block_ID *queue = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * n_blocks);
        size_t head = 0;
        size_t tail = 0;

        reachable[fn->entry_block] = true;
        queue[tail++] = fn->entry_block;

        while (head < tail) {
            Ny_Block_ID curr_id = queue[head++];
            const Ny_Block *curr_blk = ny_function_get_block(fn, curr_id);
            if (curr_blk == nullptr) continue;

            for (size_t s_idx = 0; s_idx < curr_blk->succ_count; s_idx++) {
                Ny_Block_ID succ_id = curr_blk->succs[s_idx];
                size_t s = (size_t)succ_id;
                if (s < n_blocks && !reachable[s]) {
                    reachable[s] = true;
                    queue[tail++] = succ_id;
                }
            }
        }

        for (size_t blk_idx = 0; blk_idx < n_blocks; blk_idx++) {
            Ny_Block_ID b_id = (Ny_Block_ID)blk_idx;
            if (b_id != fn->entry_block && !reachable[blk_idx]) {
                Ny_Block *b = &fn->blocks[blk_idx];
                if (b->inst_count > 0 || b->succ_count > 0 || b->pred_count > 0) {
                    ny_function_remove_block(fn, b_id);
                    changed = true;
                }
            }
        }

        ny_free(queue, sizeof(Ny_Block_ID) * n_blocks);
        ny_free(reachable, sizeof(bool) * n_blocks);
    }

    for (size_t blk_idx = 0; blk_idx < fn->block_count; blk_idx++) {
        Ny_Block_ID b_id = (Ny_Block_ID)blk_idx;
        Ny_Block *blk = ny_function_get_block(fn, b_id);
        if (blk == nullptr || blk->inst_count == 0) continue;

        if (blk->succ_count == 1) {
            Ny_Block_ID succ_id = blk->succs[0];
            if (succ_id != b_id) {
                if (ny_function_merge_blocks(fn, b_id, succ_id)) {
                    changed = true;
                }
            }
        }
    }

    return changed;
}

bool ny_canonicalize_function(Ny_Module *mod, Ny_Function *fn) {
    bool changed_any = false;
    const int max_passes = 4;

    for (int pass = 0; pass < max_passes; pass++) {
        bool c1 = ny_canonicalize_instructions(mod, fn);
        bool c2 = ny_canonicalize_cfg(mod, fn);
        if (!c1 && !c2) break;
        changed_any = true;
    }

    return changed_any;
}

bool ny_canonicalize_module(Ny_Module *mod) {
    bool changed = false;
    for (size_t i = 0; i < mod->function_count; i++) {
        if (ny_canonicalize_function(mod, &mod->functions[i])) {
            changed = true;
        }
    }
    return changed;
}
