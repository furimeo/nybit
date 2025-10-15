// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"
import "core:mem"

Calling_Convention :: enum u8 {
	Default = 0,
	C,
	System,
	Fast,
}

Function_Signature :: struct {
	return_type: support.Type_ID,
	param_types: [dynamic]support.Type_ID,
	param_names: [dynamic]string,
	call_conv:   Calling_Convention,
	is_variadic: bool,
}

Function :: struct {
	id:           support.Function_ID,
	name:         string,
	signature:    Function_Signature,
	entry_block:  support.Block_ID,

	blocks:       [dynamic]Block,
	instructions: [dynamic]Instruction,
	values:       [dynamic]Value,
	operands:     [dynamic]Operand,

	allocator:    mem.Allocator,
}

function_init :: proc(
	fn: ^Function,
	id: support.Function_ID,
	name: string,
	ret_type: support.Type_ID,
	call_conv := Calling_Convention.Default,
	allocator := context.allocator,
) {
	fn.id = id
	fn.name = name
	fn.entry_block = support.INVALID_BLOCK
	fn.allocator = allocator

	fn.signature.return_type = ret_type
	fn.signature.param_types = make([dynamic]support.Type_ID, allocator)
	fn.signature.param_names = make([dynamic]string, allocator)
	fn.signature.call_conv = call_conv

	fn.blocks = make([dynamic]Block, allocator)
	fn.instructions = make([dynamic]Instruction, allocator)
	fn.values = make([dynamic]Value, allocator)
	fn.operands = make([dynamic]Operand, allocator)
}

function_get_block :: proc(fn: ^Function, id: support.Block_ID) -> ^Block {
	idx := int(id)
	return idx >= 0 && idx < len(fn.blocks) ? &fn.blocks[idx] : nil
}

function_get_instruction :: proc(fn: ^Function, id: support.Inst_ID) -> ^Instruction {
	idx := int(id)
	return idx >= 0 && idx < len(fn.instructions) ? &fn.instructions[idx] : nil
}

function_get_value :: proc(fn: ^Function, id: support.Value_ID) -> ^Value {
	idx := int(id)
	return idx >= 0 && idx < len(fn.values) ? &fn.values[idx] : nil
}

function_get_operands :: proc(fn: ^Function, inst: ^Instruction) -> []Operand {
	start := int(inst.operands.start)
	end := start + int(inst.operands.count)
	return start >= 0 && end <= len(fn.operands) ? fn.operands[start:end] : nil
}

function_create_block :: proc(fn: ^Function, name: string) -> support.Block_ID {
	id := support.Block_ID(len(fn.blocks))
	blk := Block{
		id           = id,
		name         = name,
		first_inst   = support.INVALID_INST,
		last_inst    = support.INVALID_INST,
		predecessors = make([dynamic]support.Block_ID, fn.allocator),
		successors   = make([dynamic]support.Block_ID, fn.allocator),
	}
	append(&fn.blocks, blk)
	if fn.entry_block == support.INVALID_BLOCK {
		fn.entry_block = id
	}
	return id
}

function_create_value :: proc(
	fn: ^Function,
	type: support.Type_ID,
	kind: Value_Kind,
	def: support.Inst_ID = support.INVALID_INST,
	index: u32 = 0,
	name := "",
) -> support.Value_ID {
	id := support.Value_ID(len(fn.values))
	append(&fn.values, Value{
		id    = id,
		type  = type,
		kind  = kind,
		def   = def,
		index = index,
		name  = name,
	})
	return id
}

function_append_instruction :: proc(
	fn: ^Function,
	block_id: support.Block_ID,
	opcode: Opcode,
	result: support.Value_ID = support.INVALID_VALUE,
	operands: []Operand = nil,
	flags: Inst_Flags = {},
) -> support.Inst_ID {
	blk := function_get_block(fn, block_id)
	assert(blk != nil, "invalid block id")

	inst_id := support.Inst_ID(len(fn.instructions))
	start := u32(len(fn.operands))
	count := u16(len(operands))
	for op in operands do append(&fn.operands, op)

	inst := Instruction{
		id       = inst_id,
		opcode   = opcode,
		result   = result,
		operands = {start = start, count = count},
		flags    = flags,
		block    = block_id,
		prev     = blk.last_inst,
		next     = support.INVALID_INST,
	}

	if blk.first_inst == support.INVALID_INST {
		blk.first_inst = inst_id
		blk.last_inst  = inst_id
	} else {
		fn.instructions[blk.last_inst].next = inst_id
		blk.last_inst = inst_id
	}
	blk.inst_count += 1

	append(&fn.instructions, inst)

	if result != support.INVALID_VALUE {
		fn.values[result].def = inst_id
	}

	return inst_id
}

instruction_replace_operand :: proc(fn: ^Function, inst_id: support.Inst_ID, op_idx: int, new_op: Operand) {
	inst := function_get_instruction(fn, inst_id)
	assert(inst != nil && op_idx >= 0 && op_idx < int(inst.operands.count))
	global_op_idx := int(inst.operands.start) + op_idx
	fn.operands[global_op_idx] = new_op
}

function_replace_all_uses :: proc(fn: ^Function, old_val: support.Value_ID, new_val: support.Value_ID) -> int {
	replace_count := 0
	for &inst in fn.instructions {
		if inst.opcode == .None do continue
		for i in 0..<int(inst.operands.count) {
			global_op_idx := int(inst.operands.start) + i
			op := &fn.operands[global_op_idx]
			if op.kind == .Value && op.val == old_val {
				op.val = new_val
				replace_count += 1
			}
		}
	}
	return replace_count
}

function_remove_instruction :: proc(fn: ^Function, inst_id: support.Inst_ID) {
	inst := function_get_instruction(fn, inst_id)
	if inst == nil || inst.opcode == .None do return

	blk := function_get_block(fn, inst.block)
	if blk != nil {
		if inst.prev != support.INVALID_INST {
			fn.instructions[inst.prev].next = inst.next
		} else {
			blk.first_inst = inst.next
		}

		if inst.next != support.INVALID_INST {
			fn.instructions[inst.next].prev = inst.prev
		} else {
			blk.last_inst = inst.prev
		}

		if blk.inst_count > 0 {
			blk.inst_count -= 1
		}
	}

	inst.opcode = .None
	inst.prev   = support.INVALID_INST
	inst.next   = support.INVALID_INST
}

function_remove_block :: proc(fn: ^Function, blk_id: support.Block_ID) {
	blk := function_get_block(fn, blk_id)
	if blk == nil do return

	// Xóa instructions trong block
	curr := blk.first_inst
	for curr != support.INVALID_INST {
		next := fn.instructions[curr].next
		function_remove_instruction(fn, curr)
		curr = next
	}

	// Cập nhật CFG: xóa blk_id khỏi predecessors của các successors
	for succ_id in blk.successors {
		succ := function_get_block(fn, succ_id)
		if succ != nil {
			for i := 0; i < len(succ.predecessors); i += 1 {
				if succ.predecessors[i] == blk_id {
					unordered_remove(&succ.predecessors, i)
					i -= 1
				}
			}
		}
	}

	// Cập nhật CFG: xóa blk_id khỏi successors của các predecessors
	for pred_id in blk.predecessors {
		pred := function_get_block(fn, pred_id)
		if pred != nil {
			for i := 0; i < len(pred.successors); i += 1 {
				if pred.successors[i] == blk_id {
					unordered_remove(&pred.successors, i)
					i -= 1
				}
			}
		}
	}

	clear(&blk.predecessors)
	clear(&blk.successors)
	blk.first_inst = support.INVALID_INST
	blk.last_inst  = support.INVALID_INST
	blk.inst_count = 0
}

function_split_block :: proc(
	fn: ^Function,
	blk_id: support.Block_ID,
	at_inst_id: support.Inst_ID,
	new_name: string,
) -> support.Block_ID {
	blk := function_get_block(fn, blk_id)
	assert(blk != nil, "invalid block to split")

	new_blk_id := function_create_block(fn, new_name)
	new_blk := function_get_block(fn, new_blk_id)

	// Chuyển instructions từ at_inst_id trở đi sang new_blk
	new_blk.first_inst = at_inst_id
	new_blk.last_inst  = blk.last_inst

	// Tìm instruction trước at_inst_id
	at_inst := function_get_instruction(fn, at_inst_id)
	prev_id := at_inst.prev
	at_inst.prev = support.INVALID_INST

	if prev_id != support.INVALID_INST {
		fn.instructions[prev_id].next = support.INVALID_INST
		blk.last_inst = prev_id
	} else {
		blk.first_inst = support.INVALID_INST
		blk.last_inst  = support.INVALID_INST
	}

	// Đếm lại và cập nhật owner block cho các instructions đã dời
	curr := at_inst_id
	count: u32 = 0
	for curr != support.INVALID_INST {
		fn.instructions[curr].block = new_blk_id
		count += 1
		curr = fn.instructions[curr].next
	}
	new_blk.inst_count = count
	blk.inst_count -= count

	// Di chuyển successors của blk sang new_blk
	for s in blk.successors {
		append(&new_blk.successors, s)
		succ := function_get_block(fn, s)
		if succ != nil {
			for i in 0..<len(succ.predecessors) {
				if succ.predecessors[i] == blk_id {
					succ.predecessors[i] = new_blk_id
				}
			}
		}
	}
	clear(&blk.successors)

	// Thêm unconditional branch từ blk sang new_blk
	function_append_instruction(fn, blk_id, .Branch, support.INVALID_VALUE, []Operand{operand_block(new_blk_id)})
	block_add_edge(blk, new_blk)

	return new_blk_id
}

function_merge_blocks :: proc(fn: ^Function, pred_blk_id: support.Block_ID, succ_blk_id: support.Block_ID) -> bool {
	pred_blk := function_get_block(fn, pred_blk_id)
	succ_blk := function_get_block(fn, succ_blk_id)
	if pred_blk == nil || succ_blk == nil do return false

	// Chỉ merge khi pred_blk chỉ có 1 successor là succ_blk, và succ_blk chỉ có 1 predecessor là pred_blk
	if len(pred_blk.successors) != 1 || pred_blk.successors[0] != succ_blk_id do return false
	if len(succ_blk.predecessors) != 1 || succ_blk.predecessors[0] != pred_blk_id do return false

	// Xóa terminator branch ở pred_blk
	if pred_blk.last_inst != support.INVALID_INST {
		term := function_get_instruction(fn, pred_blk.last_inst)
		if term != nil && term.opcode == .Branch {
			function_remove_instruction(fn, pred_blk.last_inst)
		}
	}

	// Nối instructions của succ_blk vào pred_blk
	if succ_blk.first_inst != support.INVALID_INST {
		if pred_blk.first_inst == support.INVALID_INST {
			pred_blk.first_inst = succ_blk.first_inst
			pred_blk.last_inst  = succ_blk.last_inst
		} else {
			fn.instructions[pred_blk.last_inst].next = succ_blk.first_inst
			fn.instructions[succ_blk.first_inst].prev = pred_blk.last_inst
			pred_blk.last_inst = succ_blk.last_inst
		}

		// Cập nhật block id cho instructions
		curr := succ_blk.first_inst
		for curr != support.INVALID_INST {
			fn.instructions[curr].block = pred_blk_id
			pred_blk.inst_count += 1
			curr = fn.instructions[curr].next
		}
	}

	// Cập nhật successors của pred_blk
	clear(&pred_blk.successors)
	for s in succ_blk.successors {
		append(&pred_blk.successors, s)
		s_blk := function_get_block(fn, s)
		if s_blk != nil {
			for i in 0..<len(s_blk.predecessors) {
				if s_blk.predecessors[i] == succ_blk_id {
					s_blk.predecessors[i] = pred_blk_id
				}
			}
		}
	}

	// Xóa succ_blk
	clear(&succ_blk.predecessors)
	clear(&succ_blk.successors)
	succ_blk.first_inst = support.INVALID_INST
	succ_blk.last_inst  = support.INVALID_INST
	succ_blk.inst_count = 0

	return true
}

function_remove_phi_incoming :: proc(fn: ^Function, blk_id: support.Block_ID, pred_id: support.Block_ID) {
	blk := function_get_block(fn, blk_id)
	if blk == nil do return

	inst_id := blk.first_inst
	for inst_id != support.INVALID_INST {
		inst := function_get_instruction(fn, inst_id)
		if inst == nil || inst.opcode != .Phi do break

		ops := function_get_operands(fn, inst)
		has_pred := false
		for i := 1; i < len(ops); i += 2 {
			if ops[i].kind == .Block && ops[i].blk == pred_id {
				has_pred = true
				break
			}
		}

		if has_pred {
			start := int(inst.operands.start)
			write_idx := start
			for i := 0; i < len(ops); i += 2 {
				if i + 1 < len(ops) && ops[i + 1].kind == .Block && ops[i + 1].blk == pred_id {
					continue
				}
				fn.operands[write_idx]     = ops[i]
				fn.operands[write_idx + 1] = ops[i + 1]
				write_idx += 2
			}
			inst.operands.count = u16(write_idx - start)
		}

		inst_id = inst.next
	}
}

