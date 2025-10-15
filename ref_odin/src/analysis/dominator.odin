// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package analysis

import "../ir"
import "../support"
import "core:mem"

Dominator_Tree :: struct {
	idom:        []support.Block_ID,
	rpo:         []int,
	entry:       support.Block_ID,

	children:    []support.Block_ID,
	child_start: []u32,
	child_count: []u32,

	frontiers:   []support.Block_ID,
	df_start:    []u32,
	df_count:    []u32,

	allocator:   mem.Allocator,
}

dominator_tree_init :: proc(dt: ^Dominator_Tree, fn: ^ir.Function, allocator := context.allocator) {
	n := len(fn.blocks)
	dt.allocator = allocator
	dt.entry     = fn.entry_block

	if n == 0 || fn.entry_block == support.INVALID_BLOCK {
		dt^ = {entry = fn.entry_block, allocator = allocator}
		return
	}

	dt.idom = make([]support.Block_ID, n, allocator)
	dt.rpo  = make([]int, n, allocator)
	for i in 0..<n {
		dt.idom[i] = support.INVALID_BLOCK
		dt.rpo[i]  = -1
	}

	// Iterative DFS for Post-Order traversal
	visited := make([]bool, n, context.temp_allocator)
	post_order := make([]support.Block_ID, n, context.temp_allocator)
	po_count := 0

	Stack_Frame :: struct {
		blk:  support.Block_ID,
		succ: int,
	}
	stack := make([]Stack_Frame, n, context.temp_allocator)
	top := 0

	entry_idx := int(fn.entry_block)
	if entry_idx >= 0 && entry_idx < n {
		visited[entry_idx] = true
		stack[0] = {blk = fn.entry_block, succ = 0}
		top = 1
	}

	for top > 0 {
		frame := &stack[top - 1]
		blk := ir.function_get_block(fn, frame.blk)

		if blk != nil && frame.succ < len(blk.successors) {
			succ_id := blk.successors[frame.succ]
			frame.succ += 1
			s_idx := int(succ_id)
			if s_idx >= 0 && s_idx < n && !visited[s_idx] {
				visited[s_idx] = true
				stack[top] = {blk = succ_id, succ = 0}
				top += 1
			}
		} else {
			post_order[po_count] = frame.blk
			po_count += 1
			top -= 1
		}
	}

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

	// Cooper-Harvey-Kennedy fixed-point iteration
	changed := true
	for changed {
		changed = false
		for i in 1..<po_count {
			b := post_order[po_count - 1 - i]
			blk := ir.function_get_block(fn, b)
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

	// Dominator tree children
	dt.child_start = make([]u32, n, allocator)
	dt.child_count = make([]u32, n, allocator)

	for b in 0..<n {
		b_id := support.Block_ID(b)
		if b_id != fn.entry_block && dt.idom[b] != support.INVALID_BLOCK {
			p := int(dt.idom[b])
			if p >= 0 && p < n {
				dt.child_count[p] += 1
			}
		}
	}

	total_children := 0
	for i in 0..<n {
		dt.child_start[i] = u32(total_children)
		total_children += int(dt.child_count[i])
	}

	dt.children = make([]support.Block_ID, total_children, allocator)
	child_cursor := make([]u32, n, context.temp_allocator)

	for b in 0..<n {
		b_id := support.Block_ID(b)
		if b_id != fn.entry_block && dt.idom[b] != support.INVALID_BLOCK {
			p := int(dt.idom[b])
			if p >= 0 && p < n {
				pos := dt.child_start[p] + child_cursor[p]
				dt.children[pos] = b_id
				child_cursor[p] += 1
			}
		}
	}

	// Dominance Frontiers (Cytron et al.)
	dt.df_start = make([]u32, n, allocator)
	dt.df_count = make([]u32, n, allocator)

	last_visited := make([]int, n, context.temp_allocator)
	for i in 0..<n do last_visited[i] = -1

	for b in 0..<n {
		b_id := support.Block_ID(b)
		blk := ir.function_get_block(fn, b_id)
		if blk == nil || len(blk.predecessors) < 2 do continue

		idom_b := dt.idom[b]
		for p in blk.predecessors {
			runner := p
			for runner != idom_b && runner != support.INVALID_BLOCK && int(runner) < n {
				r_idx := int(runner)
				if last_visited[r_idx] != b {
					last_visited[r_idx] = b
					dt.df_count[r_idx] += 1
				}
				if runner == fn.entry_block || runner == dt.idom[runner] do break
				runner = dt.idom[runner]
			}
		}
	}

	total_df := 0
	for i in 0..<n {
		dt.df_start[i] = u32(total_df)
		total_df += int(dt.df_count[i])
	}

	dt.frontiers = make([]support.Block_ID, total_df, allocator)
	df_cursor := make([]u32, n, context.temp_allocator)
	for i in 0..<n do last_visited[i] = -1

	for b in 0..<n {
		b_id := support.Block_ID(b)
		blk := ir.function_get_block(fn, b_id)
		if blk == nil || len(blk.predecessors) < 2 do continue

		idom_b := dt.idom[b]
		for p in blk.predecessors {
			runner := p
			for runner != idom_b && runner != support.INVALID_BLOCK && int(runner) < n {
				r_idx := int(runner)
				if last_visited[r_idx] != b {
					last_visited[r_idx] = b
					pos := dt.df_start[r_idx] + df_cursor[r_idx]
					dt.frontiers[pos] = b_id
					df_cursor[r_idx] += 1
				}
				if runner == fn.entry_block || runner == dt.idom[runner] do break
				runner = dt.idom[runner]
			}
		}
	}
}

dominator_tree_destroy :: proc(dt: ^Dominator_Tree) {
	if dt.allocator.procedure == nil do return
	delete(dt.idom, dt.allocator)
	delete(dt.rpo, dt.allocator)
	delete(dt.children, dt.allocator)
	delete(dt.child_start, dt.allocator)
	delete(dt.child_count, dt.allocator)
	delete(dt.frontiers, dt.allocator)
	delete(dt.df_start, dt.allocator)
	delete(dt.df_count, dt.allocator)
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

dominator_tree_strictly_dominates :: #force_inline proc(dt: ^Dominator_Tree, a, b: support.Block_ID) -> bool {
	return a != b && dominator_tree_dominates(dt, a, b)
}

dominator_tree_get_idom :: #force_inline proc(dt: ^Dominator_Tree, blk: support.Block_ID) -> support.Block_ID {
	idx := int(blk)
	return idx >= 0 && idx < len(dt.idom) ? dt.idom[idx] : support.INVALID_BLOCK
}

dominator_tree_get_children :: proc(dt: ^Dominator_Tree, blk: support.Block_ID) -> []support.Block_ID {
	idx := int(blk)
	if idx < 0 || idx >= len(dt.child_count) do return nil
	count := dt.child_count[idx]
	if count == 0 do return nil
	start := dt.child_start[idx]
	return dt.children[start : start + count]
}

dominator_tree_get_frontier :: proc(dt: ^Dominator_Tree, blk: support.Block_ID) -> []support.Block_ID {
	idx := int(blk)
	if idx < 0 || idx >= len(dt.df_count) do return nil
	count := dt.df_count[idx]
	if count == 0 do return nil
	start := dt.df_start[idx]
	return dt.frontiers[start : start + count]
}
