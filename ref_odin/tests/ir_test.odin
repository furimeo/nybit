// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package tests

import "core:testing"
import "src:ir"
import "src:support"

@(test)
test_module_lifecycle :: proc(t: ^testing.T) {
	ctx: ir.Context
	ir.context_init(&ctx, "m")
	defer ir.context_destroy(&ctx)

	mod := &ctx.module
	testing.expect(t, mod != nil)
	testing.expect_value(t, mod.name, "m")
}

@(test)
test_build_add_function :: proc(t: ^testing.T) {
	ctx: ir.Context
	ir.context_init(&ctx, "test")
	defer ir.context_destroy(&ctx)

	mod := &ctx.module
	b: ir.Builder
	ir.builder_init(&b, mod)

	fn_id := ir.builder_add_function(&b, "add", ir.TYPE_I32)
	testing.expect_value(t, fn_id, support.Function_ID(0))

	a := ir.builder_add_param(&b, ir.TYPE_I32, "a")
	b_val := ir.builder_add_param(&b, ir.TYPE_I32, "b")

	entry := ir.builder_add_block(&b, "entry")
	ir.builder_set_insert_point(&b, fn_id, entry)

	r := ir.builder_add(&b, a, b_val, ir.TYPE_I32, "r")
	ret := ir.builder_ret(&b, r)

	fn := ir.module_get_function(mod, fn_id)
	testing.expect(t, fn != nil)
	testing.expect_value(t, len(fn.blocks), 1)
	testing.expect_value(t, len(fn.instructions), 2)
	testing.expect_value(t, len(fn.values), 3)

	blk := ir.function_get_block(fn, entry)
	testing.expect_value(t, blk.inst_count, 2)
	testing.expect_value(t, blk.first_inst, support.Inst_ID(0))
	testing.expect_value(t, blk.last_inst, ret)

	inst0 := ir.function_get_instruction(fn, blk.first_inst)
	testing.expect_value(t, inst0.opcode, ir.Opcode.Add)
	testing.expect_value(t, inst0.result, r)

	val_r := ir.function_get_value(fn, r)
	testing.expect_value(t, val_r.def, blk.first_inst)

	diags := ir.validate_module(mod, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_cfg_edges :: proc(t: ^testing.T) {
	ctx: ir.Context
	ir.context_init(&ctx, "test_cfg")
	defer ir.context_destroy(&ctx)

	mod := &ctx.module
	b: ir.Builder
	ir.builder_init(&b, mod)

	fn_id := ir.builder_add_function(&b, "abs", ir.TYPE_I32)
	x := ir.builder_add_param(&b, ir.TYPE_I32, "x")

	b_entry := ir.builder_add_block(&b, "entry")
	b_neg   := ir.builder_add_block(&b, "negative")
	b_pos   := ir.builder_add_block(&b, "positive")

	ir.builder_set_insert_point(&b, fn_id, b_entry)
	zero := ir.builder_const_i64(&b, 0, ir.TYPE_I32, "zero")
	cond := ir.builder_cmp_lt_s(&b, x, zero, "cond")
	ir.builder_branch_if(&b, cond, b_neg, b_pos)

	ir.builder_set_insert_point(&b, fn_id, b_neg)
	neg_x := ir.builder_neg(&b, x, ir.TYPE_I32, "r1")
	ir.builder_ret(&b, neg_x)

	ir.builder_set_insert_point(&b, fn_id, b_pos)
	ir.builder_ret(&b, x)

	fn := ir.module_get_function(mod, fn_id)
	entry_blk := ir.function_get_block(fn, b_entry)
	neg_blk   := ir.function_get_block(fn, b_neg)
	pos_blk   := ir.function_get_block(fn, b_pos)

	testing.expect_value(t, len(entry_blk.successors), 2)
	testing.expect_value(t, len(neg_blk.predecessors), 1)
	testing.expect_value(t, neg_blk.predecessors[0], b_entry)
	testing.expect_value(t, len(pos_blk.predecessors), 1)
	testing.expect_value(t, pos_blk.predecessors[0], b_entry)

	diags := ir.validate_module(mod, context.temp_allocator)
	testing.expect_value(t, len(diags), 0)
}
