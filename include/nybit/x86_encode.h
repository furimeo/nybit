// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_X86_64_ENCODE_H
#define NYBIT_TARGET_X86_64_ENCODE_H

#include "nybit/support.h"
#include "nybit/target_x86_64.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum X86_Fixup_Kind {
    X86_FIXUP_BRANCH_REL32 = 0,
    X86_FIXUP_CALL_REL32,
    X86_FIXUP_GLOBAL_REL32,
} X86_Fixup_Kind;

typedef struct X86_Fixup {
    X86_Fixup_Kind kind;
    size_t code_offset;
    Ny_Block_ID target_block;
    Ny_String symbol_name;
    int32_t addend;
} X86_Fixup;

typedef struct X86_Relocation {
    X86_Fixup_Kind kind;
    size_t code_offset;
    Ny_String symbol_name;
    int64_t addend;
} X86_Relocation;

typedef struct X86_Code_Buffer {
    uint8_t *bytes;
    size_t count;
    size_t capacity;

    X86_Fixup *fixups;
    size_t fixup_count;
    size_t fixup_capacity;

    X86_Relocation *relocs;
    size_t reloc_count;
    size_t reloc_capacity;
} X86_Code_Buffer;

void x86_buf_init(X86_Code_Buffer *buf);
void x86_buf_destroy(X86_Code_Buffer *buf);
void x86_buf_append_byte(X86_Code_Buffer *buf, uint8_t byte);
void x86_buf_append_bytes(X86_Code_Buffer *buf, const uint8_t *src, size_t len);
void x86_buf_append_i32(X86_Code_Buffer *buf, int32_t val);
void x86_buf_append_i64(X86_Code_Buffer *buf, int64_t val);
void x86_buf_append_fixup(X86_Code_Buffer *buf, X86_Fixup fixup);
void x86_buf_append_reloc(X86_Code_Buffer *buf, X86_Relocation reloc);

bool x86_validate_instruction(const X86_Instruction *inst, Ny_Diagnostic_List *diags);
bool x86_validate_function(const X86_Function *fn, Ny_Diagnostic_List *diags);

bool x86_encode_instruction(X86_Code_Buffer *buf, const X86_Instruction *inst, Ny_Diagnostic_List *diags);
bool x86_encode_function(X86_Code_Buffer *buf, const X86_Function *fn, Ny_Diagnostic_List *diags);

typedef struct X86_Function_Code {
    Ny_String name;
    size_t offset;
    size_t size;
} X86_Function_Code;

typedef struct X86_Encoded_Global {
    Ny_String name;
    Ny_Global_Kind kind;
    uint32_t align;
    size_t offset;
    size_t size;
} X86_Encoded_Global;

typedef struct X86_Encoded_Module {
    Ny_String name;
    X86_Code_Buffer text_section;
    X86_Code_Buffer rodata_section;
    X86_Code_Buffer data_section;
    size_t bss_size;
    uint32_t bss_align;

    X86_Function_Code *functions;
    size_t function_count;
    size_t function_capacity;

    X86_Encoded_Global *globals;
    size_t global_count;
    size_t global_capacity;
} X86_Encoded_Module;

void x86_encoded_mod_init(X86_Encoded_Module *emod, Ny_String name);
void x86_encoded_mod_destroy(X86_Encoded_Module *emod);
bool x86_encode_module(X86_Encoded_Module *out_mod, const X86_Module *mod, Ny_Diagnostic_List *diags);

#ifdef __cplusplus
}
#endif

#endif
