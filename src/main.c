// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <nygen/nygen.h>
#include <nylink/nylink.h>
#include <nybit/support.h>

static uint8_t *read_checked_file(const char *path, size_t *out_size, char *err_buf, size_t err_buf_size) {
    if (!path || !out_size) return nullptr;
    *out_size = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "failed to open '%s': %s", path, strerror(errno));
        }
        return nullptr;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "failed to seek '%s'", path);
        }
        fclose(f);
        return nullptr;
    }

    long sz = ftell(f);
    if (sz < 0) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "failed to determine size of '%s'", path);
        }
        fclose(f);
        return nullptr;
    }

    if (sz == 0) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "file '%s' is empty (0 bytes)", path);
        }
        fclose(f);
        return nullptr;
    }

    const size_t MAX_LINK_FILE_SIZE = 512 * 1024 * 1024; /* 512 MB */
    if ((size_t)sz > MAX_LINK_FILE_SIZE) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "file '%s' exceeds maximum supported size (512MB)", path);
        }
        fclose(f);
        return nullptr;
    }

    if (fseek(f, 0, SEEK_SET) != 0) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "failed to seek '%s'", path);
        }
        fclose(f);
        return nullptr;
    }

    uint8_t *buf = (uint8_t *)ny_alloc((size_t)sz);
    if (!buf) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "out of memory allocating %ld bytes for '%s'", sz, path);
        }
        fclose(f);
        return nullptr;
    }

    size_t read_bytes = fread(buf, 1, (size_t)sz, f);
    fclose(f);

    if (read_bytes != (size_t)sz) {
        if (err_buf && err_buf_size > 0) {
            snprintf(err_buf, err_buf_size, "read error on '%s': expected %ld bytes, got %zu", path, sz, read_bytes);
        }
        ny_free(buf, (size_t)sz);
        return nullptr;
    }

    *out_size = (size_t)sz;
    return buf;
}

static bool parse_uint64_safe(const char *str, uint64_t *out_val) {
    if (!str || str[0] == '\0') return false;
    char *endptr = nullptr;
    errno = 0;
    int base = 10;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        base = 16;
    }
    unsigned long long val = strtoull(str, &endptr, base);
    if (errno != 0 || endptr == str || *endptr != '\0') {
        return false;
    }
    *out_val = (uint64_t)val;
    return true;
}

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static int do_cli_link(int argc, char **argv) {
    const char *output_file = nullptr;
    const char *target_str = nullptr;
    const char *entry_point = nullptr;
    const char *base_str = nullptr;
    bool is_shared = false;
    bool is_pie = false;
    const char *dynamic_linker = nullptr;
    const char *soname = nullptr;
    const char *rpath = nullptr;
    const char **needed_libs = nullptr;
    size_t needed_lib_count = 0;
    size_t needed_lib_capacity = 0;
    const char **exports = nullptr;
    size_t export_count = 0;
    size_t export_capacity = 0;
    const char *implib_path = nullptr;

    const char **search_dirs = nullptr;
    size_t search_dir_count = 0;
    size_t search_dir_capacity = 0;

    typedef struct Link_Input {
        char *path;
        bool is_lib_name;
    } Link_Input;

    Link_Input *inputs = nullptr;
    size_t input_count = 0;
    size_t input_capacity = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strncmp(arg, "-o", 2) == 0 && arg[2] != '\0') {
            output_file = arg + 2;
        } else if (strcmp(arg, "--shared") == 0 || strcmp(arg, "-shared") == 0) {
            is_shared = true;
        } else if (strcmp(arg, "--pie") == 0 || strcmp(arg, "-pie") == 0) {
            is_pie = true;
        } else if (strncmp(arg, "--dynamic-linker=", 17) == 0) {
            dynamic_linker = arg + 17;
        } else if (strcmp(arg, "--dynamic-linker") == 0 && i + 1 < argc) {
            dynamic_linker = argv[++i];
        } else if (strncmp(arg, "--soname=", 9) == 0) {
            soname = arg + 9;
        } else if (strcmp(arg, "--soname") == 0 && i + 1 < argc) {
            soname = argv[++i];
        } else if (strncmp(arg, "--rpath=", 8) == 0) {
            rpath = arg + 8;
        } else if (strcmp(arg, "--rpath") == 0 && i + 1 < argc) {
            rpath = argv[++i];
        } else if (strncmp(arg, "-rpath=", 7) == 0) {
            rpath = arg + 7;
        } else if (strcmp(arg, "-rpath") == 0 && i + 1 < argc) {
            rpath = argv[++i];
        } else if (strncmp(arg, "--export=", 9) == 0) {
            const char *sym = arg + 9;
            ny_buf_grow((void **)&exports, &export_capacity, export_count, sizeof(const char *));
            exports[export_count++] = sym;
        } else if (strcmp(arg, "--export") == 0 && i + 1 < argc) {
            const char *sym = argv[++i];
            ny_buf_grow((void **)&exports, &export_capacity, export_count, sizeof(const char *));
            exports[export_count++] = sym;
        } else if (strncmp(arg, "--implib=", 9) == 0) {
            implib_path = arg + 9;
        } else if (strcmp(arg, "--implib") == 0 && i + 1 < argc) {
            implib_path = argv[++i];
        } else if (strncmp(arg, "--needed=", 9) == 0) {
            const char *lib = arg + 9;
            ny_buf_grow((void **)&needed_libs, &needed_lib_capacity, needed_lib_count, sizeof(const char *));
            needed_libs[needed_lib_count++] = lib;
        } else if (strcmp(arg, "--needed") == 0 && i + 1 < argc) {
            const char *lib = argv[++i];
            ny_buf_grow((void **)&needed_libs, &needed_lib_capacity, needed_lib_count, sizeof(const char *));
            needed_libs[needed_lib_count++] = lib;
        } else if (strncmp(arg, "--target=", 9) == 0) {
            target_str = arg + 9;
        } else if (strcmp(arg, "--target") == 0 && i + 1 < argc) {
            target_str = argv[++i];
        } else if (strcmp(arg, "-target") == 0 && i + 1 < argc) {
            target_str = argv[++i];
        } else if (strncmp(arg, "--entry=", 8) == 0) {
            entry_point = arg + 8;
        } else if (strcmp(arg, "--entry") == 0 && i + 1 < argc) {
            entry_point = argv[++i];
        } else if (strncmp(arg, "--base=", 7) == 0) {
            base_str = arg + 7;
        } else if (strcmp(arg, "--base") == 0 && i + 1 < argc) {
            base_str = argv[++i];
        } else if (strncmp(arg, "-L", 2) == 0) {
            const char *dir = arg + 2;
            if (dir[0] == '\0' && i + 1 < argc) {
                dir = argv[++i];
            }
            if (dir[0] != '\0') {
                ny_buf_grow((void **)&search_dirs, &search_dir_capacity, search_dir_count, sizeof(const char *));
                search_dirs[search_dir_count++] = dir;
            }
        } else if (strncmp(arg, "-l", 2) == 0) {
            const char *lib = arg + 2;
            if (lib[0] == '\0' && i + 1 < argc) {
                lib = argv[++i];
            }
            if (lib[0] != '\0') {
                ny_buf_grow((void **)&inputs, &input_capacity, input_count, sizeof(Link_Input));
                Link_Input *inp = &inputs[input_count++];
                size_t llen = strlen(lib);
                inp->path = (char *)ny_alloc(llen + 1);
                memcpy(inp->path, lib, llen + 1);
                inp->is_lib_name = true;
            }
        } else if (arg[0] == '-') {
            fprintf(stderr, "error: unrecognized link option '%s'\n", arg);
            for (size_t k = 0; k < input_count; k++) {
                ny_free(inputs[k].path, strlen(inputs[k].path) + 1);
            }
            if (inputs) ny_free(inputs, input_capacity * sizeof(Link_Input));
            if (search_dirs) ny_free(search_dirs, search_dir_capacity * sizeof(const char *));
            if (needed_libs) ny_free(needed_libs, needed_lib_capacity * sizeof(const char *));
            if (exports) ny_free(exports, export_capacity * sizeof(const char *));
            return 1;
        } else {
            ny_buf_grow((void **)&inputs, &input_capacity, input_count, sizeof(Link_Input));
            Link_Input *inp = &inputs[input_count++];
            size_t plen = strlen(arg);
            inp->path = (char *)ny_alloc(plen + 1);
            memcpy(inp->path, arg, plen + 1);
            inp->is_lib_name = false;
        }
    }

    if (input_count == 0) {
        fprintf(stderr, "error: no input files specified for link\n");
        if (inputs) ny_free(inputs, input_capacity * sizeof(Link_Input));
        if (search_dirs) ny_free(search_dirs, search_dir_capacity * sizeof(const char *));
        if (needed_libs) ny_free(needed_libs, needed_lib_capacity * sizeof(const char *));
        if (exports) ny_free(exports, export_capacity * sizeof(const char *));
        return 1;
    }

    Nylink_Target_Format target_format;
#if defined(_WIN32) || defined(_WIN64)
    target_format = NYLINK_TARGET_PE;
#else
    target_format = NYLINK_TARGET_ELF64;
#endif

    if (target_str) {
        if (strcmp(target_str, "elf64") == 0 || strcmp(target_str, "elf64-x86-64") == 0 || strcmp(target_str, "x86_64-elf") == 0) {
            target_format = NYLINK_TARGET_ELF64;
        } else if (strcmp(target_str, "pe") == 0 || strcmp(target_str, "pe-x86-64") == 0 || strcmp(target_str, "pe32+") == 0 || strcmp(target_str, "x86_64-pe") == 0) {
            target_format = NYLINK_TARGET_PE;
        } else {
            fprintf(stderr, "error: unsupported target format '%s' (supported: elf64, pe-x86-64)\n", target_str);
            for (size_t k = 0; k < input_count; k++) {
                ny_free(inputs[k].path, strlen(inputs[k].path) + 1);
            }
            if (inputs) ny_free(inputs, input_capacity * sizeof(Link_Input));
            if (search_dirs) ny_free(search_dirs, search_dir_capacity * sizeof(const char *));
            if (needed_libs) ny_free(needed_libs, needed_lib_capacity * sizeof(const char *));
            if (exports) ny_free(exports, export_capacity * sizeof(const char *));
            return 1;
        }
    }

    if (is_pie && target_format == NYLINK_TARGET_PE) {
        fprintf(stderr, "error: --pie is not supported for PE target\n");
        for (size_t k = 0; k < input_count; k++) {
            ny_free(inputs[k].path, strlen(inputs[k].path) + 1);
        }
        if (inputs) ny_free(inputs, input_capacity * sizeof(Link_Input));
        if (search_dirs) ny_free(search_dirs, search_dir_capacity * sizeof(const char *));
        if (needed_libs) ny_free(needed_libs, needed_lib_capacity * sizeof(const char *));
        if (exports) ny_free(exports, export_capacity * sizeof(const char *));
        return 1;
    }

    if (!output_file) {
        if (is_shared) {
            output_file = (target_format == NYLINK_TARGET_PE) ? "a.dll" : "liba.so";
        } else {
            output_file = (target_format == NYLINK_TARGET_PE) ? "a.exe" : "a.out";
        }
    }

    if (is_shared && target_format == NYLINK_TARGET_PE && !soname) {
        const char *base = strrchr(output_file, '/');
        if (!base) base = strrchr(output_file, '\\');
        soname = base ? base + 1 : output_file;
    }

    uint64_t base_address = 0;
    if (target_format == NYLINK_TARGET_PE) {
        base_address = is_shared ? 0x180000000ULL : 0x140000000ULL;
    } else {
        base_address = (is_shared || is_pie) ? 0x0ULL : 0x400000ULL;
    }

    if (base_str) {
        if (!parse_uint64_safe(base_str, &base_address)) {
            fprintf(stderr, "error: invalid base address '%s'\n", base_str);
            for (size_t k = 0; k < input_count; k++) {
                ny_free(inputs[k].path, strlen(inputs[k].path) + 1);
            }
            if (inputs) ny_free(inputs, input_capacity * sizeof(Link_Input));
            if (search_dirs) ny_free(search_dirs, search_dir_capacity * sizeof(const char *));
            if (needed_libs) ny_free(needed_libs, needed_lib_capacity * sizeof(const char *));
            if (exports) ny_free(exports, export_capacity * sizeof(const char *));
            return 1;
        }
    }

    typedef struct Loaded_Buffer {
        uint8_t *data;
        size_t size;
    } Loaded_Buffer;

    Loaded_Buffer *loaded = (Loaded_Buffer *)ny_alloc_zero(input_count * sizeof(Loaded_Buffer));
    Nylink_Context *ctx = nylink_context_create();
    bool load_ok = true;

    for (size_t i = 0; i < input_count; i++) {
        char resolved_path[1024];
        resolved_path[0] = '\0';

        if (inputs[i].is_lib_name) {
            const char *lib_name = inputs[i].path;
            bool found = false;

            for (size_t d = 0; d < search_dir_count; d++) {
                const char *dir = search_dirs[d];
                if (target_format == NYLINK_TARGET_ELF64) {
                    snprintf(resolved_path, sizeof(resolved_path), "%s/lib%s.so", dir, lib_name);
                    if (file_exists(resolved_path)) { found = true; break; }
                    snprintf(resolved_path, sizeof(resolved_path), "%s/lib%s.a", dir, lib_name);
                    if (file_exists(resolved_path)) { found = true; break; }
                } else {
                    snprintf(resolved_path, sizeof(resolved_path), "%s/%s.lib", dir, lib_name);
                    if (file_exists(resolved_path)) { found = true; break; }
                    snprintf(resolved_path, sizeof(resolved_path), "%s/lib%s.a", dir, lib_name);
                    if (file_exists(resolved_path)) { found = true; break; }
                }
                snprintf(resolved_path, sizeof(resolved_path), "%s/%s", dir, lib_name);
                if (file_exists(resolved_path)) { found = true; break; }
            }

            if (!found) {
                fprintf(stderr, "error: library not found for -l%s\n", lib_name);
                load_ok = false;
                break;
            }
        } else {
            snprintf(resolved_path, sizeof(resolved_path), "%s", inputs[i].path);
        }

        char err_msg[256];
        err_msg[0] = '\0';
        size_t fsize = 0;
        uint8_t *fdata = read_checked_file(resolved_path, &fsize, err_msg, sizeof(err_msg));
        if (!fdata) {
            fprintf(stderr, "error: %s\n", err_msg[0] ? err_msg : "failed to read input file");
            load_ok = false;
            break;
        }

        loaded[i].data = fdata;
        loaded[i].size = fsize;

        if (fsize >= 8 && memcmp(fdata, "!<arch>\n", 8) == 0) {
            if (!nylink_add_archive(ctx, resolved_path, fdata, fsize)) {
                load_ok = false;
                break;
            }
        } else {
            if (!nylink_add_object(ctx, resolved_path, fdata, fsize)) {
                load_ok = false;
                break;
            }
        }
    }

    int exit_code = 0;
    if (!load_ok || nylink_has_errors(ctx)) {
        exit_code = 1;
    } else {
        Nylink_Output_Mode out_mode = NYLINK_OUTPUT_EXECUTABLE;
        if (is_pie) {
            out_mode = NYLINK_OUTPUT_PIE;
        } else if (is_shared) {
            out_mode = (target_format == NYLINK_TARGET_PE) ? NYLINK_OUTPUT_DLL : NYLINK_OUTPUT_SHARED;
        }
        nylink_context_set_output_mode(ctx, out_mode);

        for (size_t e = 0; e < export_count; e++) {
            nylink_add_export(ctx, exports[e]);
        }

        if (!nylink_resolve_symbols(ctx)) {
            exit_code = 1;
        } else {
            const char *eff_entry = entry_point;
            if (!eff_entry) {
                if (target_format == NYLINK_TARGET_ELF64) {
                    if (nylink_find_symbol(ctx, "_start")) {
                        eff_entry = "_start";
                    } else {
                        eff_entry = "main";
                    }
                } else {
                    eff_entry = "main";
                }
            }

            Nylink_Config cfg = {
                .target_format = target_format,
                .output_mode = out_mode,
                .base_address = base_address,
                .entry_point = is_shared ? entry_point : eff_entry,
                .dynamic_linker = dynamic_linker,
                .soname = soname,
                .rpath = rpath,
                .needed_libs = needed_libs,
                .needed_lib_count = needed_lib_count,
                .exports = exports,
                .export_count = export_count,
                .implib_path = implib_path,
            };

            if (!nylink_layout(ctx, &cfg)) {
                exit_code = 1;
            } else if (!nylink_apply_relocations(ctx)) {
                exit_code = 1;
            } else {
                char tmp_out[1040];
                snprintf(tmp_out, sizeof(tmp_out), "%s.tmp.%d", output_file, rand());

                if (!nylink_write_executable(ctx, tmp_out, &cfg)) {
                    remove(tmp_out);
                    exit_code = 1;
                } else {
                    remove(output_file);
                    if (rename(tmp_out, output_file) != 0) {
                        fprintf(stderr, "error: failed to create final output '%s': %s\n", output_file, strerror(errno));
                        remove(tmp_out);
                        exit_code = 1;
                    } else if (out_mode == NYLINK_OUTPUT_DLL) {
                        char default_implib[1040];
                        const char *actual_implib = implib_path;
                        if (!actual_implib) {
                            snprintf(default_implib, sizeof(default_implib), "%s", output_file);
                            char *dot = strrchr(default_implib, '.');
                            if (dot) {
                                snprintf(dot, sizeof(default_implib) - (size_t)(dot - default_implib), ".lib");
                            } else {
                                snprintf(default_implib + strlen(default_implib), sizeof(default_implib) - strlen(default_implib), ".lib");
                            }
                            actual_implib = default_implib;
                        }
                        const char *dll_base_name = strrchr(output_file, '/');
                        if (!dll_base_name) dll_base_name = strrchr(output_file, '\\');
                        dll_base_name = dll_base_name ? dll_base_name + 1 : output_file;
                        if (!nylink_write_pe_implib(ctx, actual_implib, dll_base_name)) {
                            exit_code = 1;
                        }
                    }
                }
            }
        }
    }

    if (exit_code != 0) {
        size_t dcount = nylink_get_diagnostic_count(ctx);
        for (size_t d = 0; d < dcount; d++) {
            const Nylink_Diagnostic *diag = nylink_get_diagnostic(ctx, d);
            if (!diag) continue;

            if (diag->object_name && diag->symbol_or_section) {
                fprintf(stderr, "%s: [%s]: %s\n", diag->object_name, diag->symbol_or_section, diag->message ? diag->message : "error");
            } else if (diag->object_name) {
                fprintf(stderr, "%s: %s\n", diag->object_name, diag->message ? diag->message : "error");
            } else {
                fprintf(stderr, "error: %s\n", diag->message ? diag->message : "linker error");
            }
        }
    }

    nylink_context_destroy(ctx);

    for (size_t i = 0; i < input_count; i++) {
        if (loaded[i].data) {
            ny_free(loaded[i].data, loaded[i].size);
        }
        ny_free(inputs[i].path, strlen(inputs[i].path) + 1);
    }
    ny_free(loaded, input_count * sizeof(Loaded_Buffer));
    if (inputs) ny_free(inputs, input_capacity * sizeof(Link_Input));
    if (search_dirs) ny_free(search_dirs, search_dir_capacity * sizeof(const char *));
    if (needed_libs) ny_free(needed_libs, needed_lib_capacity * sizeof(const char *));
    if (exports) ny_free(exports, export_capacity * sizeof(const char *));

    return exit_code;
}

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

    if (argc >= 2 && strcmp(argv[1], "link") == 0) {
        return do_cli_link(argc - 1, argv + 1);
    }

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
