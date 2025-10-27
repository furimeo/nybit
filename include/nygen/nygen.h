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

#ifdef __cplusplus
}
#endif

#endif /* NYGEN_H */
