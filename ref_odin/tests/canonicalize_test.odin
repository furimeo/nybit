// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package tests

import "core:testing"
import "src:ir"
import "src:opt"
import "src:parser"
import "src:support"

@(test)
test_dominance_same_block_violation :: proc(t: ^testing.T) {
	ctx: ir.Context
	ir.context_init(&ctx, "test_dom1")
	defer ir.context_destroy(&ctx)

	mod := &ctx.module
	b: ir.Builder
	ir.builder_init(&b, mod)

	fn_id := ir.builder_add_function(&b, "bad_dom", ir.TYPE_I32)
	entry := ir.builder_add_block(&b, "entry")
	ir.builder_set_insert_point(&b, fn_id, entry)

	fn := ir.module_get_function(mod, fn_id)
	val_b := ir.function_create_value(fn, ir.TYPE_I32, .Instruction, support.INVALID_INST, 0, "b")

	// Use val_b before its defining instruction is appended
	val_a := ir.builder_add(&b, val_b, val_b, ir.TYPE_I32, "a")

	ops := [1]ir.Operand{ir.operand_int(42)}
	ir.function_append_instruction(fn, entry, .Const, val_b, ops[:])
	ir.builder_ret(&b, val_a)

	diags := ir.validate_module(mod, context.temp_allocator)
	testing.expect(t, len(diags) > 0)
}

@(test)
test_dominance_cross_block_violation :: proc(t: ^testing.T) {
	src := `
	@function cross_dom(%cond: i8) -> i32;
	.entry;
		@branch_if %cond, .left, .right;

	.left;
		%left_val = const 10;
		@branch .merge;

	.right;
		%right_val = const 20;
		@branch .merge;

	.merge;
		%r = add %left_val, %left_val;
		@return %r;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "test_cross_dom")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect(t, len(diags) > 0)
}

@(test)
test_phi_invariants :: proc(t: ^testing.T) {
	ctx: ir.Context
	ir.context_init(&ctx, "test_phi")
	defer ir.context_destroy(&ctx)

	mod := &ctx.module
	b: ir.Builder
	ir.builder_init(&b, mod)

	fn_id := ir.builder_add_function(&b, "test_phi", ir.TYPE_I32)
	entry := ir.builder_add_block(&b, "entry")
	ir.builder_set_insert_point(&b, fn_id, entry)

	v1 := ir.builder_const_i64(&b, 10, ir.TYPE_I32, "v1")

	// Phi after non-phi instruction is invalid
	fn := ir.module_get_function(mod, fn_id)
	phi_val := ir.function_create_value(fn, ir.TYPE_I32, .Instruction, support.INVALID_INST, 0, "phi_res")
	phi_ops := [2]ir.Operand{ir.operand_value(v1), ir.operand_block(entry)}
	ir.function_append_instruction(fn, entry, .Phi, phi_val, phi_ops[:])
	ir.builder_ret(&b, phi_val)

	diags := ir.validate_module(mod, context.temp_allocator)
	testing.expect(t, len(diags) > 0)
}

@(test)
test_identity_and_comparison_canonicalization :: proc(t: ^testing.T) {
	src := `
	@function math_canon(%x: i32, %y: i32) -> i32;
	.entry;
		%c = cmp.gt.s %x, %y;
		%zero = const 0;
		%add_zero = add %x, %zero;
		%one = const 1;
		%mul_one = mul %x, %one;
		%r = add %add_zero, %mul_one;
		@return %r;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "math_canon")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function_by_name(&ctx.module, "math_canon")
	testing.expect(t, fn != nil)

	opt.canonicalize_module(&ctx.module)

	// Verify cmp.gt.s was inverted to cmp.lt.s
	c_val := ir.function_get_value(fn, support.Value_ID(2))
	testing.expect(t, c_val != nil)
	c_inst := ir.function_get_instruction(fn, c_val.def)
	testing.expect_value(t, c_inst.opcode, ir.Opcode.Cmp_Lt_S)

	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_cfg_canonicalization_cleanup :: proc(t: ^testing.T) {
	src := `
	@function cfg_canon(%cond: i8) -> i32;
	.entry;
		@branch_if %cond, .target, .target;

	.target;
		%val = const 42;
		@return %val;
		%dead1 = const 99;
		%dead2 = add %dead1, %dead1;

	.unreachable_block;
		%dead3 = const 100;
		@return %dead3;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "cfg_canon")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)
	ok := parser.parse_module(&p)
	testing.expect(t, ok)

	fn := ir.module_get_function_by_name(&ctx.module, "cfg_canon")
	testing.expect(t, fn != nil)

	opt.canonicalize_module(&ctx.module)

	// .target was merged into .entry, resulting in 2 instructions (const 42, return %val)
	entry_blk := ir.function_get_block(fn, fn.entry_block)
	testing.expect_value(t, entry_blk.inst_count, 2)

	// Unreachable block was purged
	for blk in fn.blocks {
		if blk.name == "unreachable_block" {
			testing.expect_value(t, blk.inst_count, 0)
		}
	}

	diags := ir.validate_module(&ctx.module, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_canonicalization_determinism :: proc(t: ^testing.T) {
	src := `@function determinism(%a: i32, %b: i32) -> i32;
.entry;
    %zero = const 0;
    %x = add %zero, %a;
    %cmp = cmp.gt.s %a, %b;
    %y = add %x, %zero;
    @return %y;
;;
`
	ctx1: ir.Context
	ir.context_init(&ctx1, "det1")
	defer ir.context_destroy(&ctx1)

	p1: parser.Parser
	parser.parser_init(&p1, &ctx1.module, src)
	parser.parse_module(&p1)
	opt.canonicalize_module(&ctx1.module)
	dump1 := ir.dump_module(&ctx1.module, context.temp_allocator)

	ctx2: ir.Context
	ir.context_init(&ctx2, "det2")
	defer ir.context_destroy(&ctx2)

	p2: parser.Parser
	parser.parser_init(&p2, &ctx2.module, src)
	parser.parse_module(&p2)
	opt.canonicalize_module(&ctx2.module)
	dump2 := ir.dump_module(&ctx2.module, context.temp_allocator)

	testing.expect_value(t, dump1, dump2)
}
