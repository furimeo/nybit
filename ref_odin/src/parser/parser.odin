// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package parser

import "../ir"
import "../support"
import "core:fmt"
import "core:mem"
import "core:strings"

Diagnostic :: struct {
	message: string,
	line:    int,
	col:     int,
}

Parser :: struct {
	lexer:       Lexer,
	curr:        Token,
	peek:        Token,
	module:      ^ir.Module,
	builder:     ir.Builder,
	diagnostics: [dynamic]Diagnostic,
	allocator:   mem.Allocator,
}

parser_init :: proc(p: ^Parser, mod: ^ir.Module, src: string, allocator := context.temp_allocator) {
	p.module = mod
	p.allocator = allocator
	p.diagnostics = make([dynamic]Diagnostic, allocator)
	ir.builder_init(&p.builder, mod)
	lexer_init(&p.lexer, src)

	// Prime lookahead (curr and peek)
	p.curr = lexer_next(&p.lexer)
	p.peek = lexer_next(&p.lexer)
}

@(private="file")
advance :: proc(p: ^Parser) -> Token {
	prev := p.curr
	p.curr = p.peek
	p.peek = lexer_next(&p.lexer)
	return prev
}

@(private="file")
report_error :: proc(p: ^Parser, msg: string, line := -1, col := -1) {
	err_line := line >= 0 ? line : p.curr.line
	err_col := col >= 0 ? col : p.curr.col
	append(&p.diagnostics, Diagnostic{
		message = strings.clone(msg, p.allocator),
		line    = err_line,
		col     = err_col,
	})
}

@(private="file")
expect :: proc(p: ^Parser, kind: Token_Kind) -> bool {
	if p.curr.kind == kind {
		advance(p)
		return true
	}
	report_error(p, fmt.aprintf("expected %v, got %v '%s'", kind, p.curr.kind, p.curr.text, allocator = p.allocator))
	return false
}

@(private="file")
lookup_type :: proc(mod: ^ir.Module, name: string) -> support.Type_ID {
	switch name {
	case "void": return ir.TYPE_VOID
	case "i8":   return ir.TYPE_I8
	case "i16":  return ir.TYPE_I16
	case "i32":  return ir.TYPE_I32
	case "i64":  return ir.TYPE_I64
	case "i128": return ir.TYPE_I128
	case "f16":  return ir.TYPE_F16
	case "f32":  return ir.TYPE_F32
	case "f64":  return ir.TYPE_F64
	case "ptr":  return ir.TYPE_PTR
	}
	// Vector types: v4i32, v8i32, v4f32, etc.
	if len(name) > 2 && name[0] == 'v' {
		// e.g. v4i32
		elem_idx := 1
		lanes: u16 = 0
		for elem_idx < len(name) && (name[elem_idx] >= '0' && name[elem_idx] <= '9') {
			lanes = lanes * 10 + u16(name[elem_idx] - '0')
			elem_idx += 1
		}
		if lanes > 0 && elem_idx < len(name) {
			elem_t := lookup_type(mod, name[elem_idx:])
			if elem_t != support.INVALID_TYPE {
				return ir.type_table_add_vector(&mod.types, elem_t, lanes)
			}
		}
	}
	return support.INVALID_TYPE
}

@(private="file")
lookup_opcode :: proc(name: string) -> ir.Opcode {
	switch name {
	case "const":          return .Const
	case "const_null":     return .Const_Null
	case "add":            return .Add
	case "sub":            return .Sub
	case "mul":            return .Mul
	case "div.s", "div":   return .Div_S
	case "div.u":          return .Div_U
	case "rem.s", "rem":   return .Rem_S
	case "rem.u":          return .Rem_U
	case "neg":            return .Neg
	case "fadd":           return .FAdd
	case "fsub":           return .FSub
	case "fmul":           return .FMul
	case "fdiv":           return .FDiv
	case "frem":           return .FRem
	case "fneg":           return .FNeg
	case "and":            return .And
	case "or":             return .Or
	case "xor":            return .Xor
	case "not":            return .Not
	case "shl":            return .Shl
	case "shr":            return .Shr
	case "sar":            return .Sar
	case "rotl":           return .Rotl
	case "rotr":           return .Rotr
	case "cmp.eq":         return .Cmp_Eq
	case "cmp.ne":         return .Cmp_Ne
	case "cmp.lt.s", "cmp.lt": return .Cmp_Lt_S
	case "cmp.lt.u":       return .Cmp_Lt_U
	case "cmp.le.s", "cmp.le": return .Cmp_Le_S
	case "cmp.le.u":       return .Cmp_Le_U
	case "cmp.gt.s", "cmp.gt": return .Cmp_Gt_S
	case "cmp.gt.u":       return .Cmp_Gt_U
	case "cmp.ge.s", "cmp.ge": return .Cmp_Ge_S
	case "cmp.ge.u":       return .Cmp_Ge_U
	case "fcmp.eq":        return .FCmp_Eq
	case "fcmp.ne":        return .FCmp_Ne
	case "fcmp.lt":        return .FCmp_Lt
	case "fcmp.le":        return .FCmp_Le
	case "fcmp.gt":        return .FCmp_Gt
	case "fcmp.ge":        return .FCmp_Ge
	case "select":         return .Select
	case "addr":           return .Addr
	case "addr_offset":    return .Addr_Offset
	case "load":           return .Load
	case "store":          return .Store
	case "stack_slot":     return .Stack_Slot
	case "stack_addr":     return .Stack_Addr
	case "atomic_load":    return .Atomic_Load
	case "atomic_store":   return .Atomic_Store
	case "atomic_rmw":     return .Atomic_Rmw
	case "atomic_cmpxchg": return .Atomic_Cmpxchg
	case "fence":          return .Fence
	case "cast":           return .Cast
	case "extend":         return .Extend
	case "truncate":       return .Truncate
	case "bitcast":        return .Bitcast
	case "sext":           return .SExt
	case "zext":           return .ZExt
	case "fext":           return .FExt
	case "ftrunc":         return .FTrunc
	case "sitofp":         return .SIToFP
	case "uitofp":         return .UIToFP
	case "fptosi":         return .FPToSI
	case "fptoui":         return .FPToUI
	case "vadd":           return .VAdd
	case "vsub":           return .VSub
	case "vmul":           return .VMul
	case "vdiv":           return .VDiv
	case "vand":           return .VAnd
	case "vor":            return .VOr
	case "vxor":           return .VXor
	case "vshuffle":       return .VShuffle
	case "branch", "@branch":       return .Branch
	case "branch_if", "@branch_if": return .Branch_If
	case "switch", "@switch":       return .Switch
	case "return", "@return":       return .Return
	case "trap", "@trap":           return .Trap
	case "call":           return .Call
	case "call_indirect":  return .Call_Indirect
	case "phi":            return .Phi
	}
	return .None
}

Function_Parser_State :: struct {
	fn_id:          support.Function_ID,
	val_map:        map[string]support.Value_ID,
	blk_map:        map[string]support.Block_ID,
	defined_blocks: map[support.Block_ID]bool,
	current_block:  support.Block_ID,
}

@(private="file")
get_or_create_block :: proc(p: ^Parser, s: ^Function_Parser_State, name: string) -> support.Block_ID {
	if id, exists := s.blk_map[name]; exists {
		return id
	}
	fn := ir.module_get_function(p.module, s.fn_id)
	id := ir.function_create_block(fn, name)
	s.blk_map[name] = id
	return id
}

@(private="file")
parse_function :: proc(p: ^Parser) -> bool {
	// Consumed '@' already in Directive token, text is "function"
	fn_tok := advance(p)
	if fn_tok.kind != .Directive || fn_tok.text != "function" {
		report_error(p, "expected '@function'")
		return false
	}

	if p.curr.kind != .Ident {
		report_error(p, "expected function name identifier")
		return false
	}
	fn_name := advance(p).text

	if !expect(p, .LParen) do return false

	ret_type := ir.TYPE_VOID

	fn_id := ir.module_add_function(p.module, fn_name, ret_type)
	p.builder.cur_fn = fn_id
	p.builder.cur_block = support.INVALID_BLOCK

	s: Function_Parser_State
	s.fn_id = fn_id
	s.val_map = make(map[string]support.Value_ID, 16, p.allocator)
	s.blk_map = make(map[string]support.Block_ID, 16, p.allocator)
	s.defined_blocks = make(map[support.Block_ID]bool, 16, p.allocator)
	s.current_block = support.INVALID_BLOCK

	// Parse parameters: (%a: i32, %b: i32) or (%a, %b)
	for p.curr.kind != .RParen && p.curr.kind != .EOF {
		if p.curr.kind != .Value {
			report_error(p, "expected SSA parameter value (e.g. %a)")
			return false
		}
		param_name := advance(p).text
		p_type := ir.TYPE_I32 // default if untyped

		if p.curr.kind == .Colon {
			advance(p)
			if p.curr.kind != .Ident {
				report_error(p, "expected type after ':'")
				return false
			}
			t_name := advance(p).text
			p_type = lookup_type(p.module, t_name)
			if p_type == support.INVALID_TYPE {
				report_error(p, fmt.aprintf("unknown type '%s'", t_name, allocator = p.allocator))
				return false
			}
		}

		val_id := ir.builder_add_param(&p.builder, p_type, param_name)
		s.val_map[param_name] = val_id

		if p.curr.kind == .Comma {
			advance(p)
		} else if p.curr.kind != .RParen {
			report_error(p, "expected ',' or ')' in parameter list")
			return false
		}
	}

	if !expect(p, .RParen) do return false

	// Optional return type: -> i32
	if p.curr.kind == .Arrow {
		advance(p)
		if p.curr.kind != .Ident {
			report_error(p, "expected return type after '->'")
			return false
		}
		ret_name := advance(p).text
		ret_type = lookup_type(p.module, ret_name)
		if ret_type == support.INVALID_TYPE {
			report_error(p, fmt.aprintf("unknown return type '%s'", ret_name, allocator = p.allocator))
			return false
		}
		fn := ir.module_get_function(p.module, fn_id)
		fn.signature.return_type = ret_type
	}

	if !expect(p, .Semicolon) do return false

	// Implicit entry block if not explicitly declared immediately
	entry_id := get_or_create_block(p, &s, "entry")
	s.current_block = entry_id
	p.builder.cur_block = entry_id

	// Parse block & instruction statements until ';;'
	for p.curr.kind != .Double_Semicolon && p.curr.kind != .EOF {
		// 1. Block Label: .label;
		if p.curr.kind == .Block_Label {
			blk_name := advance(p).text
			if !expect(p, .Semicolon) do return false

			blk_id := get_or_create_block(p, &s, blk_name)
			s.defined_blocks[blk_id] = true
			s.current_block = blk_id
			p.builder.cur_block = blk_id
			continue
		}

		// 2. Extra statement separator: ';'
		if p.curr.kind == .Semicolon {
			advance(p)
			continue
		}

		// Any instruction before explicit block label marks entry as defined
		s.defined_blocks[s.current_block] = true

		// 3. SSA Value Assignment: %result = opcode operands;
		if p.curr.kind == .Value {
			res_name := advance(p).text
			if !expect(p, .Equal) do return false

			if _, exists := s.val_map[res_name]; exists {
				report_error(p, fmt.aprintf("SSA value '%%%s' is already defined", res_name, allocator = p.allocator))
				return false
			}

			if p.curr.kind != .Ident && p.curr.kind != .Directive {
				report_error(p, "expected opcode after '='")
				return false
			}

			op_tok := advance(p)
			op := lookup_opcode(op_tok.text)
			if op == .None {
				report_error(p, fmt.aprintf("unknown opcode '%s'", op_tok.text, allocator = p.allocator), op_tok.line, op_tok.col)
				return false
			}

			// Parse operands
			ops := make([dynamic]ir.Operand, p.allocator)
			res_type := ir.TYPE_I32

			for p.curr.kind != .Semicolon && p.curr.kind != .EOF {
				if p.curr.kind == .Value {
					v_name := advance(p).text
					v_id, ok := s.val_map[v_name]
					if !ok {
						report_error(p, fmt.aprintf("unknown SSA value '%%%s'", v_name, allocator = p.allocator))
						return false
					}
					append(&ops, ir.operand_value(v_id))
					if op != .Const && op != .Const_Null {
						v_ptr := ir.function_get_value(ir.module_get_function(p.module, fn_id), v_id)
						if v_ptr != nil {
							res_type = v_ptr.type
						}
					}
				} else if p.curr.kind == .Block_Label {
					b_name := advance(p).text
					b_id := get_or_create_block(p, &s, b_name)
					append(&ops, ir.operand_block(b_id))
				} else if p.curr.kind == .Directive {
					dir_name := advance(p).text
					target_fn := ir.module_get_function_by_name(p.module, dir_name)
					if target_fn != nil {
						append(&ops, ir.operand_function(target_fn.id))
					} else {
						append(&ops, ir.operand_function(support.INVALID_FUNCTION))
					}
				} else if p.curr.kind == .Int {
					append(&ops, ir.operand_int(advance(p).int_val))
				} else if p.curr.kind == .Float {
					append(&ops, ir.operand_float(advance(p).float_val))
					res_type = ir.TYPE_F64
				} else if p.curr.kind == .Ident {
					ident_name := advance(p).text
					t_id := lookup_type(p.module, ident_name)
					if t_id != support.INVALID_TYPE {
						append(&ops, ir.operand_type(t_id))
						res_type = t_id
					} else {
						report_error(p, fmt.aprintf("unexpected identifier operand '%s'", ident_name, allocator = p.allocator))
						return false
					}
				} else {
					report_error(p, fmt.aprintf("unexpected operand token %v '%s'", p.curr.kind, p.curr.text, allocator = p.allocator))
					return false
				}

				if p.curr.kind == .Comma {
					advance(p)
				} else if p.curr.kind != .Semicolon {
					report_error(p, "expected ',' or ';' after operand")
					return false
				}
			}

			if !expect(p, .Semicolon) do return false

			// Compare operations return boolean/i8
			#partial switch op {
			case .Cmp_Eq, .Cmp_Ne, .Cmp_Lt_S, .Cmp_Lt_U, .Cmp_Le_S, .Cmp_Le_U, .Cmp_Gt_S, .Cmp_Gt_U, .Cmp_Ge_S, .Cmp_Ge_U,
			     .FCmp_Eq, .FCmp_Ne, .FCmp_Lt, .FCmp_Le, .FCmp_Gt, .FCmp_Ge:
				res_type = ir.TYPE_I8
			}

			fn := ir.module_get_function(p.module, fn_id)
			res_id := ir.function_create_value(fn, res_type, .Instruction, support.INVALID_INST, 0, res_name)
			s.val_map[res_name] = res_id

			ir.function_append_instruction(fn, s.current_block, op, res_id, ops[:])
			continue
		}

		// 4. Directive Instructions: @return, @branch, @branch_if, @trap
		if p.curr.kind == .Directive {
			dir_tok := advance(p)
			dir := dir_tok.text

			if dir == "return" {
				val := support.INVALID_VALUE
				if p.curr.kind == .Value {
					v_name := advance(p).text
					v_id, ok := s.val_map[v_name]
					if !ok {
						report_error(p, fmt.aprintf("unknown SSA value '%%%s' in @return", v_name, allocator = p.allocator))
						return false
					}
					val = v_id
				}
				if !expect(p, .Semicolon) do return false
				ir.builder_ret(&p.builder, val)
				continue
			}

			if dir == "branch" {
				if p.curr.kind != .Block_Label {
					report_error(p, "expected block label after @branch")
					return false
				}
				tgt_name := advance(p).text
				tgt_id := get_or_create_block(p, &s, tgt_name)
				if !expect(p, .Semicolon) do return false
				ir.builder_branch(&p.builder, tgt_id)
				continue
			}

			if dir == "branch_if" {
				if p.curr.kind != .Value {
					report_error(p, "expected condition SSA value after @branch_if")
					return false
				}
				c_name := advance(p).text
				cond_id, ok := s.val_map[c_name]
				if !ok {
					report_error(p, fmt.aprintf("unknown SSA condition '%%%s' in @branch_if", c_name, allocator = p.allocator))
					return false
				}

				if !expect(p, .Comma) do return false
				if p.curr.kind != .Block_Label {
					report_error(p, "expected true block label in @branch_if")
					return false
				}
				true_name := advance(p).text
				true_id := get_or_create_block(p, &s, true_name)

				if !expect(p, .Comma) do return false
				if p.curr.kind != .Block_Label {
					report_error(p, "expected false block label in @branch_if")
					return false
				}
				false_name := advance(p).text
				false_id := get_or_create_block(p, &s, false_name)

				if !expect(p, .Semicolon) do return false
				ir.builder_branch_if(&p.builder, cond_id, true_id, false_id)
				continue
			}

			if dir == "trap" {
				if !expect(p, .Semicolon) do return false
				ir.builder_trap(&p.builder)
				continue
			}

			report_error(p, fmt.aprintf("unknown directive instruction '@%s'", dir, allocator = p.allocator), dir_tok.line, dir_tok.col)
			return false
		}

		// 5. Plain instruction without result (e.g. store %ptr, %val; call @callee, ...)
		if p.curr.kind == .Ident {
			op_tok := advance(p)
			op := lookup_opcode(op_tok.text)
			if op == .None {
				report_error(p, fmt.aprintf("unknown instruction '%s'", op_tok.text, allocator = p.allocator), op_tok.line, op_tok.col)
				return false
			}

			ops := make([dynamic]ir.Operand, p.allocator)
			for p.curr.kind != .Semicolon && p.curr.kind != .EOF {
				if p.curr.kind == .Value {
					v_name := advance(p).text
					v_id, ok := s.val_map[v_name]
					if !ok {
						report_error(p, fmt.aprintf("unknown SSA value '%%%s'", v_name, allocator = p.allocator))
						return false
					}
					append(&ops, ir.operand_value(v_id))
				} else if p.curr.kind == .Block_Label {
					b_name := advance(p).text
					b_id := get_or_create_block(p, &s, b_name)
					append(&ops, ir.operand_block(b_id))
				} else if p.curr.kind == .Directive {
					dir_name := advance(p).text
					target_fn := ir.module_get_function_by_name(p.module, dir_name)
					if target_fn != nil {
						append(&ops, ir.operand_function(target_fn.id))
					} else {
						append(&ops, ir.operand_function(support.INVALID_FUNCTION))
					}
				} else if p.curr.kind == .Int {
					append(&ops, ir.operand_int(advance(p).int_val))
				} else if p.curr.kind == .Float {
					append(&ops, ir.operand_float(advance(p).float_val))
				} else {
					report_error(p, fmt.aprintf("unexpected operand token %v '%s'", p.curr.kind, p.curr.text, allocator = p.allocator))
					return false
				}

				if p.curr.kind == .Comma {
					advance(p)
				} else if p.curr.kind != .Semicolon {
					report_error(p, "expected ',' or ';' after operand")
					return false
				}
			}

			if !expect(p, .Semicolon) do return false

			fn := ir.module_get_function(p.module, fn_id)
			ir.function_append_instruction(fn, s.current_block, op, support.INVALID_VALUE, ops[:])
			continue
		}

		report_error(p, fmt.aprintf("unexpected token in function body: %v '%s'", p.curr.kind, p.curr.text, allocator = p.allocator))
		return false
	}

	if !expect(p, .Double_Semicolon) do return false

	// Post-function validation: check that all referenced blocks were defined
	fn := ir.module_get_function(p.module, fn_id)
	for name, blk_id in s.blk_map {
		blk := ir.function_get_block(fn, blk_id)
		if blk == nil || blk.first_inst == support.INVALID_INST {
			report_error(p, fmt.aprintf("block '.%s' referenced but has no instructions", name, allocator = p.allocator))
			return false
		}
	}

	return true
}

@(private="file")
parse_global :: proc(p: ^Parser) -> bool {
	// Consumed '@' in Directive token: text is "global"
	advance(p)

	if p.curr.kind != .Ident {
		report_error(p, "expected global variable name")
		return false
	}
	g_name := advance(p).text

	if !expect(p, .Colon) do return false

	if p.curr.kind != .Ident {
		report_error(p, "expected type after ':'")
		return false
	}
	t_name := advance(p).text
	t_id := lookup_type(p.module, t_name)
	if t_id == support.INVALID_TYPE {
		report_error(p, fmt.aprintf("unknown type '%s' in global declaration", t_name, allocator = p.allocator))
		return false
	}

	if !expect(p, .Semicolon) do return false

	ir.module_add_global(p.module, g_name, t_id)
	return true
}

parse_module :: proc(p: ^Parser) -> bool {
	for p.curr.kind != .EOF {
		if p.curr.kind == .Directive {
			if p.curr.text == "function" {
				if !parse_function(p) do return false
				continue
			} else if p.curr.text == "global" {
				if !parse_global(p) do return false
				continue
			} else {
				report_error(p, fmt.aprintf("unexpected top-level directive '@%s'", p.curr.text, allocator = p.allocator))
				return false
			}
		}

		if p.curr.kind == .Semicolon || p.curr.kind == .Double_Semicolon {
			advance(p)
			continue
		}

		report_error(p, fmt.aprintf("unexpected top-level token: %v '%s'", p.curr.kind, p.curr.text, allocator = p.allocator))
		return false
	}

	return len(p.diagnostics) == 0
}
