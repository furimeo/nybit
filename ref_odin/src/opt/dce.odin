// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package opt

import "../analysis"
import "../ir"
import "../support"

inst_has_side_effects :: proc(inst: ^ir.Instruction) -> bool {
	if ir.opcode_is_terminator(inst.opcode) do return true
	if .Volatile in inst.flags do return true

	#partial switch inst.opcode {
	case .Store, .Atomic_Store, .Atomic_Rmw, .Atomic_Cmpxchg, .Fence:
		return true
	case .Call, .Call_Indirect:
		return true
	case .Trap:
		return true
	}

	return false
}

opt_pass_dce :: proc(mod: ^ir.Module, fn: ^ir.Function, am: ^analysis.Analysis_Manager = nil) -> bool {
	num_values := len(fn.values)
	if num_values == 0 do return false

	local_ud: analysis.Use_Def
	ud_ptr: ^analysis.Use_Def = nil
	if am != nil {
		ud_ptr = analysis.analysis_get_use_def(am)
	} else {
		analysis.use_def_init(&local_ud, fn, context.temp_allocator)
		ud_ptr = &local_ud
	}

	active_use_count := make([]u32, num_values, context.temp_allocator)
	for i in 0..<num_values {
		active_use_count[i] = analysis.use_def_use_count(ud_ptr, support.Value_ID(i))
	}

	dead_worklist := make([dynamic]support.Inst_ID, context.temp_allocator)

	for &inst in fn.instructions {
		if inst.opcode == .None do continue
		if inst.result == support.INVALID_VALUE do continue
		if inst_has_side_effects(&inst) do continue

		r_idx := int(inst.result)
		if r_idx >= 0 && r_idx < num_values && active_use_count[r_idx] == 0 {
			append(&dead_worklist, inst.id)
		}
	}

	changed := false

	for len(dead_worklist) > 0 {
		inst_id := pop(&dead_worklist)
		inst := ir.function_get_instruction(fn, inst_id)
		if inst == nil || inst.opcode == .None do continue
		if inst_has_side_effects(inst) do continue

		ops := ir.function_get_operands(fn, inst)
		for op in ops {
			if op.kind == .Value {
				v_idx := int(op.val)
				if v_idx >= 0 && v_idx < num_values {
					if active_use_count[v_idx] > 0 {
						active_use_count[v_idx] -= 1
						if active_use_count[v_idx] == 0 {
							def_id := analysis.use_def_get_def(ud_ptr, op.val)
							if def_id != support.INVALID_INST {
								def_inst := ir.function_get_instruction(fn, def_id)
								if def_inst != nil && !inst_has_side_effects(def_inst) {
									append(&dead_worklist, def_id)
								}
							}
						}
					}
				}
			}
		}

		ir.function_remove_instruction(fn, inst_id)
		changed = true
	}

	if changed && am != nil {
		analysis.analysis_invalidate_use_def(am)
	}

	return changed
}
