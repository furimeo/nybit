// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package analysis

import "../ir"
import "../support"
import "core:mem"

Liveness_Info :: struct {
	num_blocks:      int,
	words_per_block: int,
	live_in:         []u64,
	live_out:        []u64,
	allocator:       mem.Allocator,
}

bitset_set :: #force_inline proc(bits: []u64, words_per_block: int, blk: support.Block_ID, val: support.Value_ID) {
	b := int(blk)
	v := int(val)
	word_idx := b * words_per_block + (v / 64)
	if word_idx >= 0 && word_idx < len(bits) {
		bits[word_idx] |= (u64(1) << uint(v % 64))
	}
}

bitset_test :: #force_inline proc(bits: []u64, words_per_block: int, blk: support.Block_ID, val: support.Value_ID) -> bool {
	b := int(blk)
	v := int(val)
	word_idx := b * words_per_block + (v / 64)
	if word_idx >= 0 && word_idx < len(bits) {
		return (bits[word_idx] & (u64(1) << uint(v % 64))) != 0
	}
	return false
}

liveness_init :: proc(liv: ^Liveness_Info, fn: ^ir.Function, allocator := context.allocator) {
	b_count := len(fn.blocks)
	v_count := len(fn.values)
	liv.allocator = allocator

	if b_count == 0 {
		liv^ = {allocator = allocator}
		return
	}

	words := (v_count + 63) / 64
	if words == 0 do words = 1

	liv.num_blocks      = b_count
	liv.words_per_block = words
	total_words         := b_count * words

	liv.live_in  = make([]u64, total_words, allocator)
	liv.live_out = make([]u64, total_words, allocator)

	use_gen  := make([]u64, total_words, context.temp_allocator)
	def_kill := make([]u64, total_words, context.temp_allocator)
	phi_defs := make([]u64, total_words, context.temp_allocator)

	// Pass 1: compute local use_gen, def_kill, and phi_defs
	for b in 0..<b_count {
		b_id := support.Block_ID(b)
		blk := ir.function_get_block(fn, b_id)
		if blk == nil do continue

		inst_id := blk.first_inst
		for inst_id != support.INVALID_INST {
			inst := ir.function_get_instruction(fn, inst_id)
			if inst == nil || inst.opcode == .None {
				if inst != nil do inst_id = inst.next
				else do break
				continue
			}

			if inst.opcode == .Phi {
				if inst.result != support.INVALID_VALUE && int(inst.result) < v_count {
					bitset_set(phi_defs, words, b_id, inst.result)
					bitset_set(def_kill, words, b_id, inst.result)
				}
			} else {
				ops := ir.function_get_operands(fn, inst)
				for op in ops {
					if op.kind == .Value && int(op.val) < v_count {
						if !bitset_test(def_kill, words, b_id, op.val) {
							bitset_set(use_gen, words, b_id, op.val)
						}
					}
				}
				if inst.result != support.INVALID_VALUE && int(inst.result) < v_count {
					bitset_set(def_kill, words, b_id, inst.result)
				}
			}
			inst_id = inst.next
		}
	}

	cur_out := make([]u64, words, context.temp_allocator)

	// Fixed-point backward dataflow iteration
	changed := true
	for changed {
		changed = false
		for b := b_count - 1; b >= 0; b -= 1 {
			b_id := support.Block_ID(b)
			blk := ir.function_get_block(fn, b_id)
			if blk == nil do continue

			b_offset := b * words
			for w in 0..<words do cur_out[w] = 0

			for succ_id in blk.successors {
				s := int(succ_id)
				if s < 0 || s >= b_count do continue
				s_offset := s * words

				// Non-phi live-in from successor
				for w in 0..<words {
					in_val := liv.live_in[s_offset + w]
					pdef   := phi_defs[s_offset + w]
					cur_out[w] |= (in_val & ~pdef)
				}

				// Phi values associated with predecessor b_id
				s_blk := ir.function_get_block(fn, succ_id)
				if s_blk != nil {
					phi_id := s_blk.first_inst
					for phi_id != support.INVALID_INST {
						phi_inst := ir.function_get_instruction(fn, phi_id)
						if phi_inst == nil || phi_inst.opcode != .Phi do break
						phi_ops := ir.function_get_operands(fn, phi_inst)
						for k := 0; k < len(phi_ops); k += 2 {
							if k + 1 < len(phi_ops) && phi_ops[k + 1].kind == .Block && phi_ops[k + 1].blk == b_id {
								if phi_ops[k].kind == .Value && int(phi_ops[k].val) < v_count {
									v := int(phi_ops[k].val)
									cur_out[v / 64] |= (u64(1) << uint(v % 64))
								}
							}
						}
						phi_id = phi_inst.next
					}
				}
			}

			// Update live_out
			for w in 0..<words {
				liv.live_out[b_offset + w] = cur_out[w]
			}

			// In[b] = use_gen[b] | (live_out[b] & ~def_kill[b])
			for w in 0..<words {
				old_in := liv.live_in[b_offset + w]
				kill   := def_kill[b_offset + w]
				gen    := use_gen[b_offset + w]
				new_in := gen | (cur_out[w] & ~kill)
				if new_in != old_in {
					liv.live_in[b_offset + w] = new_in
					changed = true
				}
			}
		}
	}
}

liveness_destroy :: proc(liv: ^Liveness_Info) {
	if liv.allocator.procedure == nil do return
	delete(liv.live_in, liv.allocator)
	delete(liv.live_out, liv.allocator)
	liv^ = {}
}

liveness_is_live_in :: #force_inline proc(liv: ^Liveness_Info, blk: support.Block_ID, val: support.Value_ID) -> bool {
	b := int(blk)
	v := int(val)
	if b < 0 || b >= liv.num_blocks || liv.words_per_block == 0 do return false
	word_idx := b * liv.words_per_block + (v / 64)
	if word_idx >= len(liv.live_in) do return false
	bit := u64(1) << uint(v % 64)
	return (liv.live_in[word_idx] & bit) != 0
}

liveness_is_live_out :: #force_inline proc(liv: ^Liveness_Info, blk: support.Block_ID, val: support.Value_ID) -> bool {
	b := int(blk)
	v := int(val)
	if b < 0 || b >= liv.num_blocks || liv.words_per_block == 0 do return false
	word_idx := b * liv.words_per_block + (v / 64)
	if word_idx >= len(liv.live_out) do return false
	bit := u64(1) << uint(v % 64)
	return (liv.live_out[word_idx] & bit) != 0
}

liveness_get_live_in_slice :: proc(liv: ^Liveness_Info, blk: support.Block_ID) -> []u64 {
	b := int(blk)
	if b < 0 || b >= liv.num_blocks || liv.words_per_block == 0 do return nil
	start := b * liv.words_per_block
	return liv.live_in[start : start + liv.words_per_block]
}

liveness_get_live_out_slice :: proc(liv: ^Liveness_Info, blk: support.Block_ID) -> []u64 {
	b := int(blk)
	if b < 0 || b >= liv.num_blocks || liv.words_per_block == 0 do return nil
	start := b * liv.words_per_block
	return liv.live_out[start : start + liv.words_per_block]
}
