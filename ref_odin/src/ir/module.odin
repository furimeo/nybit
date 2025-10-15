// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"
import "core:mem"

Linkage :: enum u8 {
	Default = 0,
	Export,
	Import,
	Internal,
}

Global_Kind :: enum u8 {
	Variable = 0,
	Constant,
	String_Literal,
}

Global :: struct {
	id:        support.Symbol_ID,
	name:      string,
	type:      support.Type_ID,
	kind:      Global_Kind,
	linkage:   Linkage,
	alignment: u32,
	init_data: []byte,
}

Module :: struct {
	name:      string,
	types:     Type_Table,
	functions: [dynamic]Function,
	globals:   [dynamic]Global,
	allocator: mem.Allocator,
}

Context :: struct {
	backing: mem.Allocator,
	arena:   support.Arena,
	module:  Module,
}

context_init :: proc(ctx: ^Context, module_name: string, arena_size := 16 * mem.Megabyte, backing := context.allocator) {
	ctx.backing = backing
	alloc := support.arena_init(&ctx.arena, arena_size, backing)
	ctx.module.name = module_name
	ctx.module.allocator = alloc
	type_table_init(&ctx.module.types, alloc)
	ctx.module.functions = make([dynamic]Function, alloc)
	ctx.module.globals = make([dynamic]Global, alloc)
}

context_destroy :: proc(ctx: ^Context) {
	support.arena_destroy(&ctx.arena, ctx.backing)
	ctx^ = {}
}

module_get_function :: proc(mod: ^Module, id: support.Function_ID) -> ^Function {
	idx := int(id)
	return idx >= 0 && idx < len(mod.functions) ? &mod.functions[idx] : nil
}

module_get_function_by_name :: proc(mod: ^Module, name: string) -> ^Function {
	for &fn in mod.functions {
		if fn.name == name do return &fn
	}
	return nil
}

module_add_function :: proc(
	mod: ^Module,
	name: string,
	ret_type: support.Type_ID,
	call_conv := Calling_Convention.Default,
) -> support.Function_ID {
	id := support.Function_ID(len(mod.functions))
	fn: Function
	function_init(&fn, id, name, ret_type, call_conv, mod.allocator)
	append(&mod.functions, fn)
	return id
}

module_add_global :: proc(
	mod: ^Module,
	name: string,
	type: support.Type_ID,
	kind := Global_Kind.Variable,
	linkage := Linkage.Default,
	alignment: u32 = 0,
) -> support.Symbol_ID {
	id := support.Symbol_ID(len(mod.globals))
	align := alignment > 0 ? alignment : type_align(&mod.types, type)
	append(&mod.globals, Global{
		id        = id,
		name      = name,
		type      = type,
		kind      = kind,
		linkage   = linkage,
		alignment = align,
	})
	return id
}
