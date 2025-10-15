// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"
import "core:fmt"
import "core:strings"

dump_operand :: proc(sb: ^strings.Builder, mod: ^Module, fn: ^Function, op: Operand) {
	#partial switch op.kind {
	case .Value:
		val := function_get_value(fn, op.val)
		if val != nil && len(val.name) > 0 {
			fmt.sbprintf(sb, "%%%s", val.name)
		} else {
			fmt.sbprintf(sb, "%%%d", op.val)
		}
	case .Block:
		blk := function_get_block(fn, op.blk)
		if blk != nil && len(blk.name) > 0 {
			fmt.sbprintf(sb, ".%s", blk.name)
		} else {
			fmt.sbprintf(sb, ".b%d", op.blk)
		}
	case .Imm_Int:
		fmt.sbprintf(sb, "%d", op.imm_int)
	case .Imm_Float:
		fmt.sbprintf(sb, "%f", op.imm_float)
	case .Function:
		target := module_get_function(mod, op.fn_id)
		if target != nil {
			fmt.sbprintf(sb, "@%s", target.name)
		} else {
			fmt.sbprintf(sb, "@fn_%d", op.fn_id)
		}
	case .Symbol:
		fmt.sbprintf(sb, "@sym_%d", op.sym_id)
	case .Type:
		strings.write_string(sb, type_name(&mod.types, op.type_id))
	case .None:
		strings.write_string(sb, "none")
	}
}

dump_instruction :: proc(sb: ^strings.Builder, mod: ^Module, fn: ^Function, inst: ^Instruction) {
	strings.write_string(sb, "    ")

	if inst.result != support.INVALID_VALUE {
		val := function_get_value(fn, inst.result)
		if val != nil && len(val.name) > 0 {
			fmt.sbprintf(sb, "%%%s = ", val.name)
		} else {
			fmt.sbprintf(sb, "%%%d = ", inst.result)
		}
	}

	strings.write_string(sb, opcode_name(inst.opcode))

	operands := function_get_operands(fn, inst)
	for op, idx in operands {
		strings.write_string(sb, idx == 0 ? " " : ", ")
		dump_operand(sb, mod, fn, op)
	}

	strings.write_string(sb, ";\n")
}

dump_function :: proc(sb: ^strings.Builder, mod: ^Module, fn: ^Function) {
	fmt.sbprintf(sb, "@function %s(", fn.name)
	for pt, idx in fn.signature.param_types {
		if idx > 0 do strings.write_string(sb, ", ")
		p_name := idx < len(fn.signature.param_names) ? fn.signature.param_names[idx] : ""
		if len(p_name) > 0 {
			fmt.sbprintf(sb, "%%%s: %s", p_name, type_name(&mod.types, pt))
		} else {
			fmt.sbprintf(sb, "%%%d: %s", idx, type_name(&mod.types, pt))
		}
	}
	if fn.signature.is_variadic {
		if len(fn.signature.param_types) > 0 do strings.write_string(sb, ", ...")
		else do strings.write_string(sb, "...")
	}
	fmt.sbprintf(sb, ") -> %s;\n", type_name(&mod.types, fn.signature.return_type))

	for &blk, idx in fn.blocks {
		if idx > 0 do strings.write_byte(sb, '\n')
		fmt.sbprintf(sb, ".%s;\n", blk.name)

		curr := blk.first_inst
		for curr != support.INVALID_INST {
			inst := function_get_instruction(fn, curr)
			if inst == nil do break
			dump_instruction(sb, mod, fn, inst)
			curr = inst.next
		}
	}

	strings.write_string(sb, ";;\n")
}

dump_module :: proc(mod: ^Module, allocator := context.allocator) -> string {
	sb: strings.Builder
	strings.builder_init(&sb, allocator)

	for &g in mod.globals {
		fmt.sbprintf(&sb, "@global %s: %s;\n", g.name, type_name(&mod.types, g.type))
	}
	if len(mod.globals) > 0 && len(mod.functions) > 0 {
		strings.write_byte(&sb, '\n')
	}

	for &fn, idx in mod.functions {
		if idx > 0 do strings.write_byte(&sb, '\n')
		dump_function(&sb, mod, &fn)
	}

	return strings.to_string(sb)
}
