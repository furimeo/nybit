// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"

Block :: struct {
	id:           support.Block_ID,
	name:         string,
	first_inst:   support.Inst_ID,
	last_inst:    support.Inst_ID,
	inst_count:   u32,
	predecessors: [dynamic]support.Block_ID,
	successors:   [dynamic]support.Block_ID,
}

block_add_edge :: proc(pred_blk: ^Block, succ_blk: ^Block) {
	has_succ := false
	for s in pred_blk.successors {
		if s == succ_blk.id {
			has_succ = true
			break
		}
	}
	if !has_succ {
		append(&pred_blk.successors, succ_blk.id)
	}

	has_pred := false
	for p in succ_blk.predecessors {
		if p == pred_blk.id {
			has_pred = true
			break
		}
	}
	if !has_pred {
		append(&succ_blk.predecessors, pred_blk.id)
	}
}

block_remove_edge :: proc(pred_blk: ^Block, succ_blk: ^Block) {
	for i := 0; i < len(pred_blk.successors); i += 1 {
		if pred_blk.successors[i] == succ_blk.id {
			unordered_remove(&pred_blk.successors, i)
			break
		}
	}
	for i := 0; i < len(succ_blk.predecessors); i += 1 {
		if succ_blk.predecessors[i] == pred_blk.id {
			unordered_remove(&succ_blk.predecessors, i)
			break
		}
	}
}

