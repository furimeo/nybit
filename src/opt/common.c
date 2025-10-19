// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/ir.h>
#include <nybit/support.h>

[[nodiscard]] uint32_t ny_opt_get_type_bit_width(Ny_Type_ID type_id) {
    switch (type_id) {
        case NY_TYPE_I8:  return 8;
        case NY_TYPE_I16: return 16;
        case NY_TYPE_I32: return 32;
        case NY_TYPE_I64: return 64;
        case NY_TYPE_PTR: return 64;
        case NY_TYPE_F16: return 16;
        case NY_TYPE_F32: return 32;
        case NY_TYPE_F64: return 64;
        default: return 32;
    }
}

[[nodiscard]] int64_t ny_opt_mask_to_width(int64_t val, uint32_t width) {
    if (width >= 64) return val;
    int64_t mask = ((int64_t)1 << width) - 1;
    return val & mask;
}

[[nodiscard]] int64_t ny_opt_sign_extend_width(int64_t val, uint32_t width) {
    if (width >= 64) return val;
    uint32_t shift = 64 - width;
    return (val << shift) >> shift;
}

[[nodiscard]] bool ny_opt_get_operand_const_int(const Ny_Function *fn, Ny_Operand op, int64_t *out_val) {
    if (op.kind == NY_OP_IMM_INT) {
        if (out_val) *out_val = op.imm_int;
        return true;
    }
    if (op.kind == NY_OP_VALUE) {
        const Ny_Value *v = ny_function_get_value(fn, op.val);
        if (v != nullptr && v->kind == NY_VAL_INSTRUCTION && v->def != NY_INVALID_INST) {
            const Ny_Instruction *def_inst = ny_function_get_instruction(fn, v->def);
            if (def_inst != nullptr && def_inst->opcode == NY_OPCODE_CONST) {
                const Ny_Operand *ops = ny_function_get_operands(fn, def_inst);
                if (def_inst->op_count == 1 && ops[0].kind == NY_OP_IMM_INT) {
                    if (out_val) *out_val = ops[0].imm_int;
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] bool ny_opt_get_operand_const_float(const Ny_Function *fn, Ny_Operand op, double *out_val) {
    if (op.kind == NY_OP_IMM_FLOAT) {
        if (out_val) *out_val = op.imm_float;
        return true;
    }
    if (op.kind == NY_OP_VALUE) {
        const Ny_Value *v = ny_function_get_value(fn, op.val);
        if (v != nullptr && v->kind == NY_VAL_INSTRUCTION && v->def != NY_INVALID_INST) {
            const Ny_Instruction *def_inst = ny_function_get_instruction(fn, v->def);
            if (def_inst != nullptr && def_inst->opcode == NY_OPCODE_CONST) {
                const Ny_Operand *ops = ny_function_get_operands(fn, def_inst);
                if (def_inst->op_count == 1 && ops[0].kind == NY_OP_IMM_FLOAT) {
                    if (out_val) *out_val = ops[0].imm_float;
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] bool ny_opt_is_commutative(Ny_Opcode op) {
    switch (op) {
        case NY_OPCODE_ADD:
        case NY_OPCODE_MUL:
        case NY_OPCODE_FADD:
        case NY_OPCODE_FMUL:
        case NY_OPCODE_AND:
        case NY_OPCODE_OR:
        case NY_OPCODE_XOR:
        case NY_OPCODE_CMP_EQ:
        case NY_OPCODE_CMP_NE:
        case NY_OPCODE_FCMP_EQ:
        case NY_OPCODE_FCMP_NE:
            return true;
        default:
            return false;
    }
}
