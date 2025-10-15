// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package opt

import "../ir"
import "../support"

// Canonicalization:
// 1. Chuẩn hóa so sánh: cmp.gt -> cmp.lt, cmp.ge -> cmp.le (đảo operands)
// 2. Chuẩn hóa giao hoán: đưa constant sang RHS, sắp xếp deterministic theo Value_ID
// 3. Rút gọn identity / trivial constants:
//    x + 0 -> x, x - 0 -> x, x * 1 -> x, x * 0 -> 0, x | 0 -> x
//    x ^ x -> 0, x - x -> 0
//    cast/truncate cùng kiểu -> x
// 4. Dọn dẹp dead instructions sau terminator trong block
// 5. Chuẩn hóa rẽ nhánh: @branch_if %c, .B, .B -> @branch .B
// 6. Loại bỏ trivial phi: phi [%x, %x] -> %x
// 7. Loại bỏ unreachable blocks (BFS O(N))
// 8. Gộp block fallthrough (Block merging)

is_commutative :: #force_inline proc(op: ir.Opcode) -> bool {
	#partial switch op {
	case .Add, .Mul, .FAdd, .FMul, .And, .Or, .Xor, .Cmp_Eq, .Cmp_Ne, .FCmp_Eq, .FCmp_Ne:
		return true
	}
	return false
}

get_const_int :: proc(fn: ^ir.Function, op: ir.Operand) -> (i64, bool) {
	if op.kind == .Imm_Int do return op.imm_int, true
	if op.kind == .Value {
		v := ir.function_get_value(fn, op.val)
		if v != nil && v.kind == .Instruction && v.def != support.INVALID_INST {
			def_inst := ir.function_get_instruction(fn, v.def)
			if def_inst != nil && def_inst.opcode == .Const {
				ops := ir.function_get_operands(fn, def_inst)
				if len(ops) == 1 && ops[0].kind == .Imm_Int {
					return ops[0].imm_int, true
				}
			}
		}
	}
	return 0, false
}

is_const_zero :: #force_inline proc(fn: ^ir.Function, op: ir.Operand) -> bool {
	if val, ok := get_const_int(fn, op); ok && val == 0 do return true
	if op.kind == .Imm_Float && op.imm_float == 0.0 do return true
	return false
}

is_const_one :: #force_inline proc(fn: ^ir.Function, op: ir.Operand) -> bool {
	if val, ok := get_const_int(fn, op); ok && val == 1 do return true
	if op.kind == .Imm_Float && op.imm_float == 1.0 do return true
	return false
}

canonicalize_instructions :: proc(mod: ^ir.Module, fn: ^ir.Function) -> bool {
	changed := false

	for &inst in fn.instructions {
		if inst.opcode == .None do continue
		operands := ir.function_get_operands(fn, &inst)

		// 1. Canonicalize comparisons (GT -> LT, GE -> LE)
		#partial switch inst.opcode {
		case .Cmp_Gt_S:
			inst.opcode = .Cmp_Lt_S
			if len(operands) == 2 {
				tmp := operands[0]
				ir.instruction_replace_operand(fn, inst.id, 0, operands[1])
				ir.instruction_replace_operand(fn, inst.id, 1, tmp)
				changed = true
			}
		case .Cmp_Gt_U:
			inst.opcode = .Cmp_Lt_U
			if len(operands) == 2 {
				tmp := operands[0]
				ir.instruction_replace_operand(fn, inst.id, 0, operands[1])
				ir.instruction_replace_operand(fn, inst.id, 1, tmp)
				changed = true
			}
		case .Cmp_Ge_S:
			inst.opcode = .Cmp_Le_S
			if len(operands) == 2 {
				tmp := operands[0]
				ir.instruction_replace_operand(fn, inst.id, 0, operands[1])
				ir.instruction_replace_operand(fn, inst.id, 1, tmp)
				changed = true
			}
		case .Cmp_Ge_U:
			inst.opcode = .Cmp_Le_U
			if len(operands) == 2 {
				tmp := operands[0]
				ir.instruction_replace_operand(fn, inst.id, 0, operands[1])
				ir.instruction_replace_operand(fn, inst.id, 1, tmp)
				changed = true
			}
		case .FCmp_Gt:
			inst.opcode = .FCmp_Lt
			if len(operands) == 2 {
				tmp := operands[0]
				ir.instruction_replace_operand(fn, inst.id, 0, operands[1])
				ir.instruction_replace_operand(fn, inst.id, 1, tmp)
				changed = true
			}
		case .FCmp_Ge:
			inst.opcode = .FCmp_Le
			if len(operands) == 2 {
				tmp := operands[0]
				ir.instruction_replace_operand(fn, inst.id, 0, operands[1])
				ir.instruction_replace_operand(fn, inst.id, 1, tmp)
				changed = true
			}
		}

		operands = ir.function_get_operands(fn, &inst)

		// 2. Commutative operand ordering
		if is_commutative(inst.opcode) && len(operands) == 2 {
			op0, op1 := operands[0], operands[1]
			should_swap := false

			if (op0.kind == .Imm_Int || op0.kind == .Imm_Float) && op1.kind == .Value {
				should_swap = true
			} else if op0.kind == .Value && op1.kind == .Value {
				_, v0_const := get_const_int(fn, op0)
				_, v1_const := get_const_int(fn, op1)
				if v0_const && !v1_const {
					should_swap = true
				} else if !v0_const && !v1_const && op0.val > op1.val {
					should_swap = true
				}
			}

			if should_swap {
				ir.instruction_replace_operand(fn, inst.id, 0, op1)
				ir.instruction_replace_operand(fn, inst.id, 1, op0)
				operands = ir.function_get_operands(fn, &inst)
				changed = true
			}
		}

		// 3. Identity and trivial constant simplification
		if inst.result != support.INVALID_VALUE && len(operands) == 2 {
			op0, op1 := operands[0], operands[1]

			#partial switch inst.opcode {
			case .Add:
				// %x + 0 -> %x
				if op0.kind == .Value && is_const_zero(fn, op1) {
					ir.function_replace_all_uses(fn, inst.result, op0.val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
					continue
				}

			case .Sub:
				// %x - 0 -> %x
				if op0.kind == .Value && is_const_zero(fn, op1) {
					ir.function_replace_all_uses(fn, inst.result, op0.val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
					continue
				}
				// %x - %x -> 0
				if op0.kind == .Value && op1.kind == .Value && op0.val == op1.val {
					res_type := fn.values[inst.result].type
					zero_val := ir.function_create_value(fn, res_type, .Instruction, support.INVALID_INST, 0, "zero")
					zero_ops := [1]ir.Operand{ir.operand_int(0)}
					ir.function_append_instruction(fn, inst.block, .Const, zero_val, zero_ops[:])
					ir.function_replace_all_uses(fn, inst.result, zero_val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
					continue
				}

			case .Mul:
				// %x * 1 -> %x
				if op0.kind == .Value && is_const_one(fn, op1) {
					ir.function_replace_all_uses(fn, inst.result, op0.val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
					continue
				}

			case .Or:
				// %x | 0 -> %x
				if op0.kind == .Value && is_const_zero(fn, op1) {
					ir.function_replace_all_uses(fn, inst.result, op0.val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
					continue
				}

			case .Xor:
				// %x ^ %x -> 0
				if op0.kind == .Value && op1.kind == .Value && op0.val == op1.val {
					res_type := fn.values[inst.result].type
					zero_val := ir.function_create_value(fn, res_type, .Instruction, support.INVALID_INST, 0, "zero")
					zero_ops := [1]ir.Operand{ir.operand_int(0)}
					ir.function_append_instruction(fn, inst.block, .Const, zero_val, zero_ops[:])
					ir.function_replace_all_uses(fn, inst.result, zero_val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
					continue
				}
			}
		}

		// 4. Redundant Cast / Truncate / Extend of same type
		#partial switch inst.opcode {
		case .Cast, .Bitcast, .SExt, .ZExt, .Extend, .Truncate:
			if inst.result != support.INVALID_VALUE && len(operands) == 1 && operands[0].kind == .Value {
				res_type := fn.values[inst.result].type
				src_type := fn.values[operands[0].val].type
				if res_type == src_type {
					ir.function_replace_all_uses(fn, inst.result, operands[0].val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
					continue
				}
			}
		}
	}

	return changed
}

canonicalize_cfg :: proc(mod: ^ir.Module, fn: ^ir.Function) -> bool {
	changed := false

	// 1. Prune dead instructions after terminator in every block
	for &blk in fn.blocks {
		if blk.first_inst == support.INVALID_INST do continue

		curr := blk.first_inst
		for curr != support.INVALID_INST {
			inst := ir.function_get_instruction(fn, curr)
			if inst == nil do break
			next := inst.next

			if ir.opcode_is_terminator(inst.opcode) {
				dead := next
				for dead != support.INVALID_INST {
					dead_next := fn.instructions[dead].next
					ir.function_remove_instruction(fn, dead)
					dead = dead_next
					changed = true
				}
				blk.last_inst = curr
				inst.next = support.INVALID_INST
				break
			}
			curr = next
		}
	}

	// 2. Redundant branch_if: @branch_if %cond, .B, .B -> @branch .B
	for &blk in fn.blocks {
		if blk.last_inst == support.INVALID_INST do continue
		term := ir.function_get_instruction(fn, blk.last_inst)
		if term != nil && term.opcode == .Branch_If {
			ops := ir.function_get_operands(fn, term)
			if len(ops) == 3 && ops[1].kind == .Block && ops[2].kind == .Block && ops[1].blk == ops[2].blk {
				target := ops[1].blk
				term.opcode = .Branch
				ir.instruction_replace_operand(fn, term.id, 0, ir.operand_block(target))
				term.operands.count = 1

				if len(blk.successors) > 1 {
					clear(&blk.successors)
					append(&blk.successors, target)
				}
				target_blk := ir.function_get_block(fn, target)
				if target_blk != nil {
					pred_count := 0
					for i := 0; i < len(target_blk.predecessors); i += 1 {
						if target_blk.predecessors[i] == blk.id {
							pred_count += 1
							if pred_count > 1 {
								unordered_remove(&target_blk.predecessors, i)
								i -= 1
							}
						}
					}
				}
				changed = true
			}
		}
	}

	// 3. Trivial Phi Elimination: phi [%x, %x] -> %x
	for &blk in fn.blocks {
		curr := blk.first_inst
		for curr != support.INVALID_INST {
			inst := ir.function_get_instruction(fn, curr)
			if inst == nil do break
			next := inst.next

			if inst.opcode == .Phi && inst.result != support.INVALID_VALUE {
				ops := ir.function_get_operands(fn, inst)
				if len(ops) >= 2 {
					first_val := ops[0].val
					all_same := true
					for i := 2; i < len(ops); i += 2 {
						if ops[i].kind == .Value && ops[i].val != first_val {
							all_same = false
							break
						}
					}
					if all_same {
						ir.function_replace_all_uses(fn, inst.result, first_val)
						ir.function_remove_instruction(fn, inst.id)
						changed = true
					}
				}
			}
			curr = next
		}
	}

	// 4. Eliminate Unreachable Blocks (Linear BFS queue with head/tail pointers, zero array reallocation)
	n_blocks := len(fn.blocks)
	if fn.entry_block != support.INVALID_BLOCK && n_blocks > 1 {
		reachable := make([]bool, n_blocks, context.temp_allocator)
		queue := make([]support.Block_ID, n_blocks, context.temp_allocator)
		head, tail := 0, 0

		reachable[fn.entry_block] = true
		queue[tail] = fn.entry_block
		tail += 1

		for head < tail {
			curr_id := queue[head]
			head += 1

			curr_blk := ir.function_get_block(fn, curr_id)
			if curr_blk == nil do continue

			for succ_id in curr_blk.successors {
				if int(succ_id) < n_blocks && !reachable[succ_id] {
					reachable[succ_id] = true
					queue[tail] = succ_id
					tail += 1
				}
			}
		}

		for blk_idx := 0; blk_idx < n_blocks; blk_idx += 1 {
			b_id := support.Block_ID(blk_idx)
			if b_id != fn.entry_block && !reachable[blk_idx] {
				blk := &fn.blocks[blk_idx]
				if blk.inst_count > 0 || len(blk.successors) > 0 || len(blk.predecessors) > 0 {
					ir.function_remove_block(fn, b_id)
					changed = true
				}
			}
		}
	}

	// 5. Block Merging
	for blk_idx := 0; blk_idx < len(fn.blocks); blk_idx += 1 {
		b_id := support.Block_ID(blk_idx)
		blk := ir.function_get_block(fn, b_id)
		if blk == nil || blk.inst_count == 0 do continue

		if len(blk.successors) == 1 {
			succ_id := blk.successors[0]
			if succ_id != b_id {
				if ir.function_merge_blocks(fn, b_id, succ_id) {
					changed = true
				}
			}
		}
	}

	return changed
}

canonicalize_function :: proc(mod: ^ir.Module, fn: ^ir.Function) -> bool {
	changed_any := false
	max_passes := 4

	for pass := 0; pass < max_passes; pass += 1 {
		c1 := canonicalize_instructions(mod, fn)
		c2 := canonicalize_cfg(mod, fn)
		if !c1 && !c2 do break
		changed_any = true
	}

	return changed_any
}

canonicalize_module :: proc(mod: ^ir.Module) -> bool {
	changed := false
	for &fn in mod.functions {
		if canonicalize_function(mod, &fn) {
			changed = true
		}
	}
	return changed
}
