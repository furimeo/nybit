// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"

void ny_builder_init(Ny_Builder *b, Ny_Module *mod) {
    b->module = mod;
    b->cur_fn = NY_INVALID_FUNCTION;
    b->cur_block = NY_INVALID_BLOCK;
}

void ny_builder_set_insert_point(Ny_Builder *b, Ny_Function_ID fn_id, Ny_Block_ID block_id) {
    b->cur_fn = fn_id;
    b->cur_block = block_id;
}

Ny_Function *ny_builder_current_function(Ny_Builder *b) {
    return ny_module_get_function(b->module, b->cur_fn);
}

Ny_Function_ID ny_builder_add_function(Ny_Builder *b, const char *name, Ny_Type_ID ret_type) {
    Ny_Function_ID fn_id = ny_module_create_function(b->module, ny_str(name), ret_type, NY_CC_DEFAULT);
    b->cur_fn = fn_id;
    b->cur_block = NY_INVALID_BLOCK;
    return fn_id;
}

Ny_Value_ID ny_builder_add_param(Ny_Builder *b, const char *name, Ny_Type_ID type) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL);

    uint32_t idx = (uint32_t)fn->param_count;
    ny_function_add_param(fn, type, ny_str(name));
    return ny_function_create_value(fn, type, NY_VAL_ARGUMENT, NY_INVALID_INST, idx, ny_str(name));
}

Ny_Block_ID ny_builder_add_block(Ny_Builder *b, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL);
    return ny_function_create_block(fn, ny_str(name));
}

Ny_Value_ID ny_builder_const_i64(Ny_Builder *b, int64_t val, Ny_Type_ID type, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Value_ID res = ny_function_create_value(fn, type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str(name));
    Ny_Operand op = ny_operand_int(val);
    ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_CONST, res, &op, 1, 0);
    return res;
}

Ny_Value_ID ny_builder_const_f64(Ny_Builder *b, double val, Ny_Type_ID type, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Value_ID res = ny_function_create_value(fn, type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str(name));
    Ny_Operand op = ny_operand_float(val);
    ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_CONST, res, &op, 1, 0);
    return res;
}

Ny_Value_ID ny_builder_binary(Ny_Builder *b, Ny_Opcode op, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Value_ID res = ny_function_create_value(fn, type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str(name));
    Ny_Operand ops[2] = { ny_operand_value(lhs), ny_operand_value(rhs) };
    ny_function_append_instruction(fn, b->cur_block, op, res, ops, 2, 0);
    return res;
}

Ny_Value_ID ny_builder_add(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_ADD, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_sub(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_SUB, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_mul(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_MUL, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_div_s(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_DIV_S, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_div_u(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_DIV_U, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_rem_s(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_REM_S, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_rem_u(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_REM_U, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_neg(Ny_Builder *b, Ny_Value_ID val, Ny_Type_ID type, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Value_ID res = ny_function_create_value(fn, type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str(name));
    Ny_Operand op = ny_operand_value(val);
    ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_NEG, res, &op, 1, 0);
    return res;
}

Ny_Value_ID ny_builder_and(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_AND, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_or(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_OR, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_xor(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_XOR, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_not(Ny_Builder *b, Ny_Value_ID val, Ny_Type_ID type, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Value_ID res = ny_function_create_value(fn, type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str(name));
    Ny_Operand op = ny_operand_value(val);
    ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_NOT, res, &op, 1, 0);
    return res;
}

Ny_Value_ID ny_builder_shl(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_SHL, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_shr(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_SHR, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_sar(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name) {
    return ny_builder_binary(b, NY_OPCODE_SAR, lhs, rhs, type, name);
}

Ny_Value_ID ny_builder_cmp(Ny_Builder *b, Ny_Opcode op, Ny_Value_ID lhs, Ny_Value_ID rhs, const char *name) {
    return ny_builder_binary(b, op, lhs, rhs, NY_TYPE_I8, name);
}

Ny_Value_ID ny_builder_select(Ny_Builder *b, Ny_Value_ID cond, Ny_Value_ID true_val, Ny_Value_ID false_val, Ny_Type_ID type, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Value_ID res = ny_function_create_value(fn, type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str(name));
    Ny_Operand ops[3] = { ny_operand_value(cond), ny_operand_value(true_val), ny_operand_value(false_val) };
    ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_SELECT, res, ops, 3, 0);
    return res;
}

Ny_Value_ID ny_builder_load(Ny_Builder *b, Ny_Value_ID ptr, Ny_Type_ID val_type, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Value_ID res = ny_function_create_value(fn, val_type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str(name));
    Ny_Operand op = ny_operand_value(ptr);
    ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_LOAD, res, &op, 1, 0);
    return res;
}

Ny_Inst_ID ny_builder_store(Ny_Builder *b, Ny_Value_ID ptr, Ny_Value_ID val) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Operand ops[2] = { ny_operand_value(ptr), ny_operand_value(val) };
    return ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_STORE, NY_INVALID_VALUE, ops, 2, 0);
}

Ny_Inst_ID ny_builder_branch(Ny_Builder *b, Ny_Block_ID target) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Block *cur_blk = ny_function_get_block(fn, b->cur_block);
    Ny_Block *tgt_blk = ny_function_get_block(fn, target);
    assert(tgt_blk != NULL);

    ny_block_add_edge(cur_blk, tgt_blk);

    Ny_Operand op = ny_operand_block(target);
    return ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_BRANCH, NY_INVALID_VALUE, &op, 1, 0);
}

Ny_Inst_ID ny_builder_branch_if(Ny_Builder *b, Ny_Value_ID cond, Ny_Block_ID true_blk, Ny_Block_ID false_blk) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Block *cur_blk = ny_function_get_block(fn, b->cur_block);
    Ny_Block *t_blk = ny_function_get_block(fn, true_blk);
    Ny_Block *f_blk = ny_function_get_block(fn, false_blk);
    assert(t_blk != NULL && f_blk != NULL);

    ny_block_add_edge(cur_blk, t_blk);
    ny_block_add_edge(cur_blk, f_blk);

    Ny_Operand ops[3] = { ny_operand_value(cond), ny_operand_block(true_blk), ny_operand_block(false_blk) };
    return ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_BRANCH_IF, NY_INVALID_VALUE, ops, 3, 0);
}

Ny_Inst_ID ny_builder_ret(Ny_Builder *b, Ny_Value_ID val) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    if (val != NY_INVALID_VALUE) {
        Ny_Operand op = ny_operand_value(val);
        return ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_RETURN, NY_INVALID_VALUE, &op, 1, 0);
    }
    return ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_RETURN, NY_INVALID_VALUE, NULL, 0, 0);
}

Ny_Inst_ID ny_builder_trap(Ny_Builder *b) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);
    return ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_TRAP, NY_INVALID_VALUE, NULL, 0, 0);
}

Ny_Value_ID ny_builder_call(Ny_Builder *b, Ny_Function_ID callee, const Ny_Value_ID *args, size_t arg_count, Ny_Type_ID ret_type, const char *name) {
    Ny_Function *fn = ny_builder_current_function(b);
    assert(fn != NULL && b->cur_block != NY_INVALID_BLOCK);

    Ny_Value_ID res = ret_type != NY_TYPE_VOID ? ny_function_create_value(fn, ret_type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str(name)) : NY_INVALID_VALUE;

    Ny_Operand *ops = (Ny_Operand *)ny_alloc((arg_count + 1) * sizeof(Ny_Operand));
    ops[0] = ny_operand_function(callee);
    for (size_t i = 0; i < arg_count; i++) {
        ops[i + 1] = ny_operand_value(args[i]);
    }

    ny_function_append_instruction(fn, b->cur_block, NY_OPCODE_CALL, res, ops, arg_count + 1, 0);
    ny_free(ops, (arg_count + 1) * sizeof(Ny_Operand));
    return res;
}
