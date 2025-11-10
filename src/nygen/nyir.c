// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nygen/nygen.h"
#include "nybit/support.h"
#include "nybit/ir.h"
#include <stdio.h>
#include <string.h>

#define NYIR_CUSTOM_TYPE_BASE NY_PRIMITIVE_TYPE_COUNT

typedef struct {
    uint8_t *data;
    size_t count;
    size_t capacity;
} Nyir_Writer;

static void writer_ensure(Nyir_Writer *w, size_t extra) {
    ny_buf_grow((void **)&w->data, &w->capacity, w->count + extra - 1, 1);
}

static void writer_bytes(Nyir_Writer *w, const void *src, size_t len) {
    if (len == 0) return;
    writer_ensure(w, len);
    memcpy(w->data + w->count, src, len);
    w->count += len;
}

static void writer_u8(Nyir_Writer *w, uint8_t v) {
    writer_ensure(w, 1);
    w->data[w->count++] = v;
}

static void writer_u16(Nyir_Writer *w, uint16_t v) {
    writer_ensure(w, 2);
    w->data[w->count++] = (uint8_t)(v & 0xFF);
    w->data[w->count++] = (uint8_t)((v >> 8) & 0xFF);
}

static void writer_u32(Nyir_Writer *w, uint32_t v) {
    writer_ensure(w, 4);
    w->data[w->count++] = (uint8_t)(v & 0xFF);
    w->data[w->count++] = (uint8_t)((v >> 8) & 0xFF);
    w->data[w->count++] = (uint8_t)((v >> 16) & 0xFF);
    w->data[w->count++] = (uint8_t)((v >> 24) & 0xFF);
}

static void writer_u64(Nyir_Writer *w, uint64_t v) {
    writer_ensure(w, 8);
    for (size_t i = 0; i < 8; i++) {
        w->data[w->count++] = (uint8_t)((v >> (i * 8)) & 0xFF);
    }
}

static void writer_str(Nyir_Writer *w, Ny_String s) {
    writer_u32(w, (uint32_t)s.len);
    if (s.len > 0 && s.data) {
        writer_bytes(w, s.data, s.len);
    }
}

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
} Nyir_Reader;

static bool reader_bytes(Nyir_Reader *r, void *dst, size_t len) {
    if (r->pos + len > r->size) return false;
    if (len > 0) memcpy(dst, r->data + r->pos, len);
    r->pos += len;
    return true;
}

static bool reader_u8(Nyir_Reader *r, uint8_t *v) {
    return reader_bytes(r, v, 1);
}

static bool reader_u16(Nyir_Reader *r, uint16_t *v) {
    uint8_t b[2];
    if (!reader_bytes(r, b, 2)) return false;
    *v = (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
    return true;
}

static bool reader_u32(Nyir_Reader *r, uint32_t *v) {
    uint8_t b[4];
    if (!reader_bytes(r, b, 4)) return false;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

static bool reader_u64(Nyir_Reader *r, uint64_t *v) {
    uint8_t b[8];
    if (!reader_bytes(r, b, 8)) return false;
    *v = 0;
    for (size_t i = 0; i < 8; i++) {
        *v |= (uint64_t)b[i] << (i * 8);
    }
    return true;
}

static bool reader_str_arena(Nyir_Reader *r, Ny_Arena *arena, Ny_String *out) {
    uint32_t len;
    if (!reader_u32(r, &len)) return false;
    if (len == 0) {
        out->data = nullptr;
        out->len = 0;
        return true;
    }
    char *buf = (char *)ny_arena_alloc(arena, (size_t)len + 1, 1);
    if (!reader_bytes(r, buf, len)) return false;
    buf[len] = '\0';
    out->data = buf;
    out->len = len;
    return true;
}

static char *reader_str_alloc(Nyir_Reader *r, size_t *out_len) {
    uint32_t len;
    if (!reader_u32(r, &len)) return nullptr;
    if (len == 0) {
        *out_len = 0;
        return nullptr;
    }
    char *buf = (char *)ny_alloc((size_t)len + 1);
    if (!reader_bytes(r, buf, len)) {
        ny_free(buf, (size_t)len + 1);
        return nullptr;
    }
    buf[len] = '\0';
    *out_len = len;
    return buf;
}

static void serialize_types(Nyir_Writer *w, const Ny_Type_Table *tt) {
    uint32_t custom_count = 0;
    if (tt->count > NYIR_CUSTOM_TYPE_BASE) {
        custom_count = (uint32_t)(tt->count - NYIR_CUSTOM_TYPE_BASE);
    }
    writer_u32(w, custom_count);

    for (size_t i = NYIR_CUSTOM_TYPE_BASE; i < tt->count; i++) {
        const Ny_Type *t = &tt->types[i];
        writer_u8(w, (uint8_t)t->kind);
        writer_u32(w, t->size);
        writer_u32(w, t->align);
        writer_u32(w, t->elem_type);
        writer_u16(w, t->vector_lanes);
        writer_u32(w, t->array_count);
        writer_u32(w, (uint32_t)t->field_count);
        writer_str(w, t->name);
        for (size_t f = 0; f < t->field_count; f++) {
            writer_u32(w, t->field_types[f]);
        }
        for (size_t f = 0; f < t->field_count; f++) {
            writer_u32(w, t->field_offsets[f]);
        }
    }
}

static void serialize_globals(Nyir_Writer *w, const Ny_Module *mod) {
    writer_u32(w, (uint32_t)mod->global_count);
    for (size_t i = 0; i < mod->global_count; i++) {
        const Ny_Global *g = &mod->globals[i];
        writer_str(w, g->name);
        writer_u32(w, g->type);
        writer_u8(w, (uint8_t)g->kind);
        writer_u32(w, g->align);
        uint32_t isz = (g->init_bytes && g->init_size > 0) ? (uint32_t)g->init_size : 0;
        writer_u32(w, isz);
        if (isz > 0) {
            writer_bytes(w, g->init_bytes, isz);
        }
    }
}

static void serialize_operand(Nyir_Writer *w, const Ny_Operand *op) {
    writer_u8(w, op->kind);
    switch (op->kind) {
    case NY_OP_VALUE:    writer_u32(w, op->val); break;
    case NY_OP_BLOCK:    writer_u32(w, op->blk); break;
    case NY_OP_IMM_INT:  writer_u64(w, (uint64_t)op->imm_int); break;
    case NY_OP_IMM_FLOAT: writer_u64(w, 0); {
        uint64_t bits;
        memcpy(&bits, &op->imm_float, sizeof(bits));
        size_t off = w->count - 8;
        for (size_t i = 0; i < 8; i++) {
            w->data[off + i] = (uint8_t)((bits >> (i * 8)) & 0xFF);
        }
        break;
    }
    case NY_OP_FUNCTION: writer_u32(w, op->fn_id); break;
    case NY_OP_SYMBOL:   writer_u32(w, op->sym_id); break;
    case NY_OP_GLOBAL:   writer_u32(w, op->global_id); break;
    case NY_OP_TYPE:     writer_u32(w, op->type_id); break;
    default: break;
    }
}

static void serialize_functions(Nyir_Writer *w, const Ny_Module *mod) {
    writer_u32(w, (uint32_t)mod->function_count);
    for (size_t i = 0; i < mod->function_count; i++) {
        const Ny_Function *fn = &mod->functions[i];
        writer_str(w, fn->name);
        writer_u32(w, fn->return_type);
        writer_u8(w, fn->call_conv);
        writer_u8(w, fn->is_variadic ? 1 : 0);
        writer_u32(w, fn->entry_block);

        writer_u32(w, (uint32_t)fn->param_count);
        for (size_t p = 0; p < fn->param_count; p++) {
            writer_u32(w, fn->param_types[p]);
            writer_str(w, fn->param_names[p]);
        }

        writer_u32(w, (uint32_t)fn->block_count);
        for (size_t b = 0; b < fn->block_count; b++) {
            const Ny_Block *blk = &fn->blocks[b];
            writer_str(w, blk->name);
            writer_u32(w, blk->first_inst);
            writer_u32(w, blk->last_inst);
            writer_u32(w, blk->inst_count);
            writer_u32(w, (uint32_t)blk->pred_count);
            for (size_t p = 0; p < blk->pred_count; p++) {
                writer_u32(w, blk->preds[p]);
            }
            writer_u32(w, (uint32_t)blk->succ_count);
            for (size_t s = 0; s < blk->succ_count; s++) {
                writer_u32(w, blk->succs[s]);
            }
        }

        writer_u32(w, (uint32_t)fn->inst_count);
        for (size_t i2 = 0; i2 < fn->inst_count; i2++) {
            const Ny_Instruction *inst = &fn->instructions[i2];
            writer_u16(w, inst->opcode);
            writer_u16(w, inst->flags);
            writer_u32(w, inst->result);
            writer_u32(w, inst->op_start);
            writer_u16(w, inst->op_count);
            writer_u32(w, inst->block);
            writer_u32(w, inst->prev);
            writer_u32(w, inst->next);
        }

        writer_u32(w, (uint32_t)fn->val_count);
        for (size_t v = 0; v < fn->val_count; v++) {
            const Ny_Value *val = &fn->values[v];
            writer_u32(w, val->type);
            writer_u8(w, (uint8_t)val->kind);
            writer_u32(w, val->def);
            writer_u32(w, val->index);
            writer_str(w, val->name);
        }

        writer_u32(w, (uint32_t)fn->op_count);
        for (size_t o = 0; o < fn->op_count; o++) {
            serialize_operand(w, &fn->operands[o]);
        }
    }
}

bool nygen_serialize_nyir(const Ny_Module *mod, uint8_t **out_data, size_t *out_size) {
    if (!mod || !out_data || !out_size) return false;
    *out_data = nullptr;
    *out_size = 0;

    Nyir_Writer w = {0};
    writer_u8(&w, NYIR_MAGIC0);
    writer_u8(&w, NYIR_MAGIC1);
    writer_u8(&w, NYIR_MAGIC2);
    writer_u8(&w, NYIR_MAGIC3);
    writer_u16(&w, NYIR_VERSION);
    writer_u16(&w, 0);

    writer_str(&w, mod->name);
    serialize_types(&w, &mod->types);
    serialize_globals(&w, mod);
    serialize_functions(&w, mod);

    uint8_t *exact_buf = (uint8_t *)ny_alloc(w.count);
    if (w.count > 0 && w.data) {
        memcpy(exact_buf, w.data, w.count);
        ny_free(w.data, w.capacity);
    }
    *out_data = exact_buf;
    *out_size = w.count;
    return true;
}

static void add_nyir_diag(Nygen_Diagnostic **diags, size_t *count, const char *msg) {
    if (!diags || !count) return;
    size_t old = *count;
    size_t new_count = old + 1;
    *diags = (Nygen_Diagnostic *)ny_realloc(*diags, old * sizeof(Nygen_Diagnostic), new_count * sizeof(Nygen_Diagnostic));
    size_t len = strlen(msg);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, msg, len + 1);
    (*diags)[old].message = copy;
    (*diags)[old].line = 0;
    (*diags)[old].col = 0;
    *count = new_count;
}

static bool deserialize_types(Nyir_Reader *r, Ny_Context *ctx, Nygen_Diagnostic **diags, size_t *dcount) {
    Ny_Type_Table *tt = &ctx->module.types;

    uint32_t custom_count;
    if (!reader_u32(r, &custom_count)) {
        add_nyir_diag(diags, dcount, "nyir: truncated type section header");
        return false;
    }
    if (custom_count > (1u << 20)) {
        add_nyir_diag(diags, dcount, "nyir: type count exceeds sanity limit");
        return false;
    }

    size_t total_types = NYIR_CUSTOM_TYPE_BASE + custom_count;
    if (total_types > tt->capacity) {
        ny_buf_grow((void **)&tt->types, &tt->capacity, total_types - 1, sizeof(Ny_Type));
    }

    for (uint32_t i = 0; i < custom_count; i++) {
        size_t idx = NYIR_CUSTOM_TYPE_BASE + i;
        Ny_Type *t = &tt->types[idx];
        memset(t, 0, sizeof(*t));

        uint8_t kind;
        if (!reader_u8(r, &kind)) { add_nyir_diag(diags, dcount, "nyir: truncated type kind"); return false; }
        if (kind > NY_TYPE_KIND_STRUCT) { add_nyir_diag(diags, dcount, "nyir: invalid type kind"); return false; }
        t->kind = (Ny_Type_Kind)kind;

        if (!reader_u32(r, &t->size)) { add_nyir_diag(diags, dcount, "nyir: truncated type size"); return false; }
        if (!reader_u32(r, &t->align)) { add_nyir_diag(diags, dcount, "nyir: truncated type align"); return false; }
        if (!reader_u32(r, &t->elem_type)) { add_nyir_diag(diags, dcount, "nyir: truncated type elem_type"); return false; }
        if (t->elem_type != NY_INVALID_TYPE && t->elem_type >= total_types) {
            add_nyir_diag(diags, dcount, "nyir: type elem_type out of bounds");
            return false;
        }
        if (!reader_u16(r, &t->vector_lanes)) { add_nyir_diag(diags, dcount, "nyir: truncated vector_lanes"); return false; }
        if (!reader_u32(r, &t->array_count)) { add_nyir_diag(diags, dcount, "nyir: truncated array_count"); return false; }

        uint32_t field_count;
        if (!reader_u32(r, &field_count)) { add_nyir_diag(diags, dcount, "nyir: truncated field_count"); return false; }
        if (field_count > (1u << 24)) { add_nyir_diag(diags, dcount, "nyir: field count exceeds sanity limit"); return false; }
        t->field_count = field_count;

        size_t name_len;
        char *name = reader_str_alloc(r, &name_len);
        if (name_len > 0 && !name) { add_nyir_diag(diags, dcount, "nyir: truncated type name"); return false; }
        t->name = ny_str_slice(name, name_len);

        if (field_count > 0) {
            t->field_types = (Ny_Type_ID *)ny_alloc(field_count * sizeof(Ny_Type_ID));
            t->field_offsets = (uint32_t *)ny_alloc(field_count * sizeof(uint32_t));
            for (uint32_t f = 0; f < field_count; f++) {
                if (!reader_u32(r, &t->field_types[f])) { add_nyir_diag(diags, dcount, "nyir: truncated field type"); return false; }
                if (t->field_types[f] >= total_types) { add_nyir_diag(diags, dcount, "nyir: field type out of bounds"); return false; }
            }
            for (uint32_t f = 0; f < field_count; f++) {
                if (!reader_u32(r, &t->field_offsets[f])) { add_nyir_diag(diags, dcount, "nyir: truncated field offset"); return false; }
            }
        }

        tt->count = idx + 1;
    }
    return true;
}

static bool deserialize_globals(Nyir_Reader *r, Ny_Context *ctx, Nygen_Diagnostic **diags, size_t *dcount) {
    Ny_Module *mod = &ctx->module;

    uint32_t global_count;
    if (!reader_u32(r, &global_count)) { add_nyir_diag(diags, dcount, "nyir: truncated global count"); return false; }
    if (global_count > (1u << 20)) { add_nyir_diag(diags, dcount, "nyir: global count exceeds sanity limit"); return false; }

    if (global_count > 0) {
        mod->global_capacity = global_count;
        mod->globals = (Ny_Global *)ny_alloc_zero(global_count * sizeof(Ny_Global));
    }
    mod->global_count = global_count;

    for (uint32_t i = 0; i < global_count; i++) {
        Ny_Global *g = &mod->globals[i];
        g->id = i;

        if (!reader_str_arena(r, &ctx->arena, &g->name)) { add_nyir_diag(diags, dcount, "nyir: truncated global name"); return false; }
        if (!reader_u32(r, &g->type)) { add_nyir_diag(diags, dcount, "nyir: truncated global type"); return false; }
        if (g->type >= mod->types.count) { add_nyir_diag(diags, dcount, "nyir: global type out of bounds"); return false; }

        uint8_t kind;
        if (!reader_u8(r, &kind)) { add_nyir_diag(diags, dcount, "nyir: truncated global kind"); return false; }
        if (kind > NY_GLOBAL_BSS) { add_nyir_diag(diags, dcount, "nyir: invalid global kind"); return false; }
        g->kind = (Ny_Global_Kind)kind;

        if (!reader_u32(r, &g->align)) { add_nyir_diag(diags, dcount, "nyir: truncated global align"); return false; }
        if (!reader_u32(r, (uint32_t *)&g->init_size)) { add_nyir_diag(diags, dcount, "nyir: truncated global init_size"); return false; }
        if (g->init_size > (1u << 28)) { add_nyir_diag(diags, dcount, "nyir: global init_size exceeds sanity limit"); return false; }

        if (g->init_size > 0) {
            g->init_bytes = (uint8_t *)ny_alloc(g->init_size);
            if (!reader_bytes(r, g->init_bytes, g->init_size)) { add_nyir_diag(diags, dcount, "nyir: truncated global init data"); return false; }
        }
    }
    return true;
}

static bool deserialize_operand(Nyir_Reader *r, Ny_Operand *op, uint32_t val_count, uint32_t block_count,
                                  uint32_t fn_count, uint32_t global_count, uint32_t type_count,
                                  Nygen_Diagnostic **diags, size_t *dcount) {
    if (!reader_u8(r, &op->kind)) { add_nyir_diag(diags, dcount, "nyir: truncated operand kind"); return false; }
    if (op->kind > NY_OP_TYPE) { add_nyir_diag(diags, dcount, "nyir: invalid operand kind"); return false; }
    op->val = 0;
    switch (op->kind) {
    case NY_OP_VALUE:
        if (!reader_u32(r, &op->val)) { add_nyir_diag(diags, dcount, "nyir: truncated operand value"); return false; }
        if (op->val >= val_count) { add_nyir_diag(diags, dcount, "nyir: operand value out of bounds"); return false; }
        break;
    case NY_OP_BLOCK:
        if (!reader_u32(r, &op->blk)) { add_nyir_diag(diags, dcount, "nyir: truncated operand block"); return false; }
        if (op->blk >= block_count) { add_nyir_diag(diags, dcount, "nyir: operand block out of bounds"); return false; }
        break;
    case NY_OP_IMM_INT:
        if (!reader_u64(r, (uint64_t *)&op->imm_int)) { add_nyir_diag(diags, dcount, "nyir: truncated operand imm_int"); return false; }
        break;
    case NY_OP_IMM_FLOAT: {
        uint64_t bits;
        if (!reader_u64(r, &bits)) { add_nyir_diag(diags, dcount, "nyir: truncated operand imm_float"); return false; }
        memcpy(&op->imm_float, &bits, sizeof(bits));
        break;
    }
    case NY_OP_FUNCTION:
        if (!reader_u32(r, &op->fn_id)) { add_nyir_diag(diags, dcount, "nyir: truncated operand function"); return false; }
        if (op->fn_id >= fn_count) { add_nyir_diag(diags, dcount, "nyir: operand function out of bounds"); return false; }
        break;
    case NY_OP_SYMBOL:
        if (!reader_u32(r, &op->sym_id)) { add_nyir_diag(diags, dcount, "nyir: truncated operand symbol"); return false; }
        break;
    case NY_OP_GLOBAL:
        if (!reader_u32(r, &op->global_id)) { add_nyir_diag(diags, dcount, "nyir: truncated operand global"); return false; }
        if (op->global_id >= global_count) { add_nyir_diag(diags, dcount, "nyir: operand global out of bounds"); return false; }
        break;
    case NY_OP_TYPE:
        if (!reader_u32(r, &op->type_id)) { add_nyir_diag(diags, dcount, "nyir: truncated operand type"); return false; }
        if (op->type_id >= type_count) { add_nyir_diag(diags, dcount, "nyir: operand type out of bounds"); return false; }
        break;
    default:
        break;
    }
    return true;
}

static bool deserialize_functions(Nyir_Reader *r, Ny_Context *ctx, Nygen_Diagnostic **diags, size_t *dcount) {
    Ny_Module *mod = &ctx->module;
    uint32_t type_count = (uint32_t)mod->types.count;
    uint32_t global_count = (uint32_t)mod->global_count;

    uint32_t fn_count;
    if (!reader_u32(r, &fn_count)) { add_nyir_diag(diags, dcount, "nyir: truncated function count"); return false; }
    if (fn_count > (1u << 20)) { add_nyir_diag(diags, dcount, "nyir: function count exceeds sanity limit"); return false; }

    if (fn_count > 0) {
        mod->function_capacity = fn_count;
        mod->functions = (Ny_Function *)ny_alloc_zero(fn_count * sizeof(Ny_Function));
    }
    mod->function_count = fn_count;

    for (uint32_t fi = 0; fi < fn_count; fi++) {
        Ny_Function *fn = &mod->functions[fi];
        fn->id = fi;
        fn->entry_block = NY_INVALID_BLOCK;

        if (!reader_str_arena(r, &ctx->arena, &fn->name)) { add_nyir_diag(diags, dcount, "nyir: truncated function name"); return false; }
        if (!reader_u32(r, &fn->return_type)) { add_nyir_diag(diags, dcount, "nyir: truncated function return_type"); return false; }
        if (fn->return_type >= type_count) { add_nyir_diag(diags, dcount, "nyir: function return_type out of bounds"); return false; }

        if (!reader_u8(r, &fn->call_conv)) { add_nyir_diag(diags, dcount, "nyir: truncated function call_conv"); return false; }
        uint8_t is_var;
        if (!reader_u8(r, &is_var)) { add_nyir_diag(diags, dcount, "nyir: truncated function is_variadic"); return false; }
        fn->is_variadic = is_var != 0;
        if (!reader_u32(r, &fn->entry_block)) { add_nyir_diag(diags, dcount, "nyir: truncated function entry_block"); return false; }

        uint32_t param_count;
        if (!reader_u32(r, &param_count)) { add_nyir_diag(diags, dcount, "nyir: truncated param count"); return false; }
        if (param_count > (1u << 20)) { add_nyir_diag(diags, dcount, "nyir: param count exceeds sanity limit"); return false; }

        fn->param_count = param_count;
        fn->param_capacity = param_count;
        if (param_count > 0) {
            fn->param_types = (Ny_Type_ID *)ny_alloc_zero(param_count * sizeof(Ny_Type_ID));
            fn->param_names = (Ny_String *)ny_alloc_zero(param_count * sizeof(Ny_String));
        }
        for (uint32_t p = 0; p < param_count; p++) {
            if (!reader_u32(r, &fn->param_types[p])) { add_nyir_diag(diags, dcount, "nyir: truncated param type"); return false; }
            if (fn->param_types[p] >= type_count) { add_nyir_diag(diags, dcount, "nyir: param type out of bounds"); return false; }
            if (!reader_str_arena(r, &ctx->arena, &fn->param_names[p])) { add_nyir_diag(diags, dcount, "nyir: truncated param name"); return false; }
        }

        uint32_t block_count;
        if (!reader_u32(r, &block_count)) { add_nyir_diag(diags, dcount, "nyir: truncated block count"); return false; }
        if (block_count > (1u << 20)) { add_nyir_diag(diags, dcount, "nyir: block count exceeds sanity limit"); return false; }

        fn->block_count = block_count;
        fn->block_capacity = block_count;
        if (block_count > 0) {
            fn->blocks = (Ny_Block *)ny_alloc_zero(block_count * sizeof(Ny_Block));
        }
        for (uint32_t b = 0; b < block_count; b++) {
            Ny_Block *blk = &fn->blocks[b];
            blk->id = b;
            blk->first_inst = NY_INVALID_INST;
            blk->last_inst = NY_INVALID_INST;

            if (!reader_str_arena(r, &ctx->arena, &blk->name)) { add_nyir_diag(diags, dcount, "nyir: truncated block name"); return false; }
            if (!reader_u32(r, &blk->first_inst)) { add_nyir_diag(diags, dcount, "nyir: truncated block first_inst"); return false; }
            if (!reader_u32(r, &blk->last_inst)) { add_nyir_diag(diags, dcount, "nyir: truncated block last_inst"); return false; }
            if (!reader_u32(r, &blk->inst_count)) { add_nyir_diag(diags, dcount, "nyir: truncated block inst_count"); return false; }

            uint32_t pred_count;
            if (!reader_u32(r, &pred_count)) { add_nyir_diag(diags, dcount, "nyir: truncated pred count"); return false; }
            if (pred_count > block_count) { add_nyir_diag(diags, dcount, "nyir: pred count exceeds block count"); return false; }
            blk->pred_count = pred_count;
            blk->pred_capacity = pred_count;
            if (pred_count > 0) {
                blk->preds = (Ny_Block_ID *)ny_alloc(pred_count * sizeof(Ny_Block_ID));
                for (uint32_t p = 0; p < pred_count; p++) {
                    if (!reader_u32(r, &blk->preds[p])) { add_nyir_diag(diags, dcount, "nyir: truncated pred entry"); return false; }
                    if (blk->preds[p] >= block_count) { add_nyir_diag(diags, dcount, "nyir: pred block id out of bounds"); return false; }
                }
            }

            uint32_t succ_count;
            if (!reader_u32(r, &succ_count)) { add_nyir_diag(diags, dcount, "nyir: truncated succ count"); return false; }
            if (succ_count > block_count) { add_nyir_diag(diags, dcount, "nyir: succ count exceeds block count"); return false; }
            blk->succ_count = succ_count;
            blk->succ_capacity = succ_count;
            if (succ_count > 0) {
                blk->succs = (Ny_Block_ID *)ny_alloc(succ_count * sizeof(Ny_Block_ID));
                for (uint32_t s = 0; s < succ_count; s++) {
                    if (!reader_u32(r, &blk->succs[s])) { add_nyir_diag(diags, dcount, "nyir: truncated succ entry"); return false; }
                    if (blk->succs[s] >= block_count) { add_nyir_diag(diags, dcount, "nyir: succ block id out of bounds"); return false; }
                }
            }
        }

        uint32_t inst_count;
        if (!reader_u32(r, &inst_count)) { add_nyir_diag(diags, dcount, "nyir: truncated inst count"); return false; }
        if (inst_count > (1u << 24)) { add_nyir_diag(diags, dcount, "nyir: inst count exceeds sanity limit"); return false; }

        fn->inst_count = inst_count;
        fn->inst_capacity = inst_count;
        if (inst_count > 0) {
            fn->instructions = (Ny_Instruction *)ny_alloc_zero(inst_count * sizeof(Ny_Instruction));
        }
        for (uint32_t i2 = 0; i2 < inst_count; i2++) {
            Ny_Instruction *inst = &fn->instructions[i2];
            inst->id = i2;
            if (!reader_u16(r, &inst->opcode)) { add_nyir_diag(diags, dcount, "nyir: truncated inst opcode"); return false; }
            if (inst->opcode >= NY_OPCODE_COUNT) { add_nyir_diag(diags, dcount, "nyir: invalid opcode"); return false; }
            if (!reader_u16(r, &inst->flags)) { add_nyir_diag(diags, dcount, "nyir: truncated inst flags"); return false; }
            if (!reader_u32(r, &inst->result)) { add_nyir_diag(diags, dcount, "nyir: truncated inst result"); return false; }
            if (!reader_u32(r, &inst->op_start)) { add_nyir_diag(diags, dcount, "nyir: truncated inst op_start"); return false; }
            if (!reader_u16(r, &inst->op_count)) { add_nyir_diag(diags, dcount, "nyir: truncated inst op_count"); return false; }
            if (!reader_u32(r, &inst->block)) { add_nyir_diag(diags, dcount, "nyir: truncated inst block"); return false; }
            if (inst->block >= block_count) { add_nyir_diag(diags, dcount, "nyir: inst block out of bounds"); return false; }
            if (!reader_u32(r, &inst->prev)) { add_nyir_diag(diags, dcount, "nyir: truncated inst prev"); return false; }
            if (!reader_u32(r, &inst->next)) { add_nyir_diag(diags, dcount, "nyir: truncated inst next"); return false; }
        }

        uint32_t val_count;
        if (!reader_u32(r, &val_count)) { add_nyir_diag(diags, dcount, "nyir: truncated val count"); return false; }
        if (val_count > (1u << 24)) { add_nyir_diag(diags, dcount, "nyir: val count exceeds sanity limit"); return false; }

        fn->val_count = val_count;
        fn->val_capacity = val_count;
        if (val_count > 0) {
            fn->values = (Ny_Value *)ny_alloc_zero(val_count * sizeof(Ny_Value));
        }
        for (uint32_t v = 0; v < val_count; v++) {
            Ny_Value *val = &fn->values[v];
            val->id = v;
            if (!reader_u32(r, &val->type)) { add_nyir_diag(diags, dcount, "nyir: truncated value type"); return false; }
            if (val->type >= type_count) { add_nyir_diag(diags, dcount, "nyir: value type out of bounds"); return false; }
            uint8_t kind;
            if (!reader_u8(r, &kind)) { add_nyir_diag(diags, dcount, "nyir: truncated value kind"); return false; }
            if (kind > NY_VAL_CONSTANT) { add_nyir_diag(diags, dcount, "nyir: invalid value kind"); return false; }
            val->kind = (Ny_Value_Kind)kind;
            if (!reader_u32(r, &val->def)) { add_nyir_diag(diags, dcount, "nyir: truncated value def"); return false; }
            if (!reader_u32(r, &val->index)) { add_nyir_diag(diags, dcount, "nyir: truncated value index"); return false; }
            if (!reader_str_arena(r, &ctx->arena, &val->name)) { add_nyir_diag(diags, dcount, "nyir: truncated value name"); return false; }
        }

        uint32_t op_count;
        if (!reader_u32(r, &op_count)) { add_nyir_diag(diags, dcount, "nyir: truncated op count"); return false; }
        if (op_count > (1u << 24)) { add_nyir_diag(diags, dcount, "nyir: op count exceeds sanity limit"); return false; }

        fn->op_count = op_count;
        fn->op_capacity = op_count;
        if (op_count > 0) {
            fn->operands = (Ny_Operand *)ny_alloc_zero(op_count * sizeof(Ny_Operand));
        }
        for (uint32_t o = 0; o < op_count; o++) {
            if (!deserialize_operand(r, &fn->operands[o], val_count, block_count, fn_count, global_count, type_count, diags, dcount)) {
                return false;
            }
        }

        for (uint32_t i2 = 0; i2 < inst_count; i2++) {
            Ny_Instruction *inst = &fn->instructions[i2];
            if (inst->op_start + inst->op_count > op_count) {
                add_nyir_diag(diags, dcount, "nyir: instruction operand range out of bounds");
                return false;
            }
        }
    }
    return true;
}

Ny_Context *nygen_deserialize_nyir(const uint8_t *data, size_t size, Nygen_Diagnostic **out_diags, size_t *out_diag_count) {
    if (out_diags) *out_diags = nullptr;
    if (out_diag_count) *out_diag_count = 0;

    if (!data || size < 12) {
        add_nyir_diag(out_diags, out_diag_count, "nyir: file too small for header");
        return nullptr;
    }

    if (data[0] != NYIR_MAGIC0 || data[1] != NYIR_MAGIC1 ||
        data[2] != NYIR_MAGIC2 || data[3] != NYIR_MAGIC3) {
        add_nyir_diag(out_diags, out_diag_count, "nyir: invalid magic bytes");
        return nullptr;
    }

    Nyir_Reader r = { .data = data, .size = size, .pos = 4 };
    uint16_t version;
    if (!reader_u16(&r, &version)) {
        add_nyir_diag(out_diags, out_diag_count, "nyir: truncated version");
        return nullptr;
    }
    if (version != NYIR_VERSION) {
        char msg[128];
        snprintf(msg, sizeof(msg), "nyir: unsupported version %u (expected %u)", version, NYIR_VERSION);
        add_nyir_diag(out_diags, out_diag_count, msg);
        return nullptr;
    }
    uint16_t reserved;
    if (!reader_u16(&r, &reserved)) {
        add_nyir_diag(out_diags, out_diag_count, "nyir: truncated reserved field");
        return nullptr;
    }

    Ny_Context *ctx = (Ny_Context *)ny_alloc_zero(sizeof(Ny_Context));
    ny_context_init(ctx, "nyir_module");

    Ny_String mod_name;
    if (!reader_str_arena(&r, &ctx->arena, &mod_name)) {
        add_nyir_diag(out_diags, out_diag_count, "nyir: truncated module name");
        ny_context_destroy(ctx);
        ny_free(ctx, sizeof(Ny_Context));
        return nullptr;
    }
    ctx->module.name = mod_name;

    if (!deserialize_types(&r, ctx, out_diags, out_diag_count)) {
        ny_context_destroy(ctx);
        ny_free(ctx, sizeof(Ny_Context));
        return nullptr;
    }
    if (!deserialize_globals(&r, ctx, out_diags, out_diag_count)) {
        ny_context_destroy(ctx);
        ny_free(ctx, sizeof(Ny_Context));
        return nullptr;
    }
    if (!deserialize_functions(&r, ctx, out_diags, out_diag_count)) {
        ny_context_destroy(ctx);
        ny_free(ctx, sizeof(Ny_Context));
        return nullptr;
    }

    if (r.pos != r.size) {
        add_nyir_diag(out_diags, out_diag_count, "nyir: trailing data after end of module");
        ny_context_destroy(ctx);
        ny_free(ctx, sizeof(Ny_Context));
        return nullptr;
    }

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_validate_module(&ctx->module, &val_diags);
    if (!valid) {
        for (size_t i = 0; i < val_diags.count; i++) {
            add_nyir_diag(out_diags, out_diag_count, val_diags.items[i].message);
        }
        ny_diagnostic_list_destroy(&val_diags);
        ny_context_destroy(ctx);
        ny_free(ctx, sizeof(Ny_Context));
        return nullptr;
    }
    ny_diagnostic_list_destroy(&val_diags);

    return ctx;
}
