// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <nybit/support.h>
#include <nybit/ir.h>
#include <nybit/parser.h>
#include <nybit/analysis.h>
#include <nybit/opt.h>
#include <nybit/machine.h>
#include <nybit/target.h>
#include <nybit/target_x86_64.h>
#include <nybit/regalloc.h>
#include <nybit/object.h>

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
    char *buf = (char *)ny_alloc(cap);

    for (;;) {
        if (len + 2048 > cap) {
            size_t new_cap = cap * 2;
            buf = (char *)ny_realloc(buf, cap, new_cap);
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
    Ny_Opt_Level opt_level = NY_OPT_O1;
    bool dump_raw = false;
    bool run_analysis = false;
    bool do_opt = true;
    bool emit_mir = false;
    bool emit_x86 = false;
    bool emit_bytes = false;
    bool emit_obj = false;
    const char *target_name = nullptr;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-O0") == 0) {
            opt_level = NY_OPT_O0;
            do_opt = false;
        } else if (strcmp(argv[i], "-O1") == 0) {
            opt_level = NY_OPT_O1;
            do_opt = true;
        } else if (strcmp(argv[i], "-O2") == 0) {
            opt_level = NY_OPT_O2;
            do_opt = true;
        } else if (strcmp(argv[i], "--dump-raw") == 0) {
            dump_raw = true;
        } else if (strcmp(argv[i], "--analyze") == 0) {
            run_analysis = true;
        } else if (strcmp(argv[i], "--no-opt") == 0) {
            do_opt = false;
        } else if (strcmp(argv[i], "--emit-machine-ir") == 0) {
            emit_mir = true;
        } else if (strcmp(argv[i], "--emit-x86") == 0 || strcmp(argv[i], "-S") == 0) {
            emit_x86 = true;
        } else if (strcmp(argv[i], "--emit-bytes") == 0) {
            emit_bytes = true;
        } else if (strcmp(argv[i], "--emit-obj") == 0 || strcmp(argv[i], "-c") == 0) {
            emit_obj = true;
        } else if ((strcmp(argv[i], "--target") == 0 || strcmp(argv[i], "-target") == 0) && i + 1 < argc) {
            target_name = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unrecognized flag '%s'\n", argv[i]);
            return 1;
        } else {
            input_file = argv[i];
        }
    }

    char *source_text = nullptr;
    size_t source_len = 0;
    bool free_source = false;

    if (input_file) {
        source_text = read_entire_file(input_file, &source_len);
        if (!source_text) return 1;
        free_source = true;
    } else {
        source_text = (char *)DEFAULT_DEMO_SOURCE;
        source_len = strlen(DEFAULT_DEMO_SOURCE);
    }

    Ny_Context ctx;
    ny_context_init(&ctx, "nybit_main");

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, source_text, source_len, &ctx.arena);

    bool parse_ok = ny_parse_module(&parser);
    if (!parse_ok) {
        for (size_t i = 0; i < parser.diag_count; i++) {
            fprintf(stderr, "parse error [%u:%u]: %s\n",
                    parser.diagnostics[i].line,
                    parser.diagnostics[i].col,
                    parser.diagnostics[i].message);
        }
        ny_parser_destroy(&parser);
        ny_context_destroy(&ctx);
        if (free_source) ny_free(source_text, source_len + 1);
        return 1;
    }
    ny_parser_destroy(&parser);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_validate_module(&ctx.module, &val_diags);
    if (!valid) {
        for (size_t i = 0; i < val_diags.count; i++) {
            fprintf(stderr, "validation error: %s\n", val_diags.items[i].message);
        }
        ny_diagnostic_list_destroy(&val_diags);
        ny_context_destroy(&ctx);
        if (free_source) ny_free(source_text, source_len + 1);
        return 1;
    }
    ny_diagnostic_list_destroy(&val_diags);

    if (dump_raw) {
        char *raw_dump = ny_dump_module(&ctx.module, &ctx.arena);
        fputs(raw_dump, stdout);
    }

    if (do_opt) {
        ny_opt_run_module_pipeline(&ctx.module, opt_level);

        ny_diagnostic_list_init(&val_diags);
        bool post_valid = ny_validate_module(&ctx.module, &val_diags);
        if (!post_valid) {
            for (size_t i = 0; i < val_diags.count; i++) {
                fprintf(stderr, "post-opt validation error: %s\n", val_diags.items[i].message);
            }
            ny_diagnostic_list_destroy(&val_diags);
            ny_context_destroy(&ctx);
            if (free_source) ny_free(source_text, source_len + 1);
            return 1;
        }
        ny_diagnostic_list_destroy(&val_diags);
    }

    if (emit_x86 || emit_bytes || emit_obj) {
        const Ny_Target *target = target_name ? ny_target_find(target_name) : ny_target_get_default();
        if (!target) {
            fprintf(stderr, "error: unknown target '%s'\n", target_name ? target_name : "default");
            ny_context_destroy(&ctx);
            if (free_source) ny_free(source_text, source_len + 1);
            return 1;
        }

        Ny_Machine_Module mmod;
        Ny_Diagnostic_List mir_diags;
        ny_diagnostic_list_init(&mir_diags);
        bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &mir_diags);
        if (!mir_ok) {
            for (size_t i = 0; i < mir_diags.count; i++) {
                fprintf(stderr, "lowering error: %s\n", mir_diags.items[i].message);
            }
            ny_diagnostic_list_destroy(&mir_diags);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            if (free_source) ny_free(source_text, source_len + 1);
            return 1;
        }
        ny_diagnostic_list_destroy(&mir_diags);

        for (size_t f = 0; f < mmod.function_count; f++) {
            Ny_Diagnostic_List ra_diags;
            ny_diagnostic_list_init(&ra_diags);
            bool ra_ok = ny_regalloc_run(&mmod.functions[f], target->abi, nullptr, &ra_diags);
            if (!ra_ok) {
                for (size_t d = 0; d < ra_diags.count; d++) {
                    fprintf(stderr, "regalloc error: %s\n", ra_diags.items[d].message);
                }
                ny_diagnostic_list_destroy(&ra_diags);
                ny_mmod_destroy(&mmod);
                ny_context_destroy(&ctx);
                if (free_source) ny_free(source_text, source_len + 1);
                return 1;
            }
            bool val_ok = ny_mfunc_validate_allocated(&mmod.functions[f], &ra_diags);
            if (!val_ok) {
                for (size_t d = 0; d < ra_diags.count; d++) {
                    fprintf(stderr, "machine validation error: %s\n", ra_diags.items[d].message);
                }
                ny_diagnostic_list_destroy(&ra_diags);
                ny_mmod_destroy(&mmod);
                ny_context_destroy(&ctx);
                if (free_source) ny_free(source_text, source_len + 1);
                return 1;
            }
            ny_diagnostic_list_destroy(&ra_diags);
        }

        X86_Module xmod;
        Ny_Diagnostic_List x86_diags;
        ny_diagnostic_list_init(&x86_diags);
        bool x86_ok = x86_lower_machine_mod(target, &mmod, &xmod, &x86_diags);
        if (!x86_ok) {
            for (size_t i = 0; i < x86_diags.count; i++) {
                fprintf(stderr, "target lowering error: %s\n", x86_diags.items[i].message);
            }
            ny_diagnostic_list_destroy(&x86_diags);
            x86_mod_destroy(&xmod);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            if (free_source) ny_free(source_text, source_len + 1);
            return 1;
        }
        ny_diagnostic_list_destroy(&x86_diags);

        if (emit_obj) {
            X86_Encoded_Module emod;
            Ny_Diagnostic_List enc_diags;
            ny_diagnostic_list_init(&enc_diags);
            bool enc_ok = x86_encode_module(&emod, &xmod, &enc_diags);
            if (!enc_ok) {
                for (size_t i = 0; i < enc_diags.count; i++) {
                    fprintf(stderr, "encode error: %s\n", enc_diags.items[i].message);
                }
                ny_diagnostic_list_destroy(&enc_diags);
                x86_encoded_mod_destroy(&emod);
                x86_mod_destroy(&xmod);
                ny_mmod_destroy(&mmod);
                ny_context_destroy(&ctx);
                if (free_source) ny_free(source_text, source_len + 1);
                return 1;
            }
            ny_diagnostic_list_destroy(&enc_diags);

            Ny_Object_Buffer obj_buf;
            ny_obj_buf_init(&obj_buf);
            Ny_Diagnostic_List obj_diags;
            ny_diagnostic_list_init(&obj_diags);
            bool obj_ok = ny_emit_object_module(&obj_buf, target, &emod, &obj_diags);
            if (!obj_ok) {
                for (size_t i = 0; i < obj_diags.count; i++) {
                    fprintf(stderr, "object emission error: %s\n", obj_diags.items[i].message);
                }
                ny_diagnostic_list_destroy(&obj_diags);
                ny_obj_buf_destroy(&obj_buf);
                x86_encoded_mod_destroy(&emod);
                x86_mod_destroy(&xmod);
                ny_mmod_destroy(&mmod);
                ny_context_destroy(&ctx);
                if (free_source) ny_free(source_text, source_len + 1);
                return 1;
            }
            ny_diagnostic_list_destroy(&obj_diags);

            if (output_file) {
                FILE *out_f = fopen(output_file, "wb");
                if (!out_f) {
                    fprintf(stderr, "error: failed to open output file '%s'\n", output_file);
                    ny_obj_buf_destroy(&obj_buf);
                    x86_encoded_mod_destroy(&emod);
                    x86_mod_destroy(&xmod);
                    ny_mmod_destroy(&mmod);
                    ny_context_destroy(&ctx);
                    if (free_source) ny_free(source_text, source_len + 1);
                    return 1;
                }
                fwrite(obj_buf.bytes, 1, obj_buf.count, out_f);
                fclose(out_f);
            } else {
                for (size_t i = 0; i < obj_buf.count; i++) {
                    printf("%02x%c", obj_buf.bytes[i], (i + 1 == obj_buf.count || (i + 1) % 16 == 0) ? '\n' : ' ');
                }
            }
            ny_obj_buf_destroy(&obj_buf);
            x86_encoded_mod_destroy(&emod);
        } else if (emit_bytes) {
            X86_Encoded_Module emod;
            Ny_Diagnostic_List enc_diags;
            ny_diagnostic_list_init(&enc_diags);
            bool enc_ok = x86_encode_module(&emod, &xmod, &enc_diags);
            if (!enc_ok) {
                for (size_t i = 0; i < enc_diags.count; i++) {
                    fprintf(stderr, "encode error: %s\n", enc_diags.items[i].message);
                }
                ny_diagnostic_list_destroy(&enc_diags);
                x86_encoded_mod_destroy(&emod);
                x86_mod_destroy(&xmod);
                ny_mmod_destroy(&mmod);
                ny_context_destroy(&ctx);
                if (free_source) ny_free(source_text, source_len + 1);
                return 1;
            }
            ny_diagnostic_list_destroy(&enc_diags);

            if (output_file) {
                FILE *out_f = fopen(output_file, "wb");
                if (!out_f) {
                    fprintf(stderr, "error: failed to open output file '%s'\n", output_file);
                    x86_encoded_mod_destroy(&emod);
                    x86_mod_destroy(&xmod);
                    ny_mmod_destroy(&mmod);
                    ny_context_destroy(&ctx);
                    if (free_source) ny_free(source_text, source_len + 1);
                    return 1;
                }
                fwrite(emod.text_section.bytes, 1, emod.text_section.count, out_f);
                fclose(out_f);
            } else {
                for (size_t i = 0; i < emod.text_section.count; i++) {
                    printf("%02x%c", emod.text_section.bytes[i], (i + 1 == emod.text_section.count || (i + 1) % 16 == 0) ? '\n' : ' ');
                }
            }
            x86_encoded_mod_destroy(&emod);
        } else {
            char *x86_dump = x86_dump_mod(&xmod, &ctx.arena);
            if (output_file) {
                FILE *out_f = fopen(output_file, "wb");
                if (!out_f) {
                    fprintf(stderr, "error: failed to open output file '%s'\n", output_file);
                    x86_mod_destroy(&xmod);
                    ny_mmod_destroy(&mmod);
                    ny_context_destroy(&ctx);
                    if (free_source) ny_free(source_text, source_len + 1);
                    return 1;
                }
                fputs(x86_dump, out_f);
                fclose(out_f);
            } else {
                fputs(x86_dump, stdout);
            }
        }
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
    } else if (emit_mir) {
        Ny_Machine_Module mmod;
        Ny_Diagnostic_List mir_diags;
        ny_diagnostic_list_init(&mir_diags);
        bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &mir_diags);
        if (!mir_ok) {
            for (size_t i = 0; i < mir_diags.count; i++) {
                fprintf(stderr, "lowering error: %s\n", mir_diags.items[i].message);
            }
            ny_diagnostic_list_destroy(&mir_diags);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            if (free_source) ny_free(source_text, source_len + 1);
            return 1;
        }
        ny_diagnostic_list_destroy(&mir_diags);

        char *mir_dump = ny_mir_dump_module(&mmod, &ctx.arena);
        if (output_file) {
            FILE *out_f = fopen(output_file, "wb");
            if (!out_f) {
                fprintf(stderr, "error: failed to open output file '%s'\n", output_file);
                ny_mmod_destroy(&mmod);
                ny_context_destroy(&ctx);
                if (free_source) ny_free(source_text, source_len + 1);
                return 1;
            }
            fputs(mir_dump, out_f);
            fclose(out_f);
        } else {
            fputs(mir_dump, stdout);
        }
        ny_mmod_destroy(&mmod);
    } else {
        char *opt_dump = ny_dump_module(&ctx.module, &ctx.arena);
        if (output_file) {
            FILE *out_f = fopen(output_file, "wb");
            if (!out_f) {
                fprintf(stderr, "error: failed to open output file '%s'\n", output_file);
                ny_context_destroy(&ctx);
                if (free_source) ny_free(source_text, source_len + 1);
                return 1;
            }
            fputs(opt_dump, out_f);
            fclose(out_f);
        } else if (!dump_raw) {
            fputs(opt_dump, stdout);
        }
    }

    if (run_analysis) {
        for (size_t i = 0; i < ctx.module.function_count; i++) {
            Ny_Function *fn = &ctx.module.functions[i];
            Ny_Analysis_Manager am;
            ny_analysis_manager_init(&am, fn);

            Ny_CFG_Info *cfg = ny_analysis_get_cfg(&am);
            Ny_Dominator_Tree *dt = ny_analysis_get_dom(&am);
            Ny_Use_Def *ud = ny_analysis_get_use_def(&am);
            Ny_Liveness_Info *liv = ny_analysis_get_liveness(&am);

            printf("@%.*s: %zu blocks, %zu uses, %zu live-in words\n",
                   (int)fn->name.len, fn->name.data,
                   cfg->rpo_count, ud->total_uses, liv->words_per_block);
            (void)dt;
            ny_analysis_manager_destroy(&am);
        }
    }

    ny_context_destroy(&ctx);
    if (free_source) {
        ny_free(source_text, source_len + 1);
    }

    if (g_ny_mem_tracker.current_allocated != 0) {
        fprintf(stderr, "memory leak: %zu bytes remaining\n",
                g_ny_mem_tracker.current_allocated);
        return 2;
    }

    return 0;
}
