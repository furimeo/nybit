// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package tests

import "core:testing"
import "src:analysis"
import "src:ir"
import "src:parser"
import "src:support"

@(test)
test_analysis_cfg_linear :: proc(t: ^testing.T) {
	src := `
	@function linear(%a: i32) -> i32;
	.entry;
		%v1 = add %a, %a;
		@branch .b1;
	.b1;
		%v2 = add %v1, %a;
		@return %v2;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_linear")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	cfg: analysis.CFG_Info
	analysis.cfg_info_init(&cfg, fn)
	defer analysis.cfg_info_destroy(&cfg)

	testing.expect_value(t, len(cfg.rpo), 2)
	testing.expect_value(t, cfg.rpo[0], support.Block_ID(0))
	testing.expect_value(t, cfg.rpo[1], support.Block_ID(1))

	testing.expect(t, analysis.cfg_is_reachable(&cfg, support.Block_ID(0)))
	testing.expect(t, analysis.cfg_is_reachable(&cfg, support.Block_ID(1)))
	testing.expect_value(t, len(cfg.back_edges), 0)
	testing.expect(t, !analysis.cfg_is_loop_header(&cfg, support.Block_ID(0)))
	testing.expect(t, !analysis.cfg_is_loop_header(&cfg, support.Block_ID(1)))
}

@(test)
test_analysis_cfg_diamond_and_unreachable :: proc(t: ^testing.T) {
	src := `
	@function diamond(%cond: i8) -> i32;
	.entry;
		%zero = const 0;
		@branch_if %cond, .left, .right;
	.left;
		@branch .merge;
	.right;
		@branch .merge;
	.merge;
		@return %zero;
	.dead;
		@return %zero;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_diamond")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	cfg: analysis.CFG_Info
	analysis.cfg_info_init(&cfg, fn)
	defer analysis.cfg_info_destroy(&cfg)

	testing.expect_value(t, len(cfg.rpo), 4)
	testing.expect(t, analysis.cfg_is_reachable(&cfg, support.Block_ID(0)))
	testing.expect(t, analysis.cfg_is_reachable(&cfg, support.Block_ID(1)))
	testing.expect(t, analysis.cfg_is_reachable(&cfg, support.Block_ID(2)))
	testing.expect(t, analysis.cfg_is_reachable(&cfg, support.Block_ID(3)))
	testing.expect(t, !analysis.cfg_is_reachable(&cfg, support.Block_ID(4)))

	testing.expect_value(t, analysis.cfg_rpo_index(&cfg, support.Block_ID(4)), -1)
	testing.expect(t, analysis.cfg_rpo_index(&cfg, support.Block_ID(0)) < analysis.cfg_rpo_index(&cfg, support.Block_ID(1)))
	testing.expect(t, analysis.cfg_rpo_index(&cfg, support.Block_ID(0)) < analysis.cfg_rpo_index(&cfg, support.Block_ID(2)))
	testing.expect(t, analysis.cfg_rpo_index(&cfg, support.Block_ID(1)) < analysis.cfg_rpo_index(&cfg, support.Block_ID(3)))
	testing.expect(t, analysis.cfg_rpo_index(&cfg, support.Block_ID(2)) < analysis.cfg_rpo_index(&cfg, support.Block_ID(3)))
}

@(test)
test_analysis_cfg_loop :: proc(t: ^testing.T) {
	src := `
	@function loop_test(%n: i32) -> i32;
	.entry;
		%zero = const 0;
		%ten = const 10;
		@branch .head;
	.head;
		%cond = cmp.lt.s %n, %ten;
		@branch_if %cond, .body, .exit;
	.body;
		@branch .head;
	.exit;
		@return %zero;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_loop")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	cfg: analysis.CFG_Info
	analysis.cfg_info_init(&cfg, fn)
	defer analysis.cfg_info_destroy(&cfg)

	testing.expect_value(t, len(cfg.back_edges), 1)
	testing.expect(t, analysis.cfg_is_back_edge(&cfg, support.Block_ID(2), support.Block_ID(1)))
	testing.expect(t, analysis.cfg_is_loop_header(&cfg, support.Block_ID(1)))
	testing.expect(t, !analysis.cfg_is_loop_header(&cfg, support.Block_ID(0)))
	testing.expect(t, !analysis.cfg_is_loop_header(&cfg, support.Block_ID(2)))
	testing.expect(t, !analysis.cfg_is_loop_header(&cfg, support.Block_ID(3)))
}

@(test)
test_analysis_dominance_and_frontiers :: proc(t: ^testing.T) {
	src := `
	@function dom_test(%cond: i8) -> i32;
	.entry;
		%zero = const 0;
		@branch_if %cond, .left, .right;
	.left;
		@branch .merge;
	.right;
		@branch .merge;
	.merge;
		@return %zero;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_dom")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	dt: analysis.Dominator_Tree
	analysis.dominator_tree_init(&dt, fn)
	defer analysis.dominator_tree_destroy(&dt)

	b_entry := support.Block_ID(0)
	b_left  := support.Block_ID(1)
	b_right := support.Block_ID(2)
	b_merge := support.Block_ID(3)

	testing.expect(t, analysis.dominator_tree_dominates(&dt, b_entry, b_left))
	testing.expect(t, analysis.dominator_tree_dominates(&dt, b_entry, b_right))
	testing.expect(t, analysis.dominator_tree_dominates(&dt, b_entry, b_merge))
	testing.expect(t, !analysis.dominator_tree_dominates(&dt, b_left, b_merge))
	testing.expect(t, !analysis.dominator_tree_dominates(&dt, b_right, b_merge))
	testing.expect(t, analysis.dominator_tree_strictly_dominates(&dt, b_entry, b_merge))
	testing.expect(t, !analysis.dominator_tree_strictly_dominates(&dt, b_merge, b_merge))

	children_entry := analysis.dominator_tree_get_children(&dt, b_entry)
	testing.expect_value(t, len(children_entry), 3)

	df_left := analysis.dominator_tree_get_frontier(&dt, b_left)
	testing.expect_value(t, len(df_left), 1)
	testing.expect_value(t, df_left[0], b_merge)

	df_right := analysis.dominator_tree_get_frontier(&dt, b_right)
	testing.expect_value(t, len(df_right), 1)
	testing.expect_value(t, df_right[0], b_merge)

	df_entry := analysis.dominator_tree_get_frontier(&dt, b_entry)
	testing.expect_value(t, len(df_entry), 0)

	df_merge := analysis.dominator_tree_get_frontier(&dt, b_merge)
	testing.expect_value(t, len(df_merge), 0)
}

@(test)
test_analysis_use_def :: proc(t: ^testing.T) {
	src := `
	@function ud_test(%x: i32) -> i32;
	.entry;
		%c2 = const 2;
		%c1 = const 1;
		%v1 = add %x, %x;
		%v2 = mul %v1, %c2;
		%unused = sub %x, %c1;
		@return %v2;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_ud")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	ud: analysis.Use_Def
	analysis.use_def_init(&ud, fn)
	defer analysis.use_def_destroy(&ud)

	val_x      := support.Value_ID(0)
	val_c2     := support.Value_ID(1)
	val_c1     := support.Value_ID(2)
	val_v1     := support.Value_ID(3)
	val_v2     := support.Value_ID(4)
	val_unused := support.Value_ID(5)

	testing.expect_value(t, analysis.use_def_get_def(&ud, val_x), support.INVALID_INST)
	testing.expect(t, analysis.use_def_get_def(&ud, val_v1) != support.INVALID_INST)

	// %x is used twice in add, once in sub -> 3 uses
	testing.expect_value(t, analysis.use_def_use_count(&ud, val_x), 3)
	testing.expect(t, !analysis.use_def_has_single_use(&ud, val_x))

	// %v1 is used once in mul -> 1 use
	testing.expect_value(t, analysis.use_def_use_count(&ud, val_v1), 1)
	testing.expect(t, analysis.use_def_has_single_use(&ud, val_v1))

	// %v2 is used once in return -> 1 use
	testing.expect_value(t, analysis.use_def_use_count(&ud, val_v2), 1)

	// %unused is never used -> 0 uses
	testing.expect_value(t, analysis.use_def_use_count(&ud, val_unused), 0)
	testing.expect(t, !analysis.use_def_has_uses(&ud, val_unused))

	uses_v1 := analysis.use_def_get_uses(&ud, val_v1)
	testing.expect_value(t, len(uses_v1), 1)
	testing.expect_value(t, uses_v1[0].op_idx, 0)
}

@(test)
test_analysis_liveness :: proc(t: ^testing.T) {
	src := `
	@function live_test(%cond: i8, %val: i32) -> i32;
	.entry;
		%c10 = const 10;
		%c20 = const 20;
		@branch_if %cond, .left, .right;
	.left;
		%left_res = add %val, %c10;
		@branch .merge;
	.right;
		%right_res = mul %val, %c20;
		@branch .merge;
	.merge;
		%final = phi %left_res, .left, %right_res, .right;
		@return %final;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_liv")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	liv: analysis.Liveness_Info
	analysis.liveness_init(&liv, fn)
	defer analysis.liveness_destroy(&liv)

	b_entry := support.Block_ID(0)
	b_left  := support.Block_ID(1)
	b_right := support.Block_ID(2)
	b_merge := support.Block_ID(3)

	val_val       := ir.function_get_value(fn, support.Value_ID(1)).id
	val_left_res  := ir.function_get_value(fn, support.Value_ID(4)).id
	val_right_res := ir.function_get_value(fn, support.Value_ID(5)).id

	// %val is needed in both left and right, so it must be live_out of entry, live_in of left and right
	testing.expect(t, analysis.liveness_is_live_out(&liv, b_entry, val_val))
	testing.expect(t, analysis.liveness_is_live_in(&liv, b_left, val_val))
	testing.expect(t, analysis.liveness_is_live_in(&liv, b_right, val_val))

	// %left_res is defined in left and used by phi in merge along left->merge edge
	testing.expect(t, analysis.liveness_is_live_out(&liv, b_left, val_left_res))
	testing.expect(t, !analysis.liveness_is_live_out(&liv, b_right, val_left_res))
	testing.expect(t, !analysis.liveness_is_live_in(&liv, b_merge, val_left_res))

	// %right_res is defined in right and used by phi in merge along right->merge edge
	testing.expect(t, analysis.liveness_is_live_out(&liv, b_right, val_right_res))
	testing.expect(t, !analysis.liveness_is_live_out(&liv, b_left, val_right_res))
}

@(test)
test_analysis_manager_caching_and_invalidation :: proc(t: ^testing.T) {
	src := `
	@function mgr_test(%a: i32) -> i32;
	.entry;
		%c1 = const 1;
		%b = add %a, %c1;
		@return %b;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_mgr")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	am: analysis.Analysis_Manager
	analysis.analysis_manager_init(&am, fn)
	defer analysis.analysis_manager_destroy(&am)

	testing.expect(t, !am.valid_cfg)
	testing.expect(t, !am.valid_dom)
	testing.expect(t, !am.valid_use_def)
	testing.expect(t, !am.valid_liveness)

	// Fetching CFG builds and caches it
	cfg := analysis.analysis_get_cfg(&am)
	testing.expect(t, cfg != nil)
	testing.expect(t, am.valid_cfg)
	testing.expect(t, !am.valid_dom)

	// Fetching others builds and caches them
	dom := analysis.analysis_get_dom(&am)
	testing.expect(t, dom != nil)
	testing.expect(t, am.valid_dom)

	ud := analysis.analysis_get_use_def(&am)
	testing.expect(t, ud != nil)
	testing.expect(t, am.valid_use_def)

	liv := analysis.analysis_get_liveness(&am)
	testing.expect(t, liv != nil)
	testing.expect(t, am.valid_liveness)

	// Invalidate use-def: should invalidate use-def and liveness, keep CFG and Dom
	analysis.analysis_invalidate_use_def(&am)
	testing.expect(t, am.valid_cfg)
	testing.expect(t, am.valid_dom)
	testing.expect(t, !am.valid_use_def)
	testing.expect(t, !am.valid_liveness)

	// Invalidate CFG: should invalidate CFG, Dom, and Liveness
	analysis.analysis_invalidate_cfg(&am)
	testing.expect(t, !am.valid_cfg)
	testing.expect(t, !am.valid_dom)
	testing.expect(t, !am.valid_liveness)

	// Fetching again rebuilds cleanly
	cfg2 := analysis.analysis_get_cfg(&am)
	testing.expect(t, cfg2 != nil)
	testing.expect(t, am.valid_cfg)
}
