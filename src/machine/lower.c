// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/machine.h"
#include "nybit/analysis.h"

static Ny_Reg_Class type_to_reg_class(const Ny_Type_Table *tt, Ny_Type_ID ty) {
    switch (ty) {
    case NY_TYPE_I8:
    case NY_TYPE_I16:
    case NY_TYPE_I32:
        return NY_REG_CLASS_GPR32;
    case NY_TYPE_I64:
    case NY_TYPE_PTR:
        return NY_REG_CLASS_GPR64;
    case NY_TYPE_F16:
    case NY_TYPE_F32:
        return NY_REG_CLASS_FP32;
    case NY_TYPE_F64:
        return NY_REG_CLASS_FP64;
    default:
        if (ny_type_is_vector(tt, ty)) {
            return NY_REG_CLASS_VEC128;
        }
        return NY_REG_CLASS_GPR64;
    }
}

static Ny_Machine_Cond opcode_to_cond(Ny_Opcode op) {
    switch (op) {
    case NY_OPCODE_CMP_EQ:   return NY_MCOND_EQ;
    case NY_OPCODE_CMP_NE:   return NY_MCOND_NE;
    case NY_OPCODE_CMP_LT_S: return NY_MCOND_LT_S;
    case NY_OPCODE_CMP_LT_U: return NY_MCOND_LT_U;
    case NY_OPCODE_CMP_LE_S: return NY_MCOND_LE_S;
    case NY_OPCODE_CMP_LE_U: return NY_MCOND_LE_U;
    case NY_OPCODE_CMP_GT_S: return NY_MCOND_GT_S;
    case NY_OPCODE_CMP_GT_U: return NY_MCOND_GT_U;
    case NY_OPCODE_CMP_GE_S: return NY_MCOND_GE_S;
    case NY_OPCODE_CMP_GE_U: return NY_MCOND_GE_U;
    case NY_OPCODE_FCMP_EQ:  return NY_MCOND_EQ;
    case NY_OPCODE_FCMP_NE:  return NY_MCOND_NE;
    case NY_OPCODE_FCMP_LT:  return NY_MCOND_LT_S;
    case NY_OPCODE_FCMP_LE:  return NY_MCOND_LE_S;
    case NY_OPCODE_FCMP_GT:  return NY_MCOND_GT_S;
    case NY_OPCODE_FCMP_GE:  return NY_MCOND_GE_S;
    default:                 return NY_MCOND_NONE;
    }
}

static Ny_Machine_Opcode opcode_to_mopc(Ny_Opcode op) {
    switch (op) {
    case NY_OPCODE_ADD:      return NY_MOPC_ADD;
    case NY_OPCODE_SUB:      return NY_MOPC_SUB;
    case NY_OPCODE_MUL:      return NY_MOPC_IMUL;
    case NY_OPCODE_DIV_S:    return NY_MOPC_IDIV;
    case NY_OPCODE_DIV_U:    return NY_MOPC_UDIV;
    case NY_OPCODE_REM_S:    return NY_MOPC_IREM;
    case NY_OPCODE_REM_U:    return NY_MOPC_UREM;
    case NY_OPCODE_NEG:      return NY_MOPC_NEG;

    case NY_OPCODE_AND:      return NY_MOPC_AND;
    case NY_OPCODE_OR:       return NY_MOPC_OR;
    case NY_OPCODE_XOR:      return NY_MOPC_XOR;
    case NY_OPCODE_NOT:      return NY_MOPC_NOT;
    case NY_OPCODE_SHL:      return NY_MOPC_SHL;
    case NY_OPCODE_SHR:      return NY_MOPC_SHR;
    case NY_OPCODE_SAR:      return NY_MOPC_SAR;

    case NY_OPCODE_FADD:     return NY_MOPC_FADD;
    case NY_OPCODE_FSUB:     return NY_MOPC_FSUB;
    case NY_OPCODE_FMUL:     return NY_MOPC_FMUL;
    case NY_OPCODE_FDIV:     return NY_MOPC_FDIV;
    case NY_OPCODE_FNEG:     return NY_MOPC_FNEG;

    case NY_OPCODE_LOAD:     return NY_MOPC_LOAD;
    case NY_OPCODE_STORE:    return NY_MOPC_STORE;
    case NY_OPCODE_SELECT:   return NY_MOPC_SELECT;
    case NY_OPCODE_CALL:     return NY_MOPC_CALL;
    case NY_OPCODE_CALL_INDIRECT: return NY_MOPC_CALL_IND;
    case NY_OPCODE_PHI:      return NY_MOPC_PHI;
    case NY_OPCODE_TRAP:     return NY_MOPC_TRAP;
    default:                 return NY_MOPC_NONE;
    }
}

static Ny_Machine_Operand lower_operand(const Ny_Module *mod, const Ny_Function *fn, Ny_Operand op, const Ny_Machine_Reg *val_map, const Ny_Block_ID *blk_map) {
    switch (op.kind) {
    case NY_OP_VALUE:
        return ny_mop_reg(val_map[op.val]);
    case NY_OP_IMM_INT:
        return ny_mop_imm_int(op.imm_int);
    case NY_OP_IMM_FLOAT:
        return ny_mop_imm_float(op.imm_float);
    case NY_OP_BLOCK:
        return ny_mop_block(blk_map ? blk_map[op.blk] : op.blk);
    case NY_OP_FUNCTION: {
        Ny_Function *tgt = ny_module_get_function((Ny_Module *)mod, op.fn_id);
        Ny_String name = tgt ? tgt->name : ny_str("");
        return ny_mop_symbol(name, op.fn_id);
    }
    case NY_OP_GLOBAL: {
        Ny_Global *g = ny_module_get_global((Ny_Module *)mod, op.global_id);
        Ny_String name = g ? g->name : ny_str("");
        return ny_mop_symbol(name, op.global_id);
    }
    case NY_OP_SYMBOL:
        return ny_mop_symbol(ny_str(""), op.sym_id);
    case NY_OP_TYPE:
    case NY_OP_NONE:
    default:
        return (Ny_Machine_Operand){ .kind = NY_MOP_KIND_NONE };
    }
    (void)fn;
}

static bool lower_function(const Ny_Module *ir_mod, const Ny_Function *fn, Ny_Machine_Module *mmod, Ny_Diagnostic_List *diags) {
    Ny_Machine_Function *mfn = ny_mmod_create_function(mmod, fn->name, fn->return_type, fn->call_conv);

    Ny_CFG_Info cfg;
    ny_cfg_info_init(&cfg, fn);

    size_t blk_map_size = fn->block_count > 0 ? fn->block_count : 1;
    Ny_Block_ID *blk_map = (Ny_Block_ID *)ny_alloc(blk_map_size * sizeof(Ny_Block_ID));

    for (size_t b = 0; b < fn->block_count; b++) {
        if (cfg.is_reachable[b]) {
            blk_map[b] = ny_mfunc_create_block(mfn, fn->blocks[b].name);
        } else {
            blk_map[b] = NY_INVALID_BLOCK;
        }
    }

    if (fn->entry_block != NY_INVALID_BLOCK && cfg.is_reachable[fn->entry_block]) {
        mfn->entry_block = blk_map[fn->entry_block];
    } else {
        mfn->entry_block = NY_INVALID_BLOCK;
    }

    for (size_t b = 0; b < fn->block_count; b++) {
        if (!cfg.is_reachable[b]) continue;
        Ny_Block *blk = &fn->blocks[b];
        Ny_Machine_Block *mblk = ny_mfunc_get_block(mfn, blk_map[b]);
        for (size_t p = 0; p < blk->pred_count; p++) {
            Ny_Block_ID pred_id = blk->preds[p];
            if (cfg.is_reachable[pred_id]) {
                Ny_Machine_Block *pred_mblk = ny_mfunc_get_block(mfn, blk_map[pred_id]);
                if (pred_mblk) ny_mblock_add_edge(pred_mblk, mblk);
            }
        }
    }

    size_t val_map_size = fn->val_count > 0 ? fn->val_count : 1;
    Ny_Machine_Reg *val_map = (Ny_Machine_Reg *)ny_alloc_zero(val_map_size * sizeof(Ny_Machine_Reg));

    for (size_t i = 0; i < fn->val_count; i++) {
        Ny_Value *v = ny_function_get_value(fn, (Ny_Value_ID)i);
        if (v && v->kind == NY_VAL_ARGUMENT) {
            Ny_Reg_Class rc = type_to_reg_class(&ir_mod->types, v->type);
            Ny_Machine_Reg vreg = ny_mfunc_create_vreg(mfn, rc);
            val_map[i] = vreg;
            ny_mfunc_add_param(mfn, vreg);
        } else if (v && (v->kind == NY_VAL_INSTRUCTION || v->kind == NY_VAL_CONSTANT)) {
            Ny_Reg_Class rc = type_to_reg_class(&ir_mod->types, v->type);
            val_map[i] = ny_mfunc_create_vreg(mfn, rc);
        }
    }

    for (size_t b = 0; b < fn->block_count; b++) {
        if (!cfg.is_reachable[b]) continue;
        Ny_Block *blk = &fn->blocks[b];
        Ny_Block_ID mb = blk_map[b];
        Ny_Inst_ID curr = blk->first_inst;

        while (curr != NY_INVALID_INST) {
            Ny_Instruction *inst = ny_function_get_instruction(fn, curr);
            if (!inst || inst->opcode == NY_OPCODE_NONE) {
                curr = inst ? inst->next : NY_INVALID_INST;
                continue;
            }

            Ny_Operand *ops = ny_function_get_operands(fn, inst);
            Ny_Machine_Reg def_reg = inst->result != NY_INVALID_VALUE ? val_map[inst->result] : (Ny_Machine_Reg){0};

            switch (inst->opcode) {
            case NY_OPCODE_CONST: {
                Ny_Machine_Operand mop = lower_operand(ir_mod, fn, ops[0], val_map, blk_map);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_COPY, def_reg, &mop, 1, 0);
                break;
            }
            case NY_OPCODE_CONST_NULL: {
                Ny_Machine_Operand mop = ny_mop_imm_int(0);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_COPY, def_reg, &mop, 1, 0);
                break;
            }
            case NY_OPCODE_ADD:
            case NY_OPCODE_SUB:
            case NY_OPCODE_MUL:
            case NY_OPCODE_DIV_S:
            case NY_OPCODE_DIV_U:
            case NY_OPCODE_REM_S:
            case NY_OPCODE_REM_U:
            case NY_OPCODE_AND:
            case NY_OPCODE_OR:
            case NY_OPCODE_XOR:
            case NY_OPCODE_SHL:
            case NY_OPCODE_SHR:
            case NY_OPCODE_SAR:
            case NY_OPCODE_FADD:
            case NY_OPCODE_FSUB:
            case NY_OPCODE_FMUL:
            case NY_OPCODE_FDIV: {
                Ny_Machine_Operand mops[2];
                mops[0] = lower_operand(ir_mod, fn, ops[0], val_map, blk_map);
                mops[1] = lower_operand(ir_mod, fn, ops[1], val_map, blk_map);
                Ny_Machine_Opcode mopc = opcode_to_mopc((Ny_Opcode)inst->opcode);
                ny_mfunc_append_inst(mfn, mb, mopc, def_reg, mops, 2, 0);
                break;
            }
            case NY_OPCODE_NEG:
            case NY_OPCODE_NOT:
            case NY_OPCODE_FNEG: {
                Ny_Machine_Operand mop = lower_operand(ir_mod, fn, ops[0], val_map, blk_map);
                Ny_Machine_Opcode mopc = opcode_to_mopc((Ny_Opcode)inst->opcode);
                ny_mfunc_append_inst(mfn, mb, mopc, def_reg, &mop, 1, 0);
                break;
            }
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
            case NY_OPCODE_FCMP_GE: {
                Ny_Machine_Operand mops[3];
                mops[0] = lower_operand(ir_mod, fn, ops[0], val_map, blk_map);
                mops[1] = lower_operand(ir_mod, fn, ops[1], val_map, blk_map);
                mops[2] = ny_mop_cond(opcode_to_cond((Ny_Opcode)inst->opcode));
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_CMP, def_reg, mops, 3, 0);
                break;
            }
            case NY_OPCODE_BRANCH: {
                Ny_Machine_Operand mop = ny_mop_block(blk_map[ops[0].blk]);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_JMP, (Ny_Machine_Reg){0}, &mop, 1, NY_MINST_FLAG_TERMINATOR | NY_MINST_FLAG_BRANCH);
                break;
            }
            case NY_OPCODE_BRANCH_IF: {
                Ny_Value_ID cond_val = ops[0].val;
                Ny_Block_ID true_blk = blk_map[ops[1].blk];
                Ny_Block_ID false_blk = blk_map[ops[2].blk];

                Ny_Value *v = ny_function_get_value(fn, cond_val);
                Ny_Machine_Cond cond = NY_MCOND_NE;

                if (v && v->kind == NY_VAL_INSTRUCTION && v->def != NY_INVALID_INST) {
                    Ny_Instruction *def_inst = ny_function_get_instruction(fn, v->def);
                    if (def_inst && def_inst->opcode >= NY_OPCODE_CMP_EQ && def_inst->opcode <= NY_OPCODE_FCMP_GE) {
                        cond = opcode_to_cond((Ny_Opcode)def_inst->opcode);
                    }
                }

                Ny_Machine_Operand jcc_ops[2];
                jcc_ops[0] = ny_mop_cond(cond);
                jcc_ops[1] = ny_mop_block(true_blk);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_JCC, (Ny_Machine_Reg){0}, jcc_ops, 2, NY_MINST_FLAG_BRANCH);

                Ny_Machine_Operand jmp_op = ny_mop_block(false_blk);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_JMP, (Ny_Machine_Reg){0}, &jmp_op, 1, NY_MINST_FLAG_TERMINATOR | NY_MINST_FLAG_BRANCH);
                break;
            }
            case NY_OPCODE_RETURN: {
                if (inst->op_count > 0 && ops[0].kind == NY_OP_VALUE) {
                    Ny_Machine_Operand mop = lower_operand(ir_mod, fn, ops[0], val_map, blk_map);
                    ny_mfunc_append_inst(mfn, mb, NY_MOPC_RET, (Ny_Machine_Reg){0}, &mop, 1, NY_MINST_FLAG_TERMINATOR);
                } else {
                    ny_mfunc_append_inst(mfn, mb, NY_MOPC_RET, (Ny_Machine_Reg){0}, nullptr, 0, NY_MINST_FLAG_TERMINATOR);
                }
                break;
            }
            case NY_OPCODE_TRAP: {
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_TRAP, (Ny_Machine_Reg){0}, nullptr, 0, NY_MINST_FLAG_TERMINATOR);
                break;
            }
            case NY_OPCODE_CALL: {
                Ny_Machine_Operand mops[16];
                size_t mop_count = 0;
                for (size_t j = 0; j < inst->op_count && mop_count < 16; j++) {
                    mops[mop_count++] = lower_operand(ir_mod, fn, ops[j], val_map, blk_map);
                }
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_CALL, def_reg, mops, mop_count, NY_MINST_FLAG_CALL);
                break;
            }
            case NY_OPCODE_LOAD: {
                Ny_Machine_Mem_Op mem;
                memset(&mem, 0, sizeof(mem));
                mem.base = val_map[ops[0].val];
                mem.stack_slot = NY_INVALID_SLOT;
                Ny_Machine_Operand mop = ny_mop_mem(mem);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_LOAD, def_reg, &mop, 1, 0);
                break;
            }
            case NY_OPCODE_STORE: {
                Ny_Machine_Mem_Op mem;
                memset(&mem, 0, sizeof(mem));
                mem.base = val_map[ops[0].val];
                mem.stack_slot = NY_INVALID_SLOT;

                Ny_Machine_Operand mops[2];
                mops[0] = ny_mop_mem(mem);
                mops[1] = lower_operand(ir_mod, fn, ops[1], val_map, blk_map);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_STORE, (Ny_Machine_Reg){0}, mops, 2, NY_MINST_FLAG_SIDE_EFFECT);
                break;
            }
            case NY_OPCODE_ADDR_OFFSET: {
                Ny_Machine_Mem_Op mem;
                memset(&mem, 0, sizeof(mem));
                mem.base = val_map[ops[0].val];
                mem.disp = (int32_t)ops[1].imm_int;
                mem.stack_slot = NY_INVALID_SLOT;
                Ny_Machine_Operand mop = ny_mop_mem(mem);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_LEA, def_reg, &mop, 1, 0);
                break;
            }
            case NY_OPCODE_GLOBAL_ADDR: {
                Ny_Machine_Mem_Op mem;
                memset(&mem, 0, sizeof(mem));
                mem.stack_slot = NY_INVALID_SLOT;
                if (ops[0].kind == NY_OP_GLOBAL) {
                    Ny_Global *g = ny_module_get_global((Ny_Module *)ir_mod, ops[0].global_id);
                    if (g) {
                        mem.symbol = g->id;
                        mem.symbol_name = g->name;
                    } else {
                        mem.symbol = NY_INVALID_GLOBAL;
                    }
                } else if (ops[0].kind == NY_OP_SYMBOL) {
                    mem.symbol = ops[0].sym_id;
                }
                Ny_Machine_Operand mop = ny_mop_mem(mem);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_LEA, def_reg, &mop, 1, 0);
                break;
            }
            case NY_OPCODE_STACK_SLOT: {
                uint32_t size = (uint32_t)ops[0].imm_int;
                uint32_t align = (uint32_t)ops[1].imm_int;
                Ny_Slot_ID sid = ny_mfunc_create_stack_slot(mfn, size, align);

                Ny_Machine_Mem_Op mem;
                memset(&mem, 0, sizeof(mem));
                mem.stack_slot = sid;
                Ny_Machine_Operand mop = ny_mop_mem(mem);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_LEA, def_reg, &mop, 1, 0);
                break;
            }
            case NY_OPCODE_PHI: {
                Ny_Machine_Operand mops[16];
                size_t mop_count = 0;
                for (size_t j = 0; j < inst->op_count && mop_count < 16; j++) {
                    mops[mop_count++] = lower_operand(ir_mod, fn, ops[j], val_map, blk_map);
                }
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_PHI, def_reg, mops, mop_count, 0);
                break;
            }
            case NY_OPCODE_SELECT: {
                Ny_Machine_Operand mops[3];
                mops[0] = lower_operand(ir_mod, fn, ops[0], val_map, blk_map);
                mops[1] = lower_operand(ir_mod, fn, ops[1], val_map, blk_map);
                mops[2] = lower_operand(ir_mod, fn, ops[2], val_map, blk_map);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_SELECT, def_reg, mops, 3, 0);
                break;
            }
            case NY_OPCODE_BITCAST:
            case NY_OPCODE_SEXT:
            case NY_OPCODE_ZEXT:
            case NY_OPCODE_CAST:
            case NY_OPCODE_EXTEND:
            case NY_OPCODE_TRUNCATE: {
                Ny_Machine_Operand mop = lower_operand(ir_mod, fn, ops[0], val_map, blk_map);
                ny_mfunc_append_inst(mfn, mb, NY_MOPC_COPY, def_reg, &mop, 1, 0);
                break;
            }
            default:
                break;
            }

            curr = inst->next;
        }
    }

    ny_free(val_map, val_map_size * sizeof(Ny_Machine_Reg));
    ny_free(blk_map, blk_map_size * sizeof(Ny_Block_ID));
    ny_cfg_info_destroy(&cfg);
    return true;
    (void)diags;
}

bool ny_ir_lower_to_mir(const Ny_Module *ir_mod, Ny_Machine_Module *out_mmod, Ny_Diagnostic_List *diags) {
    ny_mmod_init(out_mmod, ir_mod->name);

    for (size_t g = 0; g < ir_mod->global_count; g++) {
        const Ny_Global *glob = &ir_mod->globals[g];
        ny_mmod_add_global(out_mmod, glob->name, glob->kind, glob->align, glob->init_bytes, glob->init_size);
    }

    for (size_t i = 0; i < ir_mod->function_count; i++) {
        const Ny_Function *fn = &ir_mod->functions[i];
        if (fn->block_count == 0) continue;
        if (!lower_function(ir_mod, fn, out_mmod, diags)) {
            return false;
        }
    }
    return true;
}
