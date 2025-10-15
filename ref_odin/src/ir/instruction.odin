// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"

Opcode :: enum u16 {
	None = 0,

	Const,
	Const_Null,

	Add,
	Sub,
	Mul,
	Div_S,
	Div_U,
	Rem_S,
	Rem_U,
	Neg,

	FAdd,
	FSub,
	FMul,
	FDiv,
	FRem,
	FNeg,

	And,
	Or,
	Xor,
	Not,
	Shl,
	Shr,
	Sar,
	Rotl,
	Rotr,

	Cmp_Eq,
	Cmp_Ne,
	Cmp_Lt_S,
	Cmp_Lt_U,
	Cmp_Le_S,
	Cmp_Le_U,
	Cmp_Gt_S,
	Cmp_Gt_U,
	Cmp_Ge_S,
	Cmp_Ge_U,

	FCmp_Eq,
	FCmp_Ne,
	FCmp_Lt,
	FCmp_Le,
	FCmp_Gt,
	FCmp_Ge,

	Select,

	Addr,
	Addr_Offset,
	Load,
	Store,
	Stack_Slot,
	Stack_Addr,

	Atomic_Load,
	Atomic_Store,
	Atomic_Rmw,
	Atomic_Cmpxchg,
	Fence,

	Cast,
	Extend,
	Truncate,
	Bitcast,
	SExt,
	ZExt,
	FExt,
	FTrunc,
	SIToFP,
	UIToFP,
	FPToSI,
	FPToUI,

	VAdd,
	VSub,
	VMul,
	VDiv,
	VAnd,
	VOr,
	VXor,
	VShuffle,

	Branch,
	Branch_If,
	Switch,
	Return,
	Trap,

	Call,
	Call_Indirect,

	Phi,
}

Inst_Flag :: enum u16 {
	Volatile,
	Atomic,
	Exact,
	No_Signed_Wrap,
	No_Unsigned_Wrap,
	Fast_Math,
}
Inst_Flags :: bit_set[Inst_Flag; u16]

Operand_Range :: struct {
	start: u32,
	count: u16,
}

Operand_Kind :: enum u8 {
	None = 0,
	Value,
	Block,
	Imm_Int,
	Imm_Float,
	Function,
	Symbol,
	Type,
}

Operand :: struct {
	kind: Operand_Kind,
	using payload: struct #raw_union {
		val:       support.Value_ID,
		blk:       support.Block_ID,
		imm_int:   i64,
		imm_float: f64,
		fn_id:     support.Function_ID,
		sym_id:    support.Symbol_ID,
		type_id:   support.Type_ID,
	},
}

operand_value :: proc(val: support.Value_ID) -> Operand {
	return {kind = .Value, val = val}
}

operand_block :: proc(blk: support.Block_ID) -> Operand {
	return {kind = .Block, blk = blk}
}

operand_int :: proc(imm: i64) -> Operand {
	return {kind = .Imm_Int, imm_int = imm}
}

operand_float :: proc(imm: f64) -> Operand {
	return {kind = .Imm_Float, imm_float = imm}
}

operand_function :: proc(fn_id: support.Function_ID) -> Operand {
	return {kind = .Function, fn_id = fn_id}
}

operand_symbol :: proc(sym_id: support.Symbol_ID) -> Operand {
	return {kind = .Symbol, sym_id = sym_id}
}

operand_type :: proc(type_id: support.Type_ID) -> Operand {
	return {kind = .Type, type_id = type_id}
}

Instruction :: struct {
	id:       support.Inst_ID,
	opcode:   Opcode,
	result:   support.Value_ID,
	operands: Operand_Range,
	flags:    Inst_Flags,
	block:    support.Block_ID,
	prev:     support.Inst_ID,
	next:     support.Inst_ID,
}

opcode_is_terminator :: proc(op: Opcode) -> bool {
	#partial switch op {
	case .Branch, .Branch_If, .Switch, .Return, .Trap:
		return true
	}
	return false
}

opcode_name :: proc(op: Opcode) -> string {
	switch op {
	case .None:           return "none"
	case .Const:          return "const"
	case .Const_Null:     return "const_null"
	case .Add:            return "add"
	case .Sub:            return "sub"
	case .Mul:            return "mul"
	case .Div_S:          return "div.s"
	case .Div_U:          return "div.u"
	case .Rem_S:          return "rem.s"
	case .Rem_U:          return "rem.u"
	case .Neg:            return "neg"
	case .FAdd:           return "fadd"
	case .FSub:           return "fsub"
	case .FMul:           return "fmul"
	case .FDiv:           return "fdiv"
	case .FRem:           return "frem"
	case .FNeg:           return "fneg"
	case .And:            return "and"
	case .Or:             return "or"
	case .Xor:            return "xor"
	case .Not:            return "not"
	case .Shl:            return "shl"
	case .Shr:            return "shr"
	case .Sar:            return "sar"
	case .Rotl:           return "rotl"
	case .Rotr:           return "rotr"
	case .Cmp_Eq:         return "cmp.eq"
	case .Cmp_Ne:         return "cmp.ne"
	case .Cmp_Lt_S:       return "cmp.lt.s"
	case .Cmp_Lt_U:       return "cmp.lt.u"
	case .Cmp_Le_S:       return "cmp.le.s"
	case .Cmp_Le_U:       return "cmp.le.u"
	case .Cmp_Gt_S:       return "cmp.gt.s"
	case .Cmp_Gt_U:       return "cmp.gt.u"
	case .Cmp_Ge_S:       return "cmp.ge.s"
	case .Cmp_Ge_U:       return "cmp.ge.u"
	case .FCmp_Eq:        return "fcmp.eq"
	case .FCmp_Ne:        return "fcmp.ne"
	case .FCmp_Lt:        return "fcmp.lt"
	case .FCmp_Le:        return "fcmp.le"
	case .FCmp_Gt:        return "fcmp.gt"
	case .FCmp_Ge:        return "fcmp.ge"
	case .Select:         return "select"
	case .Addr:           return "addr"
	case .Addr_Offset:    return "addr_offset"
	case .Load:           return "load"
	case .Store:          return "store"
	case .Stack_Slot:     return "stack_slot"
	case .Stack_Addr:     return "stack_addr"
	case .Atomic_Load:    return "atomic_load"
	case .Atomic_Store:   return "atomic_store"
	case .Atomic_Rmw:     return "atomic_rmw"
	case .Atomic_Cmpxchg: return "atomic_cmpxchg"
	case .Fence:          return "fence"
	case .Cast:           return "cast"
	case .Extend:         return "extend"
	case .Truncate:       return "truncate"
	case .Bitcast:        return "bitcast"
	case .SExt:           return "sext"
	case .ZExt:           return "zext"
	case .FExt:           return "fext"
	case .FTrunc:         return "ftrunc"
	case .SIToFP:         return "sitofp"
	case .UIToFP:         return "uitofp"
	case .FPToSI:         return "fptosi"
	case .FPToUI:         return "fptoui"
	case .VAdd:           return "vadd"
	case .VSub:           return "vsub"
	case .VMul:           return "vmul"
	case .VDiv:           return "vdiv"
	case .VAnd:           return "vand"
	case .VOr:            return "vor"
	case .VXor:           return "vxor"
	case .VShuffle:       return "vshuffle"
	case .Branch:         return "@branch"
	case .Branch_If:      return "@branch_if"
	case .Switch:         return "@switch"
	case .Return:         return "@return"
	case .Trap:           return "@trap"
	case .Call:           return "call"
	case .Call_Indirect:  return "call_indirect"
	case .Phi:            return "phi"
	}
	return "unknown"
}
