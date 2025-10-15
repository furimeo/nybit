// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package opt

import "../analysis"
import "../ir"
import "../support"

Lattice_Kind :: enum u8 {
	Top = 0,
	Constant,
	Bottom,
}

Lattice_Val :: struct {
	kind:     Lattice_Kind,
	is_float: bool,
	int_val:  i64,
	flt_val:  f64,
}

lattice_constant_int :: proc(val: i64) -> Lattice_Val {
	return {kind = .Constant, is_float = false, int_val = val}
}

lattice_constant_float :: proc(val: f64) -> Lattice_Val {
	return {kind = .Constant, is_float = true, flt_val = val}
}

lattice_meet :: proc(a, b: Lattice_Val) -> Lattice_Val {
	if a.kind == .Top do return b
	if b.kind == .Top do return a
	if a.kind == .Bottom || b.kind == .Bottom do return {kind = .Bottom}

	if a.is_float != b.is_float do return {kind = .Bottom}
	if a.is_float {
		return a.flt_val == b.flt_val ? a : Lattice_Val{kind = .Bottom}
	} else {
		return a.int_val == b.int_val ? a : Lattice_Val{kind = .Bottom}
	}
}

opt_pass_sccp :: proc(mod: ^ir.Module, fn: ^ir.Function, am: ^analysis.Analysis_Manager = nil) -> bool {
	num_values := len(fn.values)
	num_blocks := len(fn.blocks)
	if num_blocks == 0 || fn.entry_block == support.INVALID_BLOCK do return false

	local_ud: analysis.Use_Def
	ud_ptr: ^analysis.Use_Def = nil
	if am != nil {
		ud_ptr = analysis.analysis_get_use_def(am)
	} else {
		analysis.use_def_init(&local_ud, fn, context.temp_allocator)
		ud_ptr = &local_ud
	}

	lat := make([]Lattice_Val, num_values, context.temp_allocator)
	block_exec := make([]bool, num_blocks, context.temp_allocator)

	// Edges marked executable
	Edge :: struct { from, to: support.Block_ID }
	exec_edges := make([dynamic]Edge, context.temp_allocator)

	is_edge_exec :: proc(edges: ^[dynamic]Edge, from, to: support.Block_ID) -> bool {
		for e in edges {
			if e.from == from && e.to == to do return true
		}
		return false
	}

	mark_edge_exec :: proc(edges: ^[dynamic]Edge, from, to: support.Block_ID) -> bool {
		if is_edge_exec(edges, from, to) do return false
		append(edges, Edge{from = from, to = to})
		return true
	}

	// Initialize lattice values
	for i in 0..<num_values {
		v := ir.function_get_value(fn, support.Value_ID(i))
		if v == nil do continue

		if v.kind == .Argument {
			lat[i] = {kind = .Bottom}
		} else if v.kind == .Instruction && v.def != support.INVALID_INST {
			inst := ir.function_get_instruction(fn, v.def)
			if inst != nil && inst.opcode == .Const {
				ops := ir.function_get_operands(fn, inst)
				if len(ops) == 1 {
					if ops[0].kind == .Imm_Int {
						lat[i] = lattice_constant_int(ops[0].imm_int)
					} else if ops[0].kind == .Imm_Float {
						lat[i] = lattice_constant_float(ops[0].imm_float)
					} else {
						lat[i] = {kind = .Bottom}
					}
				} else {
					lat[i] = {kind = .Bottom}
				}
			} else {
				lat[i] = {kind = .Top}
			}
		} else {
			lat[i] = {kind = .Top}
		}
	}

	cfg_worklist := make([dynamic]support.Block_ID, context.temp_allocator)
	ssa_worklist := make([dynamic]support.Value_ID, context.temp_allocator)

	// Entry block is executable
	block_exec[fn.entry_block] = true
	append(&cfg_worklist, fn.entry_block)

	get_operand_lat :: proc(fn: ^ir.Function, lat: []Lattice_Val, op: ir.Operand) -> Lattice_Val {
		if op.kind == .Imm_Int do return lattice_constant_int(op.imm_int)
		if op.kind == .Imm_Float do return lattice_constant_float(op.imm_float)
		if op.kind == .Value {
			v_idx := int(op.val)
			if v_idx >= 0 && v_idx < len(lat) do return lat[v_idx]
		}
		return {kind = .Bottom}
	}

	eval_instruction :: proc(mod: ^ir.Module, fn: ^ir.Function, inst: ^ir.Instruction, lat: []Lattice_Val, edges: ^[dynamic]Edge) -> Lattice_Val {
		if inst.opcode == .Const {
			ops := ir.function_get_operands(fn, inst)
			if len(ops) == 1 {
				if ops[0].kind == .Imm_Int do return lattice_constant_int(ops[0].imm_int)
				if ops[0].kind == .Imm_Float do return lattice_constant_float(ops[0].imm_float)
			}
			return {kind = .Bottom}
		}

		res_val := ir.function_get_value(fn, inst.result)
		if res_val == nil do return {kind = .Bottom}
		width := get_type_bit_width(res_val.type)
		ops := ir.function_get_operands(fn, inst)

		if inst.opcode == .Phi {
			result := Lattice_Val{kind = .Top}
			for i := 0; i < len(ops); i += 2 {
				if i + 1 < len(ops) && ops[i + 1].kind == .Block {
					pred_blk := ops[i + 1].blk
					if is_edge_exec(edges, pred_blk, inst.block) {
						op_lat := get_operand_lat(fn, lat, ops[i])
						result = lattice_meet(result, op_lat)
					}
				}
			}
			return result
		}

		if len(ops) == 1 {
			op0 := get_operand_lat(fn, lat, ops[0])
			if op0.kind == .Bottom do return {kind = .Bottom}
			if op0.kind == .Top do return {kind = .Top}

			if !op0.is_float {
				if res, ok := fold_int_unary(inst.opcode, op0.int_val, width); ok {
					return lattice_constant_int(res)
				}
			} else {
				if res, ok := fold_float_unary(inst.opcode, op0.flt_val); ok {
					return lattice_constant_float(res)
				}
			}
		}

		if len(ops) == 2 {
			op0 := get_operand_lat(fn, lat, ops[0])
			op1 := get_operand_lat(fn, lat, ops[1])

			if op0.kind == .Bottom || op1.kind == .Bottom do return {kind = .Bottom}
			if op0.kind == .Top || op1.kind == .Top do return {kind = .Top}

			if !op0.is_float && !op1.is_float {
				op0_val := ir.function_get_value(fn, ops[0].val) if ops[0].kind == .Value else nil
				cmp_w := get_type_bit_width(op0_val.type) if op0_val != nil else width

				if res, ok := fold_int_binary(inst.opcode, op0.int_val, op1.int_val, width); ok {
					return lattice_constant_int(res)
				}
				if res, ok := fold_int_cmp(inst.opcode, op0.int_val, op1.int_val, cmp_w); ok {
					return lattice_constant_int(res)
				}
			} else if op0.is_float && op1.is_float {
				if res, ok := fold_float_binary(inst.opcode, op0.flt_val, op1.flt_val); ok {
					return lattice_constant_float(res)
				}
				if res, ok := fold_float_cmp(inst.opcode, op0.flt_val, op1.flt_val); ok {
					return lattice_constant_int(res)
				}
			}
		}

		if inst.opcode == .Select && len(ops) == 3 {
			cond_lat := get_operand_lat(fn, lat, ops[0])
			if cond_lat.kind == .Constant {
				return cond_lat.int_val != 0 ? get_operand_lat(fn, lat, ops[1]) : get_operand_lat(fn, lat, ops[2])
			}
			if cond_lat.kind == .Bottom {
				t_lat := get_operand_lat(fn, lat, ops[1])
				f_lat := get_operand_lat(fn, lat, ops[2])
				return lattice_meet(t_lat, f_lat)
			}
			return {kind = .Top}
		}

		return {kind = .Bottom}
	}

	eval_terminator :: proc(fn: ^ir.Function, blk_id: support.Block_ID, lat: []Lattice_Val, edges: ^[dynamic]Edge, cfg_wl: ^[dynamic]support.Block_ID, block_exec: []bool) {
		blk := ir.function_get_block(fn, blk_id)
		if blk == nil || blk.last_inst == support.INVALID_INST do return

		term := ir.function_get_instruction(fn, blk.last_inst)
		if term == nil do return

		if term.opcode == .Branch {
			ops := ir.function_get_operands(fn, term)
			if len(ops) == 1 && ops[0].kind == .Block {
				target := ops[0].blk
				if mark_edge_exec(edges, blk_id, target) {
					t_idx := int(target)
					if t_idx >= 0 && t_idx < len(block_exec) {
						if !block_exec[t_idx] {
							block_exec[t_idx] = true
							append(cfg_wl, target)
						} else {
							append(cfg_wl, target)
						}
					}
				}
			}
		} else if term.opcode == .Branch_If {
			ops := ir.function_get_operands(fn, term)
			if len(ops) == 3 && ops[1].kind == .Block && ops[2].kind == .Block {
				cond_lat := get_operand_lat(fn, lat, ops[0])
				true_blk  := ops[1].blk
				false_blk := ops[2].blk

				if cond_lat.kind == .Constant {
					target := cond_lat.int_val != 0 ? true_blk : false_blk
					if mark_edge_exec(edges, blk_id, target) {
						t_idx := int(target)
						if t_idx >= 0 && t_idx < len(block_exec) {
							if !block_exec[t_idx] {
								block_exec[t_idx] = true
								append(cfg_wl, target)
							} else {
								append(cfg_wl, target)
							}
						}
					}
				} else if cond_lat.kind == .Bottom {
					for target in ([2]support.Block_ID{true_blk, false_blk}) {
						if mark_edge_exec(edges, blk_id, target) {
							t_idx := int(target)
							if t_idx >= 0 && t_idx < len(block_exec) {
								if !block_exec[t_idx] {
									block_exec[t_idx] = true
									append(cfg_wl, target)
								} else {
									append(cfg_wl, target)
								}
							}
						}
					}
				}
			}
		}
	}

	// Main worklist loop
	for len(cfg_worklist) > 0 || len(ssa_worklist) > 0 {
		if len(cfg_worklist) > 0 {
			blk_id := pop(&cfg_worklist)
			blk := ir.function_get_block(fn, blk_id)
			if blk == nil do continue

			curr := blk.first_inst
			for curr != support.INVALID_INST {
				inst := ir.function_get_instruction(fn, curr)
				if inst == nil do break

				if inst.result != support.INVALID_VALUE {
					r_idx := int(inst.result)
					old_val := lat[r_idx]
					new_val := eval_instruction(mod, fn, inst, lat, &exec_edges)
					if new_val != old_val {
						lat[r_idx] = new_val
						append(&ssa_worklist, inst.result)
					}
				}
				curr = inst.next
			}

			eval_terminator(fn, blk_id, lat, &exec_edges, &cfg_worklist, block_exec)
		} else if len(ssa_worklist) > 0 {
			v_id := pop(&ssa_worklist)
			uses := analysis.use_def_get_uses(ud_ptr, v_id)

			for u in uses {
				user_inst := ir.function_get_instruction(fn, u.inst)
				if user_inst == nil do continue

				b_idx := int(user_inst.block)
				if b_idx >= 0 && b_idx < len(block_exec) && block_exec[b_idx] {
					if user_inst.result != support.INVALID_VALUE {
						r_idx := int(user_inst.result)
						old_val := lat[r_idx]
						new_val := eval_instruction(mod, fn, user_inst, lat, &exec_edges)
						if new_val != old_val {
							lat[r_idx] = new_val
							append(&ssa_worklist, user_inst.result)
						}
					}
					if ir.opcode_is_terminator(user_inst.opcode) {
						eval_terminator(fn, user_inst.block, lat, &exec_edges, &cfg_worklist, block_exec)
					}
				}
			}
		}
	}

	changed := false
	cfg_changed := false

	// Materialize constants
	for &inst in fn.instructions {
		if inst.opcode == .None || inst.opcode == .Const || inst.result == support.INVALID_VALUE {
			continue
		}
		b_idx := int(inst.block)
		if b_idx < 0 || b_idx >= len(block_exec) || !block_exec[b_idx] do continue

		r_idx := int(inst.result)
		if r_idx >= 0 && r_idx < len(lat) && lat[r_idx].kind == .Constant {
			inst.opcode = .Const
			if lat[r_idx].is_float {
				ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_float(lat[r_idx].flt_val))
			} else {
				ir.instruction_replace_operand(fn, inst.id, 0, ir.operand_int(lat[r_idx].int_val))
			}
			inst.operands.count = 1
			changed = true
		}
	}

	// Materialize branches
	for blk_idx := 0; blk_idx < num_blocks; blk_idx += 1 {
		if !block_exec[blk_idx] do continue
		b_id := support.Block_ID(blk_idx)
		blk := ir.function_get_block(fn, b_id)
		if blk == nil || blk.last_inst == support.INVALID_INST do continue

		term := ir.function_get_instruction(fn, blk.last_inst)
		if term != nil && term.opcode == .Branch_If {
			ops := ir.function_get_operands(fn, term)
			if len(ops) == 3 && ops[1].kind == .Block && ops[2].kind == .Block {
				cond_lat := get_operand_lat(fn, lat, ops[0])
				if cond_lat.kind == .Constant {
					taken_blk := ops[1].blk if cond_lat.int_val != 0 else ops[2].blk
					dead_blk  := ops[2].blk if cond_lat.int_val != 0 else ops[1].blk

					term.opcode = .Branch
					ir.instruction_replace_operand(fn, term.id, 0, ir.operand_block(taken_blk))
					term.operands.count = 1

					dead_blk_ptr := ir.function_get_block(fn, dead_blk)
					if dead_blk_ptr != nil {
						ir.block_remove_edge(blk, dead_blk_ptr)
						ir.function_remove_phi_incoming(fn, dead_blk, b_id)
					}

					changed = true
					cfg_changed = true
				}
			}
		}
	}

	if am != nil {
		if cfg_changed {
			analysis.analysis_invalidate_cfg(am)
		} else if changed {
			analysis.analysis_invalidate_use_def(am)
		}
	}

	return changed
}
