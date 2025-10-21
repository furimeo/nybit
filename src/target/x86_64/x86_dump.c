// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/target_x86_64.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

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
            char *big = (char *)ny_arena_alloc(sb->arena, (size_t)n + 1, 1);
            va_start(args, fmt);
            vsnprintf(big, (size_t)n + 1, fmt, args);
            va_end(args);
            sb_append(sb, big);
        }
    }
}

static void dump_operand(Str_Builder *sb, const X86_Function *fn, const X86_Instruction *inst, X86_Operand op) {
    switch (op.kind) {
    case X86_OP_REG:
        sb_append(sb, x86_reg_name(op.reg));
        break;
    case X86_OP_IMM:
        sb_printf(sb, "%lld", (long long)op.imm);
        break;
    case X86_OP_MEM: {
        const char *size_prefix = "";
        switch (inst->size) {
        case 1: size_prefix = "byte ptr "; break;
        case 2: size_prefix = "word ptr "; break;
        case 4: size_prefix = "dword ptr "; break;
        case 8: size_prefix = "qword ptr "; break;
        default: break;
        }
        sb_append(sb, size_prefix);
        sb_append(sb, "[");

        if (op.mem.is_rip_relative) {
            sb_printf(sb, "rip + %.*s", (int)op.mem.symbol.len, op.mem.symbol.data);
        } else {
            bool has_base = false;
            if (x86_reg_is_valid(op.mem.base)) {
                sb_append(sb, x86_reg_name(op.mem.base));
                has_base = true;
            }
            if (x86_reg_is_valid(op.mem.index)) {
                if (has_base) sb_append(sb, " + ");
                sb_append(sb, x86_reg_name(op.mem.index));
                if (op.mem.scale > 1) {
                    sb_printf(sb, "*%u", (unsigned)op.mem.scale);
                }
                has_base = true;
            }
            if (op.mem.disp != 0 || !has_base) {
                if (has_base && op.mem.disp > 0) sb_printf(sb, " + %d", op.mem.disp);
                else if (has_base && op.mem.disp < 0) sb_printf(sb, " - %d", -op.mem.disp);
                else if (!has_base) sb_printf(sb, "%d", op.mem.disp);
            }
        }
        sb_append(sb, "]");
        break;
    }
    case X86_OP_LABEL: {
        Ny_Block_ID bid = op.label;
        if (bid < fn->block_count && fn->blocks[bid].name.len > 0) {
            sb_printf(sb, ".%.*s", (int)fn->blocks[bid].name.len, fn->blocks[bid].name.data);
        } else {
            sb_printf(sb, ".b%u", (unsigned)bid);
        }
        break;
    }
    case X86_OP_GLOBAL:
        if (op.global_name.len > 0) {
            sb_printf(sb, "%.*s", (int)op.global_name.len, op.global_name.data);
        } else {
            sb_append(sb, "unknown_symbol");
        }
        break;
    case X86_OP_NONE:
    default:
        break;
    }
}

static void dump_inst(Str_Builder *sb, const X86_Function *fn, const X86_Instruction *inst) {
    char mnemonic[32];
    if (inst->opcode == X86_OPC_JCC) {
        snprintf(mnemonic, sizeof(mnemonic), "j%s", x86_cond_suffix((X86_Cond)inst->cond));
    } else if (inst->opcode == X86_OPC_SETCC) {
        snprintf(mnemonic, sizeof(mnemonic), "set%s", x86_cond_suffix((X86_Cond)inst->cond));
    } else {
        snprintf(mnemonic, sizeof(mnemonic), "%s", x86_opcode_mnemonic((X86_Opcode)inst->opcode));
    }

    sb_printf(sb, "    %-7s", mnemonic);

    for (size_t i = 0; i < inst->op_count; i++) {
        if (i > 0) sb_append(sb, ", ");
        else sb_append(sb, " ");
        dump_operand(sb, fn, inst, inst->ops[i]);
    }
    sb_append(sb, "\n");
}

static void dump_function(Str_Builder *sb, const X86_Function *fn) {
    sb_printf(sb, ".globl %.*s\n", (int)fn->name.len, fn->name.data ? fn->name.data : "");
    sb_printf(sb, "%.*s:\n", (int)fn->name.len, fn->name.data ? fn->name.data : "");

    for (size_t b = 0; b < fn->block_count; b++) {
        const X86_Block *blk = &fn->blocks[b];
        sb_printf(sb, ".%.*s:\n", (int)blk->name.len, blk->name.data ? blk->name.data : "");

        for (size_t i = 0; i < blk->inst_count; i++) {
            dump_inst(sb, fn, &blk->instructions[i]);
        }
    }
}

char *x86_dump_func(const void *target_fn, Ny_Arena *scratch) {
    if (!target_fn) return "";
    const X86_Function *fn = (const X86_Function *)target_fn;
    Str_Builder sb;
    sb_init(&sb, scratch);
    dump_function(&sb, fn);
    return sb.data;
}

char *x86_dump_mod(const X86_Module *mod, Ny_Arena *scratch) {
    Str_Builder sb;
    sb_init(&sb, scratch);
    sb_append(&sb, ".intel_syntax noprefix\n.text\n");

    for (size_t i = 0; i < mod->function_count; i++) {
        if (i > 0) sb_append(&sb, "\n");
        dump_function(&sb, &mod->functions[i]);
    }
    return sb.data;
}
