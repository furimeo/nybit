// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/machine.h"
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
            char *big = (char *)ny_alloc((size_t)n + 1);
            va_start(args, fmt);
            vsnprintf(big, (size_t)n + 1, fmt, args);
            va_end(args);
            sb_append(sb, big);
            ny_free(big, (size_t)n + 1);
        }
    }
}

static void dump_reg(Str_Builder *sb, Ny_Machine_Reg reg) {
    if (reg.is_virtual) {
        sb_printf(sb, "%%v%u", reg.id);
    } else {
        sb_printf(sb, "%%r%u", (unsigned)reg.phys_reg);
    }
}

static void dump_operand(Str_Builder *sb, const Ny_Machine_Function *fn, Ny_Machine_Operand op) {
    switch (op.kind) {
    case NY_MOP_KIND_REG:
        dump_reg(sb, op.reg);
        break;
    case NY_MOP_KIND_IMM_INT:
        sb_printf(sb, "%lld", (long long)op.imm_int);
        break;
    case NY_MOP_KIND_IMM_FLOAT:
        sb_printf(sb, "%f", op.imm_float);
        break;
    case NY_MOP_KIND_MEM: {
        sb_append(sb, "[");
        bool has_base = false;
        if (op.mem.stack_slot != NY_INVALID_SLOT) {
            sb_printf(sb, "slot#%u", (unsigned)op.mem.stack_slot);
            has_base = true;
        } else if (ny_mreg_is_valid(op.mem.base)) {
            dump_reg(sb, op.mem.base);
            has_base = true;
        }
        if (ny_mreg_is_valid(op.mem.index)) {
            if (has_base) sb_append(sb, " + ");
            dump_reg(sb, op.mem.index);
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
        sb_append(sb, "]");
        break;
    }
    case NY_MOP_KIND_BLOCK: {
        Ny_Machine_Block *b = ny_mfunc_get_block(fn, op.block);
        if (b && b->name.len > 0) {
            sb_printf(sb, ".%.*s", (int)b->name.len, b->name.data);
        } else {
            sb_printf(sb, ".b%u", (unsigned)op.block);
        }
        break;
    }
    case NY_MOP_KIND_SYMBOL:
        if (op.symbol.name.len > 0) {
            sb_printf(sb, "@%.*s", (int)op.symbol.name.len, op.symbol.name.data);
        } else {
            sb_printf(sb, "@sym_%u", (unsigned)op.symbol.id);
        }
        break;
    case NY_MOP_KIND_COND:
        sb_append(sb, ny_mcond_name(op.cond));
        break;
    case NY_MOP_KIND_NONE:
        sb_append(sb, "none");
        break;
    }
}

static void dump_inst(Str_Builder *sb, const Ny_Machine_Function *fn, const Ny_Machine_Instruction *inst) {
    sb_append(sb, "    ");

    if (ny_mreg_is_valid(inst->def_reg)) {
        dump_reg(sb, inst->def_reg);
        sb_printf(sb, ": %s = ", ny_reg_class_name((Ny_Reg_Class)inst->def_reg.reg_class));
    }

    sb_append(sb, ny_mopc_name((Ny_Machine_Opcode)inst->opcode));

    Ny_Machine_Operand *ops = ny_mfunc_get_operands(fn, inst);
    for (size_t i = 0; i < inst->op_count; i++) {
        sb_append(sb, i == 0 ? " " : ", ");
        dump_operand(sb, fn, ops[i]);
    }
    sb_append(sb, "\n");
}

static void dump_function(Str_Builder *sb, const Ny_Machine_Function *fn) {
    sb_printf(sb, "machine_function @%.*s(", (int)fn->name.len, fn->name.data ? fn->name.data : "");
    for (size_t i = 0; i < fn->param_count; i++) {
        if (i > 0) sb_append(sb, ", ");
        dump_reg(sb, fn->param_regs[i]);
        sb_printf(sb, ": %s", ny_reg_class_name((Ny_Reg_Class)fn->param_regs[i].reg_class));
    }
    sb_append(sb, ") {\n");

    for (size_t b = 0; b < fn->block_count; b++) {
        const Ny_Machine_Block *blk = &fn->blocks[b];
        sb_printf(sb, ".%.*s:\n", (int)blk->name.len, blk->name.data ? blk->name.data : "");

        Ny_Inst_ID curr = blk->first_inst;
        while (curr != NY_INVALID_INST) {
            Ny_Machine_Instruction *inst = ny_mfunc_get_inst(fn, curr);
            if (!inst) break;
            dump_inst(sb, fn, inst);
            curr = inst->next;
        }
    }
    sb_append(sb, "}\n");
}

char *ny_mir_dump_module(const Ny_Machine_Module *mod, Ny_Arena *scratch) {
    Str_Builder sb;
    sb_init(&sb, scratch);

    for (size_t i = 0; i < mod->function_count; i++) {
        if (i > 0) sb_append(&sb, "\n");
        dump_function(&sb, &mod->functions[i]);
    }
    return sb.data;
}
