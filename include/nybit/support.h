// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_SUPPORT_H
#define NYBIT_SUPPORT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef uint32_t Ny_Value_ID;
typedef uint32_t Ny_Inst_ID;
typedef uint32_t Ny_Block_ID;
typedef uint32_t Ny_Function_ID;
typedef uint32_t Ny_Symbol_ID;
typedef uint32_t Ny_Global_ID;
typedef uint32_t Ny_Type_ID;
typedef uint32_t Ny_Reg_ID;
typedef uint32_t Ny_Slot_ID;

#define NY_INVALID_VALUE    UINT32_MAX
#define NY_INVALID_INST     UINT32_MAX
#define NY_INVALID_BLOCK    UINT32_MAX
#define NY_INVALID_FUNCTION UINT32_MAX
#define NY_INVALID_SYMBOL   UINT32_MAX
#define NY_INVALID_GLOBAL   UINT32_MAX
#define NY_INVALID_TYPE     UINT32_MAX
#define NY_INVALID_REG      UINT32_MAX
#define NY_INVALID_SLOT     UINT32_MAX

typedef struct Ny_Loc {
    uint32_t file_id;
    uint32_t line;
    uint32_t col;
} Ny_Loc;

typedef struct Ny_String {
    const char *data;
    size_t len;
} Ny_String;

static inline Ny_String ny_str(const char *s) {
    Ny_String str;
    str.data = s;
    str.len = s ? strlen(s) : 0;
    return str;
}

static inline Ny_String ny_str_slice(const char *data, size_t len) {
    Ny_String str;
    str.data = data;
    str.len = len;
    return str;
}

static inline bool ny_str_eq(Ny_String a, Ny_String b) {
    if (a.len != b.len) return false;
    if (a.len == 0) return true;
    return memcmp(a.data, b.data, a.len) == 0;
}

static inline bool ny_str_eq_cstr(Ny_String a, const char *b) {
    size_t blen = b ? strlen(b) : 0;
    if (a.len != blen) return false;
    if (a.len == 0) return true;
    return memcmp(a.data, b, a.len) == 0;
}

// Memory tracking
typedef struct Ny_Mem_Tracker {
    size_t current_allocated;
    size_t peak_allocated;
    size_t total_alloc_count;
    size_t total_free_count;
} Ny_Mem_Tracker;

extern Ny_Mem_Tracker g_ny_mem_tracker;

void *ny_alloc(size_t size);
void *ny_alloc_zero(size_t size);
void *ny_realloc(void *ptr, size_t old_size, size_t new_size);
void ny_free(void *ptr, size_t size);

// Buffer growth helper
void ny_buf_grow(void **data, size_t *capacity, size_t count, size_t elem_size);

// Arena allocator
typedef struct Ny_Arena_Block {
    struct Ny_Arena_Block *next;
    size_t capacity;
    size_t used;
    uint8_t data[];
} Ny_Arena_Block;

typedef struct Ny_Arena {
    Ny_Arena_Block *head;
    size_t default_block_size;
    size_t total_allocated;
} Ny_Arena;

void ny_arena_init(Ny_Arena *arena, size_t default_block_size);
void *ny_arena_alloc(Ny_Arena *arena, size_t size, size_t align);
void *ny_arena_alloc_zero(Ny_Arena *arena, size_t size, size_t align);
Ny_String ny_arena_strdup(Ny_Arena *arena, const char *src, size_t len);
void ny_arena_reset(Ny_Arena *arena);
void ny_arena_destroy(Ny_Arena *arena);

// Bitset utilities
static inline void ny_bitset_set(uint64_t *words, size_t words_per_block, uint32_t blk, uint32_t val) {
    size_t idx = (size_t)blk * words_per_block + (val / 64);
    words[idx] |= ((uint64_t)1 << (val % 64));
}

static inline bool ny_bitset_test(const uint64_t *words, size_t words_per_block, uint32_t blk, uint32_t val) {
    size_t idx = (size_t)blk * words_per_block + (val / 64);
    return (words[idx] & ((uint64_t)1 << (val % 64))) != 0;
}

#endif // NYBIT_SUPPORT_H
