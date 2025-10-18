// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nybit/ir.h>
#include <nybit/parser.h>
#include <nybit/support.h>

void test_lexer_tokens(void) {
    const char *src =
        "// Test comment\n"
        "@function add(%a: i32, %b: i32) -> i32;\n"
        ".entry;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n";

    Ny_Lexer l;
    ny_lexer_init(&l, src, strlen(src));

    Ny_Token tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_DIRECTIVE);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "function"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_IDENT);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "add"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_LPAREN);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_VALUE);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "a"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_COLON);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_IDENT);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "i32"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_COMMA);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_VALUE);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "b"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_COLON);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_IDENT);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "i32"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_RPAREN);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_ARROW);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_IDENT);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "i32"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_SEMICOLON);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_BLOCK_LABEL);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "entry"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_SEMICOLON);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_VALUE);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "r"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_EQUAL);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_IDENT);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "add"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_VALUE);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "a"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_COMMA);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_VALUE);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "b"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_SEMICOLON);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_DIRECTIVE);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "return"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_VALUE);
    TEST_ASSERT(ny_str_eq_cstr(tok.text, "r"));

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_SEMICOLON);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_DOUBLE_SEMICOLON);

    tok = ny_lexer_next(&l);
    TEST_ASSERT_EQ(tok.kind, NY_TOK_EOF);
}

void test_parse_add_canonical(void) {
    const char *src =
        "@function add(%a: i32, %b: i32) -> i32;\n"
        ".entry;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "mod");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);

    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(p.diag_count, 0);

    Ny_Function *fn = ny_module_get_function_by_name(&ctx.module, "add");
    TEST_ASSERT(fn != nullptr);
    TEST_ASSERT_EQ(fn->block_count, 1);
    TEST_ASSERT_EQ(fn->inst_count, 2);
    TEST_ASSERT_EQ(fn->val_count, 3);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_parse_abs_control_flow(void) {
    const char *src =
        "@function abs(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %zero = const 0;\n"
        "    %cond = cmp.lt.s %x, %zero;\n"
        "    @branch_if %cond, .negative, .positive;\n"
        ".negative;\n"
        "    %r1 = neg %x;\n"
        "    @return %r1;\n"
        ".positive;\n"
        "    @return %x;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "mod_abs");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);

    bool ok = ny_parse_module(&p);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(p.diag_count, 0);

    Ny_Function *fn = ny_module_get_function_by_name(&ctx.module, "abs");
    TEST_ASSERT(fn != nullptr);
    TEST_ASSERT_EQ(fn->block_count, 3);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid = ny_validate_module(&ctx.module, &diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}

void test_ir_golden_roundtrip(void) {
    const char *src =
        "@function add(%a: i32, %b: i32) -> i32;\n"
        ".entry;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n\n"
        "@function abs(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %zero = const 0;\n"
        "    %cond = cmp.lt.s %x, %zero;\n"
        "    @branch_if %cond, .negative, .positive;\n"
        ".negative;\n"
        "    %r1 = neg %x;\n"
        "    @return %r1;\n"
        ".positive;\n"
        "    @return %x;\n"
        ";;\n";

    Ny_Context ctx1;
    ny_context_init(&ctx1, "roundtrip1");

    Ny_Parser p1;
    ny_parser_init(&p1, &ctx1.module, src, strlen(src), &ctx1.arena);
    bool ok1 = ny_parse_module(&p1);
    TEST_ASSERT(ok1);

    char *dump1 = ny_dump_module(&ctx1.module, &ctx1.arena);

    Ny_Context ctx2;
    ny_context_init(&ctx2, "roundtrip2");

    Ny_Parser p2;
    ny_parser_init(&p2, &ctx2.module, dump1, strlen(dump1), &ctx2.arena);
    bool ok2 = ny_parse_module(&p2);
    TEST_ASSERT(ok2);

    char *dump2 = ny_dump_module(&ctx2.module, &ctx2.arena);

    TEST_ASSERT_STR_EQ(dump1, dump2);

    ny_parser_destroy(&p1);
    ny_context_destroy(&ctx1);
    ny_parser_destroy(&p2);
    ny_context_destroy(&ctx2);
}

void test_parser_diagnostics(void) {
    {
        const char *src =
            "@function bad(%a: i32) -> i32;\n"
            ".entry;\n"
            "    %r = add %a, %unknown;\n"
            "    @return %r;\n"
            ";;\n";
        Ny_Context ctx;
        ny_context_init(&ctx, "bad1");
        Ny_Parser p;
        ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
        bool ok = ny_parse_module(&p);
        TEST_ASSERT(!ok);
        TEST_ASSERT(p.diag_count > 0);
        ny_parser_destroy(&p);
        ny_context_destroy(&ctx);
    }

    {
        const char *src =
            "@function bad_dup(%a: i32) -> i32;\n"
            ".entry;\n"
            "    %r = const 1;\n"
            "    %r = const 2;\n"
            "    @return %r;\n"
            ";;\n";
        Ny_Context ctx;
        ny_context_init(&ctx, "bad2");
        Ny_Parser p;
        ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
        bool ok = ny_parse_module(&p);
        TEST_ASSERT(!ok);
        TEST_ASSERT(p.diag_count > 0);
        ny_parser_destroy(&p);
        ny_context_destroy(&ctx);
    }

    {
        const char *src =
            "@function bad_branch(%x: i32) -> i32;\n"
            ".entry;\n"
            "    @branch .non_existent;\n"
            ";;\n";
        Ny_Context ctx;
        ny_context_init(&ctx, "bad3");
        Ny_Parser p;
        ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
        bool ok = ny_parse_module(&p);
        TEST_ASSERT(!ok);
        TEST_ASSERT(p.diag_count > 0);
        ny_parser_destroy(&p);
        ny_context_destroy(&ctx);
    }
}
