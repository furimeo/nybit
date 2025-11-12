// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/support.h"

Ny_Mem_Tracker g_ny_mem_tracker = {0, 0, 0, 0};

void *ny_alloc(size_t size) {
    if (size == 0) return NULL;
    void *ptr = malloc(size);
    if (!ptr) {
        abort();
    }
    g_ny_mem_tracker.current_allocated += size;
    if (g_ny_mem_tracker.current_allocated > g_ny_mem_tracker.peak_allocated) {
        g_ny_mem_tracker.peak_allocated = g_ny_mem_tracker.current_allocated;
    }
    g_ny_mem_tracker.total_alloc_count++;
    return ptr;
}

void *ny_alloc_zero(size_t size) {
    void *ptr = ny_alloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void *ny_realloc(void *ptr, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        ny_free(ptr, old_size);
        return NULL;
    }
    if (!ptr) {
        return ny_alloc(new_size);
    }
    void *new_ptr = realloc(ptr, new_size);
    if (!new_ptr) {
        abort();
    }
    g_ny_mem_tracker.current_allocated += new_size;
    g_ny_mem_tracker.current_allocated -= old_size;
    if (g_ny_mem_tracker.current_allocated > g_ny_mem_tracker.peak_allocated) {
        g_ny_mem_tracker.peak_allocated = g_ny_mem_tracker.current_allocated;
    }
    return new_ptr;
}

void ny_free(void *ptr, size_t size) {
    if (!ptr) return;
    free(ptr);
    assert(g_ny_mem_tracker.current_allocated >= size);
    g_ny_mem_tracker.current_allocated -= size;
    g_ny_mem_tracker.total_free_count++;
}

void ny_buf_grow(void **data, size_t *capacity, size_t count, size_t elem_size) {
    if (count < *capacity) return;
    size_t old_cap = *capacity;
    size_t new_cap = old_cap == 0 ? 8 : old_cap * 2;
    while (new_cap <= count) {
        new_cap *= 2;
    }
    *data = ny_realloc(*data, old_cap * elem_size, new_cap * elem_size);
    *capacity = new_cap;
}

void ny_arena_init(Ny_Arena *arena, size_t default_block_size) {
    arena->head = NULL;
    arena->default_block_size = default_block_size > 0 ? default_block_size : 64 * 1024;
    arena->total_allocated = 0;
}

static size_t align_forward(size_t ptr, size_t align) {
    if (align <= 1) return ptr;
    size_t rem = ptr % align;
    return rem == 0 ? ptr : ptr + (align - rem);
}

void *ny_arena_alloc(Ny_Arena *arena, size_t size, size_t align) {
    if (size == 0) return NULL;
    if (align == 0) align = 8;

    Ny_Arena_Block *cur = arena->head;
    if (cur) {
        size_t aligned_used = align_forward(cur->used, align);
        if (aligned_used + size <= cur->capacity) {
            void *ptr = cur->data + aligned_used;
            cur->used = aligned_used + size;
            return ptr;
        }
    }

    size_t block_size = arena->default_block_size;
    if (size + align > block_size) {
        block_size = size + align + 64;
    }

    size_t total_block_bytes = sizeof(Ny_Arena_Block) + block_size;
    Ny_Arena_Block *new_block = (Ny_Arena_Block *)ny_alloc(total_block_bytes);
    new_block->next = arena->head;
    new_block->capacity = block_size;
    new_block->used = 0;
    arena->head = new_block;
    arena->total_allocated += total_block_bytes;

    size_t aligned_used = align_forward(0, align);
    void *ptr = new_block->data + aligned_used;
    new_block->used = aligned_used + size;
    return ptr;
}

void *ny_arena_alloc_zero(Ny_Arena *arena, size_t size, size_t align) {
    void *ptr = ny_arena_alloc(arena, size, align);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

Ny_String ny_arena_strdup(Ny_Arena *arena, const char *src, size_t len) {
    if (!src || len == 0) {
        Ny_String res = { "", 0 };
        return res;
    }
    char *buf = (char *)ny_arena_alloc(arena, len + 1, 1);
    memcpy(buf, src, len);
    buf[len] = '\0';
    Ny_String res = { buf, len };
    return res;
}

void ny_arena_reset(Ny_Arena *arena) {
    Ny_Arena_Block *cur = arena->head;
    while (cur) {
        cur->used = 0;
        cur = cur->next;
    }
}

void ny_arena_destroy(Ny_Arena *arena) {
    Ny_Arena_Block *cur = arena->head;
    while (cur) {
        Ny_Arena_Block *next = cur->next;
        size_t total_block_bytes = sizeof(Ny_Arena_Block) + cur->capacity;
        ny_free(cur, total_block_bytes);
        cur = next;
    }
    arena->head = NULL;
    arena->total_allocated = 0;
}
