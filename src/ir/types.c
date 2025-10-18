// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"
#include <stdio.h>

static const struct {
    const char *name;
    uint32_t size;
    uint32_t align;
} g_primitive_types[NY_PRIMITIVE_TYPE_COUNT] = {
    {"void", 0,  1},
    {"i8",   1,  1},
    {"i16",  2,  2},
    {"i32",  4,  4},
    {"i64",  8,  8},
    {"i128", 16, 16},
    {"f16",  2,  2},
    {"f32",  4,  4},
    {"f64",  8,  8},
    {"ptr",  8,  8},
};

void ny_type_table_init(Ny_Type_Table *tt) {
    tt->count = 0;
    tt->capacity = 32;
    tt->types = (Ny_Type *)ny_alloc_zero(tt->capacity * sizeof(Ny_Type));

    for (size_t i = 0; i < NY_PRIMITIVE_TYPE_COUNT; i++) {
        Ny_Type *t = &tt->types[i];
        t->kind = NY_TYPE_KIND_PRIMITIVE;
        t->size = g_primitive_types[i].size;
        t->align = g_primitive_types[i].align;
        t->elem_type = NY_INVALID_TYPE;
        t->name = ny_str(g_primitive_types[i].name);
        tt->count++;
    }
}

void ny_type_table_destroy(Ny_Type_Table *tt) {
    if (!tt->types) return;
    for (size_t i = NY_PRIMITIVE_TYPE_COUNT; i < tt->count; i++) {
        Ny_Type *t = &tt->types[i];
        if (t->name.data) {
            ny_free((void *)t->name.data, t->name.len + 1);
        }
        if (t->field_types) {
            ny_free(t->field_types, t->field_count * sizeof(Ny_Type_ID));
        }
    }
    ny_free(tt->types, tt->capacity * sizeof(Ny_Type));
    tt->types = NULL;
    tt->count = 0;
    tt->capacity = 0;
}

const Ny_Type *ny_type_get(const Ny_Type_Table *tt, Ny_Type_ID id) {
    if (id >= tt->count) return NULL;
    return &tt->types[id];
}

const char *ny_type_name(const Ny_Type_Table *tt, Ny_Type_ID id) {
    const Ny_Type *t = ny_type_get(tt, id);
    return t ? t->name.data : "<unknown>";
}

uint32_t ny_type_size(const Ny_Type_Table *tt, Ny_Type_ID id) {
    const Ny_Type *t = ny_type_get(tt, id);
    return t ? t->size : 0;
}

bool ny_type_is_integer(Ny_Type_ID id) {
    return id >= NY_TYPE_I8 && id <= NY_TYPE_I128;
}

bool ny_type_is_float(Ny_Type_ID id) {
    return id >= NY_TYPE_F16 && id <= NY_TYPE_F64;
}

bool ny_type_is_vector(const Ny_Type_Table *tt, Ny_Type_ID id) {
    const Ny_Type *t = ny_type_get(tt, id);
    return t && t->kind == NY_TYPE_KIND_VECTOR;
}

Ny_Type_ID ny_type_table_add_vector(Ny_Type_Table *tt, Ny_Type_ID elem, uint16_t lanes) {
    for (size_t i = 0; i < tt->count; i++) {
        const Ny_Type *cur = &tt->types[i];
        if (cur->kind == NY_TYPE_KIND_VECTOR && cur->elem_type == elem && cur->vector_lanes == lanes) {
            return (Ny_Type_ID)i;
        }
    }

    const Ny_Type *elem_t = ny_type_get(tt, elem);
    assert(elem_t != NULL);

    ny_buf_grow((void **)&tt->types, &tt->capacity, tt->count, sizeof(Ny_Type));
    Ny_Type_ID id = (Ny_Type_ID)tt->count++;
    Ny_Type *t = &tt->types[id];
    memset(t, 0, sizeof(Ny_Type));

    t->kind = NY_TYPE_KIND_VECTOR;
    t->elem_type = elem;
    t->vector_lanes = lanes;
    t->size = elem_t->size * (uint32_t)lanes;
    t->align = elem_t->align;

    char buf[64];
    int n = snprintf(buf, sizeof(buf), "v%u%s", (unsigned)lanes, elem_t->name.data);
    char *name_copy = (char *)ny_alloc((size_t)n + 1);
    memcpy(name_copy, buf, (size_t)n + 1);
    t->name = ny_str_slice(name_copy, (size_t)n);

    return id;
}

Ny_Type_ID ny_type_table_add_pointer(Ny_Type_Table *tt, Ny_Type_ID pointee) {
    (void)tt;
    (void)pointee;
    return NY_TYPE_PTR;
}

Ny_Type_ID ny_type_table_add_array(Ny_Type_Table *tt, Ny_Type_ID elem, uint32_t count) {
    for (size_t i = 0; i < tt->count; i++) {
        const Ny_Type *cur = &tt->types[i];
        if (cur->kind == NY_TYPE_KIND_ARRAY && cur->elem_type == elem && cur->array_count == count) {
            return (Ny_Type_ID)i;
        }
    }

    const Ny_Type *elem_t = ny_type_get(tt, elem);
    assert(elem_t != NULL);

    ny_buf_grow((void **)&tt->types, &tt->capacity, tt->count, sizeof(Ny_Type));
    Ny_Type_ID id = (Ny_Type_ID)tt->count++;
    Ny_Type *t = &tt->types[id];
    memset(t, 0, sizeof(Ny_Type));

    t->kind = NY_TYPE_KIND_ARRAY;
    t->elem_type = elem;
    t->array_count = count;
    t->size = elem_t->size * count;
    t->align = elem_t->align;

    char buf[64];
    int n = snprintf(buf, sizeof(buf), "[%u]%s", (unsigned)count, elem_t->name.data);
    char *name_copy = (char *)ny_alloc((size_t)n + 1);
    memcpy(name_copy, buf, (size_t)n + 1);
    t->name = ny_str_slice(name_copy, (size_t)n);

    return id;
}
