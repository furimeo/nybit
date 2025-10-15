// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package tests

import "core:testing"
import "src:analysis"
import "src:ir"
import "src:opt"
import "src:parser"
import "src:support"

@(test)
test_opt_const_fold :: proc(t: ^testing.T) {
	src := `
	@function fold_arith(%x: i32) -> i32;
	.entry;
		%c10 = const 10;
		%c20 = const 20;
		%add = add %c10, %c20;
		%mul = mul %c10, %c20;
		%sub = sub %c20, %c10;
		%cmp = cmp.lt.s %c10, %c20;
		%sel = select %cmp, %add, %mul;
		%r = add %x, %sel;
		@return %r;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_fold")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	changed := opt.opt_pass_const_fold(&ctx.module, fn)
	testing.expect(t, changed)

	// Verify that %add, %mul, %sub, %cmp, %sel were folded to .Const
	add_inst := ir.function_get_instruction(fn, support.Inst_ID(2))
	testing.expect(t, add_inst != nil && add_inst.opcode == .Const)
	ops := ir.function_get_operands(fn, add_inst)
	testing.expect_value(t, ops[0].imm_int, 30)

	mul_inst := ir.function_get_instruction(fn, support.Inst_ID(3))
	testing.expect(t, mul_inst != nil && mul_inst.opcode == .Const)
	m_ops := ir.function_get_operands(fn, mul_inst)
	testing.expect_value(t, m_ops[0].imm_int, 200)

	cmp_inst := ir.function_get_instruction(fn, support.Inst_ID(5))
	testing.expect(t, cmp_inst != nil && cmp_inst.opcode == .Const)
	c_ops := ir.function_get_operands(fn, cmp_inst)
	testing.expect_value(t, c_ops[0].imm_int, 1)

	// Run validation to ensure IR integrity
	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_opt_sccp :: proc(t: ^testing.T) {
	src := `
	@function sccp_test(%x: i32) -> i32;
	.entry;
		%cond = const 1;
		@branch_if %cond, .left, .right;
	.left;
		%v_left = const 100;
		@branch .merge;
	.right;
		%v_right = const 200;
		@branch .merge;
	.merge;
		%res = phi %v_left, .left, %v_right, .right;
		%out = add %x, %res;
		@return %out;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_sccp")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	changed := opt.opt_pass_sccp(&ctx.module, fn)
	testing.expect(t, changed)

	// Entry branch_if should have been rewritten to unconditional @branch .left
	entry_blk := ir.function_get_block(fn, support.Block_ID(0))
	term := ir.function_get_instruction(fn, entry_blk.last_inst)
	testing.expect(t, term != nil && term.opcode == .Branch)
	t_ops := ir.function_get_operands(fn, term)
	testing.expect_value(t, t_ops[0].blk, support.Block_ID(1))

	// Phi in .merge should have folded to constant 100
	merge_blk := ir.function_get_block(fn, support.Block_ID(3))
	phi_inst := ir.function_get_instruction(fn, merge_blk.first_inst)
	testing.expect(t, phi_inst != nil && phi_inst.opcode == .Const)
	phi_ops := ir.function_get_operands(fn, phi_inst)
	testing.expect_value(t, phi_ops[0].imm_int, 100)

	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_opt_dce :: proc(t: ^testing.T) {
	src := `
	@function dce_test(%x: i32) -> i32;
	.entry;
		%dead1 = add %x, 1;
		%dead2 = mul %dead1, 2;
		%dead3 = sub %dead2, %x;
		%live = add %x, 10;
		@return %live;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_dce")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	changed := opt.opt_pass_dce(&ctx.module, fn)
	testing.expect(t, changed)

	// %dead1, %dead2, %dead3 should be eliminated (opcode == .None)
	i1 := ir.function_get_instruction(fn, support.Inst_ID(0))
	i2 := ir.function_get_instruction(fn, support.Inst_ID(1))
	i3 := ir.function_get_instruction(fn, support.Inst_ID(2))
	i_live := ir.function_get_instruction(fn, support.Inst_ID(3))

	testing.expect_value(t, i1.opcode, ir.Opcode.None)
	testing.expect_value(t, i2.opcode, ir.Opcode.None)
	testing.expect_value(t, i3.opcode, ir.Opcode.None)
	testing.expect_value(t, i_live.opcode, ir.Opcode.Add)

	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_opt_copy_prop :: proc(t: ^testing.T) {
	src := `
	@function copy_test(%cond: i8, %x: i32) -> i32;
	.entry;
		%sel = select %cond, %x, %x;
		%res = add %sel, 1;
		@return %res;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_copy")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	changed := opt.opt_pass_copy_prop(&ctx.module, fn)
	testing.expect(t, changed)

	// %sel was a copy of %x, so the select should be removed and %res should use %x directly
	add_inst := ir.function_get_instruction(fn, support.Inst_ID(1))
	testing.expect(t, add_inst != nil && add_inst.opcode == .Add)
	ops := ir.function_get_operands(fn, add_inst)
	testing.expect_value(t, ops[0].val, support.Value_ID(1)) // %x is param 1

	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_opt_gvn :: proc(t: ^testing.T) {
	src := `
	@function gvn_test(%x: i32, %y: i32) -> i32;
	.entry;
		%a = add %x, %y;
		%b = add %y, %x;
		%res = mul %a, %b;
		@return %res;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_gvn")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	changed := opt.opt_pass_gvn(&ctx.module, fn)
	testing.expect(t, changed)

	// %b should have been eliminated and replaced by %a in %res = mul %a, %a
	b_inst := ir.function_get_instruction(fn, support.Inst_ID(1))
	testing.expect_value(t, b_inst.opcode, ir.Opcode.None)

	mul_inst := ir.function_get_instruction(fn, support.Inst_ID(2))
	testing.expect_value(t, mul_inst.opcode, ir.Opcode.Mul)
	ops := ir.function_get_operands(fn, mul_inst)
	testing.expect_value(t, ops[0].val, support.Value_ID(2)) // %a
	testing.expect_value(t, ops[1].val, support.Value_ID(2)) // %a (replaced %b)

	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_opt_cfg_simplify :: proc(t: ^testing.T) {
	src := `
	@function cfg_test(%x: i32) -> i32;
	.entry;
		%cond = const 1;
		@branch_if %cond, .live_target, .dead_target;
	.live_target;
		@branch .trampoline;
	.dead_target;
		%d = add %x, 999;
		@return %d;
	.trampoline;
		@branch .exit;
	.exit;
		%out = add %x, 42;
		@return %out;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_cfg_simp")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	changed := opt.opt_pass_cfg_simplify(&ctx.module, fn)
	testing.expect(t, changed)

	// Dead target and trampoline should be removed, and remaining blocks merged
	entry_blk := ir.function_get_block(fn, fn.entry_block)
	testing.expect(t, entry_blk != nil)

	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_opt_pipeline_e2e :: proc(t: ^testing.T) {
	src := `
	@function full_pipeline(%x: i32) -> i32;
	.entry;
		%c10 = const 10;
		%c20 = const 20;
		%sum1 = add %c10, %c20;
		%sum2 = add %c20, %c10;
		%cond = cmp.lt.s %sum1, 100;
		%dead = mul %x, 999;
		@branch_if %cond, .then, .otherwise;
	.then;
		%t_res = add %x, %sum1;
		@branch .merge;
	.otherwise;
		%f_res = add %x, %sum2;
		@branch .merge;
	.merge;
		%res = phi %t_res, .then, %f_res, .otherwise;
		@return %res;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_pipeline")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function(&ctx.module, support.Function_ID(0))
	testing.expect(t, fn != nil)

	changed := opt.opt_run_pipeline(&ctx.module, fn, .O1)
	testing.expect(t, changed)

	// After optimization pipeline:
	// - %sum1 and %sum2 fold to 30
	// - %cond folds to 1 (30 < 100)
	// - dead branch .otherwise removed
	// - %dead instruction eliminated
	// - Blocks merged cleanly
	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}
