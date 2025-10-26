// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_OBJECT_H
#define NYBIT_OBJECT_H

#include "nybit/support.h"
#include "nybit/target.h"
#include "nybit/x86_encode.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Ny_Object_Format {
    NY_OBJECT_FMT_ELF64 = 0,
    NY_OBJECT_FMT_COFF,
} Ny_Object_Format;

typedef struct Ny_Object_Buffer {
    uint8_t *bytes;
    size_t count;
    size_t capacity;
} Ny_Object_Buffer;

void ny_obj_buf_init(Ny_Object_Buffer *buf);
void ny_obj_buf_destroy(Ny_Object_Buffer *buf);
void ny_obj_buf_append_byte(Ny_Object_Buffer *buf, uint8_t byte);
void ny_obj_buf_append_bytes(Ny_Object_Buffer *buf, const void *src, size_t len);
void ny_obj_buf_append_zeros(Ny_Object_Buffer *buf, size_t count);
void ny_obj_buf_align_to(Ny_Object_Buffer *buf, size_t alignment);

bool ny_emit_elf64_x86_64(Ny_Object_Buffer *out_buf, const X86_Encoded_Module *emod, Ny_Diagnostic_List *diags);
bool ny_emit_coff_x86_64(Ny_Object_Buffer *out_buf, const X86_Encoded_Module *emod, Ny_Diagnostic_List *diags);

bool ny_emit_object_module(Ny_Object_Buffer *out_buf, const Ny_Target *target, const X86_Encoded_Module *emod, Ny_Diagnostic_List *diags);

bool ny_link_executable(const char *obj_path, const char *out_exe_path, const Ny_Target *target, Ny_Diagnostic_List *diags);
bool ny_link_executable_with_extra(const char *const *obj_paths, size_t obj_count, const char *out_exe_path, const Ny_Target *target, Ny_Diagnostic_List *diags);

#ifdef __cplusplus
}
#endif

#endif
