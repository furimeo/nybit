// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nybit/ir.h>
#include <nybit/opt.h>
#include <nybit/parser.h>
#include <nybit/support.h>

void test_dominance_same_block_violation(void) {
    Ny_Context ctx;
    ny_context_init(&ctx, "test_dom1");

    Ny_Module *mod = &ctx.module;
    Ny_Builder b;
    ny_builder_init(&b, mod);

    Ny_Function_ID fn_id = ny_builder_add_function(&b, "bad_dom", NY_TYPE_I32);
    Ny_Block_ID entry = ny_builder_add_block(&b, "entry");
    ny_builder_set_insert_point(&b, fn_id, entry);

    Ny_Function *fn = ny_module_get_function(mod, fn_id);
    Ny_Value_ID val_b = ny_function_create_value(fn, NY_TYPE_I32, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str("b"));

    Ny_Value_ID val_a = ny_builder_add(&b, val_b, val_b, NY_TYPE_I32, "a");

    Ny_Operand ops[1] = { ny_operand_int(42) };
    ny_function_append_instruction(fn, entry, NY_OPCODE_CONST, val_b, ops, 1, 0);
    ny_builder_ret(&b, val_a);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(mod, &diags);
    TEST_ASSERT(!valid);
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    ny_context_destroy(&ctx);
}

void test_dominance_cross_block_violation(void) {
    const char *src =
        "@function cross_dom(%cond: i8) -> i32;\n"
        ".entry;\n"
        "    @branch_if %cond, .left, .right;\n"
        ".left;\n"
        "    %left_val = const 10;\n"
        "    @branch .merge;\n"
        ".right;\n"
        "    %right_val = const 20;\n"
        "    @branch .merge;\n"
        ".merge;\n"
        "    %r = add %left_val, %left_val;\n"
        "    @return %r;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_cross_dom");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(!valid);
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_phi_invariants(void) {
    Ny_Context ctx;
    ny_context_init(&ctx, "test_phi");

    Ny_Module *mod = &ctx.module;
    Ny_Builder b;
    ny_builder_init(&b, mod);

    Ny_Function_ID fn_id = ny_builder_add_function(&b, "test_phi", NY_TYPE_I32);
    Ny_Block_ID entry = ny_builder_add_block(&b, "entry");
    ny_builder_set_insert_point(&b, fn_id, entry);

    Ny_Value_ID v1 = ny_builder_const_i64(&b, 10, NY_TYPE_I32, "v1");

    Ny_Function *fn = ny_module_get_function(mod, fn_id);
    Ny_Value_ID phi_val = ny_function_create_value(fn, NY_TYPE_I32, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, ny_str("phi_res"));
    Ny_Operand phi_ops[2] = { ny_operand_value(v1), ny_operand_block(entry) };
    ny_function_append_instruction(fn, entry, NY_OPCODE_PHI, phi_val, phi_ops, 2, 0);
    ny_builder_ret(&b, phi_val);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(mod, &diags);
    TEST_ASSERT(!valid);
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    ny_context_destroy(&ctx);
}

void test_identity_and_comparison_canonicalization(void) {
    const char *src =
        "@function math_canon(%x: i32, %y: i32) -> i32;\n"
        ".entry;\n"
        "    %c = cmp.gt.s %x, %y;\n"
        "    %zero = const 0;\n"
        "    %add_zero = add %x, %zero;\n"
        "    %one = const 1;\n"
        "    %mul_one = mul %x, %one;\n"
        "    %r = add %add_zero, %mul_one;\n"
        "    @return %r;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "math_canon");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function_by_name(&ctx.module, "math_canon");
    TEST_ASSERT(fn != nullptr);

    ny_canonicalize_module(&ctx.module);

    Ny_Value *c_val = ny_function_get_value(fn, 2);
    TEST_ASSERT(c_val != nullptr);
    Ny_Instruction *c_inst = ny_function_get_instruction(fn, c_val->def);
    TEST_ASSERT_EQ(c_inst->opcode, NY_OPCODE_CMP_LT_S);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_cfg_canonicalization_cleanup(void) {
    const char *src =
        "@function cfg_canon(%cond: i8) -> i32;\n"
        ".entry;\n"
        "    @branch_if %cond, .target, .target;\n"
        ".target;\n"
        "    %val = const 42;\n"
        "    @return %val;\n"
        "    %dead1 = const 99;\n"
        "    %dead2 = add %dead1, %dead1;\n"
        ".unreachable_block;\n"
        "    %dead3 = const 100;\n"
        "    @return %dead3;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "cfg_canon");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function_by_name(&ctx.module, "cfg_canon");
    TEST_ASSERT(fn != nullptr);

    ny_canonicalize_module(&ctx.module);

    Ny_Block *entry_blk = ny_function_get_block(fn, fn->entry_block);
    TEST_ASSERT_EQ(entry_blk->inst_count, 2);

    for (size_t i = 0; i < fn->block_count; i++) {
        if (ny_str_eq_cstr(fn->blocks[i].name, "unreachable_block")) {
            TEST_ASSERT_EQ(fn->blocks[i].inst_count, 0);
        }
    }

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_canonicalization_determinism(void) {
    const char *src =
        "@function determinism(%a: i32, %b: i32) -> i32;\n"
        ".entry;\n"
        "    %zero = const 0;\n"
        "    %x = add %zero, %a;\n"
        "    %cmp = cmp.gt.s %a, %b;\n"
        "    %y = add %x, %zero;\n"
        "    @return %y;\n"
        ";;\n";

    Ny_Context ctx1;
    ny_context_init(&ctx1, "det1");
    Ny_Parser p1;
    ny_parser_init(&p1, &ctx1.module, src, strlen(src), &ctx1.arena);
    ny_parse_module(&p1);
    ny_canonicalize_module(&ctx1.module);
    char *dump1 = ny_dump_module(&ctx1.module, &ctx1.arena);

    Ny_Context ctx2;
    ny_context_init(&ctx2, "det2");
    Ny_Parser p2;
    ny_parser_init(&p2, &ctx2.module, src, strlen(src), &ctx2.arena);
    ny_parse_module(&p2);
    ny_canonicalize_module(&ctx2.module);
    char *dump2 = ny_dump_module(&ctx2.module, &ctx2.arena);

    TEST_ASSERT_STR_EQ(dump1, dump2);

    ny_parser_destroy(&p1);
    ny_context_destroy(&ctx1);
    ny_parser_destroy(&p2);
    ny_context_destroy(&ctx2);
}
