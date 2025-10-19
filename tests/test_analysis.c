// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/parser.h>
#include <nybit/support.h>

void test_analysis_cfg_linear(void) {
    const char *src =
        "@function linear(%a: i32) -> i32;\n"
        ".entry;\n"
        "    %v1 = add %a, %a;\n"
        "    @branch .b1;\n"
        ".b1;\n"
        "    %v2 = add %v1, %a;\n"
        "    @return %v2;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_linear");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    Ny_CFG_Info cfg;
    ny_cfg_info_init(&cfg, fn);

    TEST_ASSERT_EQ(cfg.rpo_count, 2);
    TEST_ASSERT_EQ(cfg.rpo[0], 0);
    TEST_ASSERT_EQ(cfg.rpo[1], 1);

    TEST_ASSERT(ny_cfg_is_reachable(&cfg, 0));
    TEST_ASSERT(ny_cfg_is_reachable(&cfg, 1));
    TEST_ASSERT_EQ(cfg.back_edges_count, 0);
    TEST_ASSERT(!ny_cfg_is_loop_header(&cfg, 0));
    TEST_ASSERT(!ny_cfg_is_loop_header(&cfg, 1));

    ny_cfg_info_destroy(&cfg);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_analysis_cfg_diamond_and_unreachable(void) {
    const char *src =
        "@function diamond(%cond: i8) -> i32;\n"
        ".entry;\n"
        "    %zero = const 0;\n"
        "    @branch_if %cond, .left, .right;\n"
        ".left;\n"
        "    @branch .merge;\n"
        ".right;\n"
        "    @branch .merge;\n"
        ".merge;\n"
        "    @return %zero;\n"
        ".dead;\n"
        "    @return %zero;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_diamond");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    Ny_CFG_Info cfg;
    ny_cfg_info_init(&cfg, fn);

    TEST_ASSERT_EQ(cfg.rpo_count, 4);
    TEST_ASSERT(ny_cfg_is_reachable(&cfg, 0));
    TEST_ASSERT(ny_cfg_is_reachable(&cfg, 1));
    TEST_ASSERT(ny_cfg_is_reachable(&cfg, 2));
    TEST_ASSERT(ny_cfg_is_reachable(&cfg, 3));
    TEST_ASSERT(!ny_cfg_is_reachable(&cfg, 4));

    TEST_ASSERT_EQ(ny_cfg_rpo_index(&cfg, 4), -1);
    TEST_ASSERT(ny_cfg_rpo_index(&cfg, 0) < ny_cfg_rpo_index(&cfg, 1));
    TEST_ASSERT(ny_cfg_rpo_index(&cfg, 0) < ny_cfg_rpo_index(&cfg, 2));
    TEST_ASSERT(ny_cfg_rpo_index(&cfg, 1) < ny_cfg_rpo_index(&cfg, 3));
    TEST_ASSERT(ny_cfg_rpo_index(&cfg, 2) < ny_cfg_rpo_index(&cfg, 3));

    ny_cfg_info_destroy(&cfg);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_analysis_cfg_loop(void) {
    const char *src =
        "@function loop_test(%n: i32) -> i32;\n"
        ".entry;\n"
        "    %zero = const 0;\n"
        "    %ten = const 10;\n"
        "    @branch .head;\n"
        ".head;\n"
        "    %cond = cmp.lt.s %n, %ten;\n"
        "    @branch_if %cond, .body, .exit;\n"
        ".body;\n"
        "    @branch .head;\n"
        ".exit;\n"
        "    @return %zero;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_loop");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    Ny_CFG_Info cfg;
    ny_cfg_info_init(&cfg, fn);

    TEST_ASSERT_EQ(cfg.back_edges_count, 1);
    TEST_ASSERT(ny_cfg_is_back_edge(&cfg, 2, 1));
    TEST_ASSERT(ny_cfg_is_loop_header(&cfg, 1));
    TEST_ASSERT(!ny_cfg_is_loop_header(&cfg, 0));
    TEST_ASSERT(!ny_cfg_is_loop_header(&cfg, 2));
    TEST_ASSERT(!ny_cfg_is_loop_header(&cfg, 3));

    ny_cfg_info_destroy(&cfg);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_analysis_dominance_and_frontiers(void) {
    const char *src =
        "@function dom_test(%cond: i8) -> i32;\n"
        ".entry;\n"
        "    %zero = const 0;\n"
        "    @branch_if %cond, .left, .right;\n"
        ".left;\n"
        "    @branch .merge;\n"
        ".right;\n"
        "    @branch .merge;\n"
        ".merge;\n"
        "    @return %zero;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_dom");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    Ny_Dominator_Tree dt;
    ny_dominator_tree_init(&dt, fn);

    Ny_Block_ID b_entry = 0;
    Ny_Block_ID b_left  = 1;
    Ny_Block_ID b_right = 2;
    Ny_Block_ID b_merge = 3;

    TEST_ASSERT(ny_dominator_tree_dominates(&dt, b_entry, b_left));
    TEST_ASSERT(ny_dominator_tree_dominates(&dt, b_entry, b_right));
    TEST_ASSERT(ny_dominator_tree_dominates(&dt, b_entry, b_merge));
    TEST_ASSERT(!ny_dominator_tree_dominates(&dt, b_left, b_merge));
    TEST_ASSERT(!ny_dominator_tree_dominates(&dt, b_right, b_merge));
    TEST_ASSERT(ny_dominator_tree_strictly_dominates(&dt, b_entry, b_merge));
    TEST_ASSERT(!ny_dominator_tree_strictly_dominates(&dt, b_merge, b_merge));

    size_t child_cnt = 0;
    const Ny_Block_ID *children_entry = ny_dominator_tree_get_children(&dt, b_entry, &child_cnt);
    (void)children_entry;
    TEST_ASSERT_EQ(child_cnt, 3);

    size_t df_left_cnt = 0;
    const Ny_Block_ID *df_left = ny_dominator_tree_get_frontier(&dt, b_left, &df_left_cnt);
    TEST_ASSERT_EQ(df_left_cnt, 1);
    TEST_ASSERT_EQ(df_left[0], b_merge);

    size_t df_right_cnt = 0;
    const Ny_Block_ID *df_right = ny_dominator_tree_get_frontier(&dt, b_right, &df_right_cnt);
    TEST_ASSERT_EQ(df_right_cnt, 1);
    TEST_ASSERT_EQ(df_right[0], b_merge);

    size_t df_entry_cnt = 0;
    ny_dominator_tree_get_frontier(&dt, b_entry, &df_entry_cnt);
    TEST_ASSERT_EQ(df_entry_cnt, 0);

    size_t df_merge_cnt = 0;
    ny_dominator_tree_get_frontier(&dt, b_merge, &df_merge_cnt);
    TEST_ASSERT_EQ(df_merge_cnt, 0);

    ny_dominator_tree_destroy(&dt);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_analysis_use_def(void) {
    const char *src =
        "@function ud_test(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %c2 = const 2;\n"
        "    %c1 = const 1;\n"
        "    %v1 = add %x, %x;\n"
        "    %v2 = mul %v1, %c2;\n"
        "    %unused = sub %x, %c1;\n"
        "    @return %v2;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_ud");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    Ny_Use_Def ud;
    ny_use_def_init(&ud, fn);

    Ny_Value_ID val_x      = 0;
    Ny_Value_ID val_v1     = 3;
    Ny_Value_ID val_v2     = 4;
    Ny_Value_ID val_unused = 5;

    TEST_ASSERT_EQ(ny_use_def_get_def(&ud, val_x), NY_INVALID_INST);
    TEST_ASSERT(ny_use_def_get_def(&ud, val_v1) != NY_INVALID_INST);

    TEST_ASSERT_EQ(ny_use_def_use_count(&ud, val_x), 3);
    TEST_ASSERT(!ny_use_def_has_single_use(&ud, val_x));

    TEST_ASSERT_EQ(ny_use_def_use_count(&ud, val_v1), 1);
    TEST_ASSERT(ny_use_def_has_single_use(&ud, val_v1));

    TEST_ASSERT_EQ(ny_use_def_use_count(&ud, val_v2), 1);

    TEST_ASSERT_EQ(ny_use_def_use_count(&ud, val_unused), 0);
    TEST_ASSERT(!ny_use_def_has_uses(&ud, val_unused));

    size_t uses_v1_cnt = 0;
    const Ny_Use *uses_v1 = ny_use_def_get_uses(&ud, val_v1, &uses_v1_cnt);
    TEST_ASSERT_EQ(uses_v1_cnt, 1);
    TEST_ASSERT_EQ(uses_v1[0].op_idx, 0);

    ny_use_def_destroy(&ud);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_analysis_liveness(void) {
    const char *src =
        "@function live_test(%cond: i8, %val: i32) -> i32;\n"
        ".entry;\n"
        "    %c10 = const 10;\n"
        "    %c20 = const 20;\n"
        "    @branch_if %cond, .left, .right;\n"
        ".left;\n"
        "    %left_res = add %val, %c10;\n"
        "    @branch .merge;\n"
        ".right;\n"
        "    %right_res = mul %val, %c20;\n"
        "    @branch .merge;\n"
        ".merge;\n"
        "    %final = phi %left_res, .left, %right_res, .right;\n"
        "    @return %final;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_liv");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    Ny_Liveness_Info liv;
    ny_liveness_init(&liv, fn);

    Ny_Block_ID b_entry = 0;
    Ny_Block_ID b_left  = 1;
    Ny_Block_ID b_right = 2;
    Ny_Block_ID b_merge = 3;

    Ny_Value_ID val_val       = ny_function_get_value(fn, 1)->id;
    Ny_Value_ID val_left_res  = ny_function_get_value(fn, 4)->id;
    Ny_Value_ID val_right_res = ny_function_get_value(fn, 5)->id;

    TEST_ASSERT(ny_liveness_is_live_out(&liv, b_entry, val_val));
    TEST_ASSERT(ny_liveness_is_live_in(&liv, b_left, val_val));
    TEST_ASSERT(ny_liveness_is_live_in(&liv, b_right, val_val));

    TEST_ASSERT(ny_liveness_is_live_out(&liv, b_left, val_left_res));
    TEST_ASSERT(!ny_liveness_is_live_out(&liv, b_right, val_left_res));
    TEST_ASSERT(!ny_liveness_is_live_in(&liv, b_merge, val_left_res));

    TEST_ASSERT(ny_liveness_is_live_out(&liv, b_right, val_right_res));
    TEST_ASSERT(!ny_liveness_is_live_out(&liv, b_left, val_right_res));

    ny_liveness_destroy(&liv);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_analysis_manager_caching_and_invalidation(void) {
    const char *src =
        "@function mgr_test(%a: i32) -> i32;\n"
        ".entry;\n"
        "    %c1 = const 1;\n"
        "    %b = add %a, %c1;\n"
        "    @return %b;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_mgr");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);

    Ny_Function *fn = ny_module_get_function(&ctx.module, 0);
    TEST_ASSERT(fn != nullptr);

    Ny_Analysis_Manager am;
    ny_analysis_manager_init(&am, fn);

    TEST_ASSERT(!am.valid_cfg);
    TEST_ASSERT(!am.valid_dom);
    TEST_ASSERT(!am.valid_use_def);
    TEST_ASSERT(!am.valid_liveness);

    Ny_CFG_Info *cfg = ny_analysis_get_cfg(&am);
    TEST_ASSERT(cfg != nullptr);
    TEST_ASSERT(am.valid_cfg);
    TEST_ASSERT(!am.valid_dom);

    Ny_Dominator_Tree *dom = ny_analysis_get_dom(&am);
    TEST_ASSERT(dom != nullptr);
    TEST_ASSERT(am.valid_dom);

    Ny_Use_Def *ud = ny_analysis_get_use_def(&am);
    TEST_ASSERT(ud != nullptr);
    TEST_ASSERT(am.valid_use_def);

    Ny_Liveness_Info *liv = ny_analysis_get_liveness(&am);
    TEST_ASSERT(liv != nullptr);
    TEST_ASSERT(am.valid_liveness);

    ny_analysis_invalidate_use_def(&am);
    TEST_ASSERT(am.valid_cfg);
    TEST_ASSERT(am.valid_dom);
    TEST_ASSERT(!am.valid_use_def);
    TEST_ASSERT(!am.valid_liveness);

    ny_analysis_invalidate_cfg(&am);
    TEST_ASSERT(!am.valid_cfg);
    TEST_ASSERT(!am.valid_dom);
    TEST_ASSERT(!am.valid_liveness);

    Ny_CFG_Info *cfg2 = ny_analysis_get_cfg(&am);
    TEST_ASSERT(cfg2 != nullptr);
    TEST_ASSERT(am.valid_cfg);

    ny_analysis_manager_destroy(&am);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}
