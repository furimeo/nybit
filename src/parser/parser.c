// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/parser.h"
#include <stdio.h>

void ny_parser_init(Ny_Parser *p, Ny_Module *mod, const char *src, size_t len, Ny_Arena *arena) {
    memset(p, 0, sizeof(Ny_Parser));
    p->module = mod;
    p->arena = arena;
    ny_builder_init(&p->builder, mod);
    ny_lexer_init(&p->lexer, src, len);

    p->curr = ny_lexer_next(&p->lexer);
    p->peek = ny_lexer_next(&p->lexer);
}

void ny_parser_destroy(Ny_Parser *p) {
    for (size_t i = 0; i < p->diag_count; i++) {
        if (p->diagnostics[i].message) {
            ny_free(p->diagnostics[i].message, strlen(p->diagnostics[i].message) + 1);
        }
    }
    if (p->diagnostics) {
        ny_free(p->diagnostics, p->diag_capacity * sizeof(Ny_Parser_Diagnostic));
    }
    memset(p, 0, sizeof(Ny_Parser));
}

static Ny_Token advance_tok(Ny_Parser *p) {
    Ny_Token prev = p->curr;
    p->curr = p->peek;
    p->peek = ny_lexer_next(&p->lexer);
    return prev;
}

static void report_error(Ny_Parser *p, const char *msg, int line, int col) {
    ny_buf_grow((void **)&p->diagnostics, &p->diag_capacity, p->diag_count, sizeof(Ny_Parser_Diagnostic));
    Ny_Parser_Diagnostic *d = &p->diagnostics[p->diag_count++];
    size_t len = strlen(msg);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, msg, len + 1);
    d->message = copy;
    d->line = line >= 0 ? line : p->curr.line;
    d->col = col >= 0 ? col : p->curr.col;
}

static bool expect_tok(Ny_Parser *p, Ny_Token_Kind kind) {
    if (p->curr.kind == kind) {
        advance_tok(p);
        return true;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "unexpected token '%.*s'", (int)p->curr.text.len, p->curr.text.data ? p->curr.text.data : "");
    report_error(p, buf, p->curr.line, p->curr.col);
    return false;
}

static Ny_Type_ID lookup_type(Ny_Module *mod, Ny_String name) {
    if (ny_str_eq_cstr(name, "void")) return NY_TYPE_VOID;
    if (ny_str_eq_cstr(name, "i8"))   return NY_TYPE_I8;
    if (ny_str_eq_cstr(name, "i16"))  return NY_TYPE_I16;
    if (ny_str_eq_cstr(name, "i32"))  return NY_TYPE_I32;
    if (ny_str_eq_cstr(name, "i64"))  return NY_TYPE_I64;
    if (ny_str_eq_cstr(name, "i128")) return NY_TYPE_I128;
    if (ny_str_eq_cstr(name, "f16"))  return NY_TYPE_F16;
    if (ny_str_eq_cstr(name, "f32"))  return NY_TYPE_F32;
    if (ny_str_eq_cstr(name, "f64"))  return NY_TYPE_F64;
    if (ny_str_eq_cstr(name, "ptr"))  return NY_TYPE_PTR;

    // Vector types: v4i32, v8i32, etc.
    if (name.len > 2 && name.data[0] == 'v') {
        size_t idx = 1;
        uint16_t lanes = 0;
        while (idx < name.len && name.data[idx] >= '0' && name.data[idx] <= '9') {
            lanes = lanes * 10 + (uint16_t)(name.data[idx] - '0');
            idx++;
        }
        if (lanes > 0 && idx < name.len) {
            Ny_String elem_name = ny_str_slice(name.data + idx, name.len - idx);
            Ny_Type_ID elem_t = lookup_type(mod, elem_name);
            if (elem_t != NY_INVALID_TYPE) {
                return ny_type_table_add_vector(&mod->types, elem_t, lanes);
            }
        }
    }

    /* Check existing types registered in module type table */
    for (size_t i = 0; i < mod->types.count; i++) {
        const Ny_Type *t = &mod->types.types[i];
        if (t->name.data && ny_str_eq(t->name, name)) {
            return (Ny_Type_ID)i;
        }
    }

    return NY_INVALID_TYPE;
}

static Ny_Opcode lookup_opcode(Ny_String name) {
    if (ny_str_eq_cstr(name, "const"))          return NY_OPCODE_CONST;
    if (ny_str_eq_cstr(name, "const_null"))     return NY_OPCODE_CONST_NULL;
    if (ny_str_eq_cstr(name, "add"))            return NY_OPCODE_ADD;
    if (ny_str_eq_cstr(name, "sub"))            return NY_OPCODE_SUB;
    if (ny_str_eq_cstr(name, "mul"))            return NY_OPCODE_MUL;
    if (ny_str_eq_cstr(name, "div.s") || ny_str_eq_cstr(name, "div")) return NY_OPCODE_DIV_S;
    if (ny_str_eq_cstr(name, "div.u"))          return NY_OPCODE_DIV_U;
    if (ny_str_eq_cstr(name, "rem.s") || ny_str_eq_cstr(name, "rem")) return NY_OPCODE_REM_S;
    if (ny_str_eq_cstr(name, "rem.u"))          return NY_OPCODE_REM_U;
    if (ny_str_eq_cstr(name, "neg"))            return NY_OPCODE_NEG;
    if (ny_str_eq_cstr(name, "fadd"))           return NY_OPCODE_FADD;
    if (ny_str_eq_cstr(name, "fsub"))           return NY_OPCODE_FSUB;
    if (ny_str_eq_cstr(name, "fmul"))           return NY_OPCODE_FMUL;
    if (ny_str_eq_cstr(name, "fdiv"))           return NY_OPCODE_FDIV;
    if (ny_str_eq_cstr(name, "frem"))           return NY_OPCODE_FREM;
    if (ny_str_eq_cstr(name, "fneg"))           return NY_OPCODE_FNEG;
    if (ny_str_eq_cstr(name, "and"))            return NY_OPCODE_AND;
    if (ny_str_eq_cstr(name, "or"))             return NY_OPCODE_OR;
    if (ny_str_eq_cstr(name, "xor"))            return NY_OPCODE_XOR;
    if (ny_str_eq_cstr(name, "not"))            return NY_OPCODE_NOT;
    if (ny_str_eq_cstr(name, "shl"))            return NY_OPCODE_SHL;
    if (ny_str_eq_cstr(name, "shr"))            return NY_OPCODE_SHR;
    if (ny_str_eq_cstr(name, "sar"))            return NY_OPCODE_SAR;
    if (ny_str_eq_cstr(name, "rotl"))           return NY_OPCODE_ROTL;
    if (ny_str_eq_cstr(name, "rotr"))           return NY_OPCODE_ROTR;
    if (ny_str_eq_cstr(name, "cmp.eq"))         return NY_OPCODE_CMP_EQ;
    if (ny_str_eq_cstr(name, "cmp.ne"))         return NY_OPCODE_CMP_NE;
    if (ny_str_eq_cstr(name, "cmp.lt.s") || ny_str_eq_cstr(name, "cmp.lt")) return NY_OPCODE_CMP_LT_S;
    if (ny_str_eq_cstr(name, "cmp.lt.u"))       return NY_OPCODE_CMP_LT_U;
    if (ny_str_eq_cstr(name, "cmp.le.s") || ny_str_eq_cstr(name, "cmp.le")) return NY_OPCODE_CMP_LE_S;
    if (ny_str_eq_cstr(name, "cmp.le.u"))       return NY_OPCODE_CMP_LE_U;
    if (ny_str_eq_cstr(name, "cmp.gt.s") || ny_str_eq_cstr(name, "cmp.gt")) return NY_OPCODE_CMP_GT_S;
    if (ny_str_eq_cstr(name, "cmp.gt.u"))       return NY_OPCODE_CMP_GT_U;
    if (ny_str_eq_cstr(name, "cmp.ge.s") || ny_str_eq_cstr(name, "cmp.ge")) return NY_OPCODE_CMP_GE_S;
    if (ny_str_eq_cstr(name, "cmp.ge.u"))       return NY_OPCODE_CMP_GE_U;
    if (ny_str_eq_cstr(name, "fcmp.eq"))        return NY_OPCODE_FCMP_EQ;
    if (ny_str_eq_cstr(name, "fcmp.ne"))        return NY_OPCODE_FCMP_NE;
    if (ny_str_eq_cstr(name, "fcmp.lt"))        return NY_OPCODE_FCMP_LT;
    if (ny_str_eq_cstr(name, "fcmp.le"))        return NY_OPCODE_FCMP_LE;
    if (ny_str_eq_cstr(name, "fcmp.gt"))        return NY_OPCODE_FCMP_GT;
    if (ny_str_eq_cstr(name, "fcmp.ge"))        return NY_OPCODE_FCMP_GE;
    if (ny_str_eq_cstr(name, "select"))         return NY_OPCODE_SELECT;
    if (ny_str_eq_cstr(name, "addr"))           return NY_OPCODE_ADDR;
    if (ny_str_eq_cstr(name, "addr_offset"))    return NY_OPCODE_ADDR_OFFSET;
    if (ny_str_eq_cstr(name, "global_addr"))    return NY_OPCODE_GLOBAL_ADDR;
    if (ny_str_eq_cstr(name, "load"))           return NY_OPCODE_LOAD;
    if (ny_str_eq_cstr(name, "store"))          return NY_OPCODE_STORE;
    if (ny_str_eq_cstr(name, "stack_slot"))     return NY_OPCODE_STACK_SLOT;
    if (ny_str_eq_cstr(name, "stack_addr"))     return NY_OPCODE_STACK_ADDR;
    if (ny_str_eq_cstr(name, "atomic_load"))    return NY_OPCODE_ATOMIC_LOAD;
    if (ny_str_eq_cstr(name, "atomic_store"))   return NY_OPCODE_ATOMIC_STORE;
    if (ny_str_eq_cstr(name, "atomic_rmw"))     return NY_OPCODE_ATOMIC_RMW;
    if (ny_str_eq_cstr(name, "atomic_cmpxchg")) return NY_OPCODE_ATOMIC_CMPXCHG;
    if (ny_str_eq_cstr(name, "fence"))          return NY_OPCODE_FENCE;
    if (ny_str_eq_cstr(name, "cast"))           return NY_OPCODE_CAST;
    if (ny_str_eq_cstr(name, "extend"))         return NY_OPCODE_EXTEND;
    if (ny_str_eq_cstr(name, "truncate"))       return NY_OPCODE_TRUNCATE;
    if (ny_str_eq_cstr(name, "bitcast"))        return NY_OPCODE_BITCAST;
    if (ny_str_eq_cstr(name, "sext"))           return NY_OPCODE_SEXT;
    if (ny_str_eq_cstr(name, "zext"))           return NY_OPCODE_ZEXT;
    if (ny_str_eq_cstr(name, "fext"))           return NY_OPCODE_FEXT;
    if (ny_str_eq_cstr(name, "ftrunc"))         return NY_OPCODE_FTRUNC;
    if (ny_str_eq_cstr(name, "sitofp"))         return NY_OPCODE_SITOFP;
    if (ny_str_eq_cstr(name, "uitofp"))         return NY_OPCODE_UITOFP;
    if (ny_str_eq_cstr(name, "fptosi"))         return NY_OPCODE_FPTOSI;
    if (ny_str_eq_cstr(name, "fptoui"))         return NY_OPCODE_FPTOUI;
    if (ny_str_eq_cstr(name, "vadd"))           return NY_OPCODE_VADD;
    if (ny_str_eq_cstr(name, "vsub"))           return NY_OPCODE_VSUB;
    if (ny_str_eq_cstr(name, "vmul"))           return NY_OPCODE_VMUL;
    if (ny_str_eq_cstr(name, "vdiv"))           return NY_OPCODE_VDIV;
    if (ny_str_eq_cstr(name, "vand"))           return NY_OPCODE_VAND;
    if (ny_str_eq_cstr(name, "vor"))            return NY_OPCODE_VOR;
    if (ny_str_eq_cstr(name, "vxor"))           return NY_OPCODE_VXOR;
    if (ny_str_eq_cstr(name, "vshuffle"))       return NY_OPCODE_VSHUFFLE;
    if (ny_str_eq_cstr(name, "branch") || ny_str_eq_cstr(name, "@branch"))       return NY_OPCODE_BRANCH;
    if (ny_str_eq_cstr(name, "branch_if") || ny_str_eq_cstr(name, "@branch_if")) return NY_OPCODE_BRANCH_IF;
    if (ny_str_eq_cstr(name, "switch") || ny_str_eq_cstr(name, "@switch"))       return NY_OPCODE_SWITCH;
    if (ny_str_eq_cstr(name, "return") || ny_str_eq_cstr(name, "@return"))       return NY_OPCODE_RETURN;
    if (ny_str_eq_cstr(name, "trap") || ny_str_eq_cstr(name, "@trap"))           return NY_OPCODE_TRAP;
    if (ny_str_eq_cstr(name, "call"))           return NY_OPCODE_CALL;
    if (ny_str_eq_cstr(name, "call_indirect"))  return NY_OPCODE_CALL_INDIRECT;
    if (ny_str_eq_cstr(name, "phi"))            return NY_OPCODE_PHI;
    return NY_OPCODE_NONE;
}

typedef struct Name_Binding {
    Ny_String name;
    uint32_t id;
} Name_Binding;

typedef struct Scope_Map {
    Name_Binding *items;
    size_t count;
    size_t capacity;
} Scope_Map;

static uint32_t map_lookup(const Scope_Map *m, Ny_String name) {
    for (size_t i = 0; i < m->count; i++) {
        if (ny_str_eq(m->items[i].name, name)) {
            return m->items[i].id;
        }
    }
    return UINT32_MAX;
}

static void map_insert(Scope_Map *m, Ny_String name, uint32_t id) {
    ny_buf_grow((void **)&m->items, &m->capacity, m->count, sizeof(Name_Binding));
    m->items[m->count].name = name;
    m->items[m->count].id = id;
    m->count++;
}

static void map_free(Scope_Map *m) {
    if (m->items) {
        ny_free(m->items, m->capacity * sizeof(Name_Binding));
    }
    m->items = NULL;
    m->count = 0;
    m->capacity = 0;
}

static Ny_Block_ID get_or_create_block(Ny_Function *fn, Scope_Map *blk_map, Ny_String name) {
    uint32_t id = map_lookup(blk_map, name);
    if (id != UINT32_MAX) return (Ny_Block_ID)id;

    Ny_Block_ID b_id = ny_function_create_block(fn, name);
    map_insert(blk_map, name, (uint32_t)b_id);
    return b_id;
}

static bool parse_function(Ny_Parser *p) {
    Ny_Token fn_tok = advance_tok(p);
    if (fn_tok.kind != NY_TOK_DIRECTIVE || !ny_str_eq_cstr(fn_tok.text, "function")) {
        report_error(p, "expected '@function'", fn_tok.line, fn_tok.col);
        return false;
    }

    if (p->curr.kind != NY_TOK_IDENT) {
        report_error(p, "expected function name identifier", p->curr.line, p->curr.col);
        return false;
    }
    Ny_Token name_tok = advance_tok(p);

    if (!expect_tok(p, NY_TOK_LPAREN)) return false;

    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)name_tok.text.len, name_tok.text.data);
    Ny_Function *existing_fn = ny_module_get_function_by_name(p->module, name_buf);
    Ny_Function_ID fn_id;
    if (existing_fn && existing_fn->block_count == 0) {
        fn_id = existing_fn->id;
    } else {
        fn_id = ny_module_create_function(p->module, name_tok.text, NY_TYPE_VOID, NY_CC_DEFAULT);
    }
    Ny_Function *fn = ny_module_get_function(p->module, fn_id);
    p->builder.cur_fn = fn_id;

    Scope_Map val_map = {NULL, 0, 0};
    Scope_Map blk_map = {NULL, 0, 0};

    // Parameter list
    while (p->curr.kind != NY_TOK_RPAREN && p->curr.kind != NY_TOK_EOF) {
        if (p->curr.kind != NY_TOK_VALUE) {
            report_error(p, "expected parameter %name", p->curr.line, p->curr.col);
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }
        Ny_String p_name = advance_tok(p).text;

        if (!expect_tok(p, NY_TOK_COLON)) {
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }

        if (p->curr.kind != NY_TOK_IDENT) {
            report_error(p, "expected type name for parameter", p->curr.line, p->curr.col);
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }
        Ny_String t_name = advance_tok(p).text;
        Ny_Type_ID p_type = lookup_type(p->module, t_name);
        if (p_type == NY_INVALID_TYPE) {
            report_error(p, "unknown parameter type", p->curr.line, p->curr.col);
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }

        uint32_t param_idx = (uint32_t)fn->param_count;
        ny_function_add_param(fn, p_type, p_name);

        Ny_Value_ID v_id = ny_function_create_value(fn, p_type, NY_VAL_ARGUMENT, NY_INVALID_INST, param_idx, p_name);
        map_insert(&val_map, p_name, v_id);

        if (p->curr.kind == NY_TOK_COMMA) {
            advance_tok(p);
        } else if (p->curr.kind != NY_TOK_RPAREN) {
            report_error(p, "expected ',' or ')' in parameter list", p->curr.line, p->curr.col);
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }
    }

    if (!expect_tok(p, NY_TOK_RPAREN)) {
        map_free(&val_map);
        map_free(&blk_map);
        return false;
    }

    // Return type
    if (p->curr.kind == NY_TOK_ARROW) {
        advance_tok(p);
        if (p->curr.kind != NY_TOK_IDENT) {
            report_error(p, "expected return type after '->'", p->curr.line, p->curr.col);
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }
        Ny_String ret_name = advance_tok(p).text;
        Ny_Type_ID ret_t = lookup_type(p->module, ret_name);
        if (ret_t == NY_INVALID_TYPE) {
            report_error(p, "unknown return type", p->curr.line, p->curr.col);
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }
        fn->return_type = ret_t;
    }

    if (!expect_tok(p, NY_TOK_SEMICOLON)) {
        map_free(&val_map);
        map_free(&blk_map);
        return false;
    }

    if (p->curr.kind == NY_TOK_DIRECTIVE || p->curr.kind == NY_TOK_EOF) {
        map_free(&val_map);
        map_free(&blk_map);
        return true;
    }

    // Default entry block if not explicitly created
    Ny_Block_ID cur_block = get_or_create_block(fn, &blk_map, ny_str("entry"));
    p->builder.cur_block = cur_block;

    // Body statements
    while (p->curr.kind != NY_TOK_DOUBLE_SEMICOLON && p->curr.kind != NY_TOK_EOF) {
        // Block Label: .label;
        if (p->curr.kind == NY_TOK_BLOCK_LABEL) {
            Ny_String blk_name = advance_tok(p).text;
            if (!expect_tok(p, NY_TOK_SEMICOLON)) {
                map_free(&val_map);
                map_free(&blk_map);
                return false;
            }
            cur_block = get_or_create_block(fn, &blk_map, blk_name);
            p->builder.cur_block = cur_block;
            continue;
        }

        if (p->curr.kind == NY_TOK_SEMICOLON) {
            advance_tok(p);
            continue;
        }

        // SSA Value Assignment: %result = opcode operands;
        if (p->curr.kind == NY_TOK_VALUE) {
            Ny_String res_name = advance_tok(p).text;
            if (!expect_tok(p, NY_TOK_EQUAL)) {
                map_free(&val_map);
                map_free(&blk_map);
                return false;
            }

            if (map_lookup(&val_map, res_name) != UINT32_MAX) {
                char err_buf[128];
                snprintf(err_buf, sizeof(err_buf), "SSA value '%%%.*s' is already defined", (int)res_name.len, res_name.data);
                report_error(p, err_buf, p->curr.line, p->curr.col);
                map_free(&val_map);
                map_free(&blk_map);
                return false;
            }

            if (p->curr.kind != NY_TOK_IDENT && p->curr.kind != NY_TOK_DIRECTIVE) {
                report_error(p, "expected opcode after '='", p->curr.line, p->curr.col);
                map_free(&val_map);
                map_free(&blk_map);
                return false;
            }

            Ny_Token op_tok = advance_tok(p);
            Ny_Opcode op = lookup_opcode(op_tok.text);
            if (op == NY_OPCODE_NONE) {
                report_error(p, "unknown opcode", op_tok.line, op_tok.col);
                map_free(&val_map);
                map_free(&blk_map);
                return false;
            }

            Ny_Operand ops_buf[32];
            size_t op_count = 0;
            Ny_Type_ID res_type = NY_TYPE_I32;

            while (p->curr.kind != NY_TOK_SEMICOLON && p->curr.kind != NY_TOK_EOF) {
                if (p->curr.kind == NY_TOK_VALUE) {
                    Ny_String v_name = advance_tok(p).text;
                    uint32_t val_id = map_lookup(&val_map, v_name);
                    if (val_id == UINT32_MAX) {
                        report_error(p, "unknown SSA value", p->curr.line, p->curr.col);
                        map_free(&val_map);
                        map_free(&blk_map);
                        return false;
                    }
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_value((Ny_Value_ID)val_id);
                    if (op != NY_OPCODE_CONST && op != NY_OPCODE_CONST_NULL &&
                        op != NY_OPCODE_CALL && op != NY_OPCODE_CALL_INDIRECT) {
                        Ny_Value *v = ny_function_get_value(fn, (Ny_Value_ID)val_id);
                        if (v) res_type = v->type;
                    }
                } else if (p->curr.kind == NY_TOK_BLOCK_LABEL) {
                    Ny_String b_name = advance_tok(p).text;
                    Ny_Block_ID b_id = get_or_create_block(fn, &blk_map, b_name);
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_block(b_id);
                } else if (p->curr.kind == NY_TOK_DIRECTIVE) {
                    Ny_String d_name = advance_tok(p).text;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%.*s", (int)d_name.len, d_name.data);
                    if (op == NY_OPCODE_GLOBAL_ADDR) {
                        Ny_Global *glob = ny_module_get_global_by_name(p->module, buf);
                        if (!glob) {
                            Ny_Global_ID gid = ny_module_create_global(p->module, d_name, NY_TYPE_I32, NY_GLOBAL_DATA, 4, NULL, 4);
                            glob = ny_module_get_global(p->module, gid);
                        }
                        if (op_count < 32) ops_buf[op_count++] = ny_operand_global(glob ? glob->id : NY_INVALID_GLOBAL);
                        res_type = NY_TYPE_PTR;
                    } else {
                        Ny_Function *tgt = ny_module_get_function_by_name(p->module, buf);
                        if (!tgt) {
                            Ny_Function_ID fid = ny_module_create_function(p->module, d_name, NY_TYPE_I32, NY_CC_DEFAULT);
                            tgt = ny_module_get_function(p->module, fid);
                        }
                        if (tgt && op == NY_OPCODE_CALL) {
                            res_type = tgt->return_type;
                        }
                        if (op_count < 32) ops_buf[op_count++] = ny_operand_function(tgt ? tgt->id : NY_INVALID_FUNCTION);
                    }
                } else if (p->curr.kind == NY_TOK_INT) {
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_int(advance_tok(p).int_val);
                } else if (p->curr.kind == NY_TOK_FLOAT) {
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_float(advance_tok(p).float_val);
                    res_type = NY_TYPE_F64;
                } else if (p->curr.kind == NY_TOK_IDENT) {
                    Ny_String t_name = advance_tok(p).text;
                    Ny_Type_ID t_id = lookup_type(p->module, t_name);
                    if (t_id != NY_INVALID_TYPE) {
                        if (op_count < 32) ops_buf[op_count++] = ny_operand_type(t_id);
                        res_type = t_id;
                    } else {
                        report_error(p, "unexpected identifier operand", p->curr.line, p->curr.col);
                        map_free(&val_map);
                        map_free(&blk_map);
                        return false;
                    }
                } else {
                    report_error(p, "unexpected operand token", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }

                if (p->curr.kind == NY_TOK_COMMA) {
                    advance_tok(p);
                } else if (p->curr.kind == NY_TOK_SEMICOLON) {
                    advance_tok(p);
                    break;
                } else {
                    report_error(p, "expected ',' or ';' after operand", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
            }

            if (op >= NY_OPCODE_CMP_EQ && op <= NY_OPCODE_FCMP_GE) {
                res_type = NY_TYPE_I8;
            } else if (op == NY_OPCODE_STACK_SLOT || op == NY_OPCODE_STACK_ADDR ||
                       op == NY_OPCODE_ADDR || op == NY_OPCODE_ADDR_OFFSET ||
                       op == NY_OPCODE_GLOBAL_ADDR) {
                res_type = NY_TYPE_PTR;
            }

            Ny_Value_ID res_id = ny_function_create_value(fn, res_type, NY_VAL_INSTRUCTION, NY_INVALID_INST, 0, res_name);
            map_insert(&val_map, res_name, (uint32_t)res_id);

            Ny_Inst_ID inst_id = ny_function_append_instruction(fn, cur_block, op, res_id, ops_buf, op_count, 0);
            ny_function_set_inst_loc(fn, inst_id, (Ny_Loc){1, (uint32_t)op_tok.line, (uint32_t)op_tok.col});
            continue;
        }

        // Directive instructions: @return, @branch, @branch_if, @trap
        if (p->curr.kind == NY_TOK_DIRECTIVE) {
            Ny_Token dir_tok = advance_tok(p);
            Ny_String dir = dir_tok.text;

            if (ny_str_eq_cstr(dir, "return")) {
                Ny_Value_ID ret_val = NY_INVALID_VALUE;
                if (p->curr.kind == NY_TOK_VALUE) {
                    Ny_String v_name = advance_tok(p).text;
                    uint32_t val_id = map_lookup(&val_map, v_name);
                    if (val_id == UINT32_MAX) {
                        report_error(p, "unknown SSA value in @return", p->curr.line, p->curr.col);
                        map_free(&val_map);
                        map_free(&blk_map);
                        return false;
                    }
                    ret_val = (Ny_Value_ID)val_id;
                }
                if (!expect_tok(p, NY_TOK_SEMICOLON)) {
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
                ny_builder_ret(&p->builder, ret_val);
                continue;
            }

            if (ny_str_eq_cstr(dir, "branch")) {
                if (p->curr.kind != NY_TOK_BLOCK_LABEL) {
                    report_error(p, "expected block label after @branch", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
                Ny_String tgt_name = advance_tok(p).text;
                Ny_Block_ID tgt_id = get_or_create_block(fn, &blk_map, tgt_name);
                if (!expect_tok(p, NY_TOK_SEMICOLON)) {
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
                ny_builder_branch(&p->builder, tgt_id);
                continue;
            }

            if (ny_str_eq_cstr(dir, "branch_if")) {
                if (p->curr.kind != NY_TOK_VALUE) {
                    report_error(p, "expected condition value in @branch_if", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
                Ny_String cond_name = advance_tok(p).text;
                uint32_t cond_id = map_lookup(&val_map, cond_name);
                if (cond_id == UINT32_MAX) {
                    report_error(p, "unknown condition value in @branch_if", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }

                if (!expect_tok(p, NY_TOK_COMMA)) {
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }

                if (p->curr.kind != NY_TOK_BLOCK_LABEL) {
                    report_error(p, "expected true block label in @branch_if", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
                Ny_String true_name = advance_tok(p).text;
                Ny_Block_ID true_id = get_or_create_block(fn, &blk_map, true_name);

                if (!expect_tok(p, NY_TOK_COMMA)) {
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }

                if (p->curr.kind != NY_TOK_BLOCK_LABEL) {
                    report_error(p, "expected false block label in @branch_if", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
                Ny_String false_name = advance_tok(p).text;
                Ny_Block_ID false_id = get_or_create_block(fn, &blk_map, false_name);

                if (!expect_tok(p, NY_TOK_SEMICOLON)) {
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
                ny_builder_branch_if(&p->builder, (Ny_Value_ID)cond_id, true_id, false_id);
                continue;
            }

            if (ny_str_eq_cstr(dir, "trap")) {
                if (!expect_tok(p, NY_TOK_SEMICOLON)) {
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
                ny_builder_trap(&p->builder);
                continue;
            }

            report_error(p, "unknown directive instruction", dir_tok.line, dir_tok.col);
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }

        // Plain instruction without result (e.g. store %ptr, %val; or call @callee...)
        if (p->curr.kind == NY_TOK_IDENT) {
            Ny_Token op_tok = advance_tok(p);
            Ny_Opcode op = lookup_opcode(op_tok.text);
            if (op == NY_OPCODE_NONE) {
                report_error(p, "unknown instruction", op_tok.line, op_tok.col);
                map_free(&val_map);
                map_free(&blk_map);
                return false;
            }

            Ny_Operand ops_buf[32];
            size_t op_count = 0;

            while (p->curr.kind != NY_TOK_SEMICOLON && p->curr.kind != NY_TOK_EOF) {
                if (p->curr.kind == NY_TOK_VALUE) {
                    Ny_String v_name = advance_tok(p).text;
                    uint32_t val_id = map_lookup(&val_map, v_name);
                    if (val_id == UINT32_MAX) {
                        report_error(p, "unknown SSA value", p->curr.line, p->curr.col);
                        map_free(&val_map);
                        map_free(&blk_map);
                        return false;
                    }
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_value((Ny_Value_ID)val_id);
                } else if (p->curr.kind == NY_TOK_BLOCK_LABEL) {
                    Ny_String b_name = advance_tok(p).text;
                    Ny_Block_ID b_id = get_or_create_block(fn, &blk_map, b_name);
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_block(b_id);
                } else if (p->curr.kind == NY_TOK_DIRECTIVE) {
                    Ny_String d_name = advance_tok(p).text;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%.*s", (int)d_name.len, d_name.data);
                    Ny_Function *tgt = ny_module_get_function_by_name(p->module, buf);
                    if (!tgt) {
                        Ny_Function_ID fid = ny_module_create_function(p->module, d_name, NY_TYPE_I32, NY_CC_DEFAULT);
                        tgt = ny_module_get_function(p->module, fid);
                    }
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_function(tgt ? tgt->id : NY_INVALID_FUNCTION);
                } else if (p->curr.kind == NY_TOK_INT) {
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_int(advance_tok(p).int_val);
                } else if (p->curr.kind == NY_TOK_FLOAT) {
                    if (op_count < 32) ops_buf[op_count++] = ny_operand_float(advance_tok(p).float_val);
                } else {
                    report_error(p, "unexpected operand token", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }

                if (p->curr.kind == NY_TOK_COMMA) {
                    advance_tok(p);
                } else if (p->curr.kind == NY_TOK_SEMICOLON) {
                    advance_tok(p);
                    break;
                } else {
                    report_error(p, "expected ',' or ';' after operand", p->curr.line, p->curr.col);
                    map_free(&val_map);
                    map_free(&blk_map);
                    return false;
                }
            }

            Ny_Inst_ID inst_id = ny_function_append_instruction(fn, cur_block, op, NY_INVALID_VALUE, ops_buf, op_count, 0);
            ny_function_set_inst_loc(fn, inst_id, (Ny_Loc){1, (uint32_t)op_tok.line, (uint32_t)op_tok.col});
            continue;
        }

        report_error(p, "unexpected token in function body", p->curr.line, p->curr.col);
        map_free(&val_map);
        map_free(&blk_map);
        return false;
    }

    if (!expect_tok(p, NY_TOK_DOUBLE_SEMICOLON)) {
        map_free(&val_map);
        map_free(&blk_map);
        return false;
    }

    // Post-function validation: check that all referenced blocks were defined
    for (size_t i = 0; i < blk_map.count; i++) {
        Ny_Block *blk = ny_function_get_block(fn, (Ny_Block_ID)blk_map.items[i].id);
        if (!blk || blk->first_inst == NY_INVALID_INST) {
            char err_buf[128];
            snprintf(err_buf, sizeof(err_buf), "block '.%.*s' referenced but has no instructions", (int)blk_map.items[i].name.len, blk_map.items[i].name.data);
            report_error(p, err_buf, p->curr.line, p->curr.col);
            map_free(&val_map);
            map_free(&blk_map);
            return false;
        }
    }

    map_free(&val_map);
    map_free(&blk_map);
    return true;
}

static bool parse_global(Ny_Parser *p) {
    advance_tok(p); /* consume @global */

    bool is_readonly = false;
    if (p->curr.kind == NY_TOK_DIRECTIVE && ny_str_eq_cstr(p->curr.text, "readonly")) {
        advance_tok(p);
        is_readonly = true;
    }

    Ny_String name = {0};
    if (p->curr.kind == NY_TOK_DIRECTIVE) {
        name = advance_tok(p).text;
    } else if (p->curr.kind == NY_TOK_IDENT) {
        name = advance_tok(p).text;
    } else {
        report_error(p, "expected global name after @global", p->curr.line, p->curr.col);
        return false;
    }

    if (!expect_tok(p, NY_TOK_COLON)) {
        return false;
    }

    if (p->curr.kind != NY_TOK_IDENT) {
        report_error(p, "expected type for global", p->curr.line, p->curr.col);
        return false;
    }
    Ny_Token type_tok = advance_tok(p);
    Ny_Type_ID type = lookup_type(p->module, type_tok.text);
    if (type == NY_INVALID_TYPE) {
        report_error(p, "unknown type for global", type_tok.line, type_tok.col);
        return false;
    }

    uint32_t type_sz = ny_type_size(&p->module->types, type);
    if (type_sz == 0) type_sz = 4;
    uint32_t align = type_sz > 8 ? 8 : type_sz;

    Ny_Global_Kind kind = is_readonly ? NY_GLOBAL_CONST : NY_GLOBAL_DATA;
    uint8_t init_buf[16] = {0};
    size_t init_size = type_sz;

    if (p->curr.kind == NY_TOK_EQUAL) {
        advance_tok(p);
        if (p->curr.kind == NY_TOK_INT) {
            int64_t val = advance_tok(p).int_val;
            memcpy(init_buf, &val, type_sz <= 8 ? type_sz : 8);
        } else {
            report_error(p, "expected literal integer for global initializer", p->curr.line, p->curr.col);
            return false;
        }
    } else {
        kind = NY_GLOBAL_BSS;
    }

    if (!expect_tok(p, NY_TOK_SEMICOLON)) {
        return false;
    }

    /* Check if already defined */
    char buf[128];
    snprintf(buf, sizeof(buf), "%.*s", (int)name.len, name.data);
    Ny_Global *existing = ny_module_get_global_by_name(p->module, buf);
    if (existing) {
        existing->type = type;
        existing->kind = kind;
        existing->align = align;
        existing->init_size = init_size;
        if (existing->init_bytes) {
            ny_free(existing->init_bytes, existing->init_size);
            existing->init_bytes = NULL;
        }
        if (kind != NY_GLOBAL_BSS) {
            existing->init_bytes = (uint8_t *)ny_alloc(init_size);
            memcpy(existing->init_bytes, init_buf, init_size);
        }
    } else {
        ny_module_create_global(p->module, name, type, kind, align, kind != NY_GLOBAL_BSS ? init_buf : NULL, init_size);
    }

    return true;
}

static bool parse_type_decl(Ny_Parser *p) {
    advance_tok(p); /* consume @type */

    if (p->curr.kind != NY_TOK_IDENT) {
        report_error(p, "expected type name after @type", p->curr.line, p->curr.col);
        return false;
    }
    Ny_Token name_tok = advance_tok(p);

    if (!expect_tok(p, NY_TOK_EQUAL)) {
        return false;
    }

    if (p->curr.kind != NY_TOK_IDENT || !ny_str_eq_cstr(p->curr.text, "struct")) {
        report_error(p, "expected 'struct' in type definition", p->curr.line, p->curr.col);
        return false;
    }
    advance_tok(p); /* consume 'struct' */

    if (!expect_tok(p, NY_TOK_LBRACE)) {
        return false;
    }

    Ny_Type_ID field_types[64];
    size_t field_count = 0;

    while (p->curr.kind != NY_TOK_RBRACE && p->curr.kind != NY_TOK_EOF) {
        if (p->curr.kind != NY_TOK_IDENT) {
            report_error(p, "expected field name in struct definition", p->curr.line, p->curr.col);
            return false;
        }
        advance_tok(p); /* consume field name */

        if (!expect_tok(p, NY_TOK_COLON)) {
            return false;
        }

        if (p->curr.kind != NY_TOK_IDENT) {
            report_error(p, "expected field type in struct definition", p->curr.line, p->curr.col);
            return false;
        }
        Ny_Token ftype_tok = advance_tok(p);
        Ny_Type_ID ftype = lookup_type(p->module, ftype_tok.text);
        if (ftype == NY_INVALID_TYPE) {
            report_error(p, "unknown field type in struct definition", ftype_tok.line, ftype_tok.col);
            return false;
        }

        if (field_count < 64) {
            field_types[field_count++] = ftype;
        }

        if (p->curr.kind == NY_TOK_COMMA) {
            advance_tok(p);
        } else if (p->curr.kind != NY_TOK_RBRACE) {
            report_error(p, "expected ',' or '}' in struct definition", p->curr.line, p->curr.col);
            return false;
        }
    }

    if (!expect_tok(p, NY_TOK_RBRACE)) {
        return false;
    }
    if (!expect_tok(p, NY_TOK_SEMICOLON)) {
        return false;
    }

    char name_buf[128];
    snprintf(name_buf, sizeof(name_buf), "%.*s", (int)name_tok.text.len, name_tok.text.data);
    ny_type_table_add_struct(&p->module->types, name_buf, field_types, field_count);
    return true;
}

bool ny_parse_module(Ny_Parser *p) {
    while (p->curr.kind != NY_TOK_EOF) {
        if (p->curr.kind == NY_TOK_DIRECTIVE && ny_str_eq_cstr(p->curr.text, "function")) {
            if (!parse_function(p)) {
                return false;
            }
        } else if (p->curr.kind == NY_TOK_DIRECTIVE && ny_str_eq_cstr(p->curr.text, "global")) {
            if (!parse_global(p)) {
                return false;
            }
        } else if (p->curr.kind == NY_TOK_DIRECTIVE && ny_str_eq_cstr(p->curr.text, "type")) {
            if (!parse_type_decl(p)) {
                return false;
            }
        } else if (p->curr.kind == NY_TOK_SEMICOLON) {
            advance_tok(p);
        } else {
            report_error(p, "unexpected token at top level", p->curr.line, p->curr.col);
            return false;
        }
    }
    return p->diag_count == 0;
}
