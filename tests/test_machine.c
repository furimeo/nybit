// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nybit/machine.h"
#include "nybit/ir.h"
#include "nybit/parser.h"
#include "nybit/opt.h"

void test_machine_ir_construction(void) {
    Ny_Machine_Module mmod;
    ny_mmod_init(&mmod, ny_str("test_mmod"));

    Ny_Machine_Function *fn = ny_mmod_create_function(&mmod, ny_str("manual_add"), NY_TYPE_I32, NY_CC_DEFAULT);
    TEST_ASSERT(fn != nullptr);

    Ny_Machine_Reg v0 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR32);
    Ny_Machine_Reg v1 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR32);
    Ny_Machine_Reg v2 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR32);
    ny_mfunc_add_param(fn, v0);
    ny_mfunc_add_param(fn, v1);

    Ny_Block_ID entry = ny_mfunc_create_block(fn, ny_str("entry"));
    Ny_Machine_Operand ops[2] = { ny_mop_reg(v0), ny_mop_reg(v1) };
    Ny_Inst_ID i_add = ny_mfunc_append_inst(fn, entry, NY_MOPC_ADD, v2, ops, 2, 0);

    Ny_Machine_Operand ret_op = ny_mop_reg(v2);
    Ny_Inst_ID i_ret = ny_mfunc_append_inst(fn, entry, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_op, 1, NY_MINST_FLAG_TERMINATOR);

    TEST_ASSERT_EQ(fn->block_count, 1);
    TEST_ASSERT_EQ(fn->inst_count, 2);
    TEST_ASSERT_EQ(fn->vreg_count, 3);
    TEST_ASSERT_EQ(fn->param_count, 2);

    Ny_Machine_Block *blk = ny_mfunc_get_block(fn, entry);
    TEST_ASSERT(blk != nullptr);
    TEST_ASSERT_EQ(blk->first_inst, i_add);
    TEST_ASSERT_EQ(blk->last_inst, i_ret);

    Ny_Machine_Instruction *inst_add = ny_mfunc_get_inst(fn, i_add);
    TEST_ASSERT(inst_add != nullptr);
    TEST_ASSERT_EQ(inst_add->opcode, NY_MOPC_ADD);
    TEST_ASSERT(ny_mreg_eq(inst_add->def_reg, v2));

    ny_mmod_destroy(&mmod);
}

void test_machine_ir_lowering_arithmetic(void) {
    Ny_Context ctx;
    ny_context_init(&ctx, "test_lowering_arith");

    Ny_Builder b;
    ny_builder_init(&b, &ctx.module);

    Ny_Function_ID fn_id = ny_builder_add_function(&b, "calc", NY_TYPE_I32);
    Ny_Value_ID a = ny_builder_add_param(&b, "a", NY_TYPE_I32);
    Ny_Value_ID b_val = ny_builder_add_param(&b, "b", NY_TYPE_I32);

    Ny_Block_ID entry = ny_builder_add_block(&b, "entry");
    ny_builder_set_insert_point(&b, fn_id, entry);

    Ny_Value_ID sum = ny_builder_add(&b, a, b_val, NY_TYPE_I32, "sum");
    Ny_Value_ID diff = ny_builder_sub(&b, sum, b_val, NY_TYPE_I32, "diff");
    Ny_Value_ID prod = ny_builder_mul(&b, diff, a, NY_TYPE_I32, "prod");
    ny_builder_ret(&b, prod);

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    bool ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &diags);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(diags.count, 0);

    TEST_ASSERT_EQ(mmod.function_count, 1);
    Ny_Machine_Function *mfn = &mmod.functions[0];
    TEST_ASSERT_EQ(mfn->param_count, 2);
    TEST_ASSERT_EQ(mfn->block_count, 1);
    TEST_ASSERT_EQ(mfn->inst_count, 4);

    Ny_Machine_Instruction *inst0 = ny_mfunc_get_inst(mfn, 0);
    Ny_Machine_Instruction *inst1 = ny_mfunc_get_inst(mfn, 1);
    Ny_Machine_Instruction *inst2 = ny_mfunc_get_inst(mfn, 2);
    Ny_Machine_Instruction *inst3 = ny_mfunc_get_inst(mfn, 3);

    TEST_ASSERT_EQ(inst0->opcode, NY_MOPC_ADD);
    TEST_ASSERT_EQ(inst1->opcode, NY_MOPC_SUB);
    TEST_ASSERT_EQ(inst2->opcode, NY_MOPC_IMUL);
    TEST_ASSERT_EQ(inst3->opcode, NY_MOPC_RET);

    ny_diagnostic_list_destroy(&diags);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
}

void test_machine_ir_lowering_control_flow(void) {
    const char *src =
        "@function abs_test(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %zero = const 0;\n"
        "    %cond = cmp.lt.s %x, %zero;\n"
        "    @branch_if %cond, .negative, .positive;\n"
        ".negative;\n"
        "    %r1 = neg %x;\n"
        "    @return %r1;\n"
        ".positive;\n"
        "    @return %x;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_lowering_cfg");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool parsed = ny_parse_module(&p);
    TEST_ASSERT(parsed);

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    bool ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &diags);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(diags.count, 0);

    TEST_ASSERT_EQ(mmod.function_count, 1);
    Ny_Machine_Function *mfn = &mmod.functions[0];
    TEST_ASSERT_EQ(mfn->block_count, 3);

    Ny_Machine_Block *entry_blk = ny_mfunc_get_block(mfn, 0);
    Ny_Machine_Block *neg_blk   = ny_mfunc_get_block(mfn, 1);
    Ny_Machine_Block *pos_blk   = ny_mfunc_get_block(mfn, 2);

    TEST_ASSERT_EQ(entry_blk->succ_count, 2);
    TEST_ASSERT_EQ(neg_blk->pred_count, 1);
    TEST_ASSERT_EQ(pos_blk->pred_count, 1);

    char *dump = ny_mir_dump_module(&mmod, &ctx.arena);
    TEST_ASSERT(dump != nullptr);
    TEST_ASSERT(strstr(dump, "machine_function @abs_test") != nullptr);
    TEST_ASSERT(strstr(dump, "jcc lt_s, .negative") != nullptr);
    TEST_ASSERT(strstr(dump, "jmp .positive") != nullptr);
    TEST_ASSERT(strstr(dump, "neg") != nullptr);

    ny_parser_destroy(&p);
    ny_diagnostic_list_destroy(&diags);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
}

void test_machine_ir_lowering_memory_and_stack(void) {
    const char *src =
        "@function mem_test(%ptr: ptr, %val: i32) -> i32;\n"
        ".entry;\n"
        "    %slot = stack_slot 8, 4;\n"
        "    store %slot, %val;\n"
        "    %off = addr_offset %ptr, 16;\n"
        "    %v = load %off;\n"
        "    @return %v;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_lowering_mem");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool parsed = ny_parse_module(&p);
    TEST_ASSERT(parsed);

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    bool ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &diags);
    TEST_ASSERT(ok);

    Ny_Machine_Function *mfn = &mmod.functions[0];
    TEST_ASSERT_EQ(mfn->stack_slot_count, 1);
    TEST_ASSERT_EQ(mfn->stack_slots[0].size, 8);
    TEST_ASSERT_EQ(mfn->stack_slots[0].align, 4);

    char *dump = ny_mir_dump_module(&mmod, &ctx.arena);
    TEST_ASSERT(dump != nullptr);
    TEST_ASSERT(strstr(dump, "store") != nullptr);
    TEST_ASSERT(strstr(dump, "load") != nullptr);
    TEST_ASSERT(strstr(dump, "[slot#0]") != nullptr);

    ny_parser_destroy(&p);
    ny_diagnostic_list_destroy(&diags);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
}

void test_machine_ir_e2e_pipeline(void) {
    const char *src =
        "@function add(%a: i32, %b: i32) -> i32;\n"
        ".entry;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n\n"
        "@function opt_demo(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %c10 = const 10;\n"
        "    %c20 = const 20;\n"
        "    %sum = add %c10, %c20;\n"
        "    %cond = const 1;\n"
        "    %dead = mul %x, 999;\n"
        "    @branch_if %cond, .live_br, .dead_br;\n"
        ".live_br;\n"
        "    %res = add %x, %sum;\n"
        "    @branch .exit;\n"
        ".dead_br;\n"
        "    @return %dead;\n"
        ".exit;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_e2e_mir");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool parsed = ny_parse_module(&p);
    TEST_ASSERT(parsed);

    // Run optimization pipeline (-O1)
    ny_opt_run_module_pipeline(&ctx.module, NY_OPT_O1);

    // Lower to Machine IR
    Ny_Machine_Module mmod;
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    bool ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &diags);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(diags.count, 0);
    TEST_ASSERT_EQ(mmod.function_count, 2);

    // Verify optimized function in Machine IR
    Ny_Machine_Function *m_opt = &mmod.functions[1];
    TEST_ASSERT_EQ(m_opt->block_count, 1);

    char *dump = ny_mir_dump_module(&mmod, &ctx.arena);
    TEST_ASSERT(dump != nullptr);
    TEST_ASSERT(strstr(dump, "machine_function @add") != nullptr);
    TEST_ASSERT(strstr(dump, "machine_function @opt_demo") != nullptr);

    ny_parser_destroy(&p);
    ny_diagnostic_list_destroy(&diags);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
}
