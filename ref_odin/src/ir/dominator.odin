// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"
import "core:mem"

// Cooper-Harvey-Kennedy iterative dominance algorithm
// Không đệ quy, dùng flat array stack và RPO traversal.

Dominator_Tree :: struct {
	idom:      []support.Block_ID,
	rpo:       []int,
	entry:     support.Block_ID,
	allocator: mem.Allocator,
}

dominator_tree_init :: proc(dt: ^Dominator_Tree, fn: ^Function, allocator := context.temp_allocator) {
	n := len(fn.blocks)
	dt.allocator = allocator
	dt.entry = fn.entry_block

	if n == 0 || fn.entry_block == support.INVALID_BLOCK {
		dt^ = {}
		return
	}

	dt.idom = make([]support.Block_ID, n, allocator)
	dt.rpo  = make([]int, n, allocator)
	for i in 0..<n {
		dt.idom[i] = support.INVALID_BLOCK
		dt.rpo[i]  = -1
	}

	// Flat iterative DFS stack for post-order traversal
	visited := make([]bool, n, context.temp_allocator)
	post_order := make([]support.Block_ID, n, context.temp_allocator)
	po_count := 0

	Stack_Frame :: struct {
		blk:  support.Block_ID,
		succ: int,
	}
	stack := make([]Stack_Frame, n, context.temp_allocator)
	top := 0

	stack[0] = {blk = fn.entry_block, succ = 0}
	top = 1
	visited[fn.entry_block] = true

	for top > 0 {
		frame := &stack[top - 1]
		blk := function_get_block(fn, frame.blk)

		if blk != nil && frame.succ < len(blk.successors) {
			succ_id := blk.successors[frame.succ]
			frame.succ += 1
			if int(succ_id) < n && !visited[succ_id] {
				visited[succ_id] = true
				stack[top] = {blk = succ_id, succ = 0}
				top += 1
			}
		} else {
			post_order[po_count] = frame.blk
			po_count += 1
			top -= 1
		}
	}

	// Gán thứ tự Reverse Post-Order (RPO)
	for i in 0..<po_count {
		b := post_order[po_count - 1 - i]
		dt.rpo[b] = i
	}

	dt.idom[fn.entry_block] = fn.entry_block

	intersect :: #force_inline proc(dt: ^Dominator_Tree, b1, b2: support.Block_ID) -> support.Block_ID {
		f1, f2 := b1, b2
		for f1 != f2 {
			for dt.rpo[f1] > dt.rpo[f2] do f1 = dt.idom[f1]
			for dt.rpo[f2] > dt.rpo[f1] do f2 = dt.idom[f2]
		}
		return f1
	}

	changed := true
	for changed {
		changed = false
		for i in 1..<po_count {
			b := post_order[po_count - 1 - i]
			blk := function_get_block(fn, b)
			if blk == nil do continue

			new_idom := support.INVALID_BLOCK
			for p in blk.predecessors {
				if dt.idom[p] != support.INVALID_BLOCK {
					new_idom = p
					break
				}
			}
			if new_idom == support.INVALID_BLOCK do continue

			for p in blk.predecessors {
				if p != new_idom && dt.idom[p] != support.INVALID_BLOCK {
					new_idom = intersect(dt, p, new_idom)
				}
			}

			if dt.idom[b] != new_idom {
				dt.idom[b] = new_idom
				changed = true
			}
		}
	}
}

dominator_tree_destroy :: proc(dt: ^Dominator_Tree) {
	if dt.allocator.procedure == nil do return
	delete(dt.idom, dt.allocator)
	delete(dt.rpo, dt.allocator)
	dt^ = {}
}

dominator_tree_dominates :: #force_inline proc(dt: ^Dominator_Tree, a, b: support.Block_ID) -> bool {
	if a == b do return true
	if dt.idom == nil || int(a) >= len(dt.idom) || int(b) >= len(dt.idom) do return false
	if dt.rpo[a] < 0 || dt.rpo[b] < 0 do return false

	curr := b
	for curr != a && curr != dt.entry && dt.idom[curr] != support.INVALID_BLOCK {
		parent := dt.idom[curr]
		if parent == curr do break
		curr = parent
	}
	return curr == a
}

