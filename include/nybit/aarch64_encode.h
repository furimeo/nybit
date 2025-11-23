// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_AARCH64_ENCODE_H
#define NYBIT_TARGET_AARCH64_ENCODE_H

#include "nybit/support.h"
#include "nybit/target_aarch64.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AArch64_Fixup_Kind {
    AARCH64_FIXUP_BRANCH26 = 0,
    AARCH64_FIXUP_BRANCH19,
    AARCH64_FIXUP_CALL26,
    AARCH64_FIXUP_ADRP,
    AARCH64_FIXUP_ADD_LO12,
    AARCH64_FIXUP_LDST_LO12,
} AArch64_Fixup_Kind;

typedef struct AArch64_Fixup {
    AArch64_Fixup_Kind kind;
    size_t code_offset;
    Ny_Block_ID target_block;
    Ny_String symbol_name;
    int32_t addend;
} AArch64_Fixup;

typedef struct AArch64_Relocation {
    AArch64_Fixup_Kind kind;
    size_t code_offset;
    Ny_String symbol_name;
    int64_t addend;
} AArch64_Relocation;

typedef struct AArch64_Code_Buffer {
    uint8_t *bytes;
    size_t count;
    size_t capacity;

    AArch64_Fixup *fixups;
    size_t fixup_count;
    size_t fixup_capacity;

    AArch64_Relocation *relocs;
    size_t reloc_count;
    size_t reloc_capacity;
} AArch64_Code_Buffer;

void aarch64_buf_init(AArch64_Code_Buffer *buf);
void aarch64_buf_destroy(AArch64_Code_Buffer *buf);
void aarch64_buf_append_bytes(AArch64_Code_Buffer *buf, const uint8_t *src, size_t len);
void aarch64_buf_append_word(AArch64_Code_Buffer *buf, uint32_t word);
void aarch64_buf_append_fixup(AArch64_Code_Buffer *buf, AArch64_Fixup fixup);
void aarch64_buf_append_reloc(AArch64_Code_Buffer *buf, AArch64_Relocation reloc);

bool aarch64_validate_function(const AArch64_Function *fn, Ny_Diagnostic_List *diags);
bool aarch64_encode_instruction(AArch64_Code_Buffer *buf, const AArch64_Instruction *inst, Ny_Diagnostic_List *diags);
bool aarch64_encode_function(AArch64_Code_Buffer *buf, const AArch64_Function *fn, Ny_Diagnostic_List *diags);

typedef struct AArch64_Function_Code {
    Ny_String name;
    size_t offset;
    size_t size;
} AArch64_Function_Code;

typedef struct AArch64_Encoded_Global {
    Ny_String name;
    Ny_Global_Kind kind;
    uint32_t align;
    size_t offset;
    size_t size;
} AArch64_Encoded_Global;

typedef struct AArch64_Encoded_Module {
    Ny_String name;
    AArch64_Code_Buffer text_section;
    AArch64_Code_Buffer rodata_section;
    AArch64_Code_Buffer data_section;
    size_t bss_size;
    uint32_t bss_align;

    AArch64_Function_Code *functions;
    size_t function_count;
    size_t function_capacity;

    AArch64_Encoded_Global *globals;
    size_t global_count;
    size_t global_capacity;

    bool emit_unwind;
    bool debug_info;
    const AArch64_Module *source_mod;
} AArch64_Encoded_Module;

void aarch64_encoded_mod_init(AArch64_Encoded_Module *emod, Ny_String name);
void aarch64_encoded_mod_destroy(AArch64_Encoded_Module *emod);
bool aarch64_encode_module(AArch64_Encoded_Module *out_mod, const AArch64_Module *mod, Ny_Diagnostic_List *diags);

#ifdef __cplusplus
}
#endif

#endif
