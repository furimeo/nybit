// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package parser

import "core:strconv"
import "core:strings"
import "core:unicode/utf8"

Token_Kind :: enum u16 {
	EOF = 0,
	Invalid,

	// 6 Syntax Primitives
	Directive,        // @function, @return, @branch, etc.
	Value,            // %a, %r, %0
	Block_Label,      // .entry, .negative
	Semicolon,        // ;
	Double_Semicolon, // ;;
	Equal,            // =

	// Literals & Identifiers
	Ident,            // add, cmp.lt.s, i32
	Int,              // 42, -10, 0x1F
	Float,            // 3.14, -0.5
	String,           // "hello"

	// Punctuation
	Comma,            // ,
	Colon,            // :
	Arrow,            // ->
	LParen,           // (
	RParen,           // )
}

Token :: struct {
	kind:      Token_Kind,
	text:      string,
	line:      int,
	col:       int,
	int_val:   i64,
	float_val: f64,
}

Lexer :: struct {
	src:    string,
	cursor: int,
	line:   int,
	col:    int,
}

lexer_init :: proc(l: ^Lexer, src: string) {
	l.src    = src
	l.cursor = 0
	l.line   = 1
	l.col    = 1
}

@(private="file")
peek_byte :: #force_inline proc(l: ^Lexer) -> u8 {
	return l.cursor < len(l.src) ? l.src[l.cursor] : 0
}

@(private="file")
peek_next_byte :: #force_inline proc(l: ^Lexer) -> u8 {
	return l.cursor + 1 < len(l.src) ? l.src[l.cursor + 1] : 0
}

@(private="file")
advance_byte :: #force_inline proc(l: ^Lexer) -> u8 {
	if l.cursor >= len(l.src) do return 0
	ch := l.src[l.cursor]
	l.cursor += 1
	if ch == '\n' {
		l.line += 1
		l.col = 1
	} else {
		l.col += 1
	}
	return ch
}

@(private="file")
is_ident_start :: #force_inline proc(ch: u8) -> bool {
	return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_'
}

@(private="file")
is_ident_char :: #force_inline proc(ch: u8) -> bool {
	return is_ident_start(ch) || (ch >= '0' && ch <= '9') || ch == '.'
}

@(private="file")
is_digit :: #force_inline proc(ch: u8) -> bool {
	return ch >= '0' && ch <= '9'
}

lexer_next :: proc(l: ^Lexer) -> Token {
	for {
		ch := peek_byte(l)
		if ch == 0 {
			return Token{kind = .EOF, line = l.line, col = l.col}
		}

		// Whitespace
		if ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' {
			advance_byte(l)
			continue
		}

		// Line comments: // ...
		if ch == '/' && peek_next_byte(l) == '/' {
			advance_byte(l)
			advance_byte(l)
			for {
				c := peek_byte(l)
				if c == 0 || c == '\n' do break
				advance_byte(l)
			}
			continue
		}

		break
	}

	start_line := l.line
	start_col := l.col
	start_idx := l.cursor
	ch := advance_byte(l)

	// Directives: @identifier
	if ch == '@' {
		val_start := l.cursor
		for is_ident_char(peek_byte(l)) {
			advance_byte(l)
		}
		text := l.src[val_start:l.cursor]
		return Token{
			kind = .Directive,
			text = text,
			line = start_line,
			col  = start_col,
		}
	}

	// SSA Values: %identifier
	if ch == '%' {
		val_start := l.cursor
		for is_ident_char(peek_byte(l)) {
			advance_byte(l)
		}
		text := l.src[val_start:l.cursor]
		return Token{
			kind = .Value,
			text = text,
			line = start_line,
			col  = start_col,
		}
	}

	// Block Labels: .identifier
	if ch == '.' && is_ident_start(peek_byte(l)) {
		label_start := l.cursor
		for is_ident_char(peek_byte(l)) {
			advance_byte(l)
		}
		text := l.src[label_start:l.cursor]
		return Token{
			kind = .Block_Label,
			text = text,
			line = start_line,
			col  = start_col,
		}
	}

	// Statement / Block Terminators: ; or ;;
	if ch == ';' {
		if peek_byte(l) == ';' {
			advance_byte(l)
			return Token{
				kind = .Double_Semicolon,
				text = ";;",
				line = start_line,
				col  = start_col,
			}
		}
		return Token{
			kind = .Semicolon,
			text = ";",
			line = start_line,
			col  = start_col,
		}
	}

	// Definition: =
	if ch == '=' {
		return Token{
			kind = .Equal,
			text = "=",
			line = start_line,
			col  = start_col,
		}
	}

	// Arrow: ->
	if ch == '-' {
		if peek_byte(l) == '>' {
			advance_byte(l)
			return Token{
				kind = .Arrow,
				text = "->",
				line = start_line,
				col  = start_col,
			}
		}

		// Negative number
		if is_digit(peek_byte(l)) {
			is_float := false
			for is_digit(peek_byte(l)) || peek_byte(l) == '.' {
				if peek_byte(l) == '.' do is_float = true
				advance_byte(l)
			}
			num_str := l.src[start_idx:l.cursor]
			if is_float {
				f_val, _ := strconv.parse_f64(num_str)
				return Token{
					kind      = .Float,
					text      = num_str,
					float_val = f_val,
					line      = start_line,
					col       = start_col,
				}
			}
			i_val, _ := strconv.parse_i64(num_str)
			return Token{
				kind    = .Int,
				text    = num_str,
				int_val = i_val,
				line    = start_line,
				col     = start_col,
			}
		}
	}

	// Punctuation
	if ch == ',' do return Token{kind = .Comma,  text = ",", line = start_line, col = start_col}
	if ch == ':' do return Token{kind = .Colon,  text = ":", line = start_line, col = start_col}
	if ch == '(' do return Token{kind = .LParen, text = "(", line = start_line, col = start_col}
	if ch == ')' do return Token{kind = .RParen, text = ")", line = start_line, col = start_col}

	// Numeric literals: Positive Int or Float
	if is_digit(ch) {
		is_float := false
		if ch == '0' && (peek_byte(l) == 'x' || peek_byte(l) == 'X') {
			advance_byte(l)
			for {
				c := peek_byte(l)
				if (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') {
					advance_byte(l)
				} else {
					break
				}
			}
			num_str := l.src[start_idx:l.cursor]
			i_val, _ := strconv.parse_i64(num_str)
			return Token{
				kind    = .Int,
				text    = num_str,
				int_val = i_val,
				line    = start_line,
				col     = start_col,
			}
		}

		for is_digit(peek_byte(l)) || peek_byte(l) == '.' {
			if peek_byte(l) == '.' {
				if peek_next_byte(l) == '.' do break // .. not float
				is_float = true
			}
			advance_byte(l)
		}
		num_str := l.src[start_idx:l.cursor]
		if is_float {
			f_val, _ := strconv.parse_f64(num_str)
			return Token{
				kind      = .Float,
				text      = num_str,
				float_val = f_val,
				line      = start_line,
				col       = start_col,
			}
		}
		i_val, _ := strconv.parse_i64(num_str)
		return Token{
			kind    = .Int,
			text    = num_str,
			int_val = i_val,
			line    = start_line,
			col     = start_col,
		}
	}

	// String literal: "..."
	if ch == '"' {
		str_start := l.cursor
		for {
			c := peek_byte(l)
			if c == 0 || c == '"' do break
			if c == '\\' {
				advance_byte(l)
			}
			advance_byte(l)
		}
		text := l.src[str_start:l.cursor]
		if peek_byte(l) == '"' do advance_byte(l)
		return Token{
			kind = .String,
			text = text,
			line = start_line,
			col  = start_col,
		}
	}

	// Identifier (can have internal dots, e.g. cmp.lt.s, div.s)
	if is_ident_start(ch) {
		for is_ident_char(peek_byte(l)) {
			advance_byte(l)
		}
		text := l.src[start_idx:l.cursor]
		return Token{
			kind = .Ident,
			text = text,
			line = start_line,
			col  = start_col,
		}
	}

	return Token{
		kind = .Invalid,
		text = l.src[start_idx:l.cursor],
		line = start_line,
		col  = start_col,
	}
}
