// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "x86_internal.h"
#include <string.h>

static X86_Reg lower_reg(Ny_Machine_Reg mreg) {
    uint8_t sz = 8;
    if (mreg.reg_class == NY_REG_CLASS_GPR32 || mreg.reg_class == NY_REG_CLASS_FP32) {
        sz = 4;
    }
    if (mreg.is_virtual) {
        return x86_reg_virt(mreg.id, sz);
    }
    return x86_reg_phys((X86_Phys_Reg)mreg.phys_reg, sz);
}

static X86_Mem lower_mem(Ny_Machine_Mem_Op mem, const X86_Stack_Frame *frame) {
    X86_Mem xmem;
    memset(&xmem, 0, sizeof(xmem));

    if (mem.stack_slot != NY_INVALID_SLOT && mem.stack_slot < frame->slot_count) {
        xmem.base = x86_reg_phys(X86_RBP, 8);
        xmem.disp = frame->slot_offsets[mem.stack_slot] + mem.disp;
        return xmem;
    }

    if (ny_mreg_is_valid(mem.base)) {
        xmem.base = lower_reg(mem.base);
    }
    if (ny_mreg_is_valid(mem.index)) {
        xmem.index = lower_reg(mem.index);
        xmem.scale = mem.scale;
    }
    xmem.disp = mem.disp;
    return xmem;
}

static X86_Operand lower_operand(Ny_Machine_Operand mop, const X86_Stack_Frame *frame) {
    switch (mop.kind) {
    case NY_MOP_KIND_REG:
        return x86_op_reg(lower_reg(mop.reg));
    case NY_MOP_KIND_IMM_INT:
        return x86_op_imm(mop.imm_int);
    case NY_MOP_KIND_MEM:
        return x86_op_mem(lower_mem(mop.mem, frame));
    case NY_MOP_KIND_BLOCK:
        return x86_op_label(mop.block);
    case NY_MOP_KIND_SYMBOL:
        return x86_op_global(mop.symbol.name);
    case NY_MOP_KIND_COND:
    case NY_MOP_KIND_IMM_FLOAT:
    case NY_MOP_KIND_NONE:
    default: {
        X86_Operand empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    }
}

static X86_Cond lower_cond(Ny_Machine_Cond cond) {
    switch (cond) {
    case NY_MCOND_EQ:   return X86_COND_E;
    case NY_MCOND_NE:   return X86_COND_NE;
    case NY_MCOND_LT_S: return X86_COND_L;
    case NY_MCOND_LE_S: return X86_COND_LE;
    case NY_MCOND_GT_S: return X86_COND_G;
    case NY_MCOND_GE_S: return X86_COND_GE;
    case NY_MCOND_LT_U: return X86_COND_B;
    case NY_MCOND_LE_U: return X86_COND_BE;
    case NY_MCOND_GT_U: return X86_COND_A;
    case NY_MCOND_GE_U: return X86_COND_AE;
    case NY_MCOND_NONE:
    default:
        return X86_COND_NONE;
    }
}

static bool is_same_reg(X86_Reg a, X86_Operand b) {
    if (b.kind != X86_OP_REG) return false;
    if (a.is_virtual != b.reg.is_virtual) return false;
    if (a.is_virtual) return a.id == b.reg.id;
    return a.phys_reg == b.reg.phys_reg;
}

static void lower_instruction(X86_Block *xblk, const Ny_Machine_Function *mfn,
                              const Ny_Machine_Instruction *minst, const X86_Stack_Frame *frame,
                              Ny_Target_ABI abi) {
    Ny_Machine_Operand *mops = ny_mfunc_get_operands(mfn, minst);
    X86_Reg def_r = {0};
    bool has_def = ny_mreg_is_valid(minst->def_reg);
    if (has_def) {
        def_r = lower_reg(minst->def_reg);
    }

    switch ((Ny_Machine_Opcode)minst->opcode) {
    case NY_MOPC_COPY: {
        X86_Operand src = lower_operand(mops[0], frame);
        if (!is_same_reg(def_r, src)) {
            X86_Instruction inst = {
                .opcode = X86_OPC_MOV,
                .size = def_r.size,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(def_r), src }
            };
            x86_block_append_inst(xblk, inst);
        }
        break;
    }
    case NY_MOPC_LOAD: {
        X86_Operand src = lower_operand(mops[0], frame);
        X86_Instruction inst = {
            .opcode = X86_OPC_MOV,
            .size = def_r.size,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = { x86_op_reg(def_r), src }
        };
        x86_block_append_inst(xblk, inst);
        break;
    }
    case NY_MOPC_STORE: {
        X86_Operand dst = lower_operand(mops[0], frame);
        X86_Operand src = lower_operand(mops[1], frame);
        uint8_t sz = 8;
        if (src.kind == X86_OP_REG) sz = src.reg.size;
        X86_Instruction inst = {
            .opcode = X86_OPC_MOV,
            .size = sz,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = { dst, src }
        };
        x86_block_append_inst(xblk, inst);
        break;
    }
    case NY_MOPC_LEA: {
        X86_Operand src = lower_operand(mops[0], frame);
        X86_Instruction inst = {
            .opcode = X86_OPC_LEA,
            .size = def_r.size,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = { x86_op_reg(def_r), src }
        };
        x86_block_append_inst(xblk, inst);
        break;
    }
    case NY_MOPC_ADD:
    case NY_MOPC_SUB:
    case NY_MOPC_IMUL:
    case NY_MOPC_AND:
    case NY_MOPC_OR:
    case NY_MOPC_XOR:
    case NY_MOPC_SHL:
    case NY_MOPC_SHR:
    case NY_MOPC_SAR: {
        X86_Operand lhs = lower_operand(mops[0], frame);
        X86_Operand rhs = lower_operand(mops[1], frame);

        if (!is_same_reg(def_r, lhs)) {
            X86_Instruction mov_inst = {
                .opcode = X86_OPC_MOV,
                .size = def_r.size,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(def_r), lhs }
            };
            x86_block_append_inst(xblk, mov_inst);
        }

        X86_Opcode opc = X86_OPC_NONE;
        switch ((Ny_Machine_Opcode)minst->opcode) {
        case NY_MOPC_ADD:  opc = X86_OPC_ADD; break;
        case NY_MOPC_SUB:  opc = X86_OPC_SUB; break;
        case NY_MOPC_IMUL: opc = X86_OPC_IMUL; break;
        case NY_MOPC_AND:  opc = X86_OPC_AND; break;
        case NY_MOPC_OR:   opc = X86_OPC_OR; break;
        case NY_MOPC_XOR:  opc = X86_OPC_XOR; break;
        case NY_MOPC_SHL:  opc = X86_OPC_SHL; break;
        case NY_MOPC_SHR:  opc = X86_OPC_SHR; break;
        case NY_MOPC_SAR:  opc = X86_OPC_SAR; break;
        default: break;
        }

        X86_Instruction op_inst = {
            .opcode = (uint16_t)opc,
            .size = def_r.size,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = { x86_op_reg(def_r), rhs }
        };
        x86_block_append_inst(xblk, op_inst);
        break;
    }
    case NY_MOPC_IDIV:
    case NY_MOPC_IREM: {
        X86_Operand lhs = lower_operand(mops[0], frame);
        X86_Operand rhs = lower_operand(mops[1], frame);
        X86_Reg rax = x86_reg_phys(X86_RAX, def_r.size);
        X86_Reg rdx = x86_reg_phys(X86_RDX, def_r.size);

        X86_Instruction mov_rax = {
            .opcode = X86_OPC_MOV,
            .size = def_r.size,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = { x86_op_reg(rax), lhs }
        };
        x86_block_append_inst(xblk, mov_rax);

        X86_Instruction cdq_cqo = {
            .opcode = (uint16_t)(def_r.size == 4 ? X86_OPC_CDQ : X86_OPC_CQO),
            .size = def_r.size,
            .cond = X86_COND_NONE,
            .op_count = 0
        };
        x86_block_append_inst(xblk, cdq_cqo);

        X86_Instruction idiv_inst = {
            .opcode = X86_OPC_IDIV,
            .size = def_r.size,
            .cond = X86_COND_NONE,
            .op_count = 1,
            .ops = { rhs }
        };
        x86_block_append_inst(xblk, idiv_inst);

        X86_Reg result_reg = (minst->opcode == NY_MOPC_IDIV) ? rax : rdx;
        X86_Instruction mov_res = {
            .opcode = X86_OPC_MOV,
            .size = def_r.size,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = { x86_op_reg(def_r), x86_op_reg(result_reg) }
        };
        x86_block_append_inst(xblk, mov_res);
        break;
    }
    case NY_MOPC_NEG:
    case NY_MOPC_NOT: {
        X86_Operand src = lower_operand(mops[0], frame);
        if (!is_same_reg(def_r, src)) {
            X86_Instruction mov_inst = {
                .opcode = X86_OPC_MOV,
                .size = def_r.size,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(def_r), src }
            };
            x86_block_append_inst(xblk, mov_inst);
        }
        X86_Opcode opc = (minst->opcode == NY_MOPC_NEG) ? X86_OPC_NEG : X86_OPC_NOT;
        X86_Instruction op_inst = {
            .opcode = (uint16_t)opc,
            .size = def_r.size,
            .cond = X86_COND_NONE,
            .op_count = 1,
            .ops = { x86_op_reg(def_r) }
        };
        x86_block_append_inst(xblk, op_inst);
        break;
    }
    case NY_MOPC_CMP: {
        X86_Operand lhs = lower_operand(mops[0], frame);
        X86_Operand rhs = lower_operand(mops[1], frame);
        X86_Cond cond = lower_cond(mops[2].cond);

        uint8_t sz = 8;
        if (lhs.kind == X86_OP_REG) sz = lhs.reg.size;
        else if (rhs.kind == X86_OP_REG) sz = rhs.reg.size;

        X86_Instruction cmp_inst = {
            .opcode = X86_OPC_CMP,
            .size = sz,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = { lhs, rhs }
        };
        x86_block_append_inst(xblk, cmp_inst);

        if (has_def) {
            X86_Instruction setcc_inst = {
                .opcode = X86_OPC_SETCC,
                .size = 1,
                .cond = (uint8_t)cond,
                .op_count = 1,
                .ops = { x86_op_reg(def_r) }
            };
            x86_block_append_inst(xblk, setcc_inst);

            X86_Instruction and_inst = {
                .opcode = X86_OPC_AND,
                .size = def_r.size,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(def_r), x86_op_imm(1) }
            };
            x86_block_append_inst(xblk, and_inst);
        }
        break;
    }
    case NY_MOPC_JMP: {
        X86_Instruction jmp_inst = {
            .opcode = X86_OPC_JMP,
            .size = 8,
            .cond = X86_COND_NONE,
            .op_count = 1,
            .ops = { x86_op_label(mops[0].block) }
        };
        x86_block_append_inst(xblk, jmp_inst);
        break;
    }
    case NY_MOPC_JCC: {
        X86_Cond cond = lower_cond(mops[0].cond);
        X86_Instruction jcc_inst = {
            .opcode = X86_OPC_JCC,
            .size = 8,
            .cond = (uint8_t)cond,
            .op_count = 1,
            .ops = { x86_op_label(mops[1].block) }
        };
        x86_block_append_inst(xblk, jcc_inst);
        break;
    }
    case NY_MOPC_CALL: {
        size_t abi_arg_count = 0;
        const X86_Phys_Reg *abi_args = x86_abi_arg_regs(abi, &abi_arg_count);

        for (size_t i = 1; i < minst->op_count && (i - 1) < abi_arg_count; i++) {
            X86_Operand arg_op = lower_operand(mops[i], frame);
            uint8_t sz = 8;
            if (arg_op.kind == X86_OP_REG) sz = arg_op.reg.size;
            X86_Reg phys_dst = x86_reg_phys(abi_args[i - 1], sz);

            X86_Instruction mov_arg = {
                .opcode = X86_OPC_MOV,
                .size = sz,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(phys_dst), arg_op }
            };
            x86_block_append_inst(xblk, mov_arg);
        }

        X86_Operand callee = lower_operand(mops[0], frame);
        X86_Instruction call_inst = {
            .opcode = X86_OPC_CALL,
            .size = 8,
            .cond = X86_COND_NONE,
            .op_count = 1,
            .ops = { callee }
        };
        x86_block_append_inst(xblk, call_inst);

        if (has_def) {
            X86_Reg rax = x86_reg_phys(X86_RAX, def_r.size);
            X86_Instruction mov_ret = {
                .opcode = X86_OPC_MOV,
                .size = def_r.size,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(def_r), x86_op_reg(rax) }
            };
            x86_block_append_inst(xblk, mov_ret);
        }
        break;
    }
    case NY_MOPC_RET: {
        if (minst->op_count > 0 && mops[0].kind != NY_MOP_KIND_NONE) {
            X86_Operand ret_val = lower_operand(mops[0], frame);
            uint8_t sz = 8;
            if (ret_val.kind == X86_OP_REG) sz = ret_val.reg.size;
            X86_Reg rax = x86_reg_phys(X86_RAX, sz);

            X86_Instruction mov_rax = {
                .opcode = X86_OPC_MOV,
                .size = sz,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(rax), ret_val }
            };
            x86_block_append_inst(xblk, mov_rax);
        }
        x86_emit_epilogue(xblk, frame);
        X86_Instruction ret_inst = {
            .opcode = X86_OPC_RET,
            .size = 8,
            .cond = X86_COND_NONE,
            .op_count = 0
        };
        x86_block_append_inst(xblk, ret_inst);
        break;
    }
    case NY_MOPC_TRAP: {
        X86_Instruction ud2_inst = {
            .opcode = X86_OPC_UD2,
            .size = 0,
            .cond = X86_COND_NONE,
            .op_count = 0
        };
        x86_block_append_inst(xblk, ud2_inst);
        break;
    }
    default:
        break;
    }
}

static bool x86_lower_func_impl(const Ny_Target *target, const Ny_Machine_Function *mfn,
                                X86_Function *out_fn, Ny_Diagnostic_List *diags) {
    (void)diags;
    x86_func_init(out_fn, mfn->name, target->abi);
    x86_frame_layout(&out_fn->frame, mfn, target->abi);

    out_fn->block_count = mfn->block_count;
    out_fn->block_capacity = mfn->block_count;
    if (out_fn->block_capacity > 0) {
        out_fn->blocks = (X86_Block *)ny_alloc_zero(out_fn->block_capacity * sizeof(X86_Block));
    }

    size_t abi_arg_count = 0;
    const X86_Phys_Reg *abi_args = x86_abi_arg_regs(target->abi, &abi_arg_count);

    for (size_t b = 0; b < mfn->block_count; b++) {
        const Ny_Machine_Block *mblk = &mfn->blocks[b];
        X86_Block *xblk = &out_fn->blocks[b];
        xblk->id = mblk->id;
        xblk->name = mblk->name;

        if (b == 0 || mblk->id == mfn->entry_block) {
            x86_emit_prologue(xblk, &out_fn->frame);

            for (size_t p = 0; p < mfn->param_count && p < abi_arg_count; p++) {
                X86_Reg vreg = lower_reg(mfn->param_regs[p]);
                X86_Reg preg = x86_reg_phys(abi_args[p], vreg.size);
                if (vreg.is_virtual || vreg.phys_reg != preg.phys_reg) {
                    X86_Instruction copy_param = {
                        .opcode = X86_OPC_MOV,
                        .size = vreg.size,
                        .cond = X86_COND_NONE,
                        .op_count = 2,
                        .ops = { x86_op_reg(vreg), x86_op_reg(preg) }
                    };
                    x86_block_append_inst(xblk, copy_param);
                }
            }
        }

        Ny_Inst_ID curr = mblk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Machine_Instruction *minst = ny_mfunc_get_inst(mfn, curr);
            if (!minst) break;
            lower_instruction(xblk, mfn, minst, &out_fn->frame, target->abi);
            curr = minst->next;
        }
    }

    return true;
}

bool x86_lower_machine_func(const Ny_Target *target, const Ny_Machine_Function *mfn,
                            void **out_target_fn, Ny_Diagnostic_List *diags) {
    X86_Function *fn = (X86_Function *)ny_alloc_zero(sizeof(X86_Function));
    if (!x86_lower_func_impl(target, mfn, fn, diags)) {
        ny_free(fn, sizeof(X86_Function));
        return false;
    }
    *out_target_fn = fn;
    return true;
}

bool x86_lower_machine_mod(const Ny_Target *target, const Ny_Machine_Module *mmod,
                           X86_Module *out_x86_mod, Ny_Diagnostic_List *diags) {
    x86_mod_init(out_x86_mod, mmod->name, target->abi);
    out_x86_mod->function_capacity = mmod->function_count;
    if (out_x86_mod->function_capacity > 0) {
        out_x86_mod->functions = (X86_Function *)ny_alloc_zero(out_x86_mod->function_capacity * sizeof(X86_Function));
    }

    for (size_t i = 0; i < mmod->function_count; i++) {
        if (!x86_lower_func_impl(target, &mmod->functions[i], &out_x86_mod->functions[i], diags)) {
            return false;
        }
        out_x86_mod->function_count++;
    }

    return true;
}
