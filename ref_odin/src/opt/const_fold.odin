// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package opt

import "../analysis"
import "../ir"
import "../support"

get_operand_const_int :: proc(fn: ^ir.Function, op: ir.Operand) -> (i64, bool) {
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

get_operand_const_float :: proc(fn: ^ir.Function, op: ir.Operand) -> (f64, bool) {
	if op.kind == .Imm_Float do return op.imm_float, true
	if op.kind == .Value {
		v := ir.function_get_value(fn, op.val)
		if v != nil && v.kind == .Instruction && v.def != support.INVALID_INST {
			def_inst := ir.function_get_instruction(fn, v.def)
			if def_inst != nil && def_inst.opcode == .Const {
				ops := ir.function_get_operands(fn, def_inst)
				if len(ops) == 1 && ops[0].kind == .Imm_Float {
					return ops[0].imm_float, true
				}
			}
		}
	}
	return 0.0, false
}

get_type_bit_width :: proc(type_id: support.Type_ID) -> u32 {
	switch type_id {
	case ir.TYPE_I8:   return 8
	case ir.TYPE_I16:  return 16
	case ir.TYPE_I32:  return 32
	case ir.TYPE_I64:  return 64
	case ir.TYPE_PTR:  return 64
	case ir.TYPE_F16:  return 16
	case ir.TYPE_F32:  return 32
	case ir.TYPE_F64:  return 64
	}
	return 32
}

mask_to_width :: proc(val: i64, width: u32) -> i64 {
	if width >= 64 do return val
	mask := (i64(1) << uint(width)) - 1
	return val & mask
}

sign_extend_width :: proc(val: i64, width: u32) -> i64 {
	if width >= 64 do return val
	shift := uint(64 - width)
	return (val << shift) >> shift
}

fold_int_unary :: proc(op: ir.Opcode, a: i64, width: u32) -> (i64, bool) {
	#partial switch op {
	case .Neg:
		return sign_extend_width(-a, width), true
	case .Not:
		return sign_extend_width(~a, width), true
	}
	return 0, false
}

fold_int_binary :: proc(op: ir.Opcode, a, b: i64, width: u32) -> (i64, bool) {
	#partial switch op {
	case .Add:
		return sign_extend_width(a + b, width), true
	case .Sub:
		return sign_extend_width(a - b, width), true
	case .Mul:
		return sign_extend_width(a * b, width), true
	case .Div_S:
		if b == 0 do return 0, false
		if width == 64 && a == min(i64) && b == -1 do return 0, false
		return sign_extend_width(a / b, width), true
	case .Div_U:
		ub := u64(mask_to_width(b, width))
		if ub == 0 do return 0, false
		ua := u64(mask_to_width(a, width))
		return sign_extend_width(i64(ua / ub), width), true
	case .Rem_S:
		if b == 0 do return 0, false
		return sign_extend_width(a % b, width), true
	case .Rem_U:
		ub := u64(mask_to_width(b, width))
		if ub == 0 do return 0, false
		ua := u64(mask_to_width(a, width))
		return sign_extend_width(i64(ua % ub), width), true
	case .And:
		return a & b, true
	case .Or:
		return a | b, true
	case .Xor:
		return a ~ b, true
	case .Shl:
		sh := uint(b) % uint(width)
		return sign_extend_width(a << sh, width), true
	case .Shr:
		sh := uint(b) % uint(width)
		ua := u64(mask_to_width(a, width))
		return sign_extend_width(i64(ua >> sh), width), true
	case .Sar:
		sh := uint(b) % uint(width)
		sa := sign_extend_width(a, width)
		return sign_extend_width(sa >> sh, width), true
	case .Rotl:
		sh := uint(b) % uint(width)
		ua := u64(mask_to_width(a, width))
		res := (ua << sh) | (ua >> (uint(width) - sh))
		return sign_extend_width(i64(res), width), true
	case .Rotr:
		sh := uint(b) % uint(width)
		ua := u64(mask_to_width(a, width))
		res := (ua >> sh) | (ua << (uint(width) - sh))
		return sign_extend_width(i64(res), width), true
	}
	return 0, false
}

fold_int_cmp :: proc(op: ir.Opcode, a, b: i64, width: u32) -> (i64, bool) {
	sa := sign_extend_width(a, width)
	sb := sign_extend_width(b, width)
	ua := u64(mask_to_width(a, width))
	ub := u64(mask_to_width(b, width))

	#partial switch op {
	case .Cmp_Eq:   return sa == sb ? 1 : 0, true
	case .Cmp_Ne:   return sa != sb ? 1 : 0, true
	case .Cmp_Lt_S: return sa < sb  ? 1 : 0, true
	case .Cmp_Lt_U: return ua < ub  ? 1 : 0, true
	case .Cmp_Le_S: return sa <= sb ? 1 : 0, true
	case .Cmp_Le_U: return ua <= ub ? 1 : 0, true
	case .Cmp_Gt_S: return sa > sb  ? 1 : 0, true
	case .Cmp_Gt_U: return ua > ub  ? 1 : 0, true
	case .Cmp_Ge_S: return sa >= sb ? 1 : 0, true
	case .Cmp_Ge_U: return ua >= ub ? 1 : 0, true
	}
	return 0, false
}

fold_float_unary :: proc(op: ir.Opcode, a: f64) -> (f64, bool) {
	if op == .FNeg do return -a, true
	return 0.0, false
}

fold_float_binary :: proc(op: ir.Opcode, a, b: f64) -> (f64, bool) {
	#partial switch op {
	case .FAdd: return a + b, true
	case .FSub: return a - b, true
	case .FMul: return a * b, true
	case .FDiv:
		if b == 0.0 do return 0.0, false
		return a / b, true
	}
	return 0.0, false
}

fold_float_cmp :: proc(op: ir.Opcode, a, b: f64) -> (i64, bool) {
	#partial switch op {
	case .FCmp_Eq: return a == b ? 1 : 0, true
	case .FCmp_Ne: return a != b ? 1 : 0, true
	case .FCmp_Lt: return a < b  ? 1 : 0, true
	case .FCmp_Le: return a <= b ? 1 : 0, true
	case .FCmp_Gt: return a > b  ? 1 : 0, true
	case .FCmp_Ge: return a >= b ? 1 : 0, true
	}
	return 0, false
}


opt_pass_const_fold :: proc(mod: ^ir.Module, fn: ^ir.Function, am: ^analysis.Analysis_Manager = nil) -> bool {
	changed := false

	for &inst in fn.instructions {
		if inst.opcode == .None || inst.opcode == .Const || inst.result == support.INVALID_VALUE {
			continue
		}

		res_val := ir.function_get_value(fn, inst.result)
		if res_val == nil do continue

		ops := ir.function_get_operands(fn, &inst)
		width := get_type_bit_width(res_val.type)

		// 1. Unary integer
		if len(ops) == 1 {
			if a, ok := get_operand_const_int(fn, ops[0]); ok {
				if res, folded := fold_int_unary(inst.opcode, a, width); folded {
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}
			if a, ok := get_operand_const_float(fn, ops[0]); ok {
				if res, folded := fold_float_unary(inst.opcode, a); folded {
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_float(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}
		}

		// 2. Binary integer & Comparisons
		if len(ops) == 2 {
			a, a_ok := get_operand_const_int(fn, ops[0])
			b, b_ok := get_operand_const_int(fn, ops[1])

			op0_val := ir.function_get_value(fn, ops[0].val) if ops[0].kind == .Value else nil
			cmp_width := get_type_bit_width(op0_val.type) if op0_val != nil else width

			if a_ok && b_ok {
				if res, folded := fold_int_binary(inst.opcode, a, b, width); folded {
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(res))
					inst.operands.count = 1
					changed = true
					continue
				}
				if res, folded := fold_int_cmp(inst.opcode, a, b, cmp_width); folded {
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}

			fa, fa_ok := get_operand_const_float(fn, ops[0])
			fb, fb_ok := get_operand_const_float(fn, ops[1])
			if fa_ok && fb_ok {
				if res, folded := fold_float_binary(inst.opcode, fa, fb); folded {
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_float(res))
					inst.operands.count = 1
					changed = true
					continue
				}
				if res, folded := fold_float_cmp(inst.opcode, fa, fb); folded {
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}
		}

		// 3. Select with constant condition
		if inst.opcode == .Select && len(ops) == 3 {
			if cond_val, ok := get_operand_const_int(fn, ops[0]); ok {
				chosen_op := ops[1] if cond_val != 0 else ops[2]
				if chosen_op.kind == .Imm_Int {
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, chosen_op)
					inst.operands.count = 1
					changed = true
					continue
				} else if chosen_op.kind == .Imm_Float {
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, chosen_op)
					inst.operands.count = 1
					changed = true
					continue
				} else if chosen_op.kind == .Value {
					ir.function_replace_all_uses(fn, inst.result, chosen_op.val)
					ir.function_remove_instruction(fn, inst.id)
					changed = true
					continue
				}
			}
		}

		// 4. Casts / Conversions
		#partial switch inst.opcode {
		case .Truncate:
			if len(ops) >= 1 {
				if val, ok := get_operand_const_int(fn, ops[0]); ok {
					res := sign_extend_width(val, width)
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}
		case .SExt:
			if len(ops) >= 1 {
				if val, ok := get_operand_const_int(fn, ops[0]); ok {
					src_width: u32 = 32
					if ops[0].kind == .Value {
						v := ir.function_get_value(fn, ops[0].val)
						if v != nil do src_width = get_type_bit_width(v.type)
					}
					res := sign_extend_width(val, src_width)
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}
		case .ZExt:
			if len(ops) >= 1 {
				if val, ok := get_operand_const_int(fn, ops[0]); ok {
					src_width: u32 = 32
					if ops[0].kind == .Value {
						v := ir.function_get_value(fn, ops[0].val)
						if v != nil do src_width = get_type_bit_width(v.type)
					}
					res := mask_to_width(val, src_width)
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}
		case .SIToFP:
			if len(ops) >= 1 {
				if val, ok := get_operand_const_int(fn, ops[0]); ok {
					res := f64(val)
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_float(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}
		case .FPToSI:
			if len(ops) >= 1 {
				if val, ok := get_operand_const_float(fn, ops[0]); ok {
					res := sign_extend_width(i64(val), width)
					inst.opcode = .Const
					ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(res))
					inst.operands.count = 1
					changed = true
					continue
				}
			}
		}
	}

	if changed && am != nil {
		analysis.analysis_invalidate_use_def(am)
	}

	return changed
}
