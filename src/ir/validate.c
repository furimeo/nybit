// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"
#include <stdio.h>

void ny_diagnostic_list_init(Ny_Diagnostic_List *list) {
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void ny_diagnostic_list_destroy(Ny_Diagnostic_List *list) {
    for (size_t i = 0; i < list->count; i++) {
        if (list->items[i].message) {
            ny_free(list->items[i].message, strlen(list->items[i].message) + 1);
        }
    }
    if (list->items) {
        ny_free(list->items, list->capacity * sizeof(Ny_Diagnostic_Item));
    }
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
}

void ny_diagnostic_list_append(Ny_Diagnostic_List *list, const char *msg) {
    if (!msg) return;
    ny_buf_grow((void **)&list->items, &list->capacity, list->count, sizeof(Ny_Diagnostic_Item));
    size_t len = strlen(msg);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, msg, len + 1);
    list->items[list->count++].message = copy;
}

bool ny_validate_function(const Ny_Module *mod, const Ny_Function *fn, Ny_Diagnostic_List *out_diags) {
    (void)mod;
    size_t err_count_start = out_diags ? out_diags->count : 0;
    if (fn->block_count == 0) return true;

    if (fn->entry_block == NY_INVALID_BLOCK || (size_t)fn->entry_block >= fn->block_count) {
        if (out_diags) {
            char buf[128];
            snprintf(buf, sizeof(buf), "fn '%.*s': invalid entry block", (int)fn->name.len, fn->name.data);
            ny_diagnostic_list_append(out_diags, buf);
        }
        return false;
    }

    Ny_Block *entry = ny_function_get_block((Ny_Function *)fn, fn->entry_block);
    if (entry && entry->pred_count > 0) {
        if (out_diags) {
            char buf[128];
            snprintf(buf, sizeof(buf), "fn '%.*s': entry block must have no predecessors", (int)fn->name.len, fn->name.data);
            ny_diagnostic_list_append(out_diags, buf);
        }
    }

    Ny_Core_Dominator_Tree dt;
    ny_core_dominator_tree_init(&dt, fn);

    int *inst_pos = (int *)ny_alloc(fn->inst_count * sizeof(int));
    for (size_t i = 0; i < fn->inst_count; i++) inst_pos[i] = -1;

    for (size_t b = 0; b < fn->block_count; b++) {
        const Ny_Block *blk = &fn->blocks[b];
        if (blk->first_inst == NY_INVALID_INST) {
            if (blk->id == fn->entry_block || blk->pred_count > 0 || blk->succ_count > 0) {
                if (out_diags) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "fn '%.*s': active block '%.*s' is empty",
                             (int)fn->name.len, fn->name.data, (int)blk->name.len, blk->name.data);
                    ny_diagnostic_list_append(out_diags, buf);
                }
            }
            continue;
        }

        Ny_Inst_ID curr = blk->first_inst;
        Ny_Inst_ID prev = NY_INVALID_INST;
        const Ny_Instruction *last_inst = NULL;
        bool seen_non_phi = false;
        int pos = 0;

        while (curr != NY_INVALID_INST) {
            if ((size_t)curr >= fn->inst_count) {
                if (out_diags) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "fn '%.*s': inst id %u out of bounds", (int)fn->name.len, fn->name.data, (unsigned)curr);
                    ny_diagnostic_list_append(out_diags, buf);
                }
                break;
            }

            const Ny_Instruction *inst = &fn->instructions[curr];
            if (inst->opcode == NY_OPCODE_NONE) {
                if (out_diags) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "fn '%.*s': dead inst %u linked in block", (int)fn->name.len, fn->name.data, (unsigned)curr);
                    ny_diagnostic_list_append(out_diags, buf);
                }
            }

            last_inst = inst;
            inst_pos[curr] = pos++;

            if (inst->block != blk->id) {
                if (out_diags) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "fn '%.*s': inst %u block mismatch", (int)fn->name.len, fn->name.data, (unsigned)curr);
                    ny_diagnostic_list_append(out_diags, buf);
                }
            }

            if (inst->prev != prev) {
                if (out_diags) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "fn '%.*s': inst %u prev link broken", (int)fn->name.len, fn->name.data, (unsigned)curr);
                    ny_diagnostic_list_append(out_diags, buf);
                }
            }

            if (inst->opcode == NY_OPCODE_PHI) {
                if (seen_non_phi) {
                    if (out_diags) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "fn '%.*s': phi inst %u must be at block start", (int)fn->name.len, fn->name.data, (unsigned)curr);
                        ny_diagnostic_list_append(out_diags, buf);
                    }
                }
                if (inst->op_count % 2 != 0) {
                    if (out_diags) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "fn '%.*s': phi %u must have even operand pairs", (int)fn->name.len, fn->name.data, (unsigned)curr);
                        ny_diagnostic_list_append(out_diags, buf);
                    }
                }
            } else {
                seen_non_phi = true;
            }

            // Check operand dominance and bounds
            Ny_Operand *ops = ny_function_get_operands((Ny_Function *)fn, inst);
            for (size_t i = 0; i < inst->op_count; i++) {
                if (ops[i].kind == NY_OP_VALUE) {
                    Ny_Value_ID v_id = ops[i].val;
                    if ((size_t)v_id >= fn->val_count) {
                        if (out_diags) {
                            char buf[128];
                            snprintf(buf, sizeof(buf), "fn '%.*s': inst %u references out of bounds value %u",
                                     (int)fn->name.len, fn->name.data, (unsigned)curr, (unsigned)v_id);
                            ny_diagnostic_list_append(out_diags, buf);
                        }
                    } else {
                        const Ny_Value *v = &fn->values[v_id];
                        if (v->kind == NY_VAL_INSTRUCTION) {
                            if (v->def == NY_INVALID_INST || (size_t)v->def >= fn->inst_count) {
                                if (out_diags) {
                                    char buf[128];
                                    snprintf(buf, sizeof(buf), "fn '%.*s': value %u has invalid def inst",
                                             (int)fn->name.len, fn->name.data, (unsigned)v_id);
                                    ny_diagnostic_list_append(out_diags, buf);
                                }
                            } else {
                                const Ny_Instruction *def_inst = &fn->instructions[v->def];
                                if (inst->opcode == NY_OPCODE_PHI) {
                                    // Phi operands are edge-live from incoming predecessors
                                    if (i + 1 < inst->op_count && ops[i + 1].kind == NY_OP_BLOCK) {
                                        Ny_Block_ID in_blk = ops[i + 1].blk;
                                        if (def_inst->block != in_blk && !ny_core_dominator_tree_dominates(&dt, def_inst->block, in_blk)) {
                                            if (out_diags) {
                                                char buf[128];
                                                snprintf(buf, sizeof(buf), "fn '%.*s': phi incoming value %u does not dominate block %u",
                                                         (int)fn->name.len, fn->name.data, (unsigned)v_id, (unsigned)in_blk);
                                                ny_diagnostic_list_append(out_diags, buf);
                                            }
                                        }
                                    }
                                } else {
                                    if (def_inst->block == blk->id) {
                                        int d_pos = inst_pos[v->def];
                                        if (d_pos < 0 || d_pos >= pos - 1) {
                                            if (out_diags) {
                                                char buf[128];
                                                snprintf(buf, sizeof(buf), "fn '%.*s': value %u used before defined in same block",
                                                         (int)fn->name.len, fn->name.data, (unsigned)v_id);
                                                ny_diagnostic_list_append(out_diags, buf);
                                            }
                                        }
                                    } else {
                                        if (!ny_core_dominator_tree_dominates(&dt, def_inst->block, blk->id)) {
                                            if (out_diags) {
                                                char buf[128];
                                                snprintf(buf, sizeof(buf), "fn '%.*s': def of value %u in block %u does not dominate use in block %u",
                                                         (int)fn->name.len, fn->name.data, (unsigned)v_id, (unsigned)def_inst->block, (unsigned)blk->id);
                                                ny_diagnostic_list_append(out_diags, buf);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            prev = curr;
            curr = inst->next;
        }

        if (last_inst) {
            if (!ny_opcode_is_terminator((Ny_Opcode)last_inst->opcode)) {
                if (out_diags) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "fn '%.*s': block '%.*s' last inst is not terminator",
                             (int)fn->name.len, fn->name.data, (int)blk->name.len, blk->name.data);
                    ny_diagnostic_list_append(out_diags, buf);
                }
            }
        }
    }

    ny_free(inst_pos, fn->inst_count * sizeof(int));
    ny_core_dominator_tree_destroy(&dt);

    return out_diags ? (out_diags->count == err_count_start) : true;
}

bool ny_validate_module(const Ny_Module *mod, Ny_Diagnostic_List *out_diags) {
    bool ok = true;

    for (size_t g = 0; g < mod->global_count; g++) {
        const Ny_Global *glob = &mod->globals[g];
        if (glob->name.len == 0) {
            if (out_diags) ny_diagnostic_list_append(out_diags, "module: global variable with empty name");
            ok = false;
        }
        for (size_t g2 = g + 1; g2 < mod->global_count; g2++) {
            if (ny_str_eq(glob->name, mod->globals[g2].name)) {
                if (out_diags) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "module: duplicate global variable '%.*s'", (int)glob->name.len, glob->name.data);
                    ny_diagnostic_list_append(out_diags, buf);
                }
                ok = false;
            }
        }
    }

    for (size_t i = 0; i < mod->function_count; i++) {
        if (!ny_validate_function(mod, &mod->functions[i], out_diags)) {
            ok = false;
        }
    }
    return ok;
}
