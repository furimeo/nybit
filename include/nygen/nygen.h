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

size_t nygen_get_current_allocated(void);

/* Native JIT Execution API */
typedef struct Ny_JIT_Engine Ny_JIT_Engine;

typedef struct Ny_JIT_Symbol {
    const char *name;
    const void *addr;
} Ny_JIT_Symbol;

typedef const void *(*Ny_JIT_Symbol_Resolver)(const char *name, void *user_data);

typedef struct Ny_JIT_Config {
    const char *target_triple;
    Nygen_Opt_Level opt_level;
    const Ny_JIT_Symbol *symbols;
    size_t symbol_count;
    Ny_JIT_Symbol_Resolver resolver;
    void *resolver_user_data;
} Ny_JIT_Config;

void ny_jit_config_init(Ny_JIT_Config *config);
Ny_JIT_Engine *ny_jit_create(const Ny_JIT_Config *config);
bool ny_jit_compile(Ny_JIT_Engine *jit, const char *source_text, size_t source_len, Nygen_Diagnostic **out_diags, size_t *out_diag_count);
void ny_jit_diagnostics_destroy(Nygen_Diagnostic *diags, size_t diag_count);
void *ny_jit_lookup(Ny_JIT_Engine *jit, const char *symbol_name);
void ny_jit_destroy(Ny_JIT_Engine *jit);

#ifdef __cplusplus
}
#endif

#endif /* NYGEN_H */
