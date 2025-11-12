// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <nygen/nygen.h>

static const char DEFAULT_DEMO_SOURCE[] =
    "@function add(%a: i32, %b: i32) -> i32;\n"
    ".entry;\n"
    "    %r = add %a, %b;\n"
    "    @return %r;\n"
    ";;\n\n"
    "@function abs(%x: i32) -> i32;\n"
    ".entry;\n"
    "    %zero = const 0;\n"
    "    %cond = cmp.lt.s %x, %zero;\n"
    "    @branch_if %cond, .negative, .positive;\n"
    ".negative;\n"
    "    %r1 = neg %x;\n"
    "    @return %r1;\n"
    ".positive;\n"
    "    @return %x;\n"
    ";;\n\n"
    "@function opt_demo(%x: i32) -> i32;\n"
    ".entry;\n"
    "    %c10 = const 10;\n"
    "    %c20 = const 20;\n"
    "    %sum = add %c10, %c20;\n"
    "    %cond = const 1;\n"
    "    %c999 = const 999;\n"
    "    %dead = mul %x, %c999;\n"
    "    @branch_if %cond, .live_br, .dead_br;\n"
    ".live_br;\n"
    "    %res = add %x, %sum;\n"
    "    @branch .exit;\n"
    ".dead_br;\n"
    "    @return %dead;\n"
    ".exit;\n"
    "    @return %res;\n"
    ";;\n";

static char *read_entire_file(const char *path, size_t *out_len) {
    FILE *f = nullptr;
    if (strcmp(path, "-") == 0) {
        f = stdin;
    } else {
        f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "error: failed to open input file '%s'\n", path);
            return nullptr;
        }
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        if (f != stdin) fclose(f);
        return nullptr;
    }

    for (;;) {
        if (len + 2048 > cap) {
            size_t new_cap = cap * 2;
            char *new_buf = (char *)realloc(buf, new_cap);
            if (!new_buf) {
                free(buf);
                if (f != stdin) fclose(f);
                return nullptr;
            }
            buf = new_buf;
            cap = new_cap;
        }
        size_t n = fread(buf + len, 1, cap - len - 1, f);
        if (n == 0) break;
        len += n;
    }

    buf[len] = '\0';
    if (f != stdin) {
        fclose(f);
    }
    *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    const char *input_file = nullptr;
    const char *output_file = nullptr;
    const char *nyir_input_file = nullptr;
    Nygen_Config config;
    nygen_config_init(&config);

    bool dump_raw = false;
    bool emit_mir = false;
    bool emit_x86 = false;
    bool emit_bytes = false;
    bool emit_obj = false;
    bool emit_exe = false;
    bool emit_ir = false;
    bool emit_nyir = false;
    bool from_nyir = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-O0") == 0 || strcmp(argv[i], "--no-opt") == 0) {
            config.opt_level = NYGEN_OPT_O0;
        } else if (strcmp(argv[i], "-O1") == 0) {
            config.opt_level = NYGEN_OPT_O1;
        } else if (strcmp(argv[i], "-O2") == 0) {
            config.opt_level = NYGEN_OPT_O2;
        } else if (strcmp(argv[i], "--dump-raw") == 0) {
            dump_raw = true;
        } else if (strcmp(argv[i], "--analyze") == 0) {
            config.run_analysis = true;
        } else if (strcmp(argv[i], "--emit-machine-ir") == 0) {
            emit_mir = true;
        } else if (strcmp(argv[i], "--emit-x86") == 0 || strcmp(argv[i], "-S") == 0) {
            emit_x86 = true;
        } else if (strcmp(argv[i], "--emit-bytes") == 0) {
            emit_bytes = true;
        } else if (strcmp(argv[i], "--emit-obj") == 0 || strcmp(argv[i], "-c") == 0) {
            emit_obj = true;
        } else if (strcmp(argv[i], "--emit-exe") == 0) {
            emit_exe = true;
        } else if (strcmp(argv[i], "--emit-ir") == 0) {
            emit_ir = true;
        } else if (strcmp(argv[i], "--emit-nyir") == 0) {
            emit_nyir = true;
        } else if (strcmp(argv[i], "--from-nyir") == 0 && i + 1 < argc) {
            from_nyir = true;
            nyir_input_file = argv[++i];
        } else if ((strcmp(argv[i], "--target") == 0 || strcmp(argv[i], "-target") == 0) && i + 1 < argc) {
            config.target_triple = argv[++i];
        } else if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--debug") == 0) {
            config.debug_info = true;
        } else if (strcmp(argv[i], "--no-unwind") == 0) {
            config.emit_unwind = false;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unrecognized flag '%s'\n", argv[i]);
            return 1;
        } else {
            input_file = argv[i];
        }
    }

    if (dump_raw) {
        config.output_kind = NYGEN_OUTPUT_RAW_IR;
    } else if (emit_mir) {
        config.output_kind = NYGEN_OUTPUT_MACHINE_IR;
    } else if (emit_x86) {
        config.output_kind = NYGEN_OUTPUT_ASM;
    } else if (emit_bytes) {
        config.output_kind = NYGEN_OUTPUT_BYTES;
    } else if (emit_nyir) {
        config.output_kind = NYGEN_OUTPUT_NYIR;
    } else if (emit_obj) {
        config.output_kind = NYGEN_OUTPUT_OBJECT;
    } else if (emit_exe || (!emit_ir && input_file != nullptr && output_file != nullptr)) {
        config.output_kind = NYGEN_OUTPUT_EXECUTABLE;
    } else {
        config.output_kind = NYGEN_OUTPUT_OPT_IR;
    }

    if (config.output_kind == NYGEN_OUTPUT_EXECUTABLE && !output_file) {
#if defined(_WIN32)
        output_file = "a.exe";
#else
        output_file = "a.out";
#endif
    }

    char *source_text = nullptr;
    size_t source_len = 0;
    bool free_source = false;

    if (from_nyir) {
        source_text = read_entire_file(nyir_input_file, &source_len);
        if (!source_text) return 1;
        free_source = true;

        Nygen_Diagnostic *diags = nullptr;
        size_t diag_count = 0;
        Ny_Context *ctx = nygen_load_nyir((const uint8_t *)source_text, source_len, &diags, &diag_count);
        if (!ctx) {
            for (size_t i = 0; i < diag_count; i++) {
                fprintf(stderr, "%s\n", diags[i].message);
            }
            nygen_diagnostics_destroy(diags, diag_count);
            free(source_text);
            return 1;
        }
        nygen_diagnostics_destroy(diags, diag_count);

        Nygen_Result res = nygen_compile_ir(ctx, &config);
        nygen_ir_destroy(ctx);

        if (!res.success) {
            for (size_t i = 0; i < res.diagnostic_count; i++) {
                if (res.diagnostics[i].line > 0) {
                    fprintf(stderr, "error [%u:%u]: %s\n",
                            res.diagnostics[i].line, res.diagnostics[i].col, res.diagnostics[i].message);
                } else {
                    fprintf(stderr, "%s\n", res.diagnostics[i].message);
                }
            }
            nygen_result_destroy(&res);
            free(source_text);
            size_t leaked = nygen_get_current_allocated();
            if (leaked != 0) {
                fprintf(stderr, "memory leak: %zu bytes remaining\n", leaked);
                return 2;
            }
            return 1;
        }

        if (output_file) {
            FILE *f = fopen(output_file, "wb");
            if (!f) {
                fprintf(stderr, "error: failed to open output file '%s'\n", output_file);
                nygen_result_destroy(&res);
                free(source_text);
                return 1;
            }
            if (res.data && res.size > 0) {
                size_t write_len = res.size;
                if (config.output_kind == NYGEN_OUTPUT_RAW_IR ||
                    config.output_kind == NYGEN_OUTPUT_OPT_IR ||
                    config.output_kind == NYGEN_OUTPUT_MACHINE_IR ||
                    config.output_kind == NYGEN_OUTPUT_ASM) {
                    if (write_len > 0 && res.data[write_len - 1] == '\0') {
                        write_len--;
                    }
                }
                fwrite(res.data, 1, write_len, f);
            }
            fclose(f);
        } else {
            if (res.data) {
                if (config.output_kind == NYGEN_OUTPUT_BYTES || config.output_kind == NYGEN_OUTPUT_OBJECT) {
                    for (size_t i = 0; i < res.size; i++) {
                        printf("%02x%c", res.data[i], (i + 1 == res.size || (i + 1) % 16 == 0) ? '\n' : ' ');
                    }
                } else {
                    fputs((const char *)res.data, stdout);
                }
            }
        }

        if (res.analysis_report) {
            fputs(res.analysis_report, stdout);
        }

        nygen_result_destroy(&res);
        free(source_text);

        size_t leaked = nygen_get_current_allocated();
        if (leaked != 0) {
            fprintf(stderr, "memory leak: %zu bytes remaining\n", leaked);
            return 2;
        }
        return 0;
    }

    if (input_file) {
        source_text = read_entire_file(input_file, &source_len);
        if (!source_text) return 1;
        free_source = true;
    } else {
        source_text = (char *)DEFAULT_DEMO_SOURCE;
        source_len = strlen(DEFAULT_DEMO_SOURCE);
    }

    Nygen_Result res;
    bool ok = false;

    if (output_file) {
        ok = nygen_compile_to_file(source_text, source_len, output_file, &config, &res);
    } else {
        res = nygen_compile(source_text, source_len, &config);
        ok = res.success;
        if (ok && res.data) {
            if (config.output_kind == NYGEN_OUTPUT_BYTES || config.output_kind == NYGEN_OUTPUT_OBJECT || config.output_kind == NYGEN_OUTPUT_NYIR) {
                for (size_t i = 0; i < res.size; i++) {
                    printf("%02x%c", res.data[i], (i + 1 == res.size || (i + 1) % 16 == 0) ? '\n' : ' ');
                }
            } else {
                fputs((const char *)res.data, stdout);
            }
        }
    }

    if (ok && res.analysis_report) {
        fputs(res.analysis_report, stdout);
    }

    if (!ok) {
        for (size_t i = 0; i < res.diagnostic_count; i++) {
            if (res.diagnostics[i].line > 0) {
                fprintf(stderr, "parse error [%u:%u]: %s\n",
                        res.diagnostics[i].line, res.diagnostics[i].col, res.diagnostics[i].message);
            } else {
                fprintf(stderr, "%s\n", res.diagnostics[i].message);
            }
        }
    }

    nygen_result_destroy(&res);
    if (free_source) {
        free(source_text);
    }

    size_t leaked = nygen_get_current_allocated();
    if (leaked != 0) {
        fprintf(stderr, "memory leak: %zu bytes remaining\n", leaked);
        return 2;
    }

    return ok ? 0 : 1;
}
