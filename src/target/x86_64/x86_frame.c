// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "x86_internal.h"

void x86_frame_layout(X86_Stack_Frame *frame, const Ny_Machine_Function *mfn, Ny_Target_ABI abi) {
    frame->slot_count = mfn->stack_slot_count;
    frame->slot_capacity = mfn->stack_slot_count > 0 ? mfn->stack_slot_count : 0;
    frame->has_call = false;
    frame->callee_saved_mask = 0;

    for (size_t i = 0; i < mfn->inst_count; i++) {
        const Ny_Machine_Instruction *inst = &mfn->instructions[i];
        if (inst->opcode == NY_MOPC_CALL) {
            frame->has_call = true;
        }

        if (ny_mreg_is_valid(inst->def_reg) && !inst->def_reg.is_virtual) {
            if (x86_abi_is_callee_saved(abi, (X86_Phys_Reg)inst->def_reg.phys_reg)) {
                frame->callee_saved_mask |= (1 << inst->def_reg.phys_reg);
            }
        }

        const Ny_Machine_Operand *ops = ny_mfunc_get_operands(mfn, inst);
        for (size_t op_i = 0; op_i < inst->op_count; op_i++) {
            if (ops[op_i].kind == NY_MOP_KIND_REG && !ops[op_i].reg.is_virtual) {
                if (x86_abi_is_callee_saved(abi, (X86_Phys_Reg)ops[op_i].reg.phys_reg)) {
                    frame->callee_saved_mask |= (1 << ops[op_i].reg.phys_reg);
                }
            }
        }
    }

    size_t num_callee_saved = 0;
    for (size_t r = 0; r < X86_GPR_COUNT; r++) {
        if (frame->callee_saved_mask & (1 << r)) {
            num_callee_saved++;
        }
    }

    if (frame->slot_capacity > 0) {
        frame->slot_offsets = (int32_t *)ny_alloc(frame->slot_capacity * sizeof(int32_t));
    } else {
        frame->slot_offsets = NULL;
    }

    uint32_t current_offset = 0;
    for (size_t i = 0; i < mfn->stack_slot_count; i++) {
        uint32_t align = mfn->stack_slots[i].align;
        if (align < 4) align = 4;
        current_offset = (current_offset + align - 1) & ~(align - 1);
        current_offset += mfn->stack_slots[i].size;
        frame->slot_offsets[i] = -(int32_t)current_offset;
    }

    if (abi == NY_ABI_WINDOWS_X64 && frame->has_call) {
        current_offset += 32;
    }

    size_t total = (num_callee_saved * 8) + current_offset;
    size_t aligned_total = (total + 15) & ~15;
    frame->stack_size = (uint32_t)(aligned_total - (num_callee_saved * 8));
}

void x86_emit_prologue(X86_Block *blk, const X86_Stack_Frame *frame) {
    X86_Instruction push_rbp = {
        .opcode = X86_OPC_PUSH,
        .size = 8,
        .cond = X86_COND_NONE,
        .op_count = 1,
        .ops = { x86_op_reg(x86_reg_phys(X86_RBP, 8)) }
    };
    x86_block_append_inst(blk, push_rbp);

    for (size_t r = 0; r < X86_GPR_COUNT; r++) {
        if (frame->callee_saved_mask & (1 << r)) {
            X86_Instruction push_cs = {
                .opcode = X86_OPC_PUSH,
                .size = 8,
                .cond = X86_COND_NONE,
                .op_count = 1,
                .ops = { x86_op_reg(x86_reg_phys((X86_Phys_Reg)r, 8)) }
            };
            x86_block_append_inst(blk, push_cs);
        }
    }

    X86_Instruction mov_rbp_rsp = {
        .opcode = X86_OPC_MOV,
        .size = 8,
        .cond = X86_COND_NONE,
        .op_count = 2,
        .ops = {
            x86_op_reg(x86_reg_phys(X86_RBP, 8)),
            x86_op_reg(x86_reg_phys(X86_RSP, 8))
        }
    };
    x86_block_append_inst(blk, mov_rbp_rsp);

    if (frame->stack_size > 0) {
        X86_Instruction sub_rsp = {
            .opcode = X86_OPC_SUB,
            .size = 8,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = {
                x86_op_reg(x86_reg_phys(X86_RSP, 8)),
                x86_op_imm((int64_t)frame->stack_size)
            }
        };
        x86_block_append_inst(blk, sub_rsp);
    }
}

void x86_emit_epilogue(X86_Block *blk, const X86_Stack_Frame *frame) {
    if (frame->stack_size > 0) {
        X86_Instruction mov_rsp_rbp = {
            .opcode = X86_OPC_MOV,
            .size = 8,
            .cond = X86_COND_NONE,
            .op_count = 2,
            .ops = {
                x86_op_reg(x86_reg_phys(X86_RSP, 8)),
                x86_op_reg(x86_reg_phys(X86_RBP, 8))
            }
        };
        x86_block_append_inst(blk, mov_rsp_rbp);
    }

    for (ssize_t r = (ssize_t)X86_GPR_COUNT - 1; r >= 0; r--) {
        if (frame->callee_saved_mask & (1 << r)) {
            X86_Instruction pop_cs = {
                .opcode = X86_OPC_POP,
                .size = 8,
                .cond = X86_COND_NONE,
                .op_count = 1,
                .ops = { x86_op_reg(x86_reg_phys((X86_Phys_Reg)r, 8)) }
            };
            x86_block_append_inst(blk, pop_cs);
        }
    }

    X86_Instruction pop_rbp = {
        .opcode = X86_OPC_POP,
        .size = 8,
        .cond = X86_COND_NONE,
        .op_count = 1,
        .ops = { x86_op_reg(x86_reg_phys(X86_RBP, 8)) }
    };
    x86_block_append_inst(blk, pop_rbp);
}
