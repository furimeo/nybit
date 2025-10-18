// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"

void ny_function_init(Ny_Function *fn, Ny_Function_ID id, Ny_String name, Ny_Type_ID ret_type, uint8_t call_conv) {
    memset(fn, 0, sizeof(Ny_Function));
    fn->id = id;
    fn->name = name;
    fn->return_type = ret_type;
    fn->call_conv = call_conv;
    fn->entry_block = NY_INVALID_BLOCK;
}

void ny_function_destroy(Ny_Function *fn) {
    if (fn->param_types) {
        ny_free(fn->param_types, fn->param_capacity * sizeof(Ny_Type_ID));
    }
    if (fn->param_names) {
        ny_free(fn->param_names, fn->param_capacity * sizeof(Ny_String));
    }

    for (size_t i = 0; i < fn->block_count; i++) {
        Ny_Block *b = &fn->blocks[i];
        if (b->preds) {
            ny_free(b->preds, b->pred_capacity * sizeof(Ny_Block_ID));
        }
        if (b->succs) {
            ny_free(b->succs, b->succ_capacity * sizeof(Ny_Block_ID));
        }
    }
    if (fn->blocks) {
        ny_free(fn->blocks, fn->block_capacity * sizeof(Ny_Block));
    }

    if (fn->instructions) {
        ny_free(fn->instructions, fn->inst_capacity * sizeof(Ny_Instruction));
    }
    if (fn->values) {
        ny_free(fn->values, fn->val_capacity * sizeof(Ny_Value));
    }
    if (fn->operands) {
        ny_free(fn->operands, fn->op_capacity * sizeof(Ny_Operand));
    }

    memset(fn, 0, sizeof(Ny_Function));
    fn->entry_block = NY_INVALID_BLOCK;
}

void ny_function_add_param(Ny_Function *fn, Ny_Type_ID type, Ny_String name) {
    if (fn->param_count >= fn->param_capacity) {
        size_t old_cap = fn->param_capacity;
        size_t new_cap = old_cap == 0 ? 8 : old_cap * 2;
        fn->param_types = (Ny_Type_ID *)ny_realloc(fn->param_types, old_cap * sizeof(Ny_Type_ID), new_cap * sizeof(Ny_Type_ID));
        fn->param_names = (Ny_String *)ny_realloc(fn->param_names, old_cap * sizeof(Ny_String), new_cap * sizeof(Ny_String));
        fn->param_capacity = new_cap;
    }
    fn->param_types[fn->param_count] = type;
    fn->param_names[fn->param_count] = name;
    fn->param_count++;
}

Ny_Block *ny_function_get_block(const Ny_Function *fn, Ny_Block_ID id) {
    if (id >= fn->block_count) return NULL;
    return &fn->blocks[id];
}

Ny_Instruction *ny_function_get_instruction(const Ny_Function *fn, Ny_Inst_ID id) {
    if (id >= fn->inst_count) return NULL;
    return &fn->instructions[id];
}

Ny_Value *ny_function_get_value(const Ny_Function *fn, Ny_Value_ID id) {
    if (id >= fn->val_count) return NULL;
    return &fn->values[id];
}

Ny_Operand *ny_function_get_operands(const Ny_Function *fn, const Ny_Instruction *inst) {
    if (!inst) return NULL;
    size_t start = inst->op_start;
    size_t end = start + inst->op_count;
    if (end > fn->op_count) return NULL;
    return &fn->operands[start];
}

Ny_Block_ID ny_function_create_block(Ny_Function *fn, Ny_String name) {
    ny_buf_grow((void **)&fn->blocks, &fn->block_capacity, fn->block_count, sizeof(Ny_Block));
    Ny_Block_ID id = (Ny_Block_ID)fn->block_count++;
    Ny_Block *b = &fn->blocks[id];
    memset(b, 0, sizeof(Ny_Block));
    b->id = id;
    b->name = name;
    b->first_inst = NY_INVALID_INST;
    b->last_inst = NY_INVALID_INST;

    if (fn->entry_block == NY_INVALID_BLOCK) {
        fn->entry_block = id;
    }
    return id;
}

Ny_Value_ID ny_function_create_value(Ny_Function *fn, Ny_Type_ID type, Ny_Value_Kind kind, Ny_Inst_ID def, uint32_t index, Ny_String name) {
    ny_buf_grow((void **)&fn->values, &fn->val_capacity, fn->val_count, sizeof(Ny_Value));
    Ny_Value_ID id = (Ny_Value_ID)fn->val_count++;
    Ny_Value *v = &fn->values[id];
    v->id = id;
    v->type = type;
    v->kind = kind;
    v->def = def;
    v->index = index;
    v->name = name;
    return id;
}

Ny_Inst_ID ny_function_append_instruction(Ny_Function *fn, Ny_Block_ID block_id, Ny_Opcode opcode, Ny_Value_ID result, const Ny_Operand *ops, size_t op_count, uint16_t flags) {
    Ny_Block *blk = ny_function_get_block(fn, block_id);
    assert(blk != NULL);

    ny_buf_grow((void **)&fn->instructions, &fn->inst_capacity, fn->inst_count, sizeof(Ny_Instruction));
    Ny_Inst_ID inst_id = (Ny_Inst_ID)fn->inst_count++;

    uint32_t op_start = (uint32_t)fn->op_count;
    for (size_t i = 0; i < op_count; i++) {
        ny_buf_grow((void **)&fn->operands, &fn->op_capacity, fn->op_count, sizeof(Ny_Operand));
        fn->operands[fn->op_count++] = ops[i];
    }

    Ny_Instruction *inst = &fn->instructions[inst_id];
    inst->id = inst_id;
    inst->opcode = (uint16_t)opcode;
    inst->flags = flags;
    inst->result = result;
    inst->op_start = op_start;
    inst->op_count = (uint16_t)op_count;
    inst->block = block_id;
    inst->prev = blk->last_inst;
    inst->next = NY_INVALID_INST;

    if (result != NY_INVALID_VALUE) {
        Ny_Value *v = ny_function_get_value(fn, result);
        if (v) {
            v->def = inst_id;
        }
    }

    if (blk->first_inst == NY_INVALID_INST) {
        blk->first_inst = inst_id;
    } else {
        fn->instructions[blk->last_inst].next = inst_id;
    }
    blk->last_inst = inst_id;
    blk->inst_count++;

    return inst_id;
}

void ny_instruction_replace_operand(Ny_Function *fn, Ny_Inst_ID inst_id, size_t op_idx, Ny_Operand new_op) {
    Ny_Instruction *inst = ny_function_get_instruction(fn, inst_id);
    if (!inst) return;
    if (op_idx >= inst->op_count) return;

    size_t target_idx = (size_t)inst->op_start + op_idx;
    if (target_idx < fn->op_count) {
        fn->operands[target_idx] = new_op;
    }
}

void ny_function_replace_all_uses(Ny_Function *fn, Ny_Value_ID old_val, Ny_Value_ID new_val) {
    if (old_val == new_val) return;

    for (size_t i = 0; i < fn->inst_count; i++) {
        Ny_Instruction *inst = &fn->instructions[i];
        if (inst->opcode == NY_OPCODE_NONE) continue;

        size_t start = inst->op_start;
        size_t end = start + inst->op_count;
        for (size_t j = start; j < end && j < fn->op_count; j++) {
            if (fn->operands[j].kind == NY_OP_VALUE && fn->operands[j].val == old_val) {
                fn->operands[j].val = new_val;
            }
        }
    }
}

void ny_function_remove_instruction(Ny_Function *fn, Ny_Inst_ID inst_id) {
    Ny_Instruction *inst = ny_function_get_instruction(fn, inst_id);
    if (!inst || inst->opcode == NY_OPCODE_NONE) return;

    Ny_Block *blk = ny_function_get_block(fn, inst->block);
    if (blk) {
        if (inst->prev != NY_INVALID_INST) {
            fn->instructions[inst->prev].next = inst->next;
        } else {
            blk->first_inst = inst->next;
        }

        if (inst->next != NY_INVALID_INST) {
            fn->instructions[inst->next].prev = inst->prev;
        } else {
            blk->last_inst = inst->prev;
        }

        if (blk->inst_count > 0) {
            blk->inst_count--;
        }
    }

    if (inst->result != NY_INVALID_VALUE) {
        Ny_Value *v = ny_function_get_value(fn, inst->result);
        if (v) {
            v->def = NY_INVALID_INST;
        }
    }

    inst->opcode = NY_OPCODE_NONE;
    inst->prev = NY_INVALID_INST;
    inst->next = NY_INVALID_INST;
    inst->op_count = 0;
}

void ny_function_remove_block(Ny_Function *fn, Ny_Block_ID block_id) {
    Ny_Block *blk = ny_function_get_block(fn, block_id);
    if (!blk) return;

    Ny_Inst_ID curr = blk->first_inst;
    while (curr != NY_INVALID_INST) {
        Ny_Inst_ID next = fn->instructions[curr].next;
        ny_function_remove_instruction(fn, curr);
        curr = next;
    }

    for (size_t i = 0; i < blk->succ_count; i++) {
        Ny_Block *s_blk = ny_function_get_block(fn, blk->succs[i]);
        if (s_blk) {
            for (size_t j = 0; j < s_blk->pred_count; j++) {
                if (s_blk->preds[j] == block_id) {
                    s_blk->preds[j] = s_blk->preds[s_blk->pred_count - 1];
                    s_blk->pred_count--;
                    break;
                }
            }
        }
    }
    blk->succ_count = 0;

    for (size_t i = 0; i < blk->pred_count; i++) {
        Ny_Block *p_blk = ny_function_get_block(fn, blk->preds[i]);
        if (p_blk) {
            for (size_t j = 0; j < p_blk->succ_count; j++) {
                if (p_blk->succs[j] == block_id) {
                    p_blk->succs[j] = p_blk->succs[p_blk->succ_count - 1];
                    p_blk->succ_count--;
                    break;
                }
            }
        }
    }
    blk->pred_count = 0;

    blk->first_inst = NY_INVALID_INST;
    blk->last_inst = NY_INVALID_INST;
    blk->inst_count = 0;
}

bool ny_function_split_block(Ny_Function *fn, Ny_Block_ID block_id, Ny_Inst_ID split_before_inst, Ny_Block_ID *out_new_block_id) {
    Ny_Block *orig_blk = ny_function_get_block(fn, block_id);
    if (!orig_blk) return false;

    Ny_Instruction *split_inst = ny_function_get_instruction(fn, split_before_inst);
    if (!split_inst || split_inst->block != block_id) return false;

    Ny_Block_ID new_blk_id = ny_function_create_block(fn, ny_str("split"));
    orig_blk = ny_function_get_block(fn, block_id);
    Ny_Block *new_blk = ny_function_get_block(fn, new_blk_id);

    new_blk->first_inst = split_before_inst;
    new_blk->last_inst = orig_blk->last_inst;

    uint32_t moved_count = 0;
    Ny_Inst_ID curr = split_before_inst;
    while (curr != NY_INVALID_INST) {
        fn->instructions[curr].block = new_blk_id;
        moved_count++;
        curr = fn->instructions[curr].next;
    }
    new_blk->inst_count = moved_count;

    if (split_inst->prev != NY_INVALID_INST) {
        orig_blk->last_inst = split_inst->prev;
        fn->instructions[split_inst->prev].next = NY_INVALID_INST;
    } else {
        orig_blk->first_inst = NY_INVALID_INST;
        orig_blk->last_inst = NY_INVALID_INST;
    }
    split_inst->prev = NY_INVALID_INST;
    orig_blk->inst_count -= moved_count;

    // Move successors
    for (size_t i = 0; i < orig_blk->succ_count; i++) {
        Ny_Block_ID s_id = orig_blk->succs[i];
        ny_buf_grow((void **)&new_blk->succs, &new_blk->succ_capacity, new_blk->succ_count, sizeof(Ny_Block_ID));
        new_blk->succs[new_blk->succ_count++] = s_id;

        Ny_Block *s_blk = ny_function_get_block(fn, s_id);
        if (s_blk) {
            for (size_t j = 0; j < s_blk->pred_count; j++) {
                if (s_blk->preds[j] == block_id) {
                    s_blk->preds[j] = new_blk_id;
                }
            }
        }
    }
    orig_blk->succ_count = 0;

    ny_block_add_edge(orig_blk, new_blk);

    if (out_new_block_id) {
        *out_new_block_id = new_blk_id;
    }
    return true;
}

bool ny_function_merge_blocks(Ny_Function *fn, Ny_Block_ID first_id, Ny_Block_ID second_id) {
    if (first_id == second_id) return false;

    Ny_Block *first_blk = ny_function_get_block(fn, first_id);
    Ny_Block *second_blk = ny_function_get_block(fn, second_id);
    if (!first_blk || !second_blk) return false;

    if (first_blk->succ_count != 1 || first_blk->succs[0] != second_id) return false;
    if (second_blk->pred_count != 1 || second_blk->preds[0] != first_id) return false;

    // Remove unconditional branch in first_blk
    if (first_blk->last_inst != NY_INVALID_INST) {
        Ny_Instruction *last = ny_function_get_instruction(fn, first_blk->last_inst);
        if (last && last->opcode == NY_OPCODE_BRANCH) {
            ny_function_remove_instruction(fn, first_blk->last_inst);
        }
    }

    if (second_blk->inst_count > 0) {
        if (first_blk->first_inst == NY_INVALID_INST) {
            first_blk->first_inst = second_blk->first_inst;
            first_blk->last_inst = second_blk->last_inst;
        } else {
            fn->instructions[first_blk->last_inst].next = second_blk->first_inst;
            fn->instructions[second_blk->first_inst].prev = first_blk->last_inst;
            first_blk->last_inst = second_blk->last_inst;
        }

        Ny_Inst_ID curr = second_blk->first_inst;
        while (curr != NY_INVALID_INST) {
            fn->instructions[curr].block = first_id;
            first_blk->inst_count++;
            curr = fn->instructions[curr].next;
        }
    }

    // Update successors
    first_blk->succ_count = 0;
    for (size_t i = 0; i < second_blk->succ_count; i++) {
        Ny_Block_ID s_id = second_blk->succs[i];
        ny_buf_grow((void **)&first_blk->succs, &first_blk->succ_capacity, first_blk->succ_count, sizeof(Ny_Block_ID));
        first_blk->succs[first_blk->succ_count++] = s_id;

        Ny_Block *s_blk = ny_function_get_block(fn, s_id);
        if (s_blk) {
            for (size_t j = 0; j < s_blk->pred_count; j++) {
                if (s_blk->preds[j] == second_id) {
                    s_blk->preds[j] = first_id;
                }
            }
        }
    }

    second_blk->pred_count = 0;
    second_blk->succ_count = 0;
    second_blk->first_inst = NY_INVALID_INST;
    second_blk->last_inst = NY_INVALID_INST;
    second_blk->inst_count = 0;

    return true;
}

void ny_function_remove_phi_incoming(Ny_Function *fn, Ny_Block_ID blk_id, Ny_Block_ID pred_id) {
    Ny_Block *blk = ny_function_get_block(fn, blk_id);
    if (!blk) return;

    Ny_Inst_ID curr = blk->first_inst;
    while (curr != NY_INVALID_INST) {
        Ny_Instruction *inst = ny_function_get_instruction(fn, curr);
        if (!inst || inst->opcode != NY_OPCODE_PHI) break;

        Ny_Operand *ops = ny_function_get_operands(fn, inst);
        bool has_pred = false;
        for (size_t i = 1; i < inst->op_count; i += 2) {
            if (ops[i].kind == NY_OP_BLOCK && ops[i].blk == pred_id) {
                has_pred = true;
                break;
            }
        }

        if (has_pred) {
            size_t start = inst->op_start;
            size_t write_idx = start;
            for (size_t i = 0; i < inst->op_count; i += 2) {
                if (i + 1 < inst->op_count && ops[i + 1].kind == NY_OP_BLOCK && ops[i + 1].blk == pred_id) {
                    continue;
                }
                fn->operands[write_idx] = ops[i];
                fn->operands[write_idx + 1] = ops[i + 1];
                write_idx += 2;
            }
            inst->op_count = (uint16_t)(write_idx - start);
        }

        curr = inst->next;
    }
}
