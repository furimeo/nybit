// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package analysis

import "../ir"
import "../support"
import "core:mem"

CFG_Edge :: struct {
	from: support.Block_ID,
	to:   support.Block_ID,
}

CFG_Info :: struct {
	rpo:          []support.Block_ID,
	post_order:   []support.Block_ID,
	rpo_index:    []int,
	is_reachable: []bool,
	loop_headers: []bool,
	back_edges:   []CFG_Edge,
	allocator:    mem.Allocator,
}

cfg_info_init :: proc(info: ^CFG_Info, fn: ^ir.Function, allocator := context.allocator) {
	n := len(fn.blocks)
	info.allocator = allocator

	if n == 0 || fn.entry_block == support.INVALID_BLOCK {
		info^ = {allocator = allocator}
		return
	}

	info.rpo_index    = make([]int, n, allocator)
	info.is_reachable = make([]bool, n, allocator)
	info.loop_headers = make([]bool, n, allocator)
	for i in 0..<n {
		info.rpo_index[i] = -1
	}

	// 0 = unvisited, 1 = active on DFS stack, 2 = finished
	visited := make([]u8, n, context.temp_allocator)

	Stack_Frame :: struct {
		blk:  support.Block_ID,
		succ: int,
	}
	stack := make([]Stack_Frame, n, context.temp_allocator)
	top := 0

	temp_po := make([]support.Block_ID, n, context.temp_allocator)
	po_count := 0

	temp_back_edges := make([dynamic]CFG_Edge, context.temp_allocator)

	entry_idx := int(fn.entry_block)
	if entry_idx >= 0 && entry_idx < n {
		visited[entry_idx] = 1
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
			if s_idx >= 0 && s_idx < n {
				if visited[s_idx] == 1 {
					append(&temp_back_edges, CFG_Edge{from = frame.blk, to = succ_id})
					info.loop_headers[s_idx] = true
				} else if visited[s_idx] == 0 {
					visited[s_idx] = 1
					stack[top] = {blk = succ_id, succ = 0}
					top += 1
				}
			}
		} else {
			visited[frame.blk] = 2
			temp_po[po_count] = frame.blk
			po_count += 1
			top -= 1
		}
	}

	info.post_order = make([]support.Block_ID, po_count, allocator)
	info.rpo        = make([]support.Block_ID, po_count, allocator)
	for i in 0..<po_count {
		info.post_order[i] = temp_po[i]
		info.rpo[i]        = temp_po[po_count - 1 - i]
		info.rpo_index[info.rpo[i]] = i
		info.is_reachable[info.rpo[i]] = true
	}

	info.back_edges = make([]CFG_Edge, len(temp_back_edges), allocator)
	for i in 0..<len(temp_back_edges) {
		info.back_edges[i] = temp_back_edges[i]
	}
}

cfg_info_destroy :: proc(info: ^CFG_Info) {
	if info.allocator.procedure == nil do return
	delete(info.rpo, info.allocator)
	delete(info.post_order, info.allocator)
	delete(info.rpo_index, info.allocator)
	delete(info.is_reachable, info.allocator)
	delete(info.loop_headers, info.allocator)
	delete(info.back_edges, info.allocator)
	info^ = {}
}

cfg_is_reachable :: #force_inline proc(info: ^CFG_Info, blk: support.Block_ID) -> bool {
	idx := int(blk)
	return idx >= 0 && idx < len(info.is_reachable) && info.is_reachable[idx]
}

cfg_is_loop_header :: #force_inline proc(info: ^CFG_Info, blk: support.Block_ID) -> bool {
	idx := int(blk)
	return idx >= 0 && idx < len(info.loop_headers) && info.loop_headers[idx]
}

cfg_is_back_edge :: proc(info: ^CFG_Info, from, to: support.Block_ID) -> bool {
	for edge in info.back_edges {
		if edge.from == from && edge.to == to do return true
	}
	return false
}

cfg_rpo_index :: #force_inline proc(info: ^CFG_Info, blk: support.Block_ID) -> int {
	idx := int(blk)
	return idx >= 0 && idx < len(info.rpo_index) ? info.rpo_index[idx] : -1
}

cfg_get_successors :: proc(fn: ^ir.Function, blk: support.Block_ID) -> []support.Block_ID {
	b := ir.function_get_block(fn, blk)
	return b != nil ? b.successors[:] : nil
}

cfg_get_predecessors :: proc(fn: ^ir.Function, blk: support.Block_ID) -> []support.Block_ID {
	b := ir.function_get_block(fn, blk)
	return b != nil ? b.predecessors[:] : nil
}
