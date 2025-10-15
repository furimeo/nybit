// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
package tests

import "core:strings"
import "core:testing"
import "src:ir"
import "src:parser"
import "src:support"

@(test)
test_lexer_tokens :: proc(t: ^testing.T) {
	src := `
	// Test comment
	@function add(%a: i32, %b: i32) -> i32;
	.entry;
		%r = add %a, %b;
		@return %r;
	;;
	`
	l: parser.Lexer
	parser.lexer_init(&l, src)

	tok := parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Directive)
	testing.expect_value(t, tok.text, "function")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Ident)
	testing.expect_value(t, tok.text, "add")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.LParen)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Value)
	testing.expect_value(t, tok.text, "a")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Colon)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Ident)
	testing.expect_value(t, tok.text, "i32")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Comma)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Value)
	testing.expect_value(t, tok.text, "b")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Colon)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Ident)
	testing.expect_value(t, tok.text, "i32")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.RParen)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Arrow)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Ident)
	testing.expect_value(t, tok.text, "i32")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Semicolon)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Block_Label)
	testing.expect_value(t, tok.text, "entry")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Semicolon)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Value)
	testing.expect_value(t, tok.text, "r")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Equal)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Ident)
	testing.expect_value(t, tok.text, "add")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Value)
	testing.expect_value(t, tok.text, "a")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Comma)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Value)
	testing.expect_value(t, tok.text, "b")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Semicolon)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Directive)
	testing.expect_value(t, tok.text, "return")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Value)
	testing.expect_value(t, tok.text, "r")

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Semicolon)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.Double_Semicolon)

	tok = parser.lexer_next(&l)
	testing.expect_value(t, tok.kind, parser.Token_Kind.EOF)
}

@(test)
test_parse_add_canonical :: proc(t: ^testing.T) {
	src := `
	@function add(%a: i32, %b: i32) -> i32;
	.entry;
		%r = add %a, %b;
		@return %r;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "mod")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)

	ok := parser.parse_module(&p)
	testing.expect(t, ok)
	testing.expect_value(t, len(p.diagnostics), 0)

	fn := ir.module_get_function_by_name(&ctx.module, "add")
	testing.expect(t, fn != nil)
	testing.expect_value(t, len(fn.blocks), 1)
	testing.expect_value(t, len(fn.instructions), 2)
	testing.expect_value(t, len(fn.values), 3)

	diags := ir.validate_module(&ctx.module)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_parse_abs_control_flow :: proc(t: ^testing.T) {
	src := `
	@function abs(%x: i32) -> i32;
	.entry;
		%zero = const 0;
		%cond = cmp.lt.s %x, %zero;
		@branch_if %cond, .negative, .positive;

	.negative;
		%r1 = neg %x;
		@return %r1;

	.positive;
		@return %x;
	;;
	`
	ctx: ir.Context
	ir.context_init(&ctx, "mod_abs")
	defer ir.context_destroy(&ctx)

	p: parser.Parser
	parser.parser_init(&p, &ctx.module, src)

	ok := parser.parse_module(&p)
	testing.expect(t, ok)
	testing.expect_value(t, len(p.diagnostics), 0)

	fn := ir.module_get_function_by_name(&ctx.module, "abs")
	testing.expect(t, fn != nil)
	testing.expect_value(t, len(fn.blocks), 3)

	diags := ir.validate_module(&ctx.module)
	testing.expect_value(t, len(diags), 0)
}

@(test)
test_ir_golden_roundtrip :: proc(t: ^testing.T) {
	src := `@function add(%a: i32, %b: i32) -> i32;
.entry;
    %r = add %a, %b;
    @return %r;
;;

@function abs(%x: i32) -> i32;
.entry;
    %zero = const 0;
    %cond = cmp.lt.s %x, %zero;
    @branch_if %cond, .negative, .positive;

.negative;
    %r1 = neg %x;
    @return %r1;

.positive;
    @return %x;
;;
`
	// Pass 1: parse -> dump
	ctx1: ir.Context
	ir.context_init(&ctx1, "roundtrip1")
	defer ir.context_destroy(&ctx1)

	p1: parser.Parser
	parser.parser_init(&p1, &ctx1.module, src)
	ok1 := parser.parse_module(&p1)
	testing.expect(t, ok1)

	dump1 := ir.dump_module(&ctx1.module, context.temp_allocator)

	// Pass 2: parse dumped IR -> dump again
	ctx2: ir.Context
	ir.context_init(&ctx2, "roundtrip2")
	defer ir.context_destroy(&ctx2)

	p2: parser.Parser
	parser.parser_init(&p2, &ctx2.module, dump1)
	ok2 := parser.parse_module(&p2)
	testing.expect(t, ok2)

	dump2 := ir.dump_module(&ctx2.module, context.temp_allocator)

	// Section 111 contract: hai lần dump phải giống nhau
	testing.expect_value(t, dump1, dump2)
}

@(test)
test_parser_diagnostics :: proc(t: ^testing.T) {
	// Unknown SSA value
	{
		src := `
		@function bad(%a: i32) -> i32;
		.entry;
			%r = add %a, %unknown;
			@return %r;
		;;
		`
		ctx: ir.Context
		ir.context_init(&ctx, "bad1")
		defer ir.context_destroy(&ctx)

		p: parser.Parser
		parser.parser_init(&p, &ctx.module, src)
		ok := parser.parse_module(&p)
		testing.expect(t, !ok)
		testing.expect(t, len(p.diagnostics) > 0)
	}

	// SSA duplicate definition
	{
		src := `
		@function bad_dup(%a: i32) -> i32;
		.entry;
			%r = const 1;
			%r = const 2;
			@return %r;
		;;
		`
		ctx: ir.Context
		ir.context_init(&ctx, "bad2")
		defer ir.context_destroy(&ctx)

		p: parser.Parser
		parser.parser_init(&p, &ctx.module, src)
		ok := parser.parse_module(&p)
		testing.expect(t, !ok)
		testing.expect(t, len(p.diagnostics) > 0)
	}

	// Missing block referenced in branch
	{
		src := `
		@function bad_branch(%x: i32) -> i32;
		.entry;
			@branch .non_existent;
		;;
		`
		ctx: ir.Context
		ir.context_init(&ctx, "bad3")
		defer ir.context_destroy(&ctx)

		p: parser.Parser
		parser.parser_init(&p, &ctx.module, src)
		ok := parser.parse_module(&p)
		testing.expect(t, !ok)
		testing.expect(t, len(p.diagnostics) > 0)
	}
}
