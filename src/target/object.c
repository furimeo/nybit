// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/object.h"
#include <string.h>

void ny_obj_buf_init(Ny_Object_Buffer *buf) {
    buf->bytes = nullptr;
    buf->count = 0;
    buf->capacity = 0;
}

void ny_obj_buf_destroy(Ny_Object_Buffer *buf) {
    if (buf->bytes) {
        ny_free(buf->bytes, buf->capacity);
        buf->bytes = nullptr;
    }
    buf->count = 0;
    buf->capacity = 0;
}

void ny_obj_buf_append_byte(Ny_Object_Buffer *buf, uint8_t byte) {
    ny_buf_grow((void **)&buf->bytes, &buf->capacity, buf->count + 1, sizeof(uint8_t));
    buf->bytes[buf->count++] = byte;
}

void ny_obj_buf_append_bytes(Ny_Object_Buffer *buf, const void *src, size_t len) {
    if (len == 0) {
        return;
    }
    ny_buf_grow((void **)&buf->bytes, &buf->capacity, buf->count + len, sizeof(uint8_t));
    memcpy(buf->bytes + buf->count, src, len);
    buf->count += len;
}

void ny_obj_buf_append_zeros(Ny_Object_Buffer *buf, size_t count) {
    if (count == 0) {
        return;
    }
    ny_buf_grow((void **)&buf->bytes, &buf->capacity, buf->count + count, sizeof(uint8_t));
    memset(buf->bytes + buf->count, 0, count);
    buf->count += count;
}

void ny_obj_buf_align_to(Ny_Object_Buffer *buf, size_t alignment) {
    if (alignment <= 1) {
        return;
    }
    size_t rem = buf->count % alignment;
    if (rem != 0) {
        size_t pad = alignment - rem;
        ny_obj_buf_append_zeros(buf, pad);
    }
}

bool ny_emit_object_module(Ny_Object_Buffer *out_buf, const Ny_Target *target, const X86_Encoded_Module *emod, Ny_Diagnostic_List *diags) {
    if (target && target->emit_object) {
        return target->emit_object(target, emod, out_buf, diags);
    }

    if (target && target->abi == NY_ABI_SYSV_AMD64) {
        return ny_emit_elf64_x86_64(out_buf, emod, diags);
    }
    return ny_emit_coff_x86_64(out_buf, emod, diags);
}
