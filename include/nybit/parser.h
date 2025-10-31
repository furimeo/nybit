// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_PARSER_H
#define NYBIT_PARSER_H

#include "nybit/ir.h"

typedef enum Ny_Token_Kind {
    NY_TOK_EOF = 0,
    NY_TOK_DIRECTIVE,
    NY_TOK_IDENT,
    NY_TOK_VALUE,
    NY_TOK_BLOCK_LABEL,
    NY_TOK_INT,
    NY_TOK_FLOAT,
    NY_TOK_STRING,
    NY_TOK_COLON,
    NY_TOK_SEMICOLON,
    NY_TOK_DOUBLE_SEMICOLON,
    NY_TOK_COMMA,
    NY_TOK_ARROW,
    NY_TOK_EQUAL,
    NY_TOK_LPAREN,
    NY_TOK_RPAREN,
    NY_TOK_LBRACKET,
    NY_TOK_RBRACKET,
    NY_TOK_LBRACE,
    NY_TOK_RBRACE,
} Ny_Token_Kind;

typedef struct Ny_Token {
    Ny_Token_Kind kind;
    Ny_String text;
    int64_t int_val;
    double float_val;
    int line;
    int col;
} Ny_Token;

typedef struct Ny_Lexer {
    const char *src;
    size_t len;
    size_t pos;
    int line;
    int col;
} Ny_Lexer;

void ny_lexer_init(Ny_Lexer *lex, const char *src, size_t len);
Ny_Token ny_lexer_next(Ny_Lexer *lex);

typedef struct Ny_Parser_Diagnostic {
    char *message;
    int line;
    int col;
} Ny_Parser_Diagnostic;

typedef struct Ny_Parser {
    Ny_Lexer lexer;
    Ny_Token curr;
    Ny_Token peek;
    Ny_Module *module;
    Ny_Builder builder;
    Ny_Arena *arena;
    Ny_Parser_Diagnostic *diagnostics;
    size_t diag_count;
    size_t diag_capacity;
} Ny_Parser;

void ny_parser_init(Ny_Parser *p, Ny_Module *mod, const char *src, size_t len, Ny_Arena *arena);
void ny_parser_destroy(Ny_Parser *p);
bool ny_parse_module(Ny_Parser *p);

#endif // NYBIT_PARSER_H
