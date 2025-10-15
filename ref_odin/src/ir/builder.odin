// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"

Builder :: struct {
	module:    ^Module,
	cur_fn:    support.Function_ID,
	cur_block: support.Block_ID,
}

builder_init :: proc(b: ^Builder, mod: ^Module) {
	b.module = mod
	b.cur_fn = support.INVALID_FUNCTION
	b.cur_block = support.INVALID_BLOCK
}

builder_set_insert_point :: proc(b: ^Builder, fn_id: support.Function_ID, blk_id: support.Block_ID) {
	b.cur_fn = fn_id
	b.cur_block = blk_id
}

builder_current_function :: proc(b: ^Builder) -> ^Function {
	return module_get_function(b.module, b.cur_fn)
}

builder_add_function :: proc(
	b: ^Builder,
	name: string,
	ret_type: support.Type_ID,
	call_conv := Calling_Convention.Default,
) -> support.Function_ID {
	fn_id := module_add_function(b.module, name, ret_type, call_conv)
	b.cur_fn = fn_id
	b.cur_block = support.INVALID_BLOCK
	return fn_id
}

builder_add_param :: proc(b: ^Builder, type: support.Type_ID, name := "") -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil, "no active function in builder")

	idx := u32(len(fn.signature.param_types))
	append(&fn.signature.param_types, type)
	append(&fn.signature.param_names, name)

	return function_create_value(fn, type, .Argument, support.INVALID_INST, idx, name)
}

builder_add_block :: proc(b: ^Builder, name: string) -> support.Block_ID {
	fn := builder_current_function(b)
	assert(fn != nil, "no active function in builder")
	return function_create_block(fn, name)
}

builder_const_i64 :: proc(b: ^Builder, val: i64, type: support.Type_ID, name := "") -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := function_create_value(fn, type, .Instruction, support.INVALID_INST, 0, name)
	ops := [1]Operand{operand_int(val)}
	function_append_instruction(fn, b.cur_block, .Const, res, ops[:])
	return res
}

builder_const_f64 :: proc(b: ^Builder, val: f64, type: support.Type_ID, name := "") -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := function_create_value(fn, type, .Instruction, support.INVALID_INST, 0, name)
	ops := [1]Operand{operand_float(val)}
	function_append_instruction(fn, b.cur_block, .Const, res, ops[:])
	return res
}

builder_const_null :: proc(b: ^Builder, name := "") -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := function_create_value(fn, TYPE_PTR, .Instruction, support.INVALID_INST, 0, name)
	function_append_instruction(fn, b.cur_block, .Const_Null, res)
	return res
}

builder_binop :: proc(
	b: ^Builder,
	op: Opcode,
	lhs, rhs: support.Value_ID,
	type: support.Type_ID,
	name := "",
) -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := function_create_value(fn, type, .Instruction, support.INVALID_INST, 0, name)
	ops := [2]Operand{operand_value(lhs), operand_value(rhs)}
	function_append_instruction(fn, b.cur_block, op, res, ops[:])
	return res
}

builder_add :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Add, lhs, rhs, type, name)
}

builder_sub :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Sub, lhs, rhs, type, name)
}

builder_mul :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Mul, lhs, rhs, type, name)
}

builder_div_s :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Div_S, lhs, rhs, type, name)
}

builder_div_u :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Div_U, lhs, rhs, type, name)
}

builder_rem_s :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Rem_S, lhs, rhs, type, name)
}

builder_rem_u :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Rem_U, lhs, rhs, type, name)
}

builder_neg :: proc(b: ^Builder, val: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := function_create_value(fn, type, .Instruction, support.INVALID_INST, 0, name)
	ops := [1]Operand{operand_value(val)}
	function_append_instruction(fn, b.cur_block, .Neg, res, ops[:])
	return res
}

builder_and :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .And, lhs, rhs, type, name)
}

builder_or :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Or, lhs, rhs, type, name)
}

builder_xor :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Xor, lhs, rhs, type, name)
}

builder_not :: proc(b: ^Builder, val: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := function_create_value(fn, type, .Instruction, support.INVALID_INST, 0, name)
	ops := [1]Operand{operand_value(val)}
	function_append_instruction(fn, b.cur_block, .Not, res, ops[:])
	return res
}

builder_shl :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Shl, lhs, rhs, type, name)
}

builder_shr :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Shr, lhs, rhs, type, name)
}

builder_sar :: proc(b: ^Builder, lhs, rhs: support.Value_ID, type: support.Type_ID, name := "") -> support.Value_ID {
	return builder_binop(b, .Sar, lhs, rhs, type, name)
}

builder_cmp :: proc(b: ^Builder, op: Opcode, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_binop(b, op, lhs, rhs, TYPE_I8, name)
}

builder_cmp_eq :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Eq, lhs, rhs, name)
}

builder_cmp_ne :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Ne, lhs, rhs, name)
}

builder_cmp_lt_s :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Lt_S, lhs, rhs, name)
}

builder_cmp_lt_u :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Lt_U, lhs, rhs, name)
}

builder_cmp_le_s :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Le_S, lhs, rhs, name)
}

builder_cmp_le_u :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Le_U, lhs, rhs, name)
}

builder_cmp_gt_s :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Gt_S, lhs, rhs, name)
}

builder_cmp_gt_u :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Gt_U, lhs, rhs, name)
}

builder_cmp_ge_s :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Ge_S, lhs, rhs, name)
}

builder_cmp_ge_u :: proc(b: ^Builder, lhs, rhs: support.Value_ID, name := "") -> support.Value_ID {
	return builder_cmp(b, .Cmp_Ge_U, lhs, rhs, name)
}

builder_select :: proc(
	b: ^Builder,
	cond, true_val, false_val: support.Value_ID,
	type: support.Type_ID,
	name := "",
) -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := function_create_value(fn, type, .Instruction, support.INVALID_INST, 0, name)
	ops := [3]Operand{operand_value(cond), operand_value(true_val), operand_value(false_val)}
	function_append_instruction(fn, b.cur_block, .Select, res, ops[:])
	return res
}

builder_load :: proc(b: ^Builder, ptr: support.Value_ID, val_type: support.Type_ID, name := "") -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := function_create_value(fn, val_type, .Instruction, support.INVALID_INST, 0, name)
	ops := [1]Operand{operand_value(ptr)}
	function_append_instruction(fn, b.cur_block, .Load, res, ops[:])
	return res
}

builder_store :: proc(b: ^Builder, ptr, val: support.Value_ID) -> support.Inst_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	ops := [2]Operand{operand_value(ptr), operand_value(val)}
	return function_append_instruction(fn, b.cur_block, .Store, support.INVALID_VALUE, ops[:])
}

builder_branch :: proc(b: ^Builder, target: support.Block_ID) -> support.Inst_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	cur_blk := function_get_block(fn, b.cur_block)
	tgt_blk := function_get_block(fn, target)
	assert(tgt_blk != nil, "target block does not exist")

	block_add_edge(cur_blk, tgt_blk)

	ops := [1]Operand{operand_block(target)}
	return function_append_instruction(fn, b.cur_block, .Branch, support.INVALID_VALUE, ops[:])
}

builder_branch_if :: proc(
	b: ^Builder,
	cond: support.Value_ID,
	true_blk, false_blk: support.Block_ID,
) -> support.Inst_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	cur_blk := function_get_block(fn, b.cur_block)
	t_blk := function_get_block(fn, true_blk)
	f_blk := function_get_block(fn, false_blk)
	assert(t_blk != nil && f_blk != nil, "branch target block does not exist")

	block_add_edge(cur_blk, t_blk)
	block_add_edge(cur_blk, f_blk)

	ops := [3]Operand{operand_value(cond), operand_block(true_blk), operand_block(false_blk)}
	return function_append_instruction(fn, b.cur_block, .Branch_If, support.INVALID_VALUE, ops[:])
}

builder_ret :: proc(b: ^Builder, val: support.Value_ID = support.INVALID_VALUE) -> support.Inst_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	if val != support.INVALID_VALUE {
		ops := [1]Operand{operand_value(val)}
		return function_append_instruction(fn, b.cur_block, .Return, support.INVALID_VALUE, ops[:])
	}
	return function_append_instruction(fn, b.cur_block, .Return, support.INVALID_VALUE)
}

builder_call :: proc(
	b: ^Builder,
	callee: support.Function_ID,
	args: []support.Value_ID,
	ret_type: support.Type_ID,
	name := "",
) -> support.Value_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")

	res := support.INVALID_VALUE
	if ret_type != TYPE_VOID {
		res = function_create_value(fn, ret_type, .Instruction, support.INVALID_INST, 0, name)
	}

	all_ops := make([dynamic]Operand, context.temp_allocator)
	append(&all_ops, operand_function(callee))
	for a in args do append(&all_ops, operand_value(a))

	function_append_instruction(fn, b.cur_block, .Call, res, all_ops[:])
	return res
}

builder_trap :: proc(b: ^Builder) -> support.Inst_ID {
	fn := builder_current_function(b)
	assert(fn != nil && b.cur_block != support.INVALID_BLOCK, "invalid insert point")
	return function_append_instruction(fn, b.cur_block, .Trap, support.INVALID_VALUE)
}
