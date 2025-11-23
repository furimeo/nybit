// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "aarch64_internal.h"
#include <string.h>

void aarch64_frame_layout(AArch64_Stack_Frame *frame, const Ny_Machine_Function *mfn, Ny_Target_ABI abi) {
    memset(frame, 0, sizeof(*frame));
    frame->stack_size = 0;
    frame->has_call = false;
    frame->callee_saved_mask = 0;
    frame->num_callee_saved = 0;

    size_t gpr_abi_count = 0;
    const AArch64_Phys_Reg *gpr_args = aarch64_abi_arg_regs(abi, &gpr_abi_count);
    size_t fp_abi_count = 0;
    const AArch64_Phys_Reg *fp_args = aarch64_abi_fp_arg_regs(abi, &fp_abi_count);
    (void)gpr_args;
    (void)fp_args;

    uint32_t max_outgoing_stack_args = 0;

    for (size_t b = 0; b < mfn->block_count; b++) {
        Ny_Inst_ID curr = mfn->blocks[b].first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Machine_Instruction *inst = ny_mfunc_get_inst(mfn, curr);
            if (!inst) break;

            if (inst->opcode == NY_MOPC_CALL) {
                frame->has_call = true;
                size_t gpr_idx = 0;
                size_t fp_idx = 0;
                uint32_t stack_args = 0;
                for (size_t i = 1; i < inst->op_count; i++) {
                    Ny_Machine_Operand *ops = ny_mfunc_get_operands(mfn, inst);
                    bool is_fp = (ops[i].kind == NY_MOP_KIND_REG &&
                                 (ops[i].reg.reg_class == NY_REG_CLASS_FP32 ||
                                  ops[i].reg.reg_class == NY_REG_CLASS_FP64));
                    if (is_fp) {
                        if (fp_idx < fp_abi_count) {
                            fp_idx++;
                        } else {
                            stack_args++;
                        }
                    } else {
                        if (gpr_idx < gpr_abi_count) {
                            gpr_idx++;
                        } else {
                            stack_args++;
                        }
                    }
                }
                if (stack_args > max_outgoing_stack_args) {
                    max_outgoing_stack_args = stack_args;
                }
            }

            if (ny_mreg_is_valid(inst->def_reg) && !inst->def_reg.is_virtual) {
                AArch64_Phys_Reg r = (AArch64_Phys_Reg)inst->def_reg.phys_reg;
                if (aarch64_abi_is_callee_saved(abi, r)) {
                    frame->callee_saved_mask |= ((uint64_t)1 << r);
                }
            }

            Ny_Machine_Operand *ops = ny_mfunc_get_operands(mfn, inst);
            for (size_t i = 0; i < inst->op_count; i++) {
                if (ops[i].kind == NY_MOP_KIND_REG && !ops[i].reg.is_virtual) {
                    AArch64_Phys_Reg r = (AArch64_Phys_Reg)ops[i].reg.phys_reg;
                    if (aarch64_abi_is_callee_saved(abi, r)) {
                        frame->callee_saved_mask |= ((uint64_t)1 << r);
                    }
                }
            }

            curr = inst->next;
        }
    }

    uint32_t num_callee_saved = 0;
    for (uint32_t r = 0; r < AARCH64_GPR_COUNT; r++) {
        if (r == AARCH64_FP || r == AARCH64_LR) continue;
        if (frame->callee_saved_mask & ((uint64_t)1 << r)) {
            num_callee_saved++;
        }
    }
    for (uint32_t r = AARCH64_V0; r < AARCH64_PHYS_REG_COUNT; r++) {
        if (frame->callee_saved_mask & ((uint64_t)1 << r)) {
            num_callee_saved++;
        }
    }
    frame->num_callee_saved = num_callee_saved;

    uint32_t save_area_size = 16 + num_callee_saved * 8;
    save_area_size = (save_area_size + 15) & ~15u;

    uint32_t outgoing_size = max_outgoing_stack_args * 8;
    outgoing_size = (outgoing_size + 15) & ~15u;

    uint32_t locals_size = 0;
    frame->slot_count = mfn->stack_slot_count;
    frame->slot_capacity = mfn->stack_slot_count;
    if (frame->slot_capacity > 0) {
        frame->slot_offsets = (int32_t *)ny_alloc(frame->slot_capacity * sizeof(int32_t));
    }

    for (size_t i = 0; i < mfn->stack_slot_count; i++) {
        uint32_t align = mfn->stack_slots[i].align;
        if (align < 4) align = 4;
        if (align > 16) align = 16;
        locals_size = (locals_size + align - 1) & ~(align - 1);
        locals_size += mfn->stack_slots[i].size;
        frame->slot_offsets[i] = (int32_t)(outgoing_size + locals_size - mfn->stack_slots[i].size);
    }
    locals_size = (locals_size + 15) & ~15u;

    uint32_t total = save_area_size + locals_size + outgoing_size;
    total = (total + 15) & ~15u;
    frame->stack_size = total;
}

static uint32_t fp_lr_offset(const AArch64_Stack_Frame *frame) {
    return frame->stack_size - 16;
}

static uint32_t callee_saved_offset(const AArch64_Stack_Frame *frame, uint32_t index) {
    return frame->stack_size - 16 - (index + 1) * 8;
}

void aarch64_emit_prologue(AArch64_Block *blk, const AArch64_Stack_Frame *frame) {
    if (frame->stack_size > 0) {
        AArch64_Instruction sub_sp = {
            .opcode = AARCH64_OPC_SUB,
            .size = 8,
            .cond = 0,
            .op_count = 3,
            .shift = 0,
            .ops = {
                aarch64_op_reg(aarch64_reg_phys(AARCH64_SP, 8)),
                aarch64_op_reg(aarch64_reg_phys(AARCH64_SP, 8)),
                aarch64_op_imm(frame->stack_size)
            }
        };
        aarch64_block_append_inst(blk, sub_sp);
    }

    uint32_t fp_lr_off = fp_lr_offset(frame);
    AArch64_Instruction stp_fp_lr = {
        .opcode = AARCH64_OPC_STP,
        .size = 8,
        .cond = 0,
        .op_count = 4,
        .shift = 0,
        .ops = {
            aarch64_op_reg(aarch64_reg_phys(AARCH64_FP, 8)),
            aarch64_op_reg(aarch64_reg_phys(AARCH64_LR, 8)),
            aarch64_op_mem((AArch64_Mem){
                .base = aarch64_reg_phys(AARCH64_SP, 8),
                .disp = (int32_t)fp_lr_off,
                .symbol = {0},
                .is_symbol = false
            }),
            {0}
        }
    };
    aarch64_block_append_inst(blk, stp_fp_lr);

    AArch64_Instruction mov_fp = {
        .opcode = AARCH64_OPC_MOV,
        .size = 8,
        .cond = 0,
        .op_count = 2,
        .shift = 0,
        .ops = {
            aarch64_op_reg(aarch64_reg_phys(AARCH64_FP, 8)),
            aarch64_op_reg(aarch64_reg_phys(AARCH64_SP, 8))
        }
    };
    aarch64_block_append_inst(blk, mov_fp);

    uint32_t cs_index = 0;
    bool prev_saved = false;
    AArch64_Phys_Reg prev_reg = AARCH64_NO_REG;

    for (uint32_t r = 0; r < AARCH64_PHYS_REG_COUNT; r++) {
        if (r == AARCH64_FP || r == AARCH64_LR || r == AARCH64_SP) continue;
        if (!(frame->callee_saved_mask & ((uint64_t)1 << r))) continue;

        if (!prev_saved) {
            prev_reg = (AArch64_Phys_Reg)r;
            prev_saved = true;
        } else {
            uint32_t off = callee_saved_offset(frame, cs_index);
            AArch64_Instruction stp = {
                .opcode = AARCH64_OPC_STP,
                .size = 8,
                .cond = 0,
                .op_count = 4,
                .shift = 0,
                .ops = {
                    aarch64_op_reg(aarch64_reg_phys(prev_reg, 8)),
                    aarch64_op_reg(aarch64_reg_phys((AArch64_Phys_Reg)r, 8)),
                    aarch64_op_mem((AArch64_Mem){
                        .base = aarch64_reg_phys(AARCH64_SP, 8),
                        .disp = (int32_t)off,
                        .symbol = {0},
                        .is_symbol = false
                    }),
                    {0}
                }
            };
            aarch64_block_append_inst(blk, stp);
            cs_index += 2;
            prev_saved = false;
        }
    }

    if (prev_saved) {
        uint32_t off = callee_saved_offset(frame, cs_index);
        AArch64_Instruction str_single = {
            .opcode = AARCH64_OPC_STR,
            .size = 8,
            .cond = 0,
            .op_count = 2,
            .shift = 0,
            .ops = {
                aarch64_op_reg(aarch64_reg_phys(prev_reg, 8)),
                aarch64_op_mem((AArch64_Mem){
                    .base = aarch64_reg_phys(AARCH64_SP, 8),
                    .disp = (int32_t)off,
                    .symbol = {0},
                    .is_symbol = false
                })
            }
        };
        aarch64_block_append_inst(blk, str_single);
    }
}

void aarch64_emit_epilogue(AArch64_Block *blk, const AArch64_Stack_Frame *frame) {
    uint32_t fp_lr_off = fp_lr_offset(frame);
    AArch64_Instruction ldp_fp_lr = {
        .opcode = AARCH64_OPC_LDP,
        .size = 8,
        .cond = 0,
        .op_count = 4,
        .shift = 0,
        .ops = {
            aarch64_op_reg(aarch64_reg_phys(AARCH64_FP, 8)),
            aarch64_op_reg(aarch64_reg_phys(AARCH64_LR, 8)),
            aarch64_op_mem((AArch64_Mem){
                .base = aarch64_reg_phys(AARCH64_SP, 8),
                .disp = (int32_t)fp_lr_off,
                .symbol = {0},
                .is_symbol = false
            }),
            {0}
        }
    };
    aarch64_block_append_inst(blk, ldp_fp_lr);

    uint32_t cs_index = 0;
    bool prev_saved = false;
    AArch64_Phys_Reg prev_reg = AARCH64_NO_REG;

    for (uint32_t r = 0; r < AARCH64_PHYS_REG_COUNT; r++) {
        if (r == AARCH64_FP || r == AARCH64_LR || r == AARCH64_SP) continue;
        if (!(frame->callee_saved_mask & ((uint64_t)1 << r))) continue;

        if (!prev_saved) {
            prev_reg = (AArch64_Phys_Reg)r;
            prev_saved = true;
        } else {
            uint32_t off = callee_saved_offset(frame, cs_index);
            AArch64_Instruction ldp = {
                .opcode = AARCH64_OPC_LDP,
                .size = 8,
                .cond = 0,
                .op_count = 4,
                .shift = 0,
                .ops = {
                    aarch64_op_reg(aarch64_reg_phys(prev_reg, 8)),
                    aarch64_op_reg(aarch64_reg_phys((AArch64_Phys_Reg)r, 8)),
                    aarch64_op_mem((AArch64_Mem){
                        .base = aarch64_reg_phys(AARCH64_SP, 8),
                        .disp = (int32_t)off,
                        .symbol = {0},
                        .is_symbol = false
                    }),
                    {0}
                }
            };
            aarch64_block_append_inst(blk, ldp);
            cs_index += 2;
            prev_saved = false;
        }
    }

    if (prev_saved) {
        uint32_t off = callee_saved_offset(frame, cs_index);
        AArch64_Instruction ldr_single = {
            .opcode = AARCH64_OPC_LDR,
            .size = 8,
            .cond = 0,
            .op_count = 2,
            .shift = 0,
            .ops = {
                aarch64_op_reg(aarch64_reg_phys(prev_reg, 8)),
                aarch64_op_mem((AArch64_Mem){
                    .base = aarch64_reg_phys(AARCH64_SP, 8),
                    .disp = (int32_t)off,
                    .symbol = {0},
                    .is_symbol = false
                })
            }
        };
        aarch64_block_append_inst(blk, ldr_single);
    }

    if (frame->stack_size > 0) {
        AArch64_Instruction add_sp = {
            .opcode = AARCH64_OPC_ADD,
            .size = 8,
            .cond = 0,
            .op_count = 3,
            .shift = 0,
            .ops = {
                aarch64_op_reg(aarch64_reg_phys(AARCH64_SP, 8)),
                aarch64_op_reg(aarch64_reg_phys(AARCH64_SP, 8)),
                aarch64_op_imm(frame->stack_size)
            }
        };
        aarch64_block_append_inst(blk, add_sp);
    }

    AArch64_Instruction ret = {
        .opcode = AARCH64_OPC_RET,
        .size = 8,
        .cond = 0,
        .op_count = 0,
        .shift = 0
    };
    aarch64_block_append_inst(blk, ret);
}
