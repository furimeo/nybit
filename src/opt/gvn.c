// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Ny_Opcode opcode;
    Ny_Type_ID type;
    uint16_t op_count;
    Ny_Operand op0;
    Ny_Operand op1;
    Ny_Operand op2;
} GVN_Key;

static bool is_gvn_candidate(Ny_Opcode op) {
    switch (op) {
        case NY_OPCODE_ADD:
        case NY_OPCODE_SUB:
        case NY_OPCODE_MUL:
        case NY_OPCODE_DIV_S:
        case NY_OPCODE_DIV_U:
        case NY_OPCODE_REM_S:
        case NY_OPCODE_REM_U:
        case NY_OPCODE_NEG:
        case NY_OPCODE_FADD:
        case NY_OPCODE_FSUB:
        case NY_OPCODE_FMUL:
        case NY_OPCODE_FDIV:
        case NY_OPCODE_FNEG:
        case NY_OPCODE_AND:
        case NY_OPCODE_OR:
        case NY_OPCODE_XOR:
        case NY_OPCODE_NOT:
        case NY_OPCODE_SHL:
        case NY_OPCODE_SHR:
        case NY_OPCODE_SAR:
        case NY_OPCODE_ROTL:
        case NY_OPCODE_ROTR:
        case NY_OPCODE_CMP_EQ:
        case NY_OPCODE_CMP_NE:
        case NY_OPCODE_CMP_LT_S:
        case NY_OPCODE_CMP_LT_U:
        case NY_OPCODE_CMP_LE_S:
        case NY_OPCODE_CMP_LE_U:
        case NY_OPCODE_CMP_GT_S:
        case NY_OPCODE_CMP_GT_U:
        case NY_OPCODE_CMP_GE_S:
        case NY_OPCODE_CMP_GE_U:
        case NY_OPCODE_FCMP_EQ:
        case NY_OPCODE_FCMP_NE:
        case NY_OPCODE_FCMP_LT:
        case NY_OPCODE_FCMP_LE:
        case NY_OPCODE_FCMP_GT:
        case NY_OPCODE_FCMP_GE:
        case NY_OPCODE_SELECT:
        case NY_OPCODE_CAST:
        case NY_OPCODE_EXTEND:
        case NY_OPCODE_TRUNCATE:
        case NY_OPCODE_BITCAST:
        case NY_OPCODE_SEXT:
        case NY_OPCODE_ZEXT:
        case NY_OPCODE_FEXT:
        case NY_OPCODE_FTRUNC:
        case NY_OPCODE_SITOFP:
        case NY_OPCODE_FPTOSI:
            return true;
        default:
            return false;
    }
}

static bool operand_equal(Ny_Operand a, Ny_Operand b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case NY_OP_NONE:      return true;
        case NY_OP_VALUE:     return a.val == b.val;
        case NY_OP_BLOCK:     return a.blk == b.blk;
        case NY_OP_IMM_INT:   return a.imm_int == b.imm_int;
        case NY_OP_IMM_FLOAT: return a.imm_float == b.imm_float;
        case NY_OP_FUNCTION:  return a.fn_id == b.fn_id;
        case NY_OP_SYMBOL:    return a.sym_id == b.sym_id;
        case NY_OP_TYPE:      return a.type_id == b.type_id;
    }
    return false;
}

static bool gvn_key_equal(GVN_Key a, GVN_Key b) {
    if (a.opcode != b.opcode || a.type != b.type || a.op_count != b.op_count) return false;
    if (a.op_count > 0 && !operand_equal(a.op0, b.op0)) return false;
    if (a.op_count > 1 && !operand_equal(a.op1, b.op1)) return false;
    if (a.op_count > 2 && !operand_equal(a.op2, b.op2)) return false;
    return true;
}

static GVN_Key make_gvn_key(const Ny_Function *fn, const Ny_Instruction *inst) {
    const Ny_Value *res_val = ny_function_get_value(fn, inst->result);
    Ny_Type_ID t = (res_val != nullptr) ? res_val->type : NY_TYPE_VOID;
    const Ny_Operand *ops = ny_function_get_operands(fn, inst);
    size_t op_cnt = inst->op_count;

    GVN_Key key;
    memset(&key, 0, sizeof(key));
    key.opcode = (Ny_Opcode)inst->opcode;
    key.type = t;
    key.op_count = (uint16_t)op_cnt;

    if (op_cnt > 0) key.op0 = ops[0];
    if (op_cnt > 1) key.op1 = ops[1];
    if (op_cnt > 2) key.op2 = ops[2];

    if (ny_opt_is_commutative((Ny_Opcode)inst->opcode) && key.op_count == 2) {
        if (key.op0.kind == NY_OP_VALUE && key.op1.kind == NY_OP_VALUE) {
            if (key.op0.val > key.op1.val) {
                Ny_Operand tmp = key.op0;
                key.op0 = key.op1;
                key.op1 = tmp;
            }
        } else if (key.op0.kind != NY_OP_VALUE && key.op1.kind == NY_OP_VALUE) {
            Ny_Operand tmp = key.op0;
            key.op0 = key.op1;
            key.op1 = tmp;
        }
    }

    return key;
}

typedef struct {
    GVN_Key key;
    Ny_Value_ID val;
    Ny_Block_ID blk_id;
} Available_Expr;

bool ny_opt_pass_gvn(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am) {
    (void)mod;
    if (fn->block_count == 0 || fn->entry_block == NY_INVALID_BLOCK) return false;

    Ny_Dominator_Tree local_dom;
    Ny_Dominator_Tree *dom_ptr = nullptr;
    if (am != nullptr) {
        dom_ptr = ny_analysis_get_dom(am);
    } else {
        ny_dominator_tree_init(&local_dom, fn);
        dom_ptr = &local_dom;
    }

    Available_Expr *table = nullptr;
    size_t table_count = 0;
    size_t table_cap = 0;
    bool changed = false;

    for (size_t blk_idx = 0; blk_idx < fn->block_count; blk_idx++) {
        Ny_Block_ID b_id = (Ny_Block_ID)blk_idx;
        const Ny_Block *blk = ny_function_get_block(fn, b_id);
        if (blk == nullptr) continue;

        Ny_Inst_ID curr = blk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Instruction *inst = ny_function_get_instruction(fn, curr);
            if (inst == nullptr) break;
            Ny_Inst_ID next = inst->next;

            if (inst->result != NY_INVALID_VALUE && is_gvn_candidate((Ny_Opcode)inst->opcode) && !(inst->flags & NY_FLAG_VOLATILE)) {
                GVN_Key key = make_gvn_key(fn, inst);
                ssize_t match_idx = -1;

                for (size_t i = 0; i < table_count; i++) {
                    if (gvn_key_equal(table[i].key, key)) {
                        Available_Expr cand = table[i];
                        bool can_replace = false;
                        if (cand.blk_id == b_id) {
                            can_replace = true;
                        } else if (ny_dominator_tree_dominates(dom_ptr, cand.blk_id, b_id)) {
                            can_replace = true;
                        }

                        if (can_replace) {
                            match_idx = (ssize_t)i;
                            break;
                        }
                    }
                }

                if (match_idx >= 0) {
                    Available_Expr cand = table[match_idx];
                    ny_function_replace_all_uses(fn, inst->result, cand.val);
                    ny_function_remove_instruction(fn, inst->id);
                    changed = true;
                } else {
                    ny_buf_grow((void **)&table, &table_cap, table_count, sizeof(Available_Expr));
                    table[table_count].key = key;
                    table[table_count].val = inst->result;
                    table[table_count].blk_id = b_id;
                    table_count++;
                }
            }

            curr = next;
        }
    }

    if (table != nullptr) ny_free(table, sizeof(Available_Expr) * table_cap);
    if (am == nullptr) {
        ny_dominator_tree_destroy(&local_dom);
    } else if (changed) {
        ny_analysis_invalidate_use_def(am);
    }

    return changed;
}
