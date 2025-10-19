// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

bool ny_opt_pass_cfg_simplify(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am) {
    (void)mod;
    bool changed_any = false;
    const int max_iters = 4;

    for (int iter = 0; iter < max_iters; iter++) {
        bool changed = false;

        for (size_t blk_idx = 0; blk_idx < fn->block_count; blk_idx++) {
            Ny_Block_ID b_id = (Ny_Block_ID)blk_idx;
            Ny_Block *blk = ny_function_get_block(fn, b_id);
            if (blk == nullptr || blk->last_inst == NY_INVALID_INST) continue;

            Ny_Instruction *term = ny_function_get_instruction(fn, blk->last_inst);
            if (term == nullptr || term->opcode != NY_OPCODE_BRANCH_IF) continue;

            const Ny_Operand *ops = ny_function_get_operands(fn, term);
            if (term->op_count != 3 || ops[1].kind != NY_OP_BLOCK || ops[2].kind != NY_OP_BLOCK) continue;

            Ny_Block_ID true_blk = ops[1].blk;
            Ny_Block_ID false_blk = ops[2].blk;

            if (true_blk == false_blk) {
                term->opcode = NY_OPCODE_BRANCH;
                ny_instruction_replace_operand(fn, term->id, 0, ny_operand_block(true_blk));
                term->op_count = 1;

                if (blk->succ_count > 1) {
                    blk->succ_count = 1;
                    blk->succs[0] = true_blk;
                }
                Ny_Block *tgt_blk = ny_function_get_block(fn, true_blk);
                if (tgt_blk != nullptr) {
                    size_t pred_count = 0;
                    for (size_t i = 0; i < tgt_blk->pred_count; i++) {
                        if (tgt_blk->preds[i] == b_id) {
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
                continue;
            }

            int64_t c_val = 0;
            if (ny_opt_get_operand_const_int(fn, ops[0], &c_val)) {
                Ny_Block_ID taken = (c_val != 0) ? true_blk : false_blk;
                Ny_Block_ID dead  = (c_val != 0) ? false_blk : true_blk;

                term->opcode = NY_OPCODE_BRANCH;
                ny_instruction_replace_operand(fn, term->id, 0, ny_operand_block(taken));
                term->op_count = 1;

                Ny_Block *dead_blk = ny_function_get_block(fn, dead);
                if (dead_blk != nullptr) {
                    ny_block_remove_edge(blk, dead_blk);
                    ny_function_remove_phi_incoming(fn, dead, b_id);
                }
                changed = true;
            }
        }

        for (size_t b_idx = 0; b_idx < fn->block_count; b_idx++) {
            Ny_Block *blk = &fn->blocks[b_idx];
            if (blk->pred_count == 1) {
                Ny_Inst_ID curr = blk->first_inst;
                while (curr != NY_INVALID_INST) {
                    Ny_Instruction *inst = ny_function_get_instruction(fn, curr);
                    if (inst == nullptr || inst->opcode != NY_OPCODE_PHI) break;
                    Ny_Inst_ID next = inst->next;

                    const Ny_Operand *ops = ny_function_get_operands(fn, inst);
                    if (inst->op_count >= 2 && ops[0].kind == NY_OP_VALUE) {
                        ny_function_replace_all_uses(fn, inst->result, ops[0].val);
                        ny_function_remove_instruction(fn, inst->id);
                        changed = true;
                    }
                    curr = next;
                }
            }
        }

        for (size_t blk_idx = 0; blk_idx < fn->block_count; blk_idx++) {
            Ny_Block_ID b_id = (Ny_Block_ID)blk_idx;
            if (b_id == fn->entry_block) continue;

            Ny_Block *blk = ny_function_get_block(fn, b_id);
            if (blk == nullptr || blk->inst_count != 1 || blk->first_inst == NY_INVALID_INST) continue;

            Ny_Instruction *term = ny_function_get_instruction(fn, blk->first_inst);
            if (term == nullptr || term->opcode != NY_OPCODE_BRANCH) continue;

            const Ny_Operand *ops = ny_function_get_operands(fn, term);
            if (term->op_count != 1 || ops[0].kind != NY_OP_BLOCK) continue;
            Ny_Block_ID target_id = ops[0].blk;
            if (target_id == b_id) continue;

            Ny_Block *target_blk = ny_function_get_block(fn, target_id);
            if (target_blk == nullptr) continue;

            while (blk->pred_count > 0) {
                Ny_Block_ID pred_id = blk->preds[0];
                Ny_Block *pred_blk = ny_function_get_block(fn, pred_id);
                if (pred_blk == nullptr) break;

                Ny_Instruction *p_term = ny_function_get_instruction(fn, pred_blk->last_inst);
                if (p_term != nullptr) {
                    const Ny_Operand *p_ops = ny_function_get_operands(fn, p_term);
                    for (size_t op_i = 0; op_i < p_term->op_count; op_i++) {
                        if (p_ops[op_i].kind == NY_OP_BLOCK && p_ops[op_i].blk == b_id) {
                            ny_instruction_replace_operand(fn, p_term->id, op_i, ny_operand_block(target_id));
                        }
                    }
                }

                ny_block_remove_edge(pred_blk, blk);
                ny_block_add_edge(pred_blk, target_blk);

                Ny_Inst_ID phi_id = target_blk->first_inst;
                while (phi_id != NY_INVALID_INST) {
                    Ny_Instruction *phi_inst = ny_function_get_instruction(fn, phi_id);
                    if (phi_inst == nullptr || phi_inst->opcode != NY_OPCODE_PHI) break;

                    const Ny_Operand *phi_ops = ny_function_get_operands(fn, phi_inst);
                    for (size_t k = 1; k < phi_inst->op_count; k += 2) {
                        if (phi_ops[k].kind == NY_OP_BLOCK && phi_ops[k].blk == b_id) {
                            ny_instruction_replace_operand(fn, phi_inst->id, k, ny_operand_block(pred_id));
                        }
                    }
                    phi_id = phi_inst->next;
                }
                changed = true;
            }
        }

        for (size_t blk_idx = 0; blk_idx < fn->block_count; blk_idx++) {
            Ny_Block_ID b_id = (Ny_Block_ID)blk_idx;
            Ny_Block *blk = ny_function_get_block(fn, b_id);
            if (blk == nullptr || blk->inst_count == 0) continue;

            if (blk->succ_count == 1) {
                Ny_Block_ID succ_id = blk->succs[0];
                if (succ_id != b_id && succ_id != fn->entry_block) {
                    Ny_Block *succ_blk = ny_function_get_block(fn, succ_id);
                    if (succ_blk != nullptr && succ_blk->pred_count == 1) {
                        if (ny_function_merge_blocks(fn, b_id, succ_id)) {
                            changed = true;
                        }
                    }
                }
            }
        }

        size_t n_blocks = fn->block_count;
        if (fn->entry_block != NY_INVALID_BLOCK && n_blocks > 1) {
            bool *reachable = (bool *)ny_alloc(sizeof(bool) * n_blocks);
            memset(reachable, 0, sizeof(bool) * n_blocks);
            Ny_Block_ID *queue = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * n_blocks);
            size_t head = 0, tail = 0;

            reachable[fn->entry_block] = true;
            queue[tail++] = fn->entry_block;

            while (head < tail) {
                Ny_Block_ID curr_id = queue[head++];
                const Ny_Block *curr_blk = ny_function_get_block(fn, curr_id);
                if (curr_blk == nullptr) continue;

                for (size_t s_idx = 0; s_idx < curr_blk->succ_count; s_idx++) {
                    Ny_Block_ID s = curr_blk->succs[s_idx];
                    size_t s_idx_num = (size_t)s;
                    if (s_idx_num < n_blocks && !reachable[s_idx_num]) {
                        reachable[s_idx_num] = true;
                        queue[tail++] = s;
                    }
                }
            }

            for (size_t b = 0; b < n_blocks; b++) {
                Ny_Block_ID b_id = (Ny_Block_ID)b;
                if (b_id != fn->entry_block && !reachable[b]) {
                    Ny_Block *blk = ny_function_get_block(fn, b_id);
                    if (blk != nullptr && (blk->inst_count > 0 || blk->succ_count > 0 || blk->pred_count > 0)) {
                        for (size_t s_idx = 0; s_idx < blk->succ_count; s_idx++) {
                            Ny_Block_ID s = blk->succs[s_idx];
                            Ny_Block *s_blk = ny_function_get_block(fn, s);
                            if (s_blk != nullptr) {
                                ny_block_remove_edge(blk, s_blk);
                                ny_function_remove_phi_incoming(fn, s, b_id);
                            }
                        }
                        ny_function_remove_block(fn, b_id);
                        changed = true;
                    }
                }
            }

            ny_free(queue, sizeof(Ny_Block_ID) * n_blocks);
            ny_free(reachable, sizeof(bool) * n_blocks);
        }

        if (!changed) break;
        changed_any = true;
    }

    if (changed_any && am != nullptr) {
        ny_analysis_invalidate_cfg(am);
    }

    return changed_any;
}
