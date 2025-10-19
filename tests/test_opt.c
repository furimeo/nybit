// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/opt.h>
#include <nybit/parser.h>
#include <nybit/support.h>

void test_opt_const_fold(void) {
    const char *src =
        "@function fold_arith(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %c10 = const 10;\n"
        "    %c20 = const 20;\n"
        "    %add = add %c10, %c20;\n"
        "    %mul = mul %c10, %c20;\n"
        "    %sub = sub %c20, %c10;\n"
        "    %cmp = cmp.lt.s %c10, %c20;\n"
        "    %sel = select %cmp, %add, %mul;\n"
        "    %r = add %x, %sel;\n"
        "    @return %r;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_fold");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    bool changed = ny_opt_pass_const_fold(&ctx.module, fn, nullptr);
    TEST_ASSERT(changed);

    Ny_Instruction *add_inst = ny_function_get_instruction(fn, 2);
    TEST_ASSERT(add_inst != nullptr && add_inst->opcode == NY_OPCODE_CONST);
    const Ny_Operand *ops = ny_function_get_operands(fn, add_inst);
    TEST_ASSERT_EQ(ops[0].imm_int, 30);

    Ny_Instruction *mul_inst = ny_function_get_instruction(fn, 3);
    TEST_ASSERT(mul_inst != nullptr && mul_inst->opcode == NY_OPCODE_CONST);
    const Ny_Operand *m_ops = ny_function_get_operands(fn, mul_inst);
    TEST_ASSERT_EQ(m_ops[0].imm_int, 200);

    Ny_Instruction *cmp_inst = ny_function_get_instruction(fn, 5);
    TEST_ASSERT(cmp_inst != nullptr && cmp_inst->opcode == NY_OPCODE_CONST);
    const Ny_Operand *c_ops = ny_function_get_operands(fn, cmp_inst);
    TEST_ASSERT_EQ(c_ops[0].imm_int, 1);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_opt_sccp(void) {
    const char *src =
        "@function sccp_test(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %cond = const 1;\n"
        "    @branch_if %cond, .left, .right;\n"
        ".left;\n"
        "    %v_left = const 100;\n"
        "    @branch .merge;\n"
        ".right;\n"
        "    %v_right = const 200;\n"
        "    @branch .merge;\n"
        ".merge;\n"
        "    %res = phi %v_left, .left, %v_right, .right;\n"
        "    %out = add %x, %res;\n"
        "    @return %out;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_sccp");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    bool changed = ny_opt_pass_sccp(&ctx.module, fn, nullptr);
    TEST_ASSERT(changed);

    Ny_Block *entry_blk = ny_function_get_block(fn, 0);
    Ny_Instruction *term = ny_function_get_instruction(fn, entry_blk->last_inst);
    TEST_ASSERT(term != nullptr && term->opcode == NY_OPCODE_BRANCH);
    const Ny_Operand *t_ops = ny_function_get_operands(fn, term);
    TEST_ASSERT_EQ(t_ops[0].blk, 1);

    Ny_Block *merge_blk = ny_function_get_block(fn, 3);
    Ny_Instruction *phi_inst = ny_function_get_instruction(fn, merge_blk->first_inst);
    TEST_ASSERT(phi_inst != nullptr && phi_inst->opcode == NY_OPCODE_CONST);
    const Ny_Operand *phi_ops = ny_function_get_operands(fn, phi_inst);
    TEST_ASSERT_EQ(phi_ops[0].imm_int, 100);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_opt_dce(void) {
    const char *src =
        "@function dce_test(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %dead1 = add %x, 1;\n"
        "    %dead2 = mul %dead1, 2;\n"
        "    %dead3 = sub %dead2, %x;\n"
        "    %live = add %x, 10;\n"
        "    @return %live;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_dce");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    bool changed = ny_opt_pass_dce(&ctx.module, fn, nullptr);
    TEST_ASSERT(changed);

    Ny_Instruction *i1 = ny_function_get_instruction(fn, 0);
    Ny_Instruction *i2 = ny_function_get_instruction(fn, 1);
    Ny_Instruction *i3 = ny_function_get_instruction(fn, 2);
    Ny_Instruction *i_live = ny_function_get_instruction(fn, 3);

    TEST_ASSERT_EQ(i1->opcode, NY_OPCODE_NONE);
    TEST_ASSERT_EQ(i2->opcode, NY_OPCODE_NONE);
    TEST_ASSERT_EQ(i3->opcode, NY_OPCODE_NONE);
    TEST_ASSERT_EQ(i_live->opcode, NY_OPCODE_ADD);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_opt_copy_prop(void) {
    const char *src =
        "@function copy_test(%cond: i8, %x: i32) -> i32;\n"
        ".entry;\n"
        "    %sel = select %cond, %x, %x;\n"
        "    %res = add %sel, 1;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_copy");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    bool changed = ny_opt_pass_copy_prop(&ctx.module, fn, nullptr);
    TEST_ASSERT(changed);

    Ny_Instruction *add_inst = ny_function_get_instruction(fn, 1);
    TEST_ASSERT(add_inst != nullptr && add_inst->opcode == NY_OPCODE_ADD);
    const Ny_Operand *ops = ny_function_get_operands(fn, add_inst);
    TEST_ASSERT_EQ(ops[0].val, 1); // %x is param 1

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_opt_gvn(void) {
    const char *src =
        "@function gvn_test(%x: i32, %y: i32) -> i32;\n"
        ".entry;\n"
        "    %a = add %x, %y;\n"
        "    %b = add %y, %x;\n"
        "    %res = mul %a, %b;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_gvn");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    bool changed = ny_opt_pass_gvn(&ctx.module, fn, nullptr);
    TEST_ASSERT(changed);

    Ny_Instruction *b_inst = ny_function_get_instruction(fn, 1);
    TEST_ASSERT_EQ(b_inst->opcode, NY_OPCODE_NONE);

    Ny_Instruction *mul_inst = ny_function_get_instruction(fn, 2);
    TEST_ASSERT_EQ(mul_inst->opcode, NY_OPCODE_MUL);
    const Ny_Operand *ops = ny_function_get_operands(fn, mul_inst);
    TEST_ASSERT_EQ(ops[0].val, 2); // %a
    TEST_ASSERT_EQ(ops[1].val, 2); // %a (replaced %b)

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_opt_cfg_simplify(void) {
    const char *src =
        "@function cfg_test(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %cond = const 1;\n"
        "    @branch_if %cond, .live_target, .dead_target;\n"
        ".live_target;\n"
        "    @branch .trampoline;\n"
        ".dead_target;\n"
        "    %d = add %x, 999;\n"
        "    @return %d;\n"
        ".trampoline;\n"
        "    @branch .exit;\n"
        ".exit;\n"
        "    %out = add %x, 42;\n"
        "    @return %out;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_cfg_simp");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    bool changed = ny_opt_pass_cfg_simplify(&ctx.module, fn, nullptr);
    TEST_ASSERT(changed);

    Ny_Block *entry_blk = ny_function_get_block(fn, fn->entry_block);
    TEST_ASSERT(entry_blk != nullptr);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_opt_pipeline_e2e(void) {
    const char *src =
        "@function full_pipeline(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %c10 = const 10;\n"
        "    %c20 = const 20;\n"
        "    %sum1 = add %c10, %c20;\n"
        "    %sum2 = add %c20, %c10;\n"
        "    %cond = cmp.lt.s %sum1, 100;\n"
        "    %dead = mul %x, 999;\n"
        "    @branch_if %cond, .then, .otherwise;\n"
        ".then;\n"
        "    %t_res = add %x, %sum1;\n"
        "    @branch .merge;\n"
        ".otherwise;\n"
        "    %f_res = add %x, %sum2;\n"
        "    @branch .merge;\n"
        ".merge;\n"
        "    %res = phi %t_res, .then, %f_res, .otherwise;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_pipeline");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    bool changed = ny_opt_run_pipeline(&ctx.module, fn, NY_OPT_O1);
    TEST_ASSERT(changed);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}
