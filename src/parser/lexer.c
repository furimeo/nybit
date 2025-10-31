// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/parser.h"

void ny_lexer_init(Ny_Lexer *lex, const char *src, size_t len) {
    lex->src = src;
    lex->len = len;
    lex->pos = 0;
    lex->line = 1;
    lex->col = 1;
}

static char peek_char(const Ny_Lexer *lex) {
    if (lex->pos >= lex->len) return '\0';
    return lex->src[lex->pos];
}

static char advance_char(Ny_Lexer *lex) {
    if (lex->pos >= lex->len) return '\0';
    char c = lex->src[lex->pos++];
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static void skip_whitespace_and_comments(Ny_Lexer *lex) {
    while (lex->pos < lex->len) {
        char c = peek_char(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance_char(lex);
            continue;
        }
        if (c == '/' && lex->pos + 1 < lex->len && lex->src[lex->pos + 1] == '/') {
            while (lex->pos < lex->len && peek_char(lex) != '\n') {
                advance_char(lex);
            }
            continue;
        }
        break;
    }
}

static bool is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_ident_body(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9') || c == '.';
}

Ny_Token ny_lexer_next(Ny_Lexer *lex) {
    skip_whitespace_and_comments(lex);

    Ny_Token tok;
    memset(&tok, 0, sizeof(Ny_Token));
    tok.line = lex->line;
    tok.col = lex->col;

    if (lex->pos >= lex->len) {
        tok.kind = NY_TOK_EOF;
        return tok;
    }

    char c = peek_char(lex);

    // Double semicolon or semicolon
    if (c == ';') {
        advance_char(lex);
        if (peek_char(lex) == ';') {
            advance_char(lex);
            tok.kind = NY_TOK_DOUBLE_SEMICOLON;
            tok.text = ny_str(";;");
        } else {
            tok.kind = NY_TOK_SEMICOLON;
            tok.text = ny_str(";");
        }
        return tok;
    }

    // Arrow or minus
    if (c == '-') {
        if (lex->pos + 1 < lex->len && lex->src[lex->pos + 1] == '>') {
            advance_char(lex);
            advance_char(lex);
            tok.kind = NY_TOK_ARROW;
            tok.text = ny_str("->");
            return tok;
        }
    }

    // Single character tokens
    switch (c) {
    case ':': advance_char(lex); tok.kind = NY_TOK_COLON; tok.text = ny_str(":"); return tok;
    case ',': advance_char(lex); tok.kind = NY_TOK_COMMA; tok.text = ny_str(","); return tok;
    case '=': advance_char(lex); tok.kind = NY_TOK_EQUAL; tok.text = ny_str("="); return tok;
    case '(': advance_char(lex); tok.kind = NY_TOK_LPAREN; tok.text = ny_str("("); return tok;
    case ')': advance_char(lex); tok.kind = NY_TOK_RPAREN; tok.text = ny_str(")"); return tok;
    case '[': advance_char(lex); tok.kind = NY_TOK_LBRACKET; tok.text = ny_str("["); return tok;
    case ']': advance_char(lex); tok.kind = NY_TOK_RBRACKET; tok.text = ny_str("]"); return tok;
    case '{': advance_char(lex); tok.kind = NY_TOK_LBRACE; tok.text = ny_str("{"); return tok;
    case '}': advance_char(lex); tok.kind = NY_TOK_RBRACE; tok.text = ny_str("}"); return tok;
    }

    // Directives: @name
    if (c == '@') {
        advance_char(lex);
        size_t start = lex->pos;
        while (lex->pos < lex->len && is_ident_body(peek_char(lex))) {
            advance_char(lex);
        }
        tok.kind = NY_TOK_DIRECTIVE;
        tok.text = ny_str_slice(lex->src + start, lex->pos - start);
        return tok;
    }

    // Values: %name
    if (c == '%') {
        advance_char(lex);
        size_t start = lex->pos;
        while (lex->pos < lex->len && is_ident_body(peek_char(lex))) {
            advance_char(lex);
        }
        tok.kind = NY_TOK_VALUE;
        tok.text = ny_str_slice(lex->src + start, lex->pos - start);
        return tok;
    }

    // Block Labels: .name
    if (c == '.') {
        advance_char(lex);
        size_t start = lex->pos;
        while (lex->pos < lex->len && is_ident_body(peek_char(lex))) {
            advance_char(lex);
        }
        tok.kind = NY_TOK_BLOCK_LABEL;
        tok.text = ny_str_slice(lex->src + start, lex->pos - start);
        return tok;
    }

    // Numbers: integer or float
    if ((c >= '0' && c <= '9') || (c == '-' && lex->pos + 1 < lex->len && (lex->src[lex->pos + 1] >= '0' && lex->src[lex->pos + 1] <= '9'))) {
        size_t start = lex->pos;
        advance_char(lex);
        bool is_float = false;

        // Check hex 0x
        if (c == '0' && (peek_char(lex) == 'x' || peek_char(lex) == 'X')) {
            advance_char(lex);
            while (lex->pos < lex->len) {
                char ch = peek_char(lex);
                if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
                    advance_char(lex);
                } else break;
            }
            tok.kind = NY_TOK_INT;
            tok.text = ny_str_slice(lex->src + start, lex->pos - start);
            tok.int_val = strtoll(lex->src + start, NULL, 0);
            return tok;
        }

        while (lex->pos < lex->len) {
            char ch = peek_char(lex);
            if (ch >= '0' && ch <= '9') {
                advance_char(lex);
            } else if (ch == '.' && !is_float && lex->pos + 1 < lex->len && (lex->src[lex->pos + 1] >= '0' && lex->src[lex->pos + 1] <= '9')) {
                is_float = true;
                advance_char(lex);
            } else {
                break;
            }
        }

        tok.text = ny_str_slice(lex->src + start, lex->pos - start);
        if (is_float) {
            tok.kind = NY_TOK_FLOAT;
            tok.float_val = strtod(lex->src + start, NULL);
        } else {
            tok.kind = NY_TOK_INT;
            tok.int_val = strtoll(lex->src + start, NULL, 10);
        }
        return tok;
    }

    // Identifiers
    if (is_ident_start(c)) {
        size_t start = lex->pos;
        while (lex->pos < lex->len && is_ident_body(peek_char(lex))) {
            advance_char(lex);
        }
        tok.kind = NY_TOK_IDENT;
        tok.text = ny_str_slice(lex->src + start, lex->pos - start);
        return tok;
    }

    // Unexpected character
    advance_char(lex);
    tok.kind = NY_TOK_EOF;
    return tok;
}
