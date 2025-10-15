// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package opt

import "../analysis"
import "../ir"
import "../support"

opt_pass_cfg_simplify :: proc(mod: ^ir.Module, fn: ^ir.Function, am: ^analysis.Analysis_Manager = nil) -> bool {
	changed_any := false
	max_iters := 4

	for iter := 0; iter < max_iters; iter += 1 {
		changed := false

		// 1. Constant branch folding & Redundant branch_if
		for blk_idx := 0; blk_idx < len(fn.blocks); blk_idx += 1 {
			b_id := support.Block_ID(blk_idx)
			blk := ir.function_get_block(fn, b_id)
			if blk == nil || blk.last_inst == support.INVALID_INST do continue

			term := ir.function_get_instruction(fn, blk.last_inst)
			if term == nil || term.opcode != .Branch_If do continue

			ops := ir.function_get_operands(fn, term)
			if len(ops) != 3 || ops[1].kind != .Block || ops[2].kind != .Block do continue

			true_blk := ops[1].blk
			false_blk := ops[2].blk

			// Redundant branch: @branch_if %cond, .B, .B -> @branch .B
			if true_blk == false_blk {
				term.opcode = .Branch
				ir.instruction_replace_operand(fn, term.id, 0, ir.operand_block(true_blk))
				term.operands.count = 1

				if len(blk.successors) > 1 {
					clear(&blk.successors)
					append(&blk.successors, true_blk)
				}
				tgt_blk := ir.function_get_block(fn, true_blk)
				if tgt_blk != nil {
					pred_count := 0
					for i := 0; i < len(tgt_blk.predecessors); i += 1 {
						if tgt_blk.predecessors[i] == b_id {
							pred_count += 1
							if pred_count > 1 {
								unordered_remove(&tgt_blk.predecessors, i)
								i -= 1
							}
						}
					}
				}
				changed = true
				continue
			}

			// Constant branch
			if c_val, ok := get_operand_const_int(fn, ops[0]); ok {
				taken := true_blk if c_val != 0 else false_blk
				dead  := false_blk if c_val != 0 else true_blk

				term.opcode = .Branch
				ir.instruction_replace_operand(fn, term.id, 0, ir.operand_block(taken))
				term.operands.count = 1

				dead_blk := ir.function_get_block(fn, dead)
				if dead_blk != nil {
					ir.block_remove_edge(blk, dead_blk)
					ir.function_remove_phi_incoming(fn, dead, b_id)
				}
				changed = true
			}
		}

		// 2. Single-predecessor Phi simplification
		for &blk in fn.blocks {
			if len(blk.predecessors) == 1 {
				curr := blk.first_inst
				for curr != support.INVALID_INST {
					inst := ir.function_get_instruction(fn, curr)
					if inst == nil || inst.opcode != .Phi do break
					next := inst.next

					ops := ir.function_get_operands(fn, inst)
					if len(ops) >= 2 && ops[0].kind == .Value {
						ir.function_replace_all_uses(fn, inst.result, ops[0].val)
						ir.function_remove_instruction(fn, inst.id)
						changed = true
					}
					curr = next
				}
			}
		}

		// 3. Trampoline block forwarding (empty block with only @branch)
		for blk_idx := 0; blk_idx < len(fn.blocks); blk_idx += 1 {
			b_id := support.Block_ID(blk_idx)
			if b_id == fn.entry_block do continue

			blk := ir.function_get_block(fn, b_id)
			if blk == nil || blk.inst_count != 1 || blk.first_inst == support.INVALID_INST do continue

			term := ir.function_get_instruction(fn, blk.first_inst)
			if term == nil || term.opcode != .Branch do continue

			ops := ir.function_get_operands(fn, term)
			if len(ops) != 1 || ops[0].kind != .Block do continue
			target_id := ops[0].blk
			if target_id == b_id do continue

			target_blk := ir.function_get_block(fn, target_id)
			if target_blk == nil do continue

			// Redirect predecessors of b_id to target_id
			for len(blk.predecessors) > 0 {
				pred_id := blk.predecessors[0]
				pred_blk := ir.function_get_block(fn, pred_id)
				if pred_blk == nil do break

				p_term := ir.function_get_instruction(fn, pred_blk.last_inst)
				if p_term != nil {
					p_ops := ir.function_get_operands(fn, p_term)
					for op_i := 0; op_i < len(p_ops); op_i += 1 {
						if p_ops[op_i].kind == .Block && p_ops[op_i].blk == b_id {
							ir.instruction_replace_operand(fn, p_term.id, op_i, ir.operand_block(target_id))
						}
					}
				}

				ir.block_remove_edge(pred_blk, blk)
				ir.block_add_edge(pred_blk, target_blk)

				// In target_blk, update any phi with incoming b_id to incoming pred_id
				phi_id := target_blk.first_inst
				for phi_id != support.INVALID_INST {
					phi_inst := ir.function_get_instruction(fn, phi_id)
					if phi_inst == nil || phi_inst.opcode != .Phi do break
					phi_ops := ir.function_get_operands(fn, phi_inst)
					for k := 1; k < len(phi_ops); k += 2 {
						if phi_ops[k].kind == .Block && phi_ops[k].blk == b_id {
							ir.instruction_replace_operand(fn, phi_inst.id, k, ir.operand_block(pred_id))
						}
					}
					phi_id = phi_inst.next
				}
				changed = true
			}
		}

		// 4. Block Merging: if block A branches to B and B has only predecessor A
		for blk_idx := 0; blk_idx < len(fn.blocks); blk_idx += 1 {
			b_id := support.Block_ID(blk_idx)
			blk := ir.function_get_block(fn, b_id)
			if blk == nil || blk.inst_count == 0 do continue

			if len(blk.successors) == 1 {
				succ_id := blk.successors[0]
				if succ_id != b_id && succ_id != fn.entry_block {
					succ_blk := ir.function_get_block(fn, succ_id)
					if succ_blk != nil && len(succ_blk.predecessors) == 1 {
						if ir.function_merge_blocks(fn, b_id, succ_id) {
							changed = true
						}
					}
				}
			}
		}

		// 5. Unreachable Block Elimination
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

				for s in curr_blk.successors {
					s_idx := int(s)
					if s_idx >= 0 && s_idx < n_blocks && !reachable[s_idx] {
						reachable[s_idx] = true
						queue[tail] = s
						tail += 1
					}
				}
			}

			for b := 0; b < n_blocks; b += 1 {
				b_id := support.Block_ID(b)
				if b_id != fn.entry_block && !reachable[b] {
					blk := ir.function_get_block(fn, b_id)
					if blk != nil && (blk.inst_count > 0 || len(blk.successors) > 0 || len(blk.predecessors) > 0) {
						// Remove outgoing edges to successors and clean their phis
						for s in blk.successors {
							s_blk := ir.function_get_block(fn, s)
							if s_blk != nil {
								ir.block_remove_edge(blk, s_blk)
								ir.function_remove_phi_incoming(fn, s, b_id)
							}
						}
						ir.function_remove_block(fn, b_id)
						changed = true
					}
				}
			}
		}

		if !changed do break
		changed_any = true
	}

	if changed_any && am != nil {
		analysis.analysis_invalidate_cfg(am)
	}

	return changed_any
}
