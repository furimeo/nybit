// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"
import "core:fmt"
import "core:mem"
import "core:strings"

Type_ID :: support.Type_ID

Type_Kind :: enum u8 {
	Void,
	I8,
	I16,
	I32,
	I64,
	I128,
	F16,
	F32,
	F64,
	Ptr,
	Vector,
	Function,
}

TYPE_VOID :: Type_ID(0)
TYPE_I8   :: Type_ID(1)
TYPE_I16  :: Type_ID(2)
TYPE_I32  :: Type_ID(3)
TYPE_I64  :: Type_ID(4)
TYPE_I128 :: Type_ID(5)
TYPE_F16  :: Type_ID(6)
TYPE_F32  :: Type_ID(7)
TYPE_F64  :: Type_ID(8)
TYPE_PTR  :: Type_ID(9)

BUILTIN_TYPE_COUNT :: 10

Vector_Type :: struct {
	elem:  Type_ID,
	lanes: u16,
}

Function_Type :: struct {
	ret_type:    Type_ID,
	param_types: []Type_ID,
	is_variadic: bool,
}

Type_Entry :: struct {
	kind:  Type_Kind,
	name:  string,
	size:  u32,
	align: u32,
	extra: union {
		Vector_Type,
		Function_Type,
	},
}

Type_Table :: struct {
	entries:   [dynamic]Type_Entry,
	allocator: mem.Allocator,
}

type_table_init :: proc(tt: ^Type_Table, allocator := context.allocator) {
	tt.allocator = allocator
	tt.entries = make([dynamic]Type_Entry, allocator)
	reserve(&tt.entries, 32)

	append(&tt.entries, Type_Entry{kind = .Void, name = "void", size = 0,  align = 1})
	append(&tt.entries, Type_Entry{kind = .I8,   name = "i8",   size = 1,  align = 1})
	append(&tt.entries, Type_Entry{kind = .I16,  name = "i16",  size = 2,  align = 2})
	append(&tt.entries, Type_Entry{kind = .I32,  name = "i32",  size = 4,  align = 4})
	append(&tt.entries, Type_Entry{kind = .I64,  name = "i64",  size = 8,  align = 8})
	append(&tt.entries, Type_Entry{kind = .I128, name = "i128", size = 16, align = 16})
	append(&tt.entries, Type_Entry{kind = .F16,  name = "f16",  size = 2,  align = 2})
	append(&tt.entries, Type_Entry{kind = .F32,  name = "f32",  size = 4,  align = 4})
	append(&tt.entries, Type_Entry{kind = .F64,  name = "f64",  size = 8,  align = 8})
	append(&tt.entries, Type_Entry{kind = .Ptr,  name = "ptr",  size = 8,  align = 8})
}

type_table_get :: proc(tt: ^Type_Table, id: Type_ID) -> ^Type_Entry {
	idx := int(id)
	return idx >= 0 && idx < len(tt.entries) ? &tt.entries[idx] : nil
}

type_table_add_vector :: proc(tt: ^Type_Table, elem: Type_ID, lanes: u16) -> Type_ID {
	for entry, idx in tt.entries {
		if entry.kind == .Vector {
			if v, ok := entry.extra.(Vector_Type); ok && v.elem == elem && v.lanes == lanes {
				return Type_ID(idx)
			}
		}
	}

	elem_info := type_table_get(tt, elem)
	assert(elem_info != nil, "invalid vector element type")

	id := Type_ID(len(tt.entries))
	name := fmt.aprintf("v%d%s", lanes, elem_info.name, allocator = tt.allocator)
	append(&tt.entries, Type_Entry{
		kind  = .Vector,
		name  = name,
		size  = elem_info.size * u32(lanes),
		align = elem_info.align,
		extra = Vector_Type{elem = elem, lanes = lanes},
	})
	return id
}

type_table_add_function :: proc(
	tt: ^Type_Table,
	ret: Type_ID,
	params: []Type_ID,
	is_variadic := false,
) -> Type_ID {
	owned_params := make([]Type_ID, len(params), tt.allocator)
	copy(owned_params, params)

	sb: strings.Builder
	strings.builder_init(&sb, tt.allocator)
	strings.write_byte(&sb, '(')
	for p, i in owned_params {
		if i > 0 do strings.write_string(&sb, ", ")
		p_info := type_table_get(tt, p)
		strings.write_string(&sb, p_info != nil ? p_info.name : "?")
	}
	if is_variadic {
		if len(owned_params) > 0 do strings.write_string(&sb, ", ...")
		else do strings.write_string(&sb, "...")
	}
	strings.write_string(&sb, ") -> ")
	ret_info := type_table_get(tt, ret)
	strings.write_string(&sb, ret_info != nil ? ret_info.name : "?")

	id := Type_ID(len(tt.entries))
	append(&tt.entries, Type_Entry{
		kind  = .Function,
		name  = strings.to_string(sb),
		size  = 8,
		align = 8,
		extra = Function_Type{
			ret_type    = ret,
			param_types = owned_params,
			is_variadic = is_variadic,
		},
	})
	return id
}

type_size :: proc(tt: ^Type_Table, id: Type_ID) -> u32 {
	e := type_table_get(tt, id)
	return e != nil ? e.size : 0
}

type_align :: proc(tt: ^Type_Table, id: Type_ID) -> u32 {
	e := type_table_get(tt, id)
	return e != nil ? e.align : 1
}

type_name :: proc(tt: ^Type_Table, id: Type_ID) -> string {
	e := type_table_get(tt, id)
	return e != nil ? e.name : "<unknown>"
}

type_is_int :: proc(tt: ^Type_Table, id: Type_ID) -> bool {
	e := type_table_get(tt, id)
	if e == nil do return false
	#partial switch e.kind {
	case .I8, .I16, .I32, .I64, .I128: return true
	}
	return false
}

type_is_float :: proc(tt: ^Type_Table, id: Type_ID) -> bool {
	e := type_table_get(tt, id)
	if e == nil do return false
	#partial switch e.kind {
	case .F16, .F32, .F64: return true
	}
	return false
}

type_is_ptr :: proc(tt: ^Type_Table, id: Type_ID) -> bool {
	e := type_table_get(tt, id)
	return e != nil && e.kind == .Ptr
}
