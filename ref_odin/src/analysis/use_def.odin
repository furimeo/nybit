// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package analysis

import "../ir"
import "../support"
import "core:mem"

Use :: struct {
	inst:   support.Inst_ID,
	op_idx: u16,
}

Use_Def :: struct {
	def_inst:  []support.Inst_ID,
	use_start: []u32,
	use_count: []u32,
	uses:      []Use,
	allocator: mem.Allocator,
}

use_def_init :: proc(ud: ^Use_Def, fn: ^ir.Function, allocator := context.allocator) {
	n := len(fn.values)
	ud.allocator = allocator

	if n == 0 {
		ud^ = {allocator = allocator}
		return
	}

	ud.def_inst  = make([]support.Inst_ID, n, allocator)
	ud.use_start = make([]u32, n, allocator)
	ud.use_count = make([]u32, n, allocator)
	for i in 0..<n {
		ud.def_inst[i] = support.INVALID_INST
	}

	temp_counts := make([]u32, n, context.temp_allocator)

	for &inst in fn.instructions {
		if inst.opcode == .None do continue
		if inst.result != support.INVALID_VALUE {
			r_idx := int(inst.result)
			if r_idx >= 0 && r_idx < n {
				ud.def_inst[r_idx] = inst.id
			}
		}

		ops := ir.function_get_operands(fn, &inst)
		for op in ops {
			if op.kind == .Value {
				v_idx := int(op.val)
				if v_idx >= 0 && v_idx < n {
					temp_counts[v_idx] += 1
				}
			}
		}
	}

	total_uses := 0
	for i in 0..<n {
		ud.use_start[i] = u32(total_uses)
		ud.use_count[i] = temp_counts[i]
		total_uses += int(temp_counts[i])
	}

	ud.uses = make([]Use, total_uses, allocator)
	cursor := make([]u32, n, context.temp_allocator)

	for &inst in fn.instructions {
		if inst.opcode == .None do continue
		ops := ir.function_get_operands(fn, &inst)
		for op, idx in ops {
			if op.kind == .Value {
				v_idx := int(op.val)
				if v_idx >= 0 && v_idx < n {
					pos := ud.use_start[v_idx] + cursor[v_idx]
					ud.uses[pos] = Use{inst = inst.id, op_idx = u16(idx)}
					cursor[v_idx] += 1
				}
			}
		}
	}
}

use_def_destroy :: proc(ud: ^Use_Def) {
	if ud.allocator.procedure == nil do return
	delete(ud.def_inst, ud.allocator)
	delete(ud.use_start, ud.allocator)
	delete(ud.use_count, ud.allocator)
	delete(ud.uses, ud.allocator)
	ud^ = {}
}

use_def_get_def :: #force_inline proc(ud: ^Use_Def, val: support.Value_ID) -> support.Inst_ID {
	idx := int(val)
	return idx >= 0 && idx < len(ud.def_inst) ? ud.def_inst[idx] : support.INVALID_INST
}

use_def_get_uses :: #force_inline proc(ud: ^Use_Def, val: support.Value_ID) -> []Use {
	idx := int(val)
	if idx < 0 || idx >= len(ud.use_count) do return nil
	count := ud.use_count[idx]
	if count == 0 do return nil
	start := ud.use_start[idx]
	return ud.uses[start : start + count]
}

use_def_use_count :: #force_inline proc(ud: ^Use_Def, val: support.Value_ID) -> u32 {
	idx := int(val)
	return idx >= 0 && idx < len(ud.use_count) ? ud.use_count[idx] : 0
}

use_def_has_uses :: #force_inline proc(ud: ^Use_Def, val: support.Value_ID) -> bool {
	return use_def_use_count(ud, val) > 0
}

use_def_has_single_use :: #force_inline proc(ud: ^Use_Def, val: support.Value_ID) -> bool {
	return use_def_use_count(ud, val) == 1
}
