// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package ir

import "../support"

Value_Kind :: enum u8 {
	Instruction,
	Argument,
	Constant,
}

Value :: struct {
	id:    support.Value_ID,
	type:  support.Type_ID,
	kind:  Value_Kind,
	def:   support.Inst_ID,
	index: u32,
	name:  string,
}
