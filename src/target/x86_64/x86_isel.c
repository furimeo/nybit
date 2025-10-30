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

    if (mem.symbol_name.len > 0) {
        xmem.is_rip_relative = true;
        xmem.symbol = mem.symbol_name;
        xmem.disp = mem.disp;
        return xmem;
    }

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
            bool is_fp = (def_r.phys_reg >= X86_XMM0 && def_r.phys_reg <= X86_XMM7) ||
                         (src.kind == X86_OP_REG && src.reg.phys_reg >= X86_XMM0 && src.reg.phys_reg <= X86_XMM7);
            X86_Opcode opc = X86_OPC_MOV;
            if (is_fp) {
                opc = (def_r.size == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD;
            }
            X86_Instruction inst = {
                .opcode = (uint16_t)opc,
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
        bool is_fp = (def_r.phys_reg >= X86_XMM0 && def_r.phys_reg <= X86_XMM7);
        X86_Opcode opc = X86_OPC_MOV;
        if (is_fp) {
            opc = (def_r.size == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD;
        }
        X86_Instruction inst = {
            .opcode = (uint16_t)opc,
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
        bool is_fp = (src.kind == X86_OP_REG && src.reg.phys_reg >= X86_XMM0 && src.reg.phys_reg <= X86_XMM7);
        X86_Opcode opc = X86_OPC_MOV;
        if (is_fp) {
            opc = (sz == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD;
        }
        X86_Instruction inst = {
            .opcode = (uint16_t)opc,
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
    case NY_MOPC_FADD:
    case NY_MOPC_FSUB:
    case NY_MOPC_FMUL:
    case NY_MOPC_FDIV: {
        X86_Operand lhs = lower_operand(mops[0], frame);
        X86_Operand rhs = lower_operand(mops[1], frame);
        X86_Opcode mov_opc = (def_r.size == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD;

        if (!is_same_reg(def_r, lhs)) {
            X86_Instruction mov_inst = {
                .opcode = (uint16_t)mov_opc,
                .size = def_r.size,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(def_r), lhs }
            };
            x86_block_append_inst(xblk, mov_inst);
        }

        X86_Opcode opc = X86_OPC_NONE;
        if (def_r.size == 4) {
            switch ((Ny_Machine_Opcode)minst->opcode) {
            case NY_MOPC_FADD: opc = X86_OPC_ADDSS; break;
            case NY_MOPC_FSUB: opc = X86_OPC_SUBSS; break;
            case NY_MOPC_FMUL: opc = X86_OPC_MULSS; break;
            case NY_MOPC_FDIV: opc = X86_OPC_DIVSS; break;
            default: break;
            }
        } else {
            switch ((Ny_Machine_Opcode)minst->opcode) {
            case NY_MOPC_FADD: opc = X86_OPC_ADDSD; break;
            case NY_MOPC_FSUB: opc = X86_OPC_SUBSD; break;
            case NY_MOPC_FMUL: opc = X86_OPC_MULSD; break;
            case NY_MOPC_FDIV: opc = X86_OPC_DIVSD; break;
            default: break;
            }
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
        size_t gpr_arg_count = 0;
        const X86_Phys_Reg *gpr_args = x86_abi_arg_regs(abi, &gpr_arg_count);
        size_t fp_arg_count = 0;
        const X86_Phys_Reg *fp_args = x86_abi_fp_arg_regs(abi, &fp_arg_count);

        size_t gpr_idx = 0;
        size_t fp_idx = 0;

        for (size_t i = 1; i < minst->op_count; i++) {
            X86_Operand arg_op = lower_operand(mops[i], frame);
            size_t arg_idx = i - 1;
            uint8_t sz = 8;
            if (arg_op.kind == X86_OP_REG) sz = arg_op.reg.size;

            bool is_fp = false;
            if (arg_op.kind == X86_OP_REG && arg_op.reg.phys_reg >= X86_XMM0 && arg_op.reg.phys_reg <= X86_XMM7) {
                is_fp = true;
            }

            if (abi == NY_ABI_WINDOWS_X64) {
                if (arg_idx < 4) {
                    X86_Phys_Reg preg = is_fp ? fp_args[arg_idx] : gpr_args[arg_idx];
                    X86_Reg phys_dst = x86_reg_phys(preg, sz);
                    X86_Opcode mov_opc = is_fp ? ((sz == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD) : X86_OPC_MOV;

                    X86_Instruction mov_arg = {
                        .opcode = (uint16_t)mov_opc,
                        .size = sz,
                        .cond = X86_COND_NONE,
                        .op_count = 2,
                        .ops = { x86_op_reg(phys_dst), arg_op }
                    };
                    x86_block_append_inst(xblk, mov_arg);
                } else {
                    int32_t offset = 32 + (int32_t)((arg_idx - 4) * 8);
                    X86_Mem stack_dst = {
                        .base = x86_reg_phys(X86_RSP, 8),
                        .index = (X86_Reg){0},
                        .scale = 0,
                        .disp = offset,
                        .is_rip_relative = false,
                        .symbol = {0}
                    };
                    X86_Opcode mov_opc = is_fp ? ((sz == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD) : X86_OPC_MOV;
                    X86_Instruction mov_stack_arg = {
                        .opcode = (uint16_t)mov_opc,
                        .size = sz,
                        .cond = X86_COND_NONE,
                        .op_count = 2,
                        .ops = { x86_op_mem(stack_dst), arg_op }
                    };
                    x86_block_append_inst(xblk, mov_stack_arg);
                }
            } else {
                if (is_fp) {
                    if (fp_idx < fp_arg_count) {
                        X86_Reg phys_dst = x86_reg_phys(fp_args[fp_idx++], sz);
                        X86_Opcode mov_opc = (sz == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD;
                        X86_Instruction mov_arg = {
                            .opcode = (uint16_t)mov_opc,
                            .size = sz,
                            .cond = X86_COND_NONE,
                            .op_count = 2,
                            .ops = { x86_op_reg(phys_dst), arg_op }
                        };
                        x86_block_append_inst(xblk, mov_arg);
                    } else {
                        int32_t offset = (int32_t)((arg_idx - 6) * 8);
                        X86_Mem stack_dst = {
                            .base = x86_reg_phys(X86_RSP, 8),
                            .index = (X86_Reg){0},
                            .scale = 0,
                            .disp = offset,
                            .is_rip_relative = false,
                            .symbol = {0}
                        };
                        X86_Opcode mov_opc = (sz == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD;
                        X86_Instruction mov_stack_arg = {
                            .opcode = (uint16_t)mov_opc,
                            .size = sz,
                            .cond = X86_COND_NONE,
                            .op_count = 2,
                            .ops = { x86_op_mem(stack_dst), arg_op }
                        };
                        x86_block_append_inst(xblk, mov_stack_arg);
                    }
                } else {
                    if (gpr_idx < gpr_arg_count) {
                        X86_Reg phys_dst = x86_reg_phys(gpr_args[gpr_idx++], sz);
                        X86_Instruction mov_arg = {
                            .opcode = X86_OPC_MOV,
                            .size = sz,
                            .cond = X86_COND_NONE,
                            .op_count = 2,
                            .ops = { x86_op_reg(phys_dst), arg_op }
                        };
                        x86_block_append_inst(xblk, mov_arg);
                    } else {
                        int32_t offset = (int32_t)((arg_idx - 6) * 8);
                        X86_Mem stack_dst = {
                            .base = x86_reg_phys(X86_RSP, 8),
                            .index = (X86_Reg){0},
                            .scale = 0,
                            .disp = offset,
                            .is_rip_relative = false,
                            .symbol = {0}
                        };
                        X86_Instruction mov_stack_arg = {
                            .opcode = X86_OPC_MOV,
                            .size = sz,
                            .cond = X86_COND_NONE,
                            .op_count = 2,
                            .ops = { x86_op_mem(stack_dst), arg_op }
                        };
                        x86_block_append_inst(xblk, mov_stack_arg);
                    }
                }
            }
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
            bool is_fp = (def_r.phys_reg >= X86_XMM0 && def_r.phys_reg <= X86_XMM7);
            X86_Phys_Reg ret_phys = x86_abi_ret_reg(abi, def_r.size, is_fp);
            X86_Reg ret_reg = x86_reg_phys(ret_phys, def_r.size);
            X86_Opcode mov_opc = is_fp ? ((def_r.size == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD) : X86_OPC_MOV;

            X86_Instruction mov_ret = {
                .opcode = (uint16_t)mov_opc,
                .size = def_r.size,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(def_r), x86_op_reg(ret_reg) }
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
            bool is_fp = (ret_val.kind == X86_OP_REG && ret_val.reg.phys_reg >= X86_XMM0 && ret_val.reg.phys_reg <= X86_XMM7);
            X86_Phys_Reg ret_phys = x86_abi_ret_reg(abi, sz, is_fp);
            X86_Reg ret_reg = x86_reg_phys(ret_phys, sz);
            X86_Opcode mov_opc = is_fp ? ((sz == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD) : X86_OPC_MOV;

            X86_Instruction mov_ret = {
                .opcode = (uint16_t)mov_opc,
                .size = sz,
                .cond = X86_COND_NONE,
                .op_count = 2,
                .ops = { x86_op_reg(ret_reg), ret_val }
            };
            x86_block_append_inst(xblk, mov_ret);
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


    for (size_t b = 0; b < mfn->block_count; b++) {
        const Ny_Machine_Block *mblk = &mfn->blocks[b];
        X86_Block *xblk = &out_fn->blocks[b];
        xblk->id = mblk->id;
        xblk->name = mblk->name;

        if (b == 0 || mblk->id == mfn->entry_block) {
            x86_emit_prologue(xblk, &out_fn->frame);

            size_t gpr_abi_count = 0;
            const X86_Phys_Reg *gpr_args = x86_abi_arg_regs(target->abi, &gpr_abi_count);
            size_t fp_abi_count = 0;
            const X86_Phys_Reg *fp_args = x86_abi_fp_arg_regs(target->abi, &fp_abi_count);

            size_t gpr_idx = 0;
            size_t fp_idx = 0;

            for (size_t p = 0; p < mfn->param_count; p++) {
                X86_Reg vreg = lower_reg(mfn->param_regs[p]);
                bool is_fp = (mfn->param_regs[p].reg_class == NY_REG_CLASS_FP32 || mfn->param_regs[p].reg_class == NY_REG_CLASS_FP64);
                X86_Opcode mov_opc = is_fp ? ((vreg.size == 4) ? X86_OPC_MOVSS : X86_OPC_MOVSD) : X86_OPC_MOV;

                if (target->abi == NY_ABI_WINDOWS_X64) {
                    if (p < 4) {
                        X86_Phys_Reg preg_id = is_fp ? fp_args[p] : gpr_args[p];
                        X86_Reg preg = x86_reg_phys(preg_id, vreg.size);
                        if (vreg.is_virtual || vreg.phys_reg != preg.phys_reg) {
                            X86_Instruction copy_param = {
                                .opcode = (uint16_t)mov_opc,
                                .size = vreg.size,
                                .cond = X86_COND_NONE,
                                .op_count = 2,
                                .ops = { x86_op_reg(vreg), x86_op_reg(preg) }
                            };
                            x86_block_append_inst(xblk, copy_param);
                        }
                    } else {
                        int32_t offset = 16 + 32 + (int32_t)((p - 4) * 8);
                        X86_Mem arg_mem = {
                            .base = x86_reg_phys(X86_RBP, 8),
                            .index = (X86_Reg){0},
                            .scale = 0,
                            .disp = offset,
                            .is_rip_relative = false,
                            .symbol = {0}
                        };
                        X86_Instruction load_param = {
                            .opcode = (uint16_t)mov_opc,
                            .size = vreg.size,
                            .cond = X86_COND_NONE,
                            .op_count = 2,
                            .ops = { x86_op_reg(vreg), x86_op_mem(arg_mem) }
                        };
                        x86_block_append_inst(xblk, load_param);
                    }
                } else {
                    if (is_fp) {
                        if (fp_idx < fp_abi_count) {
                            X86_Reg preg = x86_reg_phys(fp_args[fp_idx++], vreg.size);
                            if (vreg.is_virtual || vreg.phys_reg != preg.phys_reg) {
                                X86_Instruction copy_param = {
                                    .opcode = (uint16_t)mov_opc,
                                    .size = vreg.size,
                                    .cond = X86_COND_NONE,
                                    .op_count = 2,
                                    .ops = { x86_op_reg(vreg), x86_op_reg(preg) }
                                };
                                x86_block_append_inst(xblk, copy_param);
                            }
                        } else {
                            int32_t offset = 16 + (int32_t)((p - 6) * 8);
                            X86_Mem arg_mem = {
                                .base = x86_reg_phys(X86_RBP, 8),
                                .index = (X86_Reg){0},
                                .scale = 0,
                                .disp = offset,
                                .is_rip_relative = false,
                                .symbol = {0}
                            };
                            X86_Instruction load_param = {
                                .opcode = (uint16_t)mov_opc,
                                .size = vreg.size,
                                .cond = X86_COND_NONE,
                                .op_count = 2,
                                .ops = { x86_op_reg(vreg), x86_op_mem(arg_mem) }
                            };
                            x86_block_append_inst(xblk, load_param);
                        }
                    } else {
                        if (gpr_idx < gpr_abi_count) {
                            X86_Reg preg = x86_reg_phys(gpr_args[gpr_idx++], vreg.size);
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
                        } else {
                            int32_t offset = 16 + (int32_t)((p - 6) * 8);
                            X86_Mem arg_mem = {
                                .base = x86_reg_phys(X86_RBP, 8),
                                .index = (X86_Reg){0},
                                .scale = 0,
                                .disp = offset,
                                .is_rip_relative = false,
                                .symbol = {0}
                            };
                            X86_Instruction load_param = {
                                .opcode = X86_OPC_MOV,
                                .size = vreg.size,
                                .cond = X86_COND_NONE,
                                .op_count = 2,
                                .ops = { x86_op_reg(vreg), x86_op_mem(arg_mem) }
                            };
                            x86_block_append_inst(xblk, load_param);
                        }
                    }
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

    out_x86_mod->global_capacity = mmod->global_count;
    if (out_x86_mod->global_capacity > 0) {
        out_x86_mod->globals = (Ny_Machine_Global *)ny_alloc_zero(out_x86_mod->global_capacity * sizeof(Ny_Machine_Global));
        for (size_t g = 0; g < mmod->global_count; g++) {
            const Ny_Machine_Global *mg = &mmod->globals[g];
            Ny_Machine_Global *dst = &out_x86_mod->globals[g];
            dst->name = mg->name;
            dst->kind = mg->kind;
            dst->align = mg->align;
            dst->data_size = mg->data_size;
            if (mg->data && mg->data_size > 0) {
                dst->data = (uint8_t *)ny_alloc(mg->data_size);
                memcpy(dst->data, mg->data, mg->data_size);
            }
            out_x86_mod->global_count++;
        }
    }

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
