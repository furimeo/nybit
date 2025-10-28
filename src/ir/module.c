// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"

void ny_context_init(Ny_Context *ctx, const char *name) {
    ny_arena_init(&ctx->arena, 1024 * 1024);
    ny_module_init(&ctx->module, ny_arena_strdup(&ctx->arena, name, name ? strlen(name) : 0));
}

void ny_context_destroy(Ny_Context *ctx) {
    ny_module_destroy(&ctx->module);
    ny_arena_destroy(&ctx->arena);
}

void ny_module_init(Ny_Module *mod, Ny_String name) {
    memset(mod, 0, sizeof(Ny_Module));
    mod->name = name;
    ny_type_table_init(&mod->types);
}

void ny_module_destroy(Ny_Module *mod) {
    for (size_t i = 0; i < mod->function_count; i++) {
        ny_function_destroy(&mod->functions[i]);
    }
    if (mod->functions) {
        ny_free(mod->functions, mod->function_capacity * sizeof(Ny_Function));
    }
    for (size_t i = 0; i < mod->global_count; i++) {
        if (mod->globals[i].init_bytes) {
            ny_free(mod->globals[i].init_bytes, mod->globals[i].init_size);
        }
    }
    if (mod->globals) {
        ny_free(mod->globals, mod->global_capacity * sizeof(Ny_Global));
    }
    ny_type_table_destroy(&mod->types);
    memset(mod, 0, sizeof(Ny_Module));
}

Ny_Function_ID ny_module_create_function(Ny_Module *mod, Ny_String name, Ny_Type_ID ret_type, uint8_t call_conv) {
    ny_buf_grow((void **)&mod->functions, &mod->function_capacity, mod->function_count, sizeof(Ny_Function));
    Ny_Function_ID id = (Ny_Function_ID)mod->function_count++;
    Ny_Function *fn = &mod->functions[id];
    ny_function_init(fn, id, name, ret_type, call_conv);
    return id;
}

Ny_Function *ny_module_get_function(Ny_Module *mod, Ny_Function_ID id) {
    if (id >= mod->function_count) return NULL;
    return &mod->functions[id];
}

Ny_Function *ny_module_get_function_by_name(Ny_Module *mod, const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < mod->function_count; i++) {
        if (ny_str_eq_cstr(mod->functions[i].name, name)) {
            return &mod->functions[i];
        }
    }
    return NULL;
}

Ny_Global_ID ny_module_create_global(Ny_Module *mod, Ny_String name, Ny_Type_ID type, Ny_Global_Kind kind, uint32_t align, const void *init_bytes, size_t init_size) {
    ny_buf_grow((void **)&mod->globals, &mod->global_capacity, mod->global_count, sizeof(Ny_Global));
    Ny_Global_ID id = (Ny_Global_ID)mod->global_count++;
    Ny_Global *g = &mod->globals[id];
    memset(g, 0, sizeof(*g));
    g->id = id;
    g->name = name;
    g->type = type;
    g->kind = kind;
    g->align = align > 0 ? align : 1;
    g->init_size = init_size;
    if (init_bytes && init_size > 0) {
        g->init_bytes = (uint8_t *)ny_alloc(init_size);
        memcpy(g->init_bytes, init_bytes, init_size);
    }
    return id;
}

Ny_Global *ny_module_get_global(Ny_Module *mod, Ny_Global_ID id) {
    if (id >= mod->global_count) return NULL;
    return &mod->globals[id];
}

Ny_Global *ny_module_get_global_by_name(Ny_Module *mod, const char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < mod->global_count; i++) {
        if (ny_str_eq_cstr(mod->globals[i].name, name)) {
            return &mod->globals[i];
        }
    }
    return NULL;
}
