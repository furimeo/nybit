// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "aarch64_internal.h"
#include <nybit/target_aarch64.h>
#include <nybit/ir.h>
#include <string.h>

static AArch64_Reg lower_reg(Ny_Machine_Reg mreg) {
    uint8_t sz = 8;
    if (mreg.reg_class == NY_REG_CLASS_GPR32 || mreg.reg_class == NY_REG_CLASS_FP32) {
        sz = 4;
    }
    if (mreg.is_virtual) {
        return aarch64_reg_virt(mreg.id, sz);
    }
    return aarch64_reg_phys((AArch64_Phys_Reg)mreg.phys_reg, sz);
}

static AArch64_Mem lower_mem(Ny_Machine_Mem_Op mem, const AArch64_Stack_Frame *frame) {
    AArch64_Mem amem;
    memset(&amem, 0, sizeof(amem));

    if (mem.symbol_name.len > 0) {
        amem.is_symbol = true;
        amem.symbol = mem.symbol_name;
        amem.disp = mem.disp;
        return amem;
    }

    if (mem.stack_slot != NY_INVALID_SLOT && mem.stack_slot < frame->slot_count) {
        amem.base = aarch64_reg_phys(AARCH64_FP, 8);
        amem.disp = frame->slot_offsets[mem.stack_slot] + mem.disp;
        return amem;
    }

    if (ny_mreg_is_valid(mem.base)) {
        amem.base = lower_reg(mem.base);
    }
    amem.disp = mem.disp;
    return amem;
}

static AArch64_Operand lower_operand(Ny_Machine_Operand mop, const AArch64_Stack_Frame *frame) {
    switch (mop.kind) {
    case NY_MOP_KIND_REG:
        return aarch64_op_reg(lower_reg(mop.reg));
    case NY_MOP_KIND_IMM_INT:
        return aarch64_op_imm(mop.imm_int);
    case NY_MOP_KIND_MEM:
        return aarch64_op_mem(lower_mem(mop.mem, frame));
    case NY_MOP_KIND_BLOCK:
        return aarch64_op_label(mop.block);
    case NY_MOP_KIND_SYMBOL:
        return aarch64_op_global(mop.symbol.name);
    case NY_MOP_KIND_COND:
    case NY_MOP_KIND_IMM_FLOAT:
    case NY_MOP_KIND_NONE:
    default: {
        AArch64_Operand empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    }
}

static AArch64_Cond lower_cond(Ny_Machine_Cond cond) {
    switch (cond) {
    case NY_MCOND_EQ:   return AARCH64_COND_EQ;
    case NY_MCOND_NE:   return AARCH64_COND_NE;
    case NY_MCOND_LT_S: return AARCH64_COND_LT_S;
    case NY_MCOND_LE_S: return AARCH64_COND_LE_S;
    case NY_MCOND_GT_S: return AARCH64_COND_GT_S;
    case NY_MCOND_GE_S: return AARCH64_COND_GE_S;
    case NY_MCOND_LT_U: return AARCH64_COND_LT_U;
    case NY_MCOND_LE_U: return AARCH64_COND_LE_U;
    case NY_MCOND_GT_U: return AARCH64_COND_GT_U;
    case NY_MCOND_GE_U: return AARCH64_COND_GE_U;
    case NY_MCOND_NONE:
    default:
        return AARCH64_COND_NONE;
    }
}

static bool is_same_reg(AArch64_Reg a, AArch64_Operand b) {
    if (b.kind != AARCH64_OP_REG) return false;
    if (a.is_virtual != b.reg.is_virtual) return false;
    if (a.is_virtual) return a.id == b.reg.id;
    return a.phys_reg == b.reg.phys_reg;
}

static void emit_mov_imm(AArch64_Block *blk, AArch64_Reg dst, int64_t val) {
    uint64_t uval = (uint64_t)val;
    if (dst.size == 4) {
        uval &= 0xFFFFFFFF;
    }

    bool first = true;
    for (int hw = 0; hw < (dst.size == 4 ? 2 : 4); hw++) {
        uint16_t chunk = (uint16_t)((uval >> (hw * 16)) & 0xFFFF);
        if (first) {
            AArch64_Instruction inst = {
                .opcode = AARCH64_OPC_MOVZ,
                .size = dst.size,
                .cond = 0,
                .op_count = 2,
                .shift = (uint8_t)hw,
                .ops = { aarch64_op_reg(dst), aarch64_op_imm(chunk) }
            };
            aarch64_block_append_inst(blk, inst);
            first = false;
        } else if (chunk != 0) {
            AArch64_Instruction inst = {
                .opcode = AARCH64_OPC_MOVK,
                .size = dst.size,
                .cond = 0,
                .op_count = 2,
                .shift = (uint8_t)hw,
                .ops = { aarch64_op_reg(dst), aarch64_op_imm(chunk) }
            };
            aarch64_block_append_inst(blk, inst);
        }
    }
    if (first) {
        AArch64_Instruction inst = {
            .opcode = AARCH64_OPC_MOVZ,
            .size = dst.size,
            .cond = 0,
            .op_count = 2,
            .shift = 0,
            .ops = { aarch64_op_reg(dst), aarch64_op_imm(0) }
        };
        aarch64_block_append_inst(blk, inst);
    }
}

static void emit_mov(AArch64_Block *blk, AArch64_Reg dst, AArch64_Operand src) {
    if (src.kind == AARCH64_OP_REG && is_same_reg(dst, src)) {
        return;
    }
    if (src.kind == AARCH64_OP_IMM) {
        emit_mov_imm(blk, dst, src.imm);
        return;
    }
    AArch64_Instruction inst = {
        .opcode = AARCH64_OPC_MOV,
        .size = dst.size,
        .cond = 0,
        .op_count = 2,
        .shift = 0,
        .ops = { aarch64_op_reg(dst), src }
    };
    aarch64_block_append_inst(blk, inst);
}

static bool imm_fits_addsub(int64_t val) {
    if (val >= 0 && val <= 4095) return true;
    if (val >= 4096 && val <= 65535 && (val & 0xFFF) == 0) return true;
    return false;
}

static void emit_slot_addr(AArch64_Block *blk, const AArch64_Stack_Frame *frame,
                           Ny_Slot_ID slot, AArch64_Reg dst) {
    int32_t off = (int32_t)frame->slot_offsets[slot];
    AArch64_Reg fp = aarch64_reg_phys(AARCH64_FP, 8);
    if (imm_fits_addsub(off)) {
        AArch64_Instruction add = {
            .opcode = AARCH64_OPC_ADD, .size = 8, .cond = 0, .op_count = 3, .shift = 0,
            .ops = { aarch64_op_reg(dst), aarch64_op_reg(fp), aarch64_op_imm(off) }
        };
        aarch64_block_append_inst(blk, add);
    } else {
        AArch64_Reg scratch = aarch64_reg_phys(AARCH64_X16, 8);
        emit_mov_imm(blk, scratch, off);
        AArch64_Instruction add = {
            .opcode = AARCH64_OPC_ADD, .size = 8, .cond = 0, .op_count = 3, .shift = 0,
            .ops = { aarch64_op_reg(dst), aarch64_op_reg(fp), aarch64_op_reg(scratch) }
        };
        aarch64_block_append_inst(blk, add);
    }
}

static void emit_load_chunk(AArch64_Block *blk, const AArch64_Stack_Frame *frame,
                            Ny_Slot_ID slot, int32_t off, AArch64_Reg dst) {
    AArch64_Mem mem = {
        .base = aarch64_reg_phys(AARCH64_FP, 8),
        .disp = (int32_t)frame->slot_offsets[slot] + off,
    };
    AArch64_Instruction ldr = {
        .opcode = AARCH64_OPC_LDR, .size = 8, .cond = 0, .op_count = 2, .shift = 0,
        .ops = { aarch64_op_reg(dst), aarch64_op_mem(mem) }
    };
    aarch64_block_append_inst(blk, ldr);
}

static void emit_store_chunk(AArch64_Block *blk, const AArch64_Stack_Frame *frame,
                             AArch64_Reg src, Ny_Slot_ID slot, int32_t off) {
    AArch64_Mem mem = {
        .base = aarch64_reg_phys(AARCH64_FP, 8),
        .disp = (int32_t)frame->slot_offsets[slot] + off,
    };
    AArch64_Instruction str = {
        .opcode = AARCH64_OPC_STR, .size = src.size, .cond = 0, .op_count = 2, .shift = 0,
        .ops = { aarch64_op_reg(src), aarch64_op_mem(mem) }
    };
    aarch64_block_append_inst(blk, str);
}

static void emit_load_fp_chunk(AArch64_Block *blk, const AArch64_Stack_Frame *frame,
                               Ny_Slot_ID slot, int32_t off, AArch64_Reg dst) {
    AArch64_Mem mem = {
        .base = aarch64_reg_phys(AARCH64_FP, 8),
        .disp = (int32_t)frame->slot_offsets[slot] + off,
    };
    AArch64_Instruction ldr = {
        .opcode = AARCH64_OPC_LDR, .size = dst.size, .cond = 0, .op_count = 2, .shift = 0,
        .ops = { aarch64_op_reg(dst), aarch64_op_mem(mem) }
    };
    aarch64_block_append_inst(blk, ldr);
}

static void emit_store_fp_chunk(AArch64_Block *blk, const AArch64_Stack_Frame *frame,
                                AArch64_Reg src, Ny_Slot_ID slot, int32_t off) {
    AArch64_Mem mem = {
        .base = aarch64_reg_phys(AARCH64_FP, 8),
        .disp = (int32_t)frame->slot_offsets[slot] + off,
    };
    AArch64_Instruction str = {
        .opcode = AARCH64_OPC_STR, .size = src.size, .cond = 0, .op_count = 2, .shift = 0,
        .ops = { aarch64_op_reg(src), aarch64_op_mem(mem) }
    };
    aarch64_block_append_inst(blk, str);
}

static void emit_store_sp(AArch64_Block *blk, AArch64_Reg src, int32_t sp_off) {
    AArch64_Mem mem = {
        .base = aarch64_reg_phys(AARCH64_SP, 8),
        .disp = sp_off,
    };
    AArch64_Instruction str = {
        .opcode = AARCH64_OPC_STR, .size = src.size, .cond = 0, .op_count = 2, .shift = 0,
        .ops = { aarch64_op_reg(src), aarch64_op_mem(mem) }
    };
    aarch64_block_append_inst(blk, str);
}

static void emit_load_sp(AArch64_Block *blk, int32_t sp_off, AArch64_Reg dst) {
    AArch64_Mem mem = {
        .base = aarch64_reg_phys(AARCH64_SP, 8),
        .disp = sp_off,
    };
    AArch64_Instruction ldr = {
        .opcode = AARCH64_OPC_LDR, .size = dst.size, .cond = 0, .op_count = 2, .shift = 0,
        .ops = { aarch64_op_reg(dst), aarch64_op_mem(mem) }
    };
    aarch64_block_append_inst(blk, ldr);
}

static void lower_instruction(AArch64_Block *xblk, const Ny_Machine_Function *mfn,
                              const Ny_Machine_Instruction *minst, const AArch64_Stack_Frame *frame,
                              Ny_Target_ABI abi, const Ny_Type_Table *tt) {
    Ny_Machine_Operand *mops = ny_mfunc_get_operands(mfn, minst);
    AArch64_Reg def_r = {0};
    bool has_def = ny_mreg_is_valid(minst->def_reg);
    if (has_def) {
        def_r = lower_reg(minst->def_reg);
    }

    switch ((Ny_Machine_Opcode)minst->opcode) {
    case NY_MOPC_COPY: {
        AArch64_Operand src = lower_operand(mops[0], frame);
        emit_mov(xblk, def_r, src);
        break;
    }
    case NY_MOPC_LOAD: {
        AArch64_Operand src = lower_operand(mops[0], frame);
        if (src.kind == AARCH64_OP_IMM) {
            emit_mov_imm(xblk, def_r, src.imm);
        } else if (src.kind == AARCH64_OP_REG) {
            emit_mov(xblk, def_r, src);
        } else if (src.kind == AARCH64_OP_MEM) {
            if (src.mem.is_symbol) {
                AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, 8);
                AArch64_Instruction adrp = {
                    .opcode = AARCH64_OPC_ADRP,
                    .size = 8,
                    .cond = 0,
                    .op_count = 2,
                    .shift = 0,
                    .ops = { aarch64_op_reg(tmp), aarch64_op_global(src.mem.symbol) }
                };
                aarch64_block_append_inst(xblk, adrp);

                AArch64_Instruction ldr = {
                    .opcode = AARCH64_OPC_LDR_SYM,
                    .size = def_r.size,
                    .cond = 0,
                    .op_count = 3,
                    .shift = 0,
                    .ops = {
                        aarch64_op_reg(def_r),
                        aarch64_op_reg(tmp),
                        aarch64_op_global(src.mem.symbol)
                    }
                };
                aarch64_block_append_inst(xblk, ldr);
            } else {
                AArch64_Instruction ldr = {
                    .opcode = AARCH64_OPC_LDR,
                    .size = def_r.size,
                    .cond = 0,
                    .op_count = 2,
                    .shift = 0,
                    .ops = { aarch64_op_reg(def_r), src }
                };
                aarch64_block_append_inst(xblk, ldr);
            }
        }
        break;
    }
    case NY_MOPC_STORE: {
        AArch64_Operand dst_op = lower_operand(mops[0], frame);
        AArch64_Operand src_op = lower_operand(mops[1], frame);
        if (dst_op.kind == AARCH64_OP_MEM && dst_op.mem.is_symbol) {
            AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, 8);
            AArch64_Instruction adrp = {
                .opcode = AARCH64_OPC_ADRP,
                .size = 8,
                .cond = 0,
                .op_count = 2,
                .shift = 0,
                .ops = { aarch64_op_reg(tmp), aarch64_op_global(dst_op.mem.symbol) }
            };
            aarch64_block_append_inst(xblk, adrp);

            AArch64_Instruction str = {
                .opcode = AARCH64_OPC_STR,
                .size = src_op.reg.size,
                .cond = 0,
                .op_count = 3,
                .shift = 0,
                .ops = {
                    src_op,
                    aarch64_op_reg(tmp),
                    aarch64_op_global(dst_op.mem.symbol)
                }
            };
            aarch64_block_append_inst(xblk, str);
        } else {
            AArch64_Instruction str = {
                .opcode = AARCH64_OPC_STR,
                .size = src_op.reg.size,
                .cond = 0,
                .op_count = 2,
                .shift = 0,
                .ops = { src_op, dst_op }
            };
            aarch64_block_append_inst(xblk, str);
        }
        break;
    }
    case NY_MOPC_LEA: {
        AArch64_Operand src = lower_operand(mops[0], frame);
        if (src.kind == AARCH64_OP_MEM && src.mem.is_symbol) {
            AArch64_Instruction adrp = {
                .opcode = AARCH64_OPC_ADRP,
                .size = 8,
                .cond = 0,
                .op_count = 2,
                .shift = 0,
                .ops = { aarch64_op_reg(def_r), aarch64_op_global(src.mem.symbol) }
            };
            aarch64_block_append_inst(xblk, adrp);

            AArch64_Instruction add_lo = {
                .opcode = AARCH64_OPC_ADD_LO12,
                .size = 8,
                .cond = 0,
                .op_count = 3,
                .shift = 0,
                .ops = {
                    aarch64_op_reg(def_r),
                    aarch64_op_reg(def_r),
                    aarch64_op_global(src.mem.symbol)
                }
            };
            aarch64_block_append_inst(xblk, add_lo);
        } else if (src.kind == AARCH64_OP_MEM) {
            AArch64_Instruction add = {
                .opcode = AARCH64_OPC_ADD,
                .size = 8,
                .cond = 0,
                .op_count = 3,
                .shift = 0,
                .ops = {
                    aarch64_op_reg(def_r),
                    aarch64_op_reg(src.mem.base),
                    aarch64_op_imm(src.mem.disp)
                }
            };
            aarch64_block_append_inst(xblk, add);
        }
        break;
    }
    case NY_MOPC_ADD:
    case NY_MOPC_SUB: {
        AArch64_Operand lhs = lower_operand(mops[0], frame);
        AArch64_Operand rhs = lower_operand(mops[1], frame);
        AArch64_Opcode opc = (minst->opcode == NY_MOPC_ADD) ? AARCH64_OPC_ADD : AARCH64_OPC_SUB;

        if (rhs.kind == AARCH64_OP_IMM && imm_fits_addsub(rhs.imm)) {
            if (!is_same_reg(def_r, lhs)) {
                emit_mov(xblk, def_r, lhs);
            }
            AArch64_Instruction inst = {
                .opcode = opc,
                .size = def_r.size,
                .cond = 0,
                .op_count = 3,
                .shift = 0,
                .ops = { aarch64_op_reg(def_r), aarch64_op_reg(def_r), rhs }
            };
            aarch64_block_append_inst(xblk, inst);
        } else {
            if (!is_same_reg(def_r, lhs)) {
                emit_mov(xblk, def_r, lhs);
            }
            if (rhs.kind == AARCH64_OP_IMM) {
                AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, def_r.size);
                emit_mov_imm(xblk, tmp, rhs.imm);
                rhs = aarch64_op_reg(tmp);
            }
            AArch64_Instruction inst = {
                .opcode = opc,
                .size = def_r.size,
                .cond = 0,
                .op_count = 3,
                .shift = 0,
                .ops = { aarch64_op_reg(def_r), aarch64_op_reg(def_r), rhs }
            };
            aarch64_block_append_inst(xblk, inst);
        }
        break;
    }
    case NY_MOPC_IMUL: {
        AArch64_Operand lhs = lower_operand(mops[0], frame);
        AArch64_Operand rhs = lower_operand(mops[1], frame);
        if (!is_same_reg(def_r, lhs)) {
            emit_mov(xblk, def_r, lhs);
        }
        if (rhs.kind == AARCH64_OP_IMM) {
            AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, def_r.size);
            emit_mov_imm(xblk, tmp, rhs.imm);
            rhs = aarch64_op_reg(tmp);
        }
        AArch64_Instruction inst = {
            .opcode = AARCH64_OPC_MUL,
            .size = def_r.size,
            .cond = 0,
            .op_count = 3,
            .shift = 0,
            .ops = { aarch64_op_reg(def_r), aarch64_op_reg(def_r), rhs }
        };
        aarch64_block_append_inst(xblk, inst);
        break;
    }
    case NY_MOPC_IDIV:
    case NY_MOPC_UDIV: {
        AArch64_Operand lhs = lower_operand(mops[0], frame);
        AArch64_Operand rhs = lower_operand(mops[1], frame);
        if (!is_same_reg(def_r, lhs)) {
            emit_mov(xblk, def_r, lhs);
        }
        if (rhs.kind == AARCH64_OP_IMM) {
            AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, def_r.size);
            emit_mov_imm(xblk, tmp, rhs.imm);
            rhs = aarch64_op_reg(tmp);
        }
        AArch64_Opcode opc = (minst->opcode == NY_MOPC_IDIV) ? AARCH64_OPC_SDIV : AARCH64_OPC_UDIV;
        AArch64_Instruction inst = {
            .opcode = opc,
            .size = def_r.size,
            .cond = 0,
            .op_count = 3,
            .shift = 0,
            .ops = { aarch64_op_reg(def_r), aarch64_op_reg(def_r), rhs }
        };
        aarch64_block_append_inst(xblk, inst);
        break;
    }
    case NY_MOPC_IREM:
    case NY_MOPC_UREM: {
        AArch64_Operand lhs = lower_operand(mops[0], frame);
        AArch64_Operand rhs = lower_operand(mops[1], frame);
        AArch64_Reg qreg = aarch64_reg_phys(AARCH64_X16, def_r.size);
        AArch64_Reg nreg = aarch64_reg_phys(AARCH64_X17, def_r.size);

        if (!is_same_reg(nreg, lhs)) {
            emit_mov(xblk, nreg, lhs);
        }
        if (rhs.kind == AARCH64_OP_IMM) {
            AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X9, def_r.size);
            emit_mov_imm(xblk, tmp, rhs.imm);
            rhs = aarch64_op_reg(tmp);
        }
        AArch64_Reg dreg = rhs.reg;

        AArch64_Opcode div_opc = (minst->opcode == NY_MOPC_IREM) ? AARCH64_OPC_SDIV : AARCH64_OPC_UDIV;
        AArch64_Instruction div = {
            .opcode = div_opc,
            .size = def_r.size,
            .cond = 0,
            .op_count = 3,
            .shift = 0,
            .ops = { aarch64_op_reg(qreg), aarch64_op_reg(nreg), aarch64_op_reg(dreg) }
        };
        aarch64_block_append_inst(xblk, div);

        AArch64_Instruction msub = {
            .opcode = AARCH64_OPC_MSUB,
            .size = def_r.size,
            .cond = 0,
            .op_count = 4,
            .shift = 0,
            .ops = {
                aarch64_op_reg(def_r),
                aarch64_op_reg(qreg),
                aarch64_op_reg(dreg),
                aarch64_op_reg(nreg)
            }
        };
        aarch64_block_append_inst(xblk, msub);
        break;
    }
    case NY_MOPC_NEG: {
        AArch64_Operand src = lower_operand(mops[0], frame);
        AArch64_Reg zr = aarch64_reg_phys((AArch64_Phys_Reg)31, def_r.size);
        AArch64_Instruction inst = {
            .opcode = AARCH64_OPC_SUB,
            .size = def_r.size,
            .cond = 0,
            .op_count = 3,
            .shift = 0,
            .ops = { aarch64_op_reg(def_r), aarch64_op_reg(zr), src }
        };
        aarch64_block_append_inst(xblk, inst);
        break;
    }
    case NY_MOPC_AND:
    case NY_MOPC_OR:
    case NY_MOPC_XOR: {
        AArch64_Operand lhs = lower_operand(mops[0], frame);
        AArch64_Operand rhs = lower_operand(mops[1], frame);
        AArch64_Opcode opc;
        switch ((Ny_Machine_Opcode)minst->opcode) {
        case NY_MOPC_AND: opc = AARCH64_OPC_AND; break;
        case NY_MOPC_OR:  opc = AARCH64_OPC_ORR; break;
        case NY_MOPC_XOR: opc = AARCH64_OPC_EOR; break;
        default: opc = AARCH64_OPC_AND; break;
        }
        if (!is_same_reg(def_r, lhs)) {
            emit_mov(xblk, def_r, lhs);
        }
        if (rhs.kind == AARCH64_OP_IMM) {
            AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, def_r.size);
            emit_mov_imm(xblk, tmp, rhs.imm);
            rhs = aarch64_op_reg(tmp);
        }
        AArch64_Instruction inst = {
            .opcode = opc,
            .size = def_r.size,
            .cond = 0,
            .op_count = 3,
            .shift = 0,
            .ops = { aarch64_op_reg(def_r), aarch64_op_reg(def_r), rhs }
        };
        aarch64_block_append_inst(xblk, inst);
        break;
    }
    case NY_MOPC_NOT: {
        AArch64_Operand src = lower_operand(mops[0], frame);
        if (!is_same_reg(def_r, src)) {
            emit_mov(xblk, def_r, src);
        }
        AArch64_Instruction inst = {
            .opcode = AARCH64_OPC_MVN,
            .size = def_r.size,
            .cond = 0,
            .op_count = 2,
            .shift = 0,
            .ops = { aarch64_op_reg(def_r), aarch64_op_reg(def_r) }
        };
        aarch64_block_append_inst(xblk, inst);
        break;
    }
    case NY_MOPC_SHL:
    case NY_MOPC_SHR:
    case NY_MOPC_SAR: {
        AArch64_Operand lhs = lower_operand(mops[0], frame);
        AArch64_Operand rhs = lower_operand(mops[1], frame);
        AArch64_Opcode opc;
        switch ((Ny_Machine_Opcode)minst->opcode) {
        case NY_MOPC_SHL: opc = AARCH64_OPC_LSL; break;
        case NY_MOPC_SHR: opc = AARCH64_OPC_LSR; break;
        case NY_MOPC_SAR: opc = AARCH64_OPC_ASR; break;
        default: opc = AARCH64_OPC_LSL; break;
        }
        if (!is_same_reg(def_r, lhs)) {
            emit_mov(xblk, def_r, lhs);
        }
        if (rhs.kind == AARCH64_OP_IMM) {
            AArch64_Instruction inst = {
                .opcode = opc,
                .size = def_r.size,
                .cond = 0,
                .op_count = 3,
                .shift = (uint8_t)(rhs.imm & 0x3F),
                .ops = { aarch64_op_reg(def_r), aarch64_op_reg(def_r), {0} }
            };
            aarch64_block_append_inst(xblk, inst);
        } else {
            AArch64_Instruction inst = {
                .opcode = opc,
                .size = def_r.size,
                .cond = 0,
                .op_count = 3,
                .shift = 0,
                .ops = { aarch64_op_reg(def_r), aarch64_op_reg(def_r), rhs }
            };
            aarch64_block_append_inst(xblk, inst);
        }
        break;
    }
    case NY_MOPC_CMP: {
        AArch64_Operand lhs = lower_operand(mops[0], frame);
        AArch64_Operand rhs = lower_operand(mops[1], frame);
        AArch64_Cond cond = lower_cond(mops[2].cond);

        if (rhs.kind == AARCH64_OP_IMM && imm_fits_addsub(rhs.imm)) {
            AArch64_Instruction cmp = {
                .opcode = AARCH64_OPC_CMP,
                .size = def_r.size > 0 ? def_r.size : 8,
                .cond = 0,
                .op_count = 3,
                .shift = 0,
                .ops = { lhs, rhs, {0} }
            };
            aarch64_block_append_inst(xblk, cmp);
        } else {
            if (rhs.kind == AARCH64_OP_IMM) {
                AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, 8);
                emit_mov_imm(xblk, tmp, rhs.imm);
                rhs = aarch64_op_reg(tmp);
            }
            AArch64_Instruction cmp = {
                .opcode = AARCH64_OPC_CMP,
                .size = def_r.size > 0 ? def_r.size : 8,
                .cond = 0,
                .op_count = 3,
                .shift = 0,
                .ops = { lhs, rhs, {0} }
            };
            aarch64_block_append_inst(xblk, cmp);
        }

        if (has_def) {
            AArch64_Instruction cset = {
                .opcode = AARCH64_OPC_CSET,
                .size = def_r.size,
                .cond = (uint8_t)cond,
                .op_count = 1,
                .shift = 0,
                .ops = { aarch64_op_reg(def_r) }
            };
            aarch64_block_append_inst(xblk, cset);
        }
        break;
    }
    case NY_MOPC_JMP: {
        AArch64_Instruction jmp = {
            .opcode = AARCH64_OPC_B,
            .size = 8,
            .cond = 0,
            .op_count = 1,
            .shift = 0,
            .ops = { aarch64_op_label(mops[0].block) }
        };
        aarch64_block_append_inst(xblk, jmp);
        break;
    }
    case NY_MOPC_JCC: {
        AArch64_Cond cond = lower_cond(mops[0].cond);
        AArch64_Instruction jcc = {
            .opcode = AARCH64_OPC_BCOND,
            .size = 8,
            .cond = (uint8_t)cond,
            .op_count = 1,
            .shift = 0,
            .ops = { aarch64_op_label(mops[1].block) }
        };
        aarch64_block_append_inst(xblk, jcc);
        break;
    }
    case NY_MOPC_CALL: {
        size_t gpr_arg_count = 0;
        const AArch64_Phys_Reg *gpr_args = aarch64_abi_arg_regs(abi, &gpr_arg_count);
        size_t fp_arg_count = 0;
        const AArch64_Phys_Reg *fp_args = aarch64_abi_fp_arg_regs(abi, &fp_arg_count);

        size_t gpr_idx = 0;
        size_t fp_idx = 0;
        uint32_t stack_offset = 0;

        typedef struct {
            AArch64_Reg dst;
            AArch64_Operand src;
            bool is_mem;
            AArch64_Mem mem;
            bool is_fp;
        } Param_Copy;

        Param_Copy copies[64];
        size_t actual_copies = 0;

        Ny_Type_ID ret_type = ny_mfunc_get_inst_ret_type(mfn, minst->id);
        Ny_Slot_ID ret_slot = ny_mfunc_get_inst_result_slot(mfn, minst->id);
        bool ret_aggregate = (tt && ret_type != NY_INVALID_TYPE && ret_slot != NY_INVALID_SLOT);
        Ny_AAPCS64_ABI ret_cls = {0};
        if (ret_aggregate) {
            ret_cls = aarch64_abi_classify_aggregate(tt, ret_type);
            if (ret_cls.kind == NY_AAPCS64_INDIRECT) {
                AArch64_Reg sret_reg = aarch64_reg_phys(AARCH64_X8, 8);
                emit_slot_addr(xblk, frame, ret_slot, sret_reg);
            }
        }

        for (size_t i = 1; i < minst->op_count && actual_copies < 64; i++) {
            Ny_Type_ID arg_type = mops[i].type;
            if (tt && arg_type != NY_INVALID_TYPE && arg_type != NY_TYPE_VOID &&
                ny_type_is_aggregate(tt, arg_type)) {
                Ny_Slot_ID arg_slot = mops[i].mem.stack_slot;
                Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(tt, arg_type);
                uint32_t arg_align = ny_type_align(tt, arg_type);
                if (arg_align > 16) arg_align = 16;

                if (cls.kind == NY_AAPCS64_GPR_AGG) {
                    if (gpr_idx + cls.reg_count <= gpr_arg_count) {
                        for (uint8_t c = 0; c < cls.reg_count && actual_copies < 64; c++) {
                            copies[actual_copies].is_mem = false;
                            copies[actual_copies].dst = aarch64_reg_phys(gpr_args[gpr_idx++], 8);
                            copies[actual_copies].is_fp = false;
                            AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, 8);
                            emit_load_chunk(xblk, frame, arg_slot, (int32_t)c * 8, tmp);
                            copies[actual_copies].src = aarch64_op_reg(tmp);
                            actual_copies++;
                        }
                    } else {
                        stack_offset = (stack_offset + arg_align - 1) & ~(arg_align - 1);
                        uint32_t total = cls.reg_count * 8;
                        AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, 8);
                        for (uint32_t off = 0; off < total; off += 8) {
                            emit_load_chunk(xblk, frame, arg_slot, (int32_t)off, tmp);
                            emit_store_sp(xblk, tmp, (int32_t)stack_offset);
                            stack_offset += 8;
                        }
                    }
                } else if (cls.kind == NY_AAPCS64_HFA) {
                    uint8_t fpsz = (cls.hfa_member == NY_TYPE_F32) ? 4 : 8;
                    if (fp_idx + cls.reg_count <= fp_arg_count) {
                        for (uint8_t c = 0; c < cls.reg_count && actual_copies < 64; c++) {
                            AArch64_Reg vreg = aarch64_reg_phys(fp_args[fp_idx++], fpsz);
                            emit_load_fp_chunk(xblk, frame, arg_slot, (int32_t)c * fpsz, vreg);
                            copies[actual_copies].is_mem = false;
                            copies[actual_copies].dst = vreg;
                            copies[actual_copies].is_fp = true;
                            copies[actual_copies].src = aarch64_op_reg(vreg);
                            actual_copies++;
                        }
                    } else {
                        stack_offset = (stack_offset + arg_align - 1) & ~(arg_align - 1);
                        AArch64_Reg vtmp = aarch64_reg_phys(AARCH64_V0, fpsz);
                        for (uint8_t c = 0; c < cls.reg_count; c++) {
                            emit_load_fp_chunk(xblk, frame, arg_slot, (int32_t)c * fpsz, vtmp);
                            emit_store_sp(xblk, vtmp, (int32_t)stack_offset);
                            stack_offset += fpsz;
                        }
                    }
                } else if (cls.kind == NY_AAPCS64_INDIRECT) {
                    if (gpr_idx < gpr_arg_count) {
                        copies[actual_copies].is_mem = false;
                        copies[actual_copies].dst = aarch64_reg_phys(gpr_args[gpr_idx++], 8);
                        copies[actual_copies].is_fp = false;
                        AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, 8);
                        emit_slot_addr(xblk, frame, arg_slot, tmp);
                        copies[actual_copies].src = aarch64_op_reg(tmp);
                        actual_copies++;
                    } else {
                        stack_offset = (stack_offset + 7) & ~7u;
                        AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, 8);
                        emit_slot_addr(xblk, frame, arg_slot, tmp);
                        emit_store_sp(xblk, tmp, (int32_t)stack_offset);
                        stack_offset += 8;
                    }
                }
                continue;
            }

            AArch64_Operand arg_op = lower_operand(mops[i], frame);
            bool is_fp = (mops[i].kind == NY_MOP_KIND_REG &&
                         (mops[i].reg.reg_class == NY_REG_CLASS_FP32 ||
                          mops[i].reg.reg_class == NY_REG_CLASS_FP64));
            uint8_t sz = 8;
            if (arg_op.kind == AARCH64_OP_REG) sz = arg_op.reg.size;

            if (is_fp) {
                if (fp_idx < fp_arg_count) {
                    copies[actual_copies].is_mem = false;
                    copies[actual_copies].dst = aarch64_reg_phys(fp_args[fp_idx++], sz);
                    copies[actual_copies].src = arg_op;
                    copies[actual_copies].is_fp = true;
                } else {
                    copies[actual_copies].is_mem = true;
                    copies[actual_copies].mem = (AArch64_Mem){
                        .base = aarch64_reg_phys(AARCH64_SP, 8),
                        .disp = (int32_t)stack_offset,
                    };
                    copies[actual_copies].src = arg_op;
                    copies[actual_copies].is_fp = true;
                    stack_offset += 8;
                }
            } else {
                if (gpr_idx < gpr_arg_count) {
                    copies[actual_copies].is_mem = false;
                    copies[actual_copies].dst = aarch64_reg_phys(gpr_args[gpr_idx++], sz);
                    copies[actual_copies].src = arg_op;
                    copies[actual_copies].is_fp = false;
                } else {
                    copies[actual_copies].is_mem = true;
                    copies[actual_copies].mem = (AArch64_Mem){
                        .base = aarch64_reg_phys(AARCH64_SP, 8),
                        .disp = (int32_t)stack_offset,
                    };
                    copies[actual_copies].src = arg_op;
                    copies[actual_copies].is_fp = false;
                    stack_offset += 8;
                }
            }
            actual_copies++;
        }

        for (size_t c = 0; c < actual_copies; c++) {
            if (copies[c].is_mem) {
                AArch64_Operand dst_op = aarch64_op_mem(copies[c].mem);
                AArch64_Instruction str = {
                    .opcode = AARCH64_OPC_STR,
                    .size = copies[c].src.reg.size > 0 ? copies[c].src.reg.size : 8,
                    .cond = 0,
                    .op_count = 2,
                    .shift = 0,
                    .ops = { copies[c].src, dst_op }
                };
                aarch64_block_append_inst(xblk, str);
            }
        }

        bool done[64] = {0};
        for (size_t c = 0; c < actual_copies; c++) {
            if (copies[c].is_mem || (copies[c].src.kind == AARCH64_OP_REG &&
                                     is_same_reg(copies[c].dst, copies[c].src))) {
                done[c] = true;
            }
        }

        bool progress = true;
        while (progress) {
            progress = false;
            for (size_t i = 0; i < actual_copies; i++) {
                if (done[i]) continue;
                bool conflict = false;
                for (size_t j = 0; j < actual_copies; j++) {
                    if (!done[j] && !copies[j].is_mem &&
                        copies[j].src.kind == AARCH64_OP_REG &&
                        !copies[j].src.reg.is_virtual &&
                        copies[j].src.reg.phys_reg == copies[i].dst.phys_reg) {
                        conflict = true;
                        break;
                    }
                }
                if (!conflict) {
                    emit_mov(xblk, copies[i].dst, copies[i].src);
                    done[i] = true;
                    progress = true;
                }
            }

            if (!progress) {
                for (size_t i = 0; i < actual_copies; i++) {
                    if (!done[i]) {
                        AArch64_Reg scratch = aarch64_reg_phys(AARCH64_X16, copies[i].src.reg.size);
                        emit_mov(xblk, scratch, copies[i].src);
                        copies[i].src = aarch64_op_reg(scratch);
                        progress = true;
                        break;
                    }
                }
            }
        }

        AArch64_Operand callee = lower_operand(mops[0], frame);
        if (callee.kind == AARCH64_OP_GLOBAL || callee.kind == AARCH64_OP_LABEL) {
            AArch64_Instruction call = {
                .opcode = AARCH64_OPC_BL,
                .size = 8,
                .cond = 0,
                .op_count = 1,
                .shift = 0,
                .ops = { callee }
            };
            aarch64_block_append_inst(xblk, call);
        } else if (callee.kind == AARCH64_OP_REG) {
            AArch64_Instruction call = {
                .opcode = AARCH64_OPC_BLR,
                .size = 8,
                .cond = 0,
                .op_count = 1,
                .shift = 0,
                .ops = { callee }
            };
            aarch64_block_append_inst(xblk, call);
        }

        if (ret_aggregate) {
            if (ret_cls.kind == NY_AAPCS64_GPR_AGG) {
                for (uint8_t c = 0; c < ret_cls.reg_count; c++) {
                    AArch64_Reg src = aarch64_reg_phys(gpr_args[c], 8);
                    emit_store_chunk(xblk, frame, src, ret_slot, (int32_t)c * 8);
                }
            } else if (ret_cls.kind == NY_AAPCS64_HFA) {
                uint8_t fpsz = (ret_cls.hfa_member == NY_TYPE_F32) ? 4 : 8;
                for (uint8_t c = 0; c < ret_cls.reg_count; c++) {
                    AArch64_Reg src = aarch64_reg_phys(fp_args[c], fpsz);
                    emit_store_fp_chunk(xblk, frame, src, ret_slot, (int32_t)c * fpsz);
                }
            } else if (ret_cls.kind == NY_AAPCS64_INDIRECT) {
                /* sret: callee wrote into the buffer pointed by X8, already in ret_slot */
            }
        } else if (has_def) {
            bool is_fp = (minst->def_reg.reg_class == NY_REG_CLASS_FP32 ||
                         minst->def_reg.reg_class == NY_REG_CLASS_FP64);
            AArch64_Phys_Reg ret_phys = aarch64_abi_ret_reg(abi, def_r.size, is_fp);
            AArch64_Reg ret_reg = aarch64_reg_phys(ret_phys, def_r.size);
            emit_mov(xblk, def_r, aarch64_op_reg(ret_reg));
        }
        break;
    }
    case NY_MOPC_RET: {
        bool ret_aggregate = false;
        Ny_Type_ID ret_type = mfn->return_type;
        if (tt && ret_type != NY_INVALID_TYPE && ret_type != NY_TYPE_VOID &&
            ny_type_is_aggregate(tt, ret_type)) {
            ret_aggregate = true;
        }

        if (ret_aggregate && minst->op_count > 0 && mops[0].kind == NY_MOP_KIND_MEM) {
            Ny_Slot_ID ret_slot = mops[0].mem.stack_slot;
            Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(tt, ret_type);

            if (cls.kind == NY_AAPCS64_GPR_AGG) {
                size_t gpr_arg_count = 0;
                const AArch64_Phys_Reg *gpr_args = aarch64_abi_arg_regs(abi, &gpr_arg_count);
                for (uint8_t c = 0; c < cls.reg_count && c < gpr_arg_count; c++) {
                    AArch64_Reg dst = aarch64_reg_phys(gpr_args[c], 8);
                    emit_load_chunk(xblk, frame, ret_slot, (int32_t)c * 8, dst);
                }
            } else if (cls.kind == NY_AAPCS64_HFA) {
                uint8_t fpsz = (cls.hfa_member == NY_TYPE_F32) ? 4 : 8;
                size_t fp_ret_count = 0;
                const AArch64_Phys_Reg *fp_ret = aarch64_abi_fp_arg_regs(abi, &fp_ret_count);
                for (uint8_t c = 0; c < cls.reg_count && c < fp_ret_count; c++) {
                    AArch64_Reg dst = aarch64_reg_phys(fp_ret[c], fpsz);
                    emit_load_fp_chunk(xblk, frame, ret_slot, (int32_t)c * fpsz, dst);
                }
            } else if (cls.kind == NY_AAPCS64_INDIRECT) {
                AArch64_Reg sret_buf = aarch64_reg_phys(AARCH64_X8, 8);
                if (frame->need_x8_save) {
                    AArch64_Mem restore = {
                        .base = aarch64_reg_phys(AARCH64_FP, 8),
                        .disp = frame->x8_save_offset,
                    };
                    AArch64_Instruction ldr = {
                        .opcode = AARCH64_OPC_LDR, .size = 8, .cond = 0,
                        .op_count = 2, .shift = 0,
                        .ops = { aarch64_op_reg(sret_buf), aarch64_op_mem(restore) }
                    };
                    aarch64_block_append_inst(xblk, ldr);
                }
                AArch64_Reg scratch = aarch64_reg_phys(AARCH64_X16, 8);
                uint32_t off = 0;
                while (off + 8 <= cls.size) {
                    emit_load_chunk(xblk, frame, ret_slot, (int32_t)off, scratch);
                    AArch64_Mem dst_m = { .base = sret_buf, .disp = (int32_t)off };
                    AArch64_Instruction str = {
                        .opcode = AARCH64_OPC_STR, .size = 8, .cond = 0,
                        .op_count = 2, .shift = 0,
                        .ops = { aarch64_op_reg(scratch), aarch64_op_mem(dst_m) }
                    };
                    aarch64_block_append_inst(xblk, str);
                    off += 8;
                }
                if (off + 4 <= cls.size) {
                    AArch64_Reg w_scratch = aarch64_reg_phys(AARCH64_X16, 4);
                    AArch64_Mem src_m = {
                        .base = aarch64_reg_phys(AARCH64_FP, 8),
                        .disp = (int32_t)frame->slot_offsets[ret_slot] + (int32_t)off,
                    };
                    AArch64_Instruction ldr = {
                        .opcode = AARCH64_OPC_LDR, .size = 4, .cond = 0,
                        .op_count = 2, .shift = 0,
                        .ops = { aarch64_op_reg(w_scratch), aarch64_op_mem(src_m) }
                    };
                    aarch64_block_append_inst(xblk, ldr);
                    AArch64_Mem dst_m = { .base = sret_buf, .disp = (int32_t)off };
                    AArch64_Instruction str = {
                        .opcode = AARCH64_OPC_STR, .size = 4, .cond = 0,
                        .op_count = 2, .shift = 0,
                        .ops = { aarch64_op_reg(w_scratch), aarch64_op_mem(dst_m) }
                    };
                    aarch64_block_append_inst(xblk, str);
                    off += 4;
                }
                while (off < cls.size) {
                    AArch64_Reg w_scratch = aarch64_reg_phys(AARCH64_X16, 4);
                    AArch64_Mem src_m = {
                        .base = aarch64_reg_phys(AARCH64_FP, 8),
                        .disp = (int32_t)frame->slot_offsets[ret_slot] + (int32_t)off,
                    };
                    AArch64_Instruction ldr = {
                        .opcode = AARCH64_OPC_LDR, .size = 4, .cond = 0,
                        .op_count = 2, .shift = 0,
                        .ops = { aarch64_op_reg(w_scratch), aarch64_op_mem(src_m) }
                    };
                    aarch64_block_append_inst(xblk, ldr);
                    AArch64_Mem dst_m = { .base = sret_buf, .disp = (int32_t)off };
                    AArch64_Instruction str = {
                        .opcode = AARCH64_OPC_STR, .size = 4, .cond = 0,
                        .op_count = 2, .shift = 0,
                        .ops = { aarch64_op_reg(w_scratch), aarch64_op_mem(dst_m) }
                    };
                    aarch64_block_append_inst(xblk, str);
                    off += 4;
                }
            }
        } else if (minst->op_count > 0 && mops[0].kind != NY_MOP_KIND_NONE) {
            AArch64_Operand ret_val = lower_operand(mops[0], frame);
            uint8_t sz = 8;
            if (ret_val.kind == AARCH64_OP_REG) sz = ret_val.reg.size;
            bool is_fp = (mops[0].kind == NY_MOP_KIND_REG &&
                         (mops[0].reg.reg_class == NY_REG_CLASS_FP32 ||
                          mops[0].reg.reg_class == NY_REG_CLASS_FP64));
            AArch64_Phys_Reg ret_phys = aarch64_abi_ret_reg(abi, sz, is_fp);
            AArch64_Reg ret_reg = aarch64_reg_phys(ret_phys, sz);
            emit_mov(xblk, ret_reg, ret_val);
        }
        aarch64_emit_epilogue(xblk, frame);
        break;
    }
    case NY_MOPC_TRAP: {
        AArch64_Instruction brk = {
            .opcode = AARCH64_OPC_BRK,
            .size = 8,
            .cond = 0,
            .op_count = 1,
            .shift = 0,
            .ops = { aarch64_op_imm(0) }
        };
        aarch64_block_append_inst(xblk, brk);
        break;
    }
    default:
        break;
    }
}

static bool aarch64_lower_func_impl(const Ny_Target *target, const Ny_Machine_Function *mfn,
                                    AArch64_Function *out_fn, Ny_Diagnostic_List *diags,
                                    const Ny_Type_Table *tt) {
    (void)diags;
    aarch64_func_init(out_fn, mfn->name, target->abi);
    aarch64_frame_layout(&out_fn->frame, mfn, target->abi, tt);

    out_fn->block_count = mfn->block_count;
    out_fn->block_capacity = mfn->block_count;
    if (out_fn->block_capacity > 0) {
        out_fn->blocks = (AArch64_Block *)ny_alloc_zero(out_fn->block_capacity * sizeof(AArch64_Block));
    }

    for (size_t b = 0; b < mfn->block_count; b++) {
        const Ny_Machine_Block *mblk = &mfn->blocks[b];
        AArch64_Block *xblk = &out_fn->blocks[b];
        xblk->id = mblk->id;
        xblk->name = mblk->name;

        if (b == 0 || mblk->id == mfn->entry_block) {
            aarch64_emit_prologue(xblk, &out_fn->frame);

            if (out_fn->frame.need_x8_save) {
                AArch64_Reg x8 = aarch64_reg_phys(AARCH64_X8, 8);
                AArch64_Mem save = {
                    .base = aarch64_reg_phys(AARCH64_FP, 8),
                    .disp = out_fn->frame.x8_save_offset,
                };
                AArch64_Instruction str = {
                    .opcode = AARCH64_OPC_STR, .size = 8, .cond = 0,
                    .op_count = 2, .shift = 0,
                    .ops = { aarch64_op_reg(x8), aarch64_op_mem(save) }
                };
                aarch64_block_append_inst(xblk, str);
            }

            size_t gpr_abi_count = 0;
            const AArch64_Phys_Reg *gpr_args = aarch64_abi_arg_regs(target->abi, &gpr_abi_count);
            size_t fp_abi_count = 0;
            const AArch64_Phys_Reg *fp_args = aarch64_abi_fp_arg_regs(target->abi, &fp_abi_count);

            size_t gpr_idx = 0;
            size_t fp_idx = 0;

            for (size_t p = 0; p < mfn->param_count; p++) {
                Ny_Type_ID ptype = mfn->param_types[p];
                if (tt && ptype != NY_INVALID_TYPE && ny_type_is_aggregate(tt, ptype)) {
                    Ny_Slot_ID pslot = mfn->param_slots[p];
                    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(tt, ptype);
                    uint32_t palign = ny_type_align(tt, ptype);
                    if (palign > 16) palign = 16;

                    if (cls.kind == NY_AAPCS64_GPR_AGG) {
                        if (gpr_idx + cls.reg_count <= gpr_abi_count) {
                            for (uint8_t c = 0; c < cls.reg_count; c++) {
                                AArch64_Reg src = aarch64_reg_phys(gpr_args[gpr_idx++], 8);
                                emit_store_chunk(xblk, &out_fn->frame, src, pslot, (int32_t)c * 8);
                            }
                        } else {
                            uint32_t stack_off = 16 + (uint32_t)gpr_idx * 8;
                            for (uint8_t c = 0; c < cls.reg_count; c++) {
                                AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, 8);
                                emit_load_sp(xblk, (int32_t)(stack_off + c * 8), tmp);
                                emit_store_chunk(xblk, &out_fn->frame, tmp, pslot, (int32_t)c * 8);
                            }
                            gpr_idx = gpr_abi_count;
                        }
                    } else if (cls.kind == NY_AAPCS64_HFA) {
                        uint8_t fpsz = (cls.hfa_member == NY_TYPE_F32) ? 4 : 8;
                        if (fp_idx + cls.reg_count <= fp_abi_count) {
                            for (uint8_t c = 0; c < cls.reg_count; c++) {
                                AArch64_Reg src = aarch64_reg_phys(fp_args[fp_idx++], fpsz);
                                emit_store_fp_chunk(xblk, &out_fn->frame, src, pslot, (int32_t)c * fpsz);
                            }
                        } else {
                            uint32_t stack_off = 16 + (uint32_t)fp_idx * fpsz;
                            for (uint8_t c = 0; c < cls.reg_count; c++) {
                                AArch64_Reg vtmp = aarch64_reg_phys(AARCH64_V0, fpsz);
                                emit_load_sp(xblk, (int32_t)(stack_off + c * fpsz), vtmp);
                                emit_store_fp_chunk(xblk, &out_fn->frame, vtmp, pslot, (int32_t)c * fpsz);
                            }
                            fp_idx = fp_abi_count;
                        }
                    } else if (cls.kind == NY_AAPCS64_INDIRECT) {
                        if (gpr_idx < gpr_abi_count) {
                            AArch64_Reg ptr = aarch64_reg_phys(gpr_args[gpr_idx++], 8);
                            AArch64_Reg scratch = aarch64_reg_phys(AARCH64_X17, 8);
                            uint32_t off = 0;
                            while (off + 8 <= cls.size) {
                                AArch64_Mem src_m = { .base = ptr, .disp = (int32_t)off };
                                AArch64_Instruction ldr = {
                                    .opcode = AARCH64_OPC_LDR, .size = 8, .cond = 0,
                                    .op_count = 2, .shift = 0,
                                    .ops = { aarch64_op_reg(scratch), aarch64_op_mem(src_m) }
                                };
                                aarch64_block_append_inst(xblk, ldr);
                                emit_store_chunk(xblk, &out_fn->frame, scratch, pslot, (int32_t)off);
                                off += 8;
                            }
                            while (off < cls.size) {
                                AArch64_Reg w_scratch = aarch64_reg_phys(AARCH64_X17, 4);
                                AArch64_Mem src_m = { .base = ptr, .disp = (int32_t)off };
                                AArch64_Instruction ldr = {
                                    .opcode = AARCH64_OPC_LDR, .size = 4, .cond = 0,
                                    .op_count = 2, .shift = 0,
                                    .ops = { aarch64_op_reg(w_scratch), aarch64_op_mem(src_m) }
                                };
                                aarch64_block_append_inst(xblk, ldr);
                                AArch64_Mem dst_m = {
                                    .base = aarch64_reg_phys(AARCH64_FP, 8),
                                    .disp = (int32_t)out_fn->frame.slot_offsets[pslot] + (int32_t)off,
                                };
                                AArch64_Instruction str = {
                                    .opcode = AARCH64_OPC_STR, .size = 4, .cond = 0,
                                    .op_count = 2, .shift = 0,
                                    .ops = { aarch64_op_reg(w_scratch), aarch64_op_mem(dst_m) }
                                };
                                aarch64_block_append_inst(xblk, str);
                                off += 4;
                            }
                        } else {
                            gpr_idx = gpr_abi_count;
                        }
                    }
                    continue;
                }

                AArch64_Reg vreg = lower_reg(mfn->param_regs[p]);
                bool is_fp = (mfn->param_regs[p].reg_class == NY_REG_CLASS_FP32 ||
                              mfn->param_regs[p].reg_class == NY_REG_CLASS_FP64);

                if (is_fp) {
                    if (fp_idx < fp_abi_count) {
                        AArch64_Reg src = aarch64_reg_phys(fp_args[fp_idx++], vreg.size);
                        emit_mov(xblk, vreg, aarch64_op_reg(src));
                    } else {
                        uint32_t stack_off = 16 + (uint32_t)fp_idx * 8;
                        AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, vreg.size);
                        emit_load_sp(xblk, (int32_t)stack_off, tmp);
                        emit_mov(xblk, vreg, aarch64_op_reg(tmp));
                    }
                } else {
                    if (gpr_idx < gpr_abi_count) {
                        AArch64_Reg src = aarch64_reg_phys(gpr_args[gpr_idx++], vreg.size);
                        emit_mov(xblk, vreg, aarch64_op_reg(src));
                    } else {
                        uint32_t stack_off = 16 + (uint32_t)gpr_idx * 8;
                        AArch64_Reg tmp = aarch64_reg_phys(AARCH64_X16, vreg.size);
                        emit_load_sp(xblk, (int32_t)stack_off, tmp);
                        emit_mov(xblk, vreg, aarch64_op_reg(tmp));
                    }
                }
            }
        }

        Ny_Inst_ID curr = mblk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Machine_Instruction *minst = ny_mfunc_get_inst(mfn, curr);
            if (!minst) break;
            lower_instruction(xblk, mfn, minst, &out_fn->frame, target->abi, tt);
            curr = minst->next;
        }
    }

    return true;
}

bool aarch64_lower_machine_func(const Ny_Target *target, const Ny_Machine_Function *mfn,
                                void **out_target_fn, Ny_Diagnostic_List *diags) {
    AArch64_Function *fn = (AArch64_Function *)ny_alloc_zero(sizeof(AArch64_Function));
    if (!aarch64_lower_func_impl(target, mfn, fn, diags, nullptr)) {
        aarch64_func_destroy(fn);
        ny_free(fn, sizeof(AArch64_Function));
        return false;
    }
    *out_target_fn = fn;
    return true;
}

bool aarch64_lower_machine_mod(const Ny_Target *target, const Ny_Machine_Module *mmod,
                               AArch64_Module *out_mod, Ny_Diagnostic_List *diags) {
    aarch64_mod_init(out_mod, mmod->name, target->abi);

    out_mod->global_capacity = mmod->global_count;
    if (out_mod->global_capacity > 0) {
        out_mod->globals = (Ny_Machine_Global *)ny_alloc_zero(out_mod->global_capacity * sizeof(Ny_Machine_Global));
        for (size_t g = 0; g < mmod->global_count; g++) {
            const Ny_Machine_Global *mg = &mmod->globals[g];
            Ny_Machine_Global *dst = &out_mod->globals[g];
            dst->name = mg->name;
            dst->kind = mg->kind;
            dst->align = mg->align;
            dst->data_size = mg->data_size;
            if (mg->data && mg->data_size > 0) {
                dst->data = (uint8_t *)ny_alloc(mg->data_size);
                memcpy(dst->data, mg->data, mg->data_size);
            }
            out_mod->global_count++;
        }
    }

    out_mod->function_capacity = mmod->function_count;
    if (out_mod->function_capacity > 0) {
        out_mod->functions = (AArch64_Function *)ny_alloc_zero(out_mod->function_capacity * sizeof(AArch64_Function));
    }

    for (size_t i = 0; i < mmod->function_count; i++) {
        if (!aarch64_lower_func_impl(target, &mmod->functions[i], &out_mod->functions[i], diags, mmod->types)) {
            return false;
        }
        out_mod->function_count++;
    }

    return true;
}
