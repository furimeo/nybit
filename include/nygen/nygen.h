// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYGEN_H
#define NYGEN_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Nygen_Output_Kind {
    NYGEN_OUTPUT_RAW_IR = 0,
    NYGEN_OUTPUT_OPT_IR,
    NYGEN_OUTPUT_MACHINE_IR,
    NYGEN_OUTPUT_ASM,
    NYGEN_OUTPUT_BYTES,
    NYGEN_OUTPUT_OBJECT,
    NYGEN_OUTPUT_EXECUTABLE,
} Nygen_Output_Kind;

typedef enum Nygen_Opt_Level {
    NYGEN_OPT_O0 = 0,
    NYGEN_OPT_O1,
    NYGEN_OPT_O2,
} Nygen_Opt_Level;

typedef struct Nygen_Config {
    const char *target_triple;
    Nygen_Opt_Level opt_level;
    Nygen_Output_Kind output_kind;
    bool run_analysis;
    const char *module_name;
} Nygen_Config;

typedef struct Nygen_Diagnostic {
    uint32_t line;
    uint32_t col;
    const char *message;
} Nygen_Diagnostic;

typedef struct Nygen_Result {
    bool success;
    uint8_t *data;
    size_t size;
    Nygen_Diagnostic *diagnostics;
    size_t diagnostic_count;
    char *analysis_report;
    void *internal_handle;
} Nygen_Result;

void nygen_config_init(Nygen_Config *config);

Nygen_Result nygen_compile(const char *source_text, size_t source_len, const Nygen_Config *config);

bool nygen_compile_to_file(const char *source_text, size_t source_len, const char *out_path, const Nygen_Config *config, Nygen_Result *out_res);

bool nygen_link_executable(const char *obj_path, const char *out_exe_path, const char *target_triple, Nygen_Result *out_res);
bool nygen_link_executable_extra(const char *const *obj_paths, size_t obj_count, const char *out_exe_path, const char *target_triple, Nygen_Result *out_res);

void nygen_result_destroy(Nygen_Result *result);
void nygen_diagnostics_destroy(Nygen_Diagnostic *diags, size_t diag_count);

size_t nygen_get_current_allocated(void);

/* Encoded module artifact for JIT consumption */
typedef enum Nygen_Symbol_Kind {
    NYGEN_SYM_FUNCTION = 0,
    NYGEN_SYM_RODATA,
    NYGEN_SYM_DATA,
    NYGEN_SYM_BSS,
} Nygen_Symbol_Kind;

typedef enum Nygen_Reloc_Kind {
    NYGEN_RELOC_CALL_REL32 = 0,
    NYGEN_RELOC_GLOBAL_REL32,
} Nygen_Reloc_Kind;

typedef struct Nygen_JIT_Symbol {
    const char *name;
    Nygen_Symbol_Kind kind;
    size_t offset;
    size_t size;
} Nygen_JIT_Symbol;

typedef struct Nygen_JIT_Reloc {
    Nygen_Reloc_Kind kind;
    size_t code_offset;
    const char *symbol_name;
    int64_t addend;
} Nygen_JIT_Reloc;

typedef struct Nygen_Encoded_Module {
    const uint8_t *text;
    size_t text_size;
    const uint8_t *rodata;
    size_t rodata_size;
    const uint8_t *data;
    size_t data_size;
    size_t bss_size;
    const Nygen_JIT_Symbol *symbols;
    size_t symbol_count;
    const Nygen_JIT_Reloc *relocs;
    size_t reloc_count;
    void *internal;
} Nygen_Encoded_Module;

bool nygen_compile_encoded(const char *source_text, size_t source_len, const Nygen_Config *config,
                           Nygen_Encoded_Module *out_module,
                           Nygen_Diagnostic **out_diags, size_t *out_diag_count);
void nygen_encoded_module_destroy(Nygen_Encoded_Module *module);

#ifdef __cplusplus
}
#endif

#endif /* NYGEN_H */
