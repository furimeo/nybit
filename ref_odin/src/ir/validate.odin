// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"
import "core:fmt"
import "core:mem"

validate_function :: proc(mod: ^Module, fn: ^Function, errors: ^[dynamic]string, allocator := context.temp_allocator) {
	if len(fn.blocks) == 0 do return

	if fn.entry_block == support.INVALID_BLOCK || int(fn.entry_block) >= len(fn.blocks) {
		append(errors, fmt.aprintf("fn '%s': invalid entry block", fn.name, allocator = allocator))
		return
	}

	entry := function_get_block(fn, fn.entry_block)
	if entry != nil && len(entry.predecessors) > 0 {
		append(errors, fmt.aprintf("fn '%s': entry block must have no predecessors", fn.name, allocator = allocator))
	}

	dt: Dominator_Tree
	dominator_tree_init(&dt, fn, allocator)

	inst_pos := make([]int, len(fn.instructions), context.temp_allocator)
	for i in 0..<len(inst_pos) do inst_pos[i] = -1

	val_defs := make([]int, len(fn.values), context.temp_allocator)
	val_uses := make([]int, len(fn.values), context.temp_allocator)

	for &blk in fn.blocks {
		// Skip dead/merged blocks
		if blk.first_inst == support.INVALID_INST {
			if blk.id == fn.entry_block || len(blk.predecessors) > 0 || len(blk.successors) > 0 {
				append(errors, fmt.aprintf("fn '%s': active block '.%s' is empty", fn.name, blk.name, allocator = allocator))
			}
			continue
		}

		curr := blk.first_inst
		prev := support.INVALID_INST
		last_inst: ^Instruction
		seen_non_phi := false
		pos := 0

		for curr != support.INVALID_INST {
			if int(curr) >= len(fn.instructions) {
				append(errors, fmt.aprintf("fn '%s': inst id %d out of bounds in block '.%s'", fn.name, curr, blk.name, allocator = allocator))
				break
			}

			inst := &fn.instructions[curr]
			if inst.opcode == .None {
				append(errors, fmt.aprintf("fn '%s': dead inst %d linked in block '.%s'", fn.name, curr, blk.name, allocator = allocator))
			}

			last_inst = inst
			inst_pos[curr] = pos
			pos += 1

			if inst.block != blk.id {
				append(errors, fmt.aprintf("fn '%s': inst %d block mismatch (%d != %d)", fn.name, curr, inst.block, blk.id, allocator = allocator))
			}

			if inst.prev != prev {
				append(errors, fmt.aprintf("fn '%s': inst %d prev link broken", fn.name, curr, allocator = allocator))
			}

			if inst.opcode == .Phi {
				if seen_non_phi {
					append(errors, fmt.aprintf("fn '%s': phi %d after non-phi in block '.%s'", fn.name, curr, blk.name, allocator = allocator))
				}
			} else {
				seen_non_phi = true
			}

			if prev != support.INVALID_INST && opcode_is_terminator(fn.instructions[prev].opcode) {
				append(errors, fmt.aprintf("fn '%s': inst %d after terminator in block '.%s'", fn.name, curr, blk.name, allocator = allocator))
			}

			if inst.result != support.INVALID_VALUE {
				if int(inst.result) >= len(fn.values) {
					append(errors, fmt.aprintf("fn '%s': inst %d result %d out of bounds", fn.name, curr, inst.result, allocator = allocator))
				} else {
					val_defs[inst.result] += 1
					if fn.values[inst.result].def != curr {
						append(errors, fmt.aprintf("fn '%s': value %d def mismatch with inst %d", fn.name, inst.result, curr, allocator = allocator))
					}
				}
			}

			operands := function_get_operands(fn, inst)
			for op in operands {
				if op.kind == .Value && int(op.val) < len(fn.values) {
					val_uses[op.val] += 1
				}
			}

			validate_inst_semantics(mod, fn, inst, operands, errors, allocator)
			prev = curr
			curr = inst.next
		}

		if prev != blk.last_inst {
			append(errors, fmt.aprintf("fn '%s': block '.%s' last_inst mismatch", fn.name, blk.name, allocator = allocator))
		}

		if last_inst == nil || !opcode_is_terminator(last_inst.opcode) {
			append(errors, fmt.aprintf("fn '%s': block '.%s' missing terminator", fn.name, blk.name, allocator = allocator))
		}

		for succ_id in blk.successors {
			succ := function_get_block(fn, succ_id)
			if succ == nil {
				append(errors, fmt.aprintf("fn '%s': block '.%s' invalid successor %d", fn.name, blk.name, succ_id, allocator = allocator))
				continue
			}
			found := false
			for p in succ.predecessors {
				if p == blk.id {
					found = true
					break
				}
			}
			if !found {
				append(errors, fmt.aprintf("fn '%s': edge '.%s' -> '.%s' missing in predecessors", fn.name, blk.name, succ.name, allocator = allocator))
			}
		}

		for pred_id in blk.predecessors {
			pred := function_get_block(fn, pred_id)
			if pred == nil {
				append(errors, fmt.aprintf("fn '%s': block '.%s' invalid predecessor %d", fn.name, blk.name, pred_id, allocator = allocator))
				continue
			}
			found := false
			for s in pred.successors {
				if s == blk.id {
					found = true
					break
				}
			}
			if !found {
				append(errors, fmt.aprintf("fn '%s': predecessor '.%s' missing edge to '.%s'", fn.name, pred.name, blk.name, allocator = allocator))
			}
		}
	}

	// SSA def/use count validation
	for v_idx in 0..<len(fn.values) {
		defs := val_defs[v_idx]
		uses := val_uses[v_idx]
		v := &fn.values[v_idx]

		switch v.kind {
		case .Argument:
			if defs != 0 {
				append(errors, fmt.aprintf("fn '%s': argument %d defined by instruction", fn.name, v_idx, allocator = allocator))
			}
		case .Instruction:
			if defs > 1 {
				append(errors, fmt.aprintf("fn '%s': value %d defined %d times (SSA violation)", fn.name, v_idx, defs, allocator = allocator))
			} else if defs == 0 && uses > 0 {
				append(errors, fmt.aprintf("fn '%s': value %d used but never defined", fn.name, v_idx, allocator = allocator))
			}
		case .Constant:
		}
	}

	// Dominance check
	for &inst in fn.instructions {
		if inst.opcode == .None do continue
		use_blk_id := inst.block

		operands := function_get_operands(fn, &inst)
		if inst.opcode == .Phi {
			for i := 0; i < len(operands); i += 2 {
				if operands[i].kind == .Value && i + 1 < len(operands) && operands[i + 1].kind == .Block {
					v_id := operands[i].val
					in_blk_id := operands[i + 1].blk
					val := function_get_value(fn, v_id)
					if val != nil && val.kind == .Instruction && val.def != support.INVALID_INST {
						def_inst := function_get_instruction(fn, val.def)
						if def_inst != nil && !dominator_tree_dominates(&dt, def_inst.block, in_blk_id) {
							append(errors, fmt.aprintf("fn '%s': phi %%%s def in block %d does not dominate edge block %d",
								fn.name, val.name, def_inst.block, in_blk_id, allocator = allocator))
						}
					}
				}
			}
		} else {
			for op in operands {
				if op.kind == .Value {
					val := function_get_value(fn, op.val)
					if val != nil && val.kind == .Instruction && val.def != support.INVALID_INST {
						def_inst := function_get_instruction(fn, val.def)
						if def_inst != nil {
							if def_inst.block == use_blk_id {
								if inst_pos[def_inst.id] >= inst_pos[inst.id] {
									append(errors, fmt.aprintf("fn '%s': value %%%s used at inst %d before def at inst %d",
										fn.name, val.name, inst.id, def_inst.id, allocator = allocator))
								}
							} else {
								if !dominator_tree_dominates(&dt, def_inst.block, use_blk_id) {
									append(errors, fmt.aprintf("fn '%s': value %%%s def in block %d does not dominate use in block %d",
										fn.name, val.name, def_inst.block, use_blk_id, allocator = allocator))
								}
							}
						}
					}
				}
			}
		}
	}
}

@(private="file")
get_operand_type :: #force_inline proc(mod: ^Module, fn: ^Function, op: Operand) -> support.Type_ID {
	#partial switch op.kind {
	case .Value:
		v := function_get_value(fn, op.val)
		return v != nil ? v.type : support.INVALID_TYPE
	case .Imm_Int:
		return TYPE_I64
	case .Imm_Float:
		return TYPE_F64
	case .Type:
		return op.type_id
	}
	return support.INVALID_TYPE
}

@(private="file")
validate_inst_semantics :: proc(
	mod: ^Module,
	fn: ^Function,
	inst: ^Instruction,
	operands: []Operand,
	errors: ^[dynamic]string,
	allocator: mem.Allocator,
) {
	tt := &mod.types

	#partial switch inst.opcode {
	case .Add, .Sub, .Mul, .Div_S, .Div_U, .Rem_S, .Rem_U:
		if len(operands) != 2 {
			append(errors, fmt.aprintf("fn '%s': inst %d (%s) expects 2 operands", fn.name, inst.id, opcode_name(inst.opcode), allocator = allocator))
			return
		}
		t_res := inst.result != support.INVALID_VALUE ? fn.values[inst.result].type : support.INVALID_TYPE
		if !type_is_int(tt, t_res) {
			append(errors, fmt.aprintf("fn '%s': inst %d (%s) non-integer result", fn.name, inst.id, opcode_name(inst.opcode), allocator = allocator))
		}

	case .FAdd, .FSub, .FMul, .FDiv, .FRem:
		if len(operands) != 2 {
			append(errors, fmt.aprintf("fn '%s': inst %d (%s) expects 2 operands", fn.name, inst.id, opcode_name(inst.opcode), allocator = allocator))
			return
		}
		t_res := inst.result != support.INVALID_VALUE ? fn.values[inst.result].type : support.INVALID_TYPE
		if !type_is_float(tt, t_res) {
			append(errors, fmt.aprintf("fn '%s': inst %d (%s) non-float result", fn.name, inst.id, opcode_name(inst.opcode), allocator = allocator))
		}

	case .And, .Or, .Xor, .Shl, .Shr, .Sar, .Rotl, .Rotr:
		if len(operands) != 2 {
			append(errors, fmt.aprintf("fn '%s': inst %d (%s) expects 2 operands", fn.name, inst.id, opcode_name(inst.opcode), allocator = allocator))
		}

	case .Neg, .Not:
		if len(operands) != 1 {
			append(errors, fmt.aprintf("fn '%s': inst %d (%s) expects 1 operand", fn.name, inst.id, opcode_name(inst.opcode), allocator = allocator))
		}

	case .Cmp_Eq, .Cmp_Ne, .Cmp_Lt_S, .Cmp_Lt_U, .Cmp_Le_S, .Cmp_Le_U, .Cmp_Gt_S, .Cmp_Gt_U, .Cmp_Ge_S, .Cmp_Ge_U:
		if len(operands) != 2 {
			append(errors, fmt.aprintf("fn '%s': inst %d (%s) expects 2 operands", fn.name, inst.id, opcode_name(inst.opcode), allocator = allocator))
		}
		t_res := inst.result != support.INVALID_VALUE ? fn.values[inst.result].type : support.INVALID_TYPE
		if t_res != TYPE_I8 {
			append(errors, fmt.aprintf("fn '%s': inst %d comparison result must be i8", fn.name, inst.id, allocator = allocator))
		}

	case .Load:
		if len(operands) != 1 {
			append(errors, fmt.aprintf("fn '%s': load %d expects 1 operand", fn.name, inst.id, allocator = allocator))
		} else {
			ptr_t := get_operand_type(mod, fn, operands[0])
			if !type_is_ptr(tt, ptr_t) {
				append(errors, fmt.aprintf("fn '%s': load %d target not a pointer", fn.name, inst.id, allocator = allocator))
			}
		}

	case .Store:
		if len(operands) != 2 {
			append(errors, fmt.aprintf("fn '%s': store %d expects 2 operands", fn.name, inst.id, allocator = allocator))
		} else {
			ptr_t := get_operand_type(mod, fn, operands[0])
			if !type_is_ptr(tt, ptr_t) {
				append(errors, fmt.aprintf("fn '%s': store %d target not a pointer", fn.name, inst.id, allocator = allocator))
			}
		}

	case .Branch:
		if len(operands) != 1 || operands[0].kind != .Block {
			append(errors, fmt.aprintf("fn '%s': branch %d expects block operand", fn.name, inst.id, allocator = allocator))
		}

	case .Branch_If:
		if len(operands) != 3 || operands[0].kind != .Value || operands[1].kind != .Block || operands[2].kind != .Block {
			append(errors, fmt.aprintf("fn '%s': branch_if %d expects (cond, true_blk, false_blk)", fn.name, inst.id, allocator = allocator))
		}

	case .Return:
		if fn.signature.return_type == TYPE_VOID {
			if len(operands) != 0 {
				append(errors, fmt.aprintf("fn '%s': void return cannot return value", fn.name, allocator = allocator))
			}
		} else {
			if len(operands) != 1 {
				append(errors, fmt.aprintf("fn '%s': return expects 1 value", fn.name, allocator = allocator))
			} else {
				ret_t := get_operand_type(mod, fn, operands[0])
				if ret_t != fn.signature.return_type {
					append(errors, fmt.aprintf("fn '%s': return type mismatch", fn.name, allocator = allocator))
				}
			}
		}

	case .Call:
		if len(operands) < 1 || operands[0].kind != .Function {
			append(errors, fmt.aprintf("fn '%s': call %d missing callee", fn.name, inst.id, allocator = allocator))
			return
		}
		callee := module_get_function(mod, operands[0].fn_id)
		if callee == nil {
			append(errors, fmt.aprintf("fn '%s': call %d callee not found", fn.name, inst.id, allocator = allocator))
			return
		}
		arg_count := len(operands) - 1
		param_count := len(callee.signature.param_types)
		if !callee.signature.is_variadic && arg_count != param_count {
			append(errors, fmt.aprintf("fn '%s': call %d arg count mismatch", fn.name, inst.id, allocator = allocator))
		}
	}
}

validate_module :: proc(mod: ^Module, allocator := context.temp_allocator) -> []string {
	errors := make([dynamic]string, allocator)
	for &fn in mod.functions {
		validate_function(mod, &fn, &errors, allocator)
	}
	return errors[:]
}
