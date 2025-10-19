// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <limits.h>

[[nodiscard]] bool ny_fold_int_unary(Ny_Opcode op, int64_t a, uint32_t width, int64_t *out_val) {
    switch (op) {
        case NY_OPCODE_NEG:
            if (out_val) *out_val = ny_opt_sign_extend_width(-a, width);
            return true;
        case NY_OPCODE_NOT:
            if (out_val) *out_val = ny_opt_sign_extend_width(~a, width);
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool ny_fold_int_binary(Ny_Opcode op, int64_t a, int64_t b, uint32_t width, int64_t *out_val) {
    if (width == 0) width = 32;

    switch (op) {
        case NY_OPCODE_ADD:
            if (out_val) *out_val = ny_opt_sign_extend_width(a + b, width);
            return true;
        case NY_OPCODE_SUB:
            if (out_val) *out_val = ny_opt_sign_extend_width(a - b, width);
            return true;
        case NY_OPCODE_MUL:
            if (out_val) *out_val = ny_opt_sign_extend_width(a * b, width);
            return true;
        case NY_OPCODE_DIV_S:
            if (b == 0) return false;
            if (width == 64 && a == INT64_MIN && b == -1) return false;
            if (out_val) *out_val = ny_opt_sign_extend_width(a / b, width);
            return true;
        case NY_OPCODE_DIV_U: {
            uint64_t ub = (uint64_t)ny_opt_mask_to_width(b, width);
            if (ub == 0) return false;
            uint64_t ua = (uint64_t)ny_opt_mask_to_width(a, width);
            if (out_val) *out_val = ny_opt_sign_extend_width((int64_t)(ua / ub), width);
            return true;
        }
        case NY_OPCODE_REM_S:
            if (b == 0) return false;
            if (out_val) *out_val = ny_opt_sign_extend_width(a % b, width);
            return true;
        case NY_OPCODE_REM_U: {
            uint64_t ub = (uint64_t)ny_opt_mask_to_width(b, width);
            if (ub == 0) return false;
            uint64_t ua = (uint64_t)ny_opt_mask_to_width(a, width);
            if (out_val) *out_val = ny_opt_sign_extend_width((int64_t)(ua % ub), width);
            return true;
        }
        case NY_OPCODE_AND:
            if (out_val) *out_val = a & b;
            return true;
        case NY_OPCODE_OR:
            if (out_val) *out_val = a | b;
            return true;
        case NY_OPCODE_XOR:
            if (out_val) *out_val = a ^ b;
            return true;
        case NY_OPCODE_SHL: {
            uint32_t sh = (uint32_t)b % width;
            if (out_val) *out_val = ny_opt_sign_extend_width(a << sh, width);
            return true;
        }
        case NY_OPCODE_SHR: {
            uint32_t sh = (uint32_t)b % width;
            uint64_t ua = (uint64_t)ny_opt_mask_to_width(a, width);
            if (out_val) *out_val = ny_opt_sign_extend_width((int64_t)(ua >> sh), width);
            return true;
        }
        case NY_OPCODE_SAR: {
            uint32_t sh = (uint32_t)b % width;
            int64_t sa = ny_opt_sign_extend_width(a, width);
            if (out_val) *out_val = ny_opt_sign_extend_width(sa >> sh, width);
            return true;
        }
        case NY_OPCODE_ROTL: {
            uint32_t sh = (uint32_t)b % width;
            uint64_t ua = (uint64_t)ny_opt_mask_to_width(a, width);
            uint64_t res = (sh == 0) ? ua : ((ua << sh) | (ua >> (width - sh)));
            if (out_val) *out_val = ny_opt_sign_extend_width((int64_t)res, width);
            return true;
        }
        case NY_OPCODE_ROTR: {
            uint32_t sh = (uint32_t)b % width;
            uint64_t ua = (uint64_t)ny_opt_mask_to_width(a, width);
            uint64_t res = (sh == 0) ? ua : ((ua >> sh) | (ua << (width - sh)));
            if (out_val) *out_val = ny_opt_sign_extend_width((int64_t)res, width);
            return true;
        }
        default:
            return false;
    }
}

[[nodiscard]] bool ny_fold_int_cmp(Ny_Opcode op, int64_t a, int64_t b, uint32_t width, int64_t *out_val) {
    if (width == 0) width = 32;
    int64_t sa = ny_opt_sign_extend_width(a, width);
    int64_t sb = ny_opt_sign_extend_width(b, width);
    uint64_t ua = (uint64_t)ny_opt_mask_to_width(a, width);
    uint64_t ub = (uint64_t)ny_opt_mask_to_width(b, width);

    switch (op) {
        case NY_OPCODE_CMP_EQ:   if (out_val) *out_val = (sa == sb) ? 1 : 0; return true;
        case NY_OPCODE_CMP_NE:   if (out_val) *out_val = (sa != sb) ? 1 : 0; return true;
        case NY_OPCODE_CMP_LT_S: if (out_val) *out_val = (sa < sb)  ? 1 : 0; return true;
        case NY_OPCODE_CMP_LT_U: if (out_val) *out_val = (ua < ub)  ? 1 : 0; return true;
        case NY_OPCODE_CMP_LE_S: if (out_val) *out_val = (sa <= sb) ? 1 : 0; return true;
        case NY_OPCODE_CMP_LE_U: if (out_val) *out_val = (ua <= ub) ? 1 : 0; return true;
        case NY_OPCODE_CMP_GT_S: if (out_val) *out_val = (sa > sb)  ? 1 : 0; return true;
        case NY_OPCODE_CMP_GT_U: if (out_val) *out_val = (ua > ub)  ? 1 : 0; return true;
        case NY_OPCODE_CMP_GE_S: if (out_val) *out_val = (sa >= sb) ? 1 : 0; return true;
        case NY_OPCODE_CMP_GE_U: if (out_val) *out_val = (ua >= ub) ? 1 : 0; return true;
        default: return false;
    }
}

[[nodiscard]] bool ny_fold_float_unary(Ny_Opcode op, double a, double *out_val) {
    if (op == NY_OPCODE_FNEG) {
        if (out_val) *out_val = -a;
        return true;
    }
    return false;
}

[[nodiscard]] bool ny_fold_float_binary(Ny_Opcode op, double a, double b, double *out_val) {
    switch (op) {
        case NY_OPCODE_FADD: if (out_val) *out_val = a + b; return true;
        case NY_OPCODE_FSUB: if (out_val) *out_val = a - b; return true;
        case NY_OPCODE_FMUL: if (out_val) *out_val = a * b; return true;
        case NY_OPCODE_FDIV:
            if (b == 0.0) return false;
            if (out_val) *out_val = a / b;
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool ny_fold_float_cmp(Ny_Opcode op, double a, double b, int64_t *out_val) {
    switch (op) {
        case NY_OPCODE_FCMP_EQ: if (out_val) *out_val = (a == b) ? 1 : 0; return true;
        case NY_OPCODE_FCMP_NE: if (out_val) *out_val = (a != b) ? 1 : 0; return true;
        case NY_OPCODE_FCMP_LT: if (out_val) *out_val = (a < b)  ? 1 : 0; return true;
        case NY_OPCODE_FCMP_LE: if (out_val) *out_val = (a <= b) ? 1 : 0; return true;
        case NY_OPCODE_FCMP_GT: if (out_val) *out_val = (a > b)  ? 1 : 0; return true;
        case NY_OPCODE_FCMP_GE: if (out_val) *out_val = (a >= b) ? 1 : 0; return true;
        default: return false;
    }
}

bool ny_opt_pass_const_fold(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am) {
    (void)mod;
    bool changed = false;

    for (size_t inst_idx = 0; inst_idx < fn->inst_count; inst_idx++) {
        Ny_Instruction *inst = &fn->instructions[inst_idx];
        if (inst->opcode == NY_OPCODE_NONE || inst->opcode == NY_OPCODE_CONST || inst->result == NY_INVALID_VALUE) {
            continue;
        }

        const Ny_Value *res_val = ny_function_get_value(fn, inst->result);
        if (res_val == nullptr) continue;

        const Ny_Operand *ops = ny_function_get_operands(fn, inst);
        size_t op_cnt = inst->op_count;
        uint32_t width = ny_opt_get_type_bit_width(res_val->type);

        if (op_cnt == 1) {
            int64_t a_int = 0;
            if (ny_opt_get_operand_const_int(fn, ops[0], &a_int)) {
                int64_t res_int = 0;
                if (ny_fold_int_unary((Ny_Opcode)inst->opcode, a_int, width, &res_int)) {
                    inst->opcode = NY_OPCODE_CONST;
                    ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(res_int));
                    inst->op_count = 1;
                    changed = true;
                    continue;
                }
            }

            double a_flt = 0.0;
            if (ny_opt_get_operand_const_float(fn, ops[0], &a_flt)) {
                double res_flt = 0.0;
                if (ny_fold_float_unary((Ny_Opcode)inst->opcode, a_flt, &res_flt)) {
                    inst->opcode = NY_OPCODE_CONST;
                    ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_float(res_flt));
                    inst->op_count = 1;
                    changed = true;
                    continue;
                }
            }
        }

        if (op_cnt == 2) {
            int64_t a_int = 0, b_int = 0;
            bool a_ok = ny_opt_get_operand_const_int(fn, ops[0], &a_int);
            bool b_ok = ny_opt_get_operand_const_int(fn, ops[1], &b_int);

            const Ny_Value *op0_val = (ops[0].kind == NY_OP_VALUE) ? ny_function_get_value(fn, ops[0].val) : nullptr;
            uint32_t cmp_width = (op0_val != nullptr) ? ny_opt_get_type_bit_width(op0_val->type) : width;

            if (a_ok && b_ok) {
                int64_t res_bin = 0;
                if (ny_fold_int_binary((Ny_Opcode)inst->opcode, a_int, b_int, width, &res_bin)) {
                    inst->opcode = NY_OPCODE_CONST;
                    ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(res_bin));
                    inst->op_count = 1;
                    changed = true;
                    continue;
                }
                int64_t res_cmp = 0;
                if (ny_fold_int_cmp((Ny_Opcode)inst->opcode, a_int, b_int, cmp_width, &res_cmp)) {
                    inst->opcode = NY_OPCODE_CONST;
                    ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(res_cmp));
                    inst->op_count = 1;
                    changed = true;
                    continue;
                }
            }

            double fa = 0.0, fb = 0.0;
            bool fa_ok = ny_opt_get_operand_const_float(fn, ops[0], &fa);
            bool fb_ok = ny_opt_get_operand_const_float(fn, ops[1], &fb);
            if (fa_ok && fb_ok) {
                double res_flt = 0.0;
                if (ny_fold_float_binary((Ny_Opcode)inst->opcode, fa, fb, &res_flt)) {
                    inst->opcode = NY_OPCODE_CONST;
                    ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_float(res_flt));
                    inst->op_count = 1;
                    changed = true;
                    continue;
                }
                int64_t res_fcmp = 0;
                if (ny_fold_float_cmp((Ny_Opcode)inst->opcode, fa, fb, &res_fcmp)) {
                    inst->opcode = NY_OPCODE_CONST;
                    ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(res_fcmp));
                    inst->op_count = 1;
                    changed = true;
                    continue;
                }
            }
        }

        if (inst->opcode == NY_OPCODE_SELECT && op_cnt == 3) {
            int64_t cond_val = 0;
            if (ny_opt_get_operand_const_int(fn, ops[0], &cond_val)) {
                Ny_Operand chosen_op = (cond_val != 0) ? ops[1] : ops[2];
                if (chosen_op.kind == NY_OP_IMM_INT) {
                    inst->opcode = NY_OPCODE_CONST;
                    ny_instruction_replace_operand(fn, inst->id, 0, chosen_op);
                    inst->op_count = 1;
                    changed = true;
                    continue;
                } else if (chosen_op.kind == NY_OP_IMM_FLOAT) {
                    inst->opcode = NY_OPCODE_CONST;
                    ny_instruction_replace_operand(fn, inst->id, 0, chosen_op);
                    inst->op_count = 1;
                    changed = true;
                    continue;
                } else if (chosen_op.kind == NY_OP_VALUE) {
                    ny_function_replace_all_uses(fn, inst->result, chosen_op.val);
                    ny_function_remove_instruction(fn, inst->id);
                    changed = true;
                    continue;
                }
            }
        }

        switch (inst->opcode) {
            case NY_OPCODE_TRUNCATE:
                if (op_cnt >= 1) {
                    int64_t val = 0;
                    if (ny_opt_get_operand_const_int(fn, ops[0], &val)) {
                        int64_t res = ny_opt_sign_extend_width(val, width);
                        inst->opcode = NY_OPCODE_CONST;
                        ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(res));
                        inst->op_count = 1;
                        changed = true;
                        continue;
                    }
                }
                break;
            case NY_OPCODE_SEXT:
                if (op_cnt >= 1) {
                    int64_t val = 0;
                    if (ny_opt_get_operand_const_int(fn, ops[0], &val)) {
                        uint32_t src_width = 32;
                        if (ops[0].kind == NY_OP_VALUE) {
                            const Ny_Value *v = ny_function_get_value(fn, ops[0].val);
                            if (v != nullptr) src_width = ny_opt_get_type_bit_width(v->type);
                        }
                        int64_t res = ny_opt_sign_extend_width(val, src_width);
                        inst->opcode = NY_OPCODE_CONST;
                        ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(res));
                        inst->op_count = 1;
                        changed = true;
                        continue;
                    }
                }
                break;
            case NY_OPCODE_ZEXT:
                if (op_cnt >= 1) {
                    int64_t val = 0;
                    if (ny_opt_get_operand_const_int(fn, ops[0], &val)) {
                        uint32_t src_width = 32;
                        if (ops[0].kind == NY_OP_VALUE) {
                            const Ny_Value *v = ny_function_get_value(fn, ops[0].val);
                            if (v != nullptr) src_width = ny_opt_get_type_bit_width(v->type);
                        }
                        int64_t res = ny_opt_mask_to_width(val, src_width);
                        inst->opcode = NY_OPCODE_CONST;
                        ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(res));
                        inst->op_count = 1;
                        changed = true;
                        continue;
                    }
                }
                break;
            case NY_OPCODE_SITOFP:
                if (op_cnt >= 1) {
                    int64_t val = 0;
                    if (ny_opt_get_operand_const_int(fn, ops[0], &val)) {
                        double res = (double)val;
                        inst->opcode = NY_OPCODE_CONST;
                        ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_float(res));
                        inst->op_count = 1;
                        changed = true;
                        continue;
                    }
                }
                break;
            case NY_OPCODE_FPTOSI:
                if (op_cnt >= 1) {
                    double val = 0.0;
                    if (ny_opt_get_operand_const_float(fn, ops[0], &val)) {
                        int64_t res = ny_opt_sign_extend_width((int64_t)val, width);
                        inst->opcode = NY_OPCODE_CONST;
                        ny_instruction_replace_operand(fn, inst->id, 0, ny_operand_int(res));
                        inst->op_count = 1;
                        changed = true;
                        continue;
                    }
                }
                break;
            default:
                break;
        }
    }

    if (changed && am != nullptr) {
        ny_analysis_invalidate_use_def(am);
    }

    return changed;
}
