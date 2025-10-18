// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nybit/ir.h>
#include <nybit/support.h>

void test_module_lifecycle(void) {
    Ny_Context ctx;
    ny_context_init(&ctx, "m");

    Ny_Module *mod = &ctx.module;
    TEST_ASSERT(mod != nullptr);
    TEST_ASSERT(ny_str_eq_cstr(mod->name, "m"));

    ny_context_destroy(&ctx);
}

void test_build_add_function(void) {
    Ny_Context ctx;
    ny_context_init(&ctx, "test");

    Ny_Module *mod = &ctx.module;
    Ny_Builder b;
    ny_builder_init(&b, mod);

    Ny_Function_ID fn_id = ny_builder_add_function(&b, "add", NY_TYPE_I32);
    TEST_ASSERT_EQ(fn_id, 0);

    Ny_Value_ID a = ny_builder_add_param(&b, "a", NY_TYPE_I32);
    Ny_Value_ID b_val = ny_builder_add_param(&b, "b", NY_TYPE_I32);

    Ny_Block_ID entry = ny_builder_add_block(&b, "entry");
    ny_builder_set_insert_point(&b, fn_id, entry);

    Ny_Value_ID r = ny_builder_add(&b, a, b_val, NY_TYPE_I32, "r");
    Ny_Inst_ID ret = ny_builder_ret(&b, r);

    Ny_Function *fn = ny_module_get_function(mod, fn_id);
    TEST_ASSERT(fn != nullptr);
    TEST_ASSERT_EQ(fn->block_count, 1);
    TEST_ASSERT_EQ(fn->inst_count, 2);
    TEST_ASSERT_EQ(fn->val_count, 3);

    Ny_Block *blk = ny_function_get_block(fn, entry);
    TEST_ASSERT(blk != nullptr);
    TEST_ASSERT_EQ(blk->inst_count, 2);
    TEST_ASSERT_EQ(blk->first_inst, 0);
    TEST_ASSERT_EQ(blk->last_inst, ret);

    Ny_Instruction *inst0 = ny_function_get_instruction(fn, blk->first_inst);
    TEST_ASSERT(inst0 != nullptr);
    TEST_ASSERT_EQ(inst0->opcode, NY_OPCODE_ADD);
    TEST_ASSERT_EQ(inst0->result, r);

    Ny_Value *val_r = ny_function_get_value(fn, r);
    TEST_ASSERT(val_r != nullptr);
    TEST_ASSERT_EQ(val_r->def, blk->first_inst);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(mod, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_context_destroy(&ctx);
}

void test_cfg_edges(void) {
    Ny_Context ctx;
    ny_context_init(&ctx, "test_cfg");

    Ny_Module *mod = &ctx.module;
    Ny_Builder b;
    ny_builder_init(&b, mod);

    Ny_Function_ID fn_id = ny_builder_add_function(&b, "abs", NY_TYPE_I32);
    Ny_Value_ID x = ny_builder_add_param(&b, "x", NY_TYPE_I32);

    Ny_Block_ID b_entry = ny_builder_add_block(&b, "entry");
    Ny_Block_ID b_neg   = ny_builder_add_block(&b, "negative");
    Ny_Block_ID b_pos   = ny_builder_add_block(&b, "positive");

    ny_builder_set_insert_point(&b, fn_id, b_entry);
    Ny_Value_ID zero = ny_builder_const_i64(&b, 0, NY_TYPE_I32, "zero");
    Ny_Value_ID cond = ny_builder_cmp(&b, NY_OPCODE_CMP_LT_S, x, zero, "cond");
    ny_builder_branch_if(&b, cond, b_neg, b_pos);

    ny_builder_set_insert_point(&b, fn_id, b_neg);
    Ny_Value_ID neg_x = ny_builder_neg(&b, x, NY_TYPE_I32, "r1");
    ny_builder_ret(&b, neg_x);

    ny_builder_set_insert_point(&b, fn_id, b_pos);
    ny_builder_ret(&b, x);

    Ny_Function *fn = ny_module_get_function(mod, fn_id);
    Ny_Block *entry_blk = ny_function_get_block(fn, b_entry);
    Ny_Block *neg_blk   = ny_function_get_block(fn, b_neg);
    Ny_Block *pos_blk   = ny_function_get_block(fn, b_pos);

    TEST_ASSERT_EQ(entry_blk->succ_count, 2);
    TEST_ASSERT_EQ(neg_blk->pred_count, 1);
    TEST_ASSERT_EQ(neg_blk->preds[0], b_entry);
    TEST_ASSERT_EQ(pos_blk->pred_count, 1);
    TEST_ASSERT_EQ(pos_blk->preds[0], b_entry);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(mod, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_context_destroy(&ctx);
}
