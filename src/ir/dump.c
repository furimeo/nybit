// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"
#include <stdio.h>
#include <stdarg.h>

typedef struct Str_Builder {
    char *data;
    size_t len;
    size_t cap;
    Ny_Arena *arena;
} Str_Builder;

static void sb_init(Str_Builder *sb, Ny_Arena *arena) {
    sb->arena = arena;
    sb->cap = 1024;
    sb->len = 0;
    sb->data = (char *)ny_arena_alloc(arena, sb->cap, 1);
    sb->data[0] = '\0';
}

static void sb_append(Str_Builder *sb, const char *s) {
    size_t slen = strlen(s);
    if (sb->len + slen + 1 > sb->cap) {
        size_t new_cap = (sb->len + slen + 1) * 2;
        char *new_data = (char *)ny_arena_alloc(sb->arena, new_cap, 1);
        memcpy(new_data, sb->data, sb->len);
        sb->data = new_data;
        sb->cap = new_cap;
    }
    memcpy(sb->data + sb->len, s, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

static void sb_printf(Str_Builder *sb, const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n > 0) {
        if ((size_t)n < sizeof(buf)) {
            sb_append(sb, buf);
        } else {
            char *big = (char *)malloc((size_t)n + 1);
            va_start(args, fmt);
            vsnprintf(big, (size_t)n + 1, fmt, args);
            va_end(args);
            sb_append(sb, big);
            free(big);
        }
    }
}

static void dump_operand(Str_Builder *sb, const Ny_Module *mod, const Ny_Function *fn, Ny_Operand op) {
    switch (op.kind) {
    case NY_OP_VALUE: {
        Ny_Value *val = ny_function_get_value((Ny_Function *)fn, op.val);
        if (val && val->name.len > 0) {
            sb_printf(sb, "%%%.*s", (int)val->name.len, val->name.data);
        } else {
            sb_printf(sb, "%%%u", (unsigned)op.val);
        }
        break;
    }
    case NY_OP_BLOCK: {
        Ny_Block *blk = ny_function_get_block((Ny_Function *)fn, op.blk);
        if (blk && blk->name.len > 0) {
            sb_printf(sb, ".%.*s", (int)blk->name.len, blk->name.data);
        } else {
            sb_printf(sb, ".b%u", (unsigned)op.blk);
        }
        break;
    }
    case NY_OP_IMM_INT:
        sb_printf(sb, "%lld", (long long)op.imm_int);
        break;
    case NY_OP_IMM_FLOAT:
        sb_printf(sb, "%f", op.imm_float);
        break;
    case NY_OP_FUNCTION: {
        Ny_Function *tgt = ny_module_get_function((Ny_Module *)mod, op.fn_id);
        if (tgt && tgt->name.len > 0) {
            sb_printf(sb, "@%.*s", (int)tgt->name.len, tgt->name.data);
        } else {
            sb_printf(sb, "@fn_%u", (unsigned)op.fn_id);
        }
        break;
    }
    case NY_OP_SYMBOL:
        sb_printf(sb, "@sym_%u", (unsigned)op.sym_id);
        break;
    case NY_OP_TYPE:
        sb_append(sb, ny_type_name(&mod->types, op.type_id));
        break;
    case NY_OP_NONE:
        sb_append(sb, "none");
        break;
    }
}

static void dump_instruction(Str_Builder *sb, const Ny_Module *mod, const Ny_Function *fn, const Ny_Instruction *inst) {
    sb_append(sb, "    ");

    if (inst->result != NY_INVALID_VALUE) {
        Ny_Value *val = ny_function_get_value((Ny_Function *)fn, inst->result);
        if (val && val->name.len > 0) {
            sb_printf(sb, "%%%.*s = ", (int)val->name.len, val->name.data);
        } else {
            sb_printf(sb, "%%%u = ", (unsigned)inst->result);
        }
    }

    sb_append(sb, ny_opcode_name((Ny_Opcode)inst->opcode));

    Ny_Operand *ops = ny_function_get_operands((Ny_Function *)fn, inst);
    for (size_t i = 0; i < inst->op_count; i++) {
        sb_append(sb, i == 0 ? " " : ", ");
        dump_operand(sb, mod, fn, ops[i]);
    }
    sb_append(sb, ";\n");
}

static void dump_function(Str_Builder *sb, const Ny_Module *mod, const Ny_Function *fn) {
    sb_printf(sb, "@function %.*s(", (int)fn->name.len, fn->name.data ? fn->name.data : "");
    for (size_t i = 0; i < fn->param_count; i++) {
        if (i > 0) sb_append(sb, ", ");
        Ny_String p_name = i < fn->param_count ? fn->param_names[i] : (Ny_String){0};
        if (p_name.len > 0) {
            sb_printf(sb, "%%%.*s: %s", (int)p_name.len, p_name.data, ny_type_name(&mod->types, fn->param_types[i]));
        } else {
            sb_printf(sb, "%%%u: %s", (unsigned)i, ny_type_name(&mod->types, fn->param_types[i]));
        }
    }
    if (fn->is_variadic) {
        if (fn->param_count > 0) sb_append(sb, ", ...");
        else sb_append(sb, "...");
    }
    sb_printf(sb, ") -> %s;\n", ny_type_name(&mod->types, fn->return_type));

    for (size_t b = 0; b < fn->block_count; b++) {
        const Ny_Block *blk = &fn->blocks[b];
        sb_printf(sb, ".%.*s;\n", (int)blk->name.len, blk->name.data ? blk->name.data : "");

        Ny_Inst_ID curr = blk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Instruction *inst = ny_function_get_instruction((Ny_Function *)fn, curr);
            if (!inst) break;
            if (inst->opcode != NY_OPCODE_NONE) {
                dump_instruction(sb, mod, fn, inst);
            }
            curr = inst->next;
        }
    }
    sb_append(sb, ";;\n");
}

char *ny_dump_module(const Ny_Module *mod, Ny_Arena *scratch) {
    Str_Builder sb;
    sb_init(&sb, scratch);

    for (size_t i = 0; i < mod->function_count; i++) {
        if (i > 0) sb_append(&sb, "\n");
        dump_function(&sb, mod, &mod->functions[i]);
    }
    return sb.data;
}
