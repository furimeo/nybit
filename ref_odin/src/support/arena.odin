// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package support

import "core:mem"

Arena :: struct {
	backing:   mem.Arena,
	allocator: mem.Allocator,
	buffer:    []byte,
}

arena_init :: proc(a: ^Arena, size: int = 16 * mem.Megabyte, backing := context.allocator) -> mem.Allocator {
	buf, err := mem.alloc_bytes(size, allocator = backing)
	assert(err == nil, "arena backing allocation failed")
	a.buffer = buf
	mem.arena_init(&a.backing, buf)
	a.allocator = mem.arena_allocator(&a.backing)
	return a.allocator
}

arena_destroy :: proc(a: ^Arena, backing := context.allocator) {
	if len(a.buffer) > 0 {
		mem.free_bytes(a.buffer, allocator = backing)
		a^ = {}
	}
}

arena_reset :: proc(a: ^Arena) {
	mem.arena_free_all(&a.backing)
}
