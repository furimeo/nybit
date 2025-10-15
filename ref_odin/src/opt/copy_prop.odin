// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package opt

import "../analysis"
import "../ir"
import "../support"

get_copy_source :: proc(fn: ^ir.Function, inst: ^ir.Instruction) -> (support.Value_ID, bool) {
	if inst.result == support.INVALID_VALUE do return support.INVALID_VALUE, false
	ops := ir.function_get_operands(fn, inst)
	res_val := ir.function_get_value(fn, inst.result)
	if res_val == nil do return support.INVALID_VALUE, false

	// 1. Trivial Phi
	if inst.opcode == .Phi && len(ops) >= 2 {
		first_val := support.INVALID_VALUE
		for i := 0; i < len(ops); i += 2 {
			if ops[i].kind == .Value {
				val := ops[i].val
				// Ignore self-references in loop phis
				if val == inst.result do continue
				if first_val == support.INVALID_VALUE {
					first_val = val
				} else if first_val != val {
					return support.INVALID_VALUE, false
				}
			} else {
				return support.INVALID_VALUE, false
			}
		}
		if first_val != support.INVALID_VALUE {
			return first_val, true
		}
	}

	// 2. Select with identical true and false values
	if inst.opcode == .Select && len(ops) == 3 {
		if ops[1].kind == .Value && ops[2].kind == .Value && ops[1].val == ops[2].val {
			return ops[1].val, true
		}
	}

	// 3. Identity Cast / Bitcast / Truncate / Extend of same type or bitwidth
	#partial switch inst.opcode {
	case .Bitcast, .Cast:
		if len(ops) >= 1 && ops[0].kind == .Value {
			src_v := ir.function_get_value(fn, ops[0].val)
			if src_v != nil && src_v.type == res_val.type {
				return ops[0].val, true
			}
		}
	case .Truncate, .SExt, .ZExt:
		if len(ops) >= 1 && ops[0].kind == .Value {
			src_v := ir.function_get_value(fn, ops[0].val)
			if src_v != nil && get_type_bit_width(src_v.type) == get_type_bit_width(res_val.type) {
				return ops[0].val, true
			}
		}
	}

	return support.INVALID_VALUE, false
}

opt_pass_copy_prop :: proc(mod: ^ir.Module, fn: ^ir.Function, am: ^analysis.Analysis_Manager = nil) -> bool {
	changed := false
	num_values := len(fn.values)
	if num_values == 0 do return false

	copy_map := make([]support.Value_ID, num_values, context.temp_allocator)
	for i in 0..<num_values {
		copy_map[i] = support.INVALID_VALUE
	}

	// Step 1: Detect all copies
	for &inst in fn.instructions {
		if inst.opcode == .None do continue
		if src, ok := get_copy_source(fn, &inst); ok {
			copy_map[inst.result] = src
		}
	}

	// Step 2: Path compression for chains of copies (e.g. a -> b -> c)
	resolve_copy :: proc(copy_map: []support.Value_ID, v: support.Value_ID) -> support.Value_ID {
		curr := v
		visited := 0
		max_depth := 64
		for visited < max_depth {
			c_idx := int(curr)
			if c_idx < 0 || c_idx >= len(copy_map) do break
			next := copy_map[c_idx]
			if next == support.INVALID_VALUE || next == curr do break
			curr = next
			visited += 1
		}
		return curr
	}

	for i in 0..<num_values {
		if copy_map[i] != support.INVALID_VALUE {
			copy_map[i] = resolve_copy(copy_map, copy_map[i])
		}
	}

	// Step 3: Replace uses and remove copy instructions
	for &inst in fn.instructions {
		if inst.opcode == .None do continue

		// Check if this instruction is a copy itself
		r_idx := int(inst.result)
		if r_idx >= 0 && r_idx < num_values && copy_map[r_idx] != support.INVALID_VALUE {
			target := copy_map[r_idx]
			ir.function_replace_all_uses(fn, inst.result, target)
			ir.function_remove_instruction(fn, inst.id)
			changed = true
			continue
		}

		// Also check any operands that can be replaced with their copy source
		ops := ir.function_get_operands(fn, &inst)
		for idx := 0; idx < len(ops); idx += 1 {
			if ops[idx].kind == .Value {
				v_idx := int(ops[idx].val)
				if v_idx >= 0 && v_idx < num_values && copy_map[v_idx] != support.INVALID_VALUE {
					ir.instruction_replace_operand(fn, inst.id, idx, ir.operand_value(copy_map[v_idx]))
					changed = true
				}
			}
		}
	}

	if changed && am != nil {
		analysis.analysis_invalidate_use_def(am)
	}

	return changed
}
