// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package opt

import "../analysis"
import "../ir"
import "../support"

GVN_Key :: struct {
	opcode:   ir.Opcode,
	type:     support.Type_ID,
	op_count: u16,
	op0:      ir.Operand,
	op1:      ir.Operand,
	op2:      ir.Operand,
}

is_gvn_candidate :: proc(op: ir.Opcode) -> bool {
	#partial switch op {
	case .Add, .Sub, .Mul, .Div_S, .Div_U, .Rem_S, .Rem_U, .Neg:
		return true
	case .FAdd, .FSub, .FMul, .FDiv, .FNeg:
		return true
	case .And, .Or, .Xor, .Not, .Shl, .Shr, .Sar, .Rotl, .Rotr:
		return true
	case .Cmp_Eq, .Cmp_Ne, .Cmp_Lt_S, .Cmp_Lt_U, .Cmp_Le_S, .Cmp_Le_U, .Cmp_Gt_S, .Cmp_Gt_U, .Cmp_Ge_S, .Cmp_Ge_U:
		return true
	case .FCmp_Eq, .FCmp_Ne, .FCmp_Lt, .FCmp_Le, .FCmp_Gt, .FCmp_Ge:
		return true
	case .Select:
		return true
	case .Cast, .Extend, .Truncate, .Bitcast, .SExt, .ZExt, .FExt, .FTrunc, .SIToFP, .FPToSI:
		return true
	}
	return false
}

operand_equal :: proc(a, b: ir.Operand) -> bool {
	if a.kind != b.kind do return false
	switch a.kind {
	case .None:      return true
	case .Value:     return a.val == b.val
	case .Block:     return a.blk == b.blk
	case .Imm_Int:   return a.imm_int == b.imm_int
	case .Imm_Float: return a.imm_float == b.imm_float
	case .Function:  return a.fn_id == b.fn_id
	case .Symbol:    return a.sym_id == b.sym_id
	case .Type:      return a.type_id == b.type_id
	}
	return false
}

gvn_key_equal :: proc(a, b: GVN_Key) -> bool {
	if a.opcode != b.opcode || a.type != b.type || a.op_count != b.op_count do return false
	if a.op_count > 0 && !operand_equal(a.op0, b.op0) do return false
	if a.op_count > 1 && !operand_equal(a.op1, b.op1) do return false
	if a.op_count > 2 && !operand_equal(a.op2, b.op2) do return false
	return true
}

make_gvn_key :: proc(fn: ^ir.Function, inst: ^ir.Instruction) -> GVN_Key {
	res_val := ir.function_get_value(fn, inst.result)
	t := res_val.type if res_val != nil else ir.TYPE_VOID
	ops := ir.function_get_operands(fn, inst)

	key := GVN_Key{
		opcode   = inst.opcode,
		type     = t,
		op_count = u16(len(ops)),
	}

	if len(ops) > 0 do key.op0 = ops[0]
	if len(ops) > 1 do key.op1 = ops[1]
	if len(ops) > 2 do key.op2 = ops[2]

	// Canonicalize operand order for commutative instructions
	if is_commutative(inst.opcode) && key.op_count == 2 {
		if key.op0.kind == .Value && key.op1.kind == .Value {
			if key.op0.val > key.op1.val {
				key.op0, key.op1 = key.op1, key.op0
			}
		} else if key.op0.kind != .Value && key.op1.kind == .Value {
			key.op0, key.op1 = key.op1, key.op0
		}
	}

	return key
}

opt_pass_gvn :: proc(mod: ^ir.Module, fn: ^ir.Function, am: ^analysis.Analysis_Manager = nil) -> bool {
	if len(fn.blocks) == 0 || fn.entry_block == support.INVALID_BLOCK do return false

	local_dom: analysis.Dominator_Tree
	dom_ptr: ^analysis.Dominator_Tree = nil
	if am != nil {
		dom_ptr = analysis.analysis_get_dom(am)
	} else {
		analysis.dominator_tree_init(&local_dom, fn, context.temp_allocator)
		dom_ptr = &local_dom
	}

	Available_Expr :: struct {
		key:     GVN_Key,
		val:     support.Value_ID,
		blk_id:  support.Block_ID,
	}

	table := make([dynamic]Available_Expr, context.temp_allocator)
	changed := false

	// Visit blocks in order
	for blk_idx := 0; blk_idx < len(fn.blocks); blk_idx += 1 {
		b_id := support.Block_ID(blk_idx)
		blk := ir.function_get_block(fn, b_id)
		if blk == nil do continue

		curr := blk.first_inst
		for curr != support.INVALID_INST {
			inst := ir.function_get_instruction(fn, curr)
			if inst == nil do break
			next := inst.next

			if inst.result != support.INVALID_VALUE && is_gvn_candidate(inst.opcode) && (.Volatile not_in inst.flags) {
				key := make_gvn_key(fn, inst)

				match_idx := -1
				for i := 0; i < len(table); i += 1 {
					if gvn_key_equal(table[i].key, key) {
						cand := table[i]
						can_replace := false
						if cand.blk_id == b_id {
							can_replace = true
						} else if analysis.dominator_tree_dominates(dom_ptr, cand.blk_id, b_id) {
							can_replace = true
						}

						if can_replace {
							match_idx = i
							break
						}
					}
				}

				if match_idx >= 0 {
					cand := table[match_idx]
					ir.function_replace_all_uses(fn, inst.result, cand.val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
				} else {
					append(&table, Available_Expr{key = key, val = inst.result, blk_id = b_id})
				}
			}

			curr = next
		}
	}

	if changed && am != nil {
		analysis.analysis_invalidate_use_def(am)
	}

	return changed
}
