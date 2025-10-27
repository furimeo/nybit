// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nygen/nygen.h"
#include "nybit/support.h"
#include "nybit/ir.h"
#include "nybit/parser.h"
#include "nybit/analysis.h"
#include "nybit/opt.h"
#include "nybit/machine.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include "nybit/regalloc.h"
#include "nybit/object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void nygen_config_init(Nygen_Config *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->target_triple = nullptr;
    config->opt_level = NYGEN_OPT_O1;
    config->output_kind = NYGEN_OUTPUT_OPT_IR;
    config->run_analysis = false;
    config->module_name = "nygen_module";
}

size_t nygen_get_current_allocated(void) {
    return g_ny_mem_tracker.current_allocated;
}

static void add_diagnostic(Nygen_Result *res, uint32_t line, uint32_t col, const char *msg) {
    size_t old_cap = res->diagnostic_count;
    size_t new_cap = old_cap + 1;
    res->diagnostics = (Nygen_Diagnostic *)ny_realloc(res->diagnostics,
                                                       old_cap * sizeof(Nygen_Diagnostic),
                                                       new_cap * sizeof(Nygen_Diagnostic));
    size_t len = strlen(msg);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, msg, len + 1);
    res->diagnostics[old_cap] = (Nygen_Diagnostic){
        .line = line,
        .col = col,
        .message = copy,
    };
    res->diagnostic_count = new_cap;
}

static void copy_diags_from_list(Nygen_Result *res, const Ny_Diagnostic_List *list) {
    for (size_t i = 0; i < list->count; i++) {
        add_diagnostic(res, 0, 0, list->items[i].message);
    }
}

void nygen_result_destroy(Nygen_Result *result) {
    if (!result) return;

    if (result->data && result->size > 0) {
        ny_free(result->data, result->size);
    }
    if (result->diagnostics && result->diagnostic_count > 0) {
        for (size_t i = 0; i < result->diagnostic_count; i++) {
            if (result->diagnostics[i].message) {
                ny_free((void *)result->diagnostics[i].message, strlen(result->diagnostics[i].message) + 1);
            }
        }
        ny_free(result->diagnostics, result->diagnostic_count * sizeof(Nygen_Diagnostic));
    }
    if (result->analysis_report) {
        ny_free(result->analysis_report, strlen(result->analysis_report) + 1);
    }
    memset(result, 0, sizeof(*result));
}

static char *build_analysis_report(const Ny_Module *mod, Ny_Arena *scratch) {
    char buf[4096];
    size_t pos = 0;

    for (size_t i = 0; i < mod->function_count; i++) {
        Ny_Function *fn = &mod->functions[i];
        if (fn->block_count == 0) continue;

        Ny_Analysis_Manager am;
        ny_analysis_manager_init(&am, fn);

        Ny_CFG_Info *cfg = ny_analysis_get_cfg(&am);
        Ny_Use_Def *ud = ny_analysis_get_use_def(&am);
        Ny_Liveness_Info *liv = ny_analysis_get_liveness(&am);

        int written = snprintf(buf + pos, sizeof(buf) - pos,
                               "@%.*s: %zu blocks, %zu uses, %zu live-in words\n",
                               (int)fn->name.len, fn->name.data,
                               cfg->rpo_count, ud->total_uses, liv->words_per_block);
        if (written > 0 && (size_t)written < sizeof(buf) - pos) {
            pos += (size_t)written;
        }

        ny_analysis_manager_destroy(&am);
    }

    if (pos == 0) return nullptr;
    (void)scratch;
    char *res = (char *)ny_alloc(pos + 1);
    memcpy(res, buf, pos + 1);
    return res;
}

Nygen_Result nygen_compile(const char *source_text, size_t source_len, const Nygen_Config *config) {
    Nygen_Result res;
    memset(&res, 0, sizeof(res));

    if (!source_text || source_len == 0 || !config) {
        add_diagnostic(&res, 0, 0, "invalid null argument to nygen_compile");
        res.success = false;
        return res;
    }

    const char *mod_name = config->module_name ? config->module_name : "nygen_module";
    Ny_Context ctx;
    ny_context_init(&ctx, mod_name);

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, source_text, source_len, &ctx.arena);
    bool parse_ok = ny_parse_module(&parser);
    if (!parse_ok) {
        for (size_t i = 0; i < parser.diag_count; i++) {
            add_diagnostic(&res, parser.diagnostics[i].line, parser.diagnostics[i].col, parser.diagnostics[i].message);
        }
        ny_parser_destroy(&parser);
        ny_context_destroy(&ctx);
        res.success = false;
        return res;
    }
    ny_parser_destroy(&parser);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_validate_module(&ctx.module, &val_diags);
    if (!valid) {
        copy_diags_from_list(&res, &val_diags);
        ny_diagnostic_list_destroy(&val_diags);
        ny_context_destroy(&ctx);
        res.success = false;
        return res;
    }
    ny_diagnostic_list_destroy(&val_diags);

    if (config->output_kind == NYGEN_OUTPUT_RAW_IR) {
        char *raw_dump = ny_dump_module(&ctx.module, &ctx.arena);
        size_t dump_len = strlen(raw_dump);
        res.size = dump_len + 1;
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, raw_dump, res.size);
        if (config->run_analysis) {
            res.analysis_report = build_analysis_report(&ctx.module, &ctx.arena);
        }
        ny_context_destroy(&ctx);
        res.success = true;
        return res;
    }

    Ny_Opt_Level opt_lvl = NY_OPT_O1;
    bool do_opt = true;
    if (config->opt_level == NYGEN_OPT_O0) {
        opt_lvl = NY_OPT_O0;
        do_opt = false;
    } else if (config->opt_level == NYGEN_OPT_O2) {
        opt_lvl = NY_OPT_O2;
        do_opt = true;
    }

    if (do_opt) {
        ny_opt_run_module_pipeline(&ctx.module, opt_lvl);

        ny_diagnostic_list_init(&val_diags);
        bool post_valid = ny_validate_module(&ctx.module, &val_diags);
        if (!post_valid) {
            copy_diags_from_list(&res, &val_diags);
            ny_diagnostic_list_destroy(&val_diags);
            ny_context_destroy(&ctx);
            res.success = false;
            return res;
        }
        ny_diagnostic_list_destroy(&val_diags);
    }

    if (config->run_analysis) {
        res.analysis_report = build_analysis_report(&ctx.module, &ctx.arena);
    }

    if (config->output_kind == NYGEN_OUTPUT_OPT_IR) {
        char *opt_dump = ny_dump_module(&ctx.module, &ctx.arena);
        size_t dump_len = strlen(opt_dump);
        res.size = dump_len + 1;
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, opt_dump, res.size);
        ny_context_destroy(&ctx);
        res.success = true;
        return res;
    }

    if (config->output_kind == NYGEN_OUTPUT_MACHINE_IR) {
        Ny_Machine_Module mmod;
        Ny_Diagnostic_List mir_diags;
        ny_diagnostic_list_init(&mir_diags);
        bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &mir_diags);
        if (!mir_ok) {
            copy_diags_from_list(&res, &mir_diags);
            ny_diagnostic_list_destroy(&mir_diags);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            res.success = false;
            return res;
        }
        ny_diagnostic_list_destroy(&mir_diags);

        char *mir_dump = ny_mir_dump_module(&mmod, &ctx.arena);
        size_t dump_len = strlen(mir_dump);
        res.size = dump_len + 1;
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, mir_dump, res.size);

        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        res.success = true;
        return res;
    }

    const Ny_Target *target = config->target_triple ? ny_target_find(config->target_triple) : ny_target_get_default();
    if (!target) {
        add_diagnostic(&res, 0, 0, "unknown target specified");
        ny_context_destroy(&ctx);
        res.success = false;
        return res;
    }

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List mir_diags;
    ny_diagnostic_list_init(&mir_diags);
    bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &mir_diags);
    if (!mir_ok) {
        copy_diags_from_list(&res, &mir_diags);
        ny_diagnostic_list_destroy(&mir_diags);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        res.success = false;
        return res;
    }
    ny_diagnostic_list_destroy(&mir_diags);

    for (size_t f = 0; f < mmod.function_count; f++) {
        Ny_Diagnostic_List ra_diags;
        ny_diagnostic_list_init(&ra_diags);
        bool ra_ok = ny_regalloc_run(&mmod.functions[f], target->abi, nullptr, &ra_diags);
        if (!ra_ok) {
            copy_diags_from_list(&res, &ra_diags);
            ny_diagnostic_list_destroy(&ra_diags);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            res.success = false;
            return res;
        }
        bool val_ok = ny_mfunc_validate_allocated(&mmod.functions[f], &ra_diags);
        if (!val_ok) {
            copy_diags_from_list(&res, &ra_diags);
            ny_diagnostic_list_destroy(&ra_diags);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            res.success = false;
            return res;
        }
        ny_diagnostic_list_destroy(&ra_diags);
    }

    X86_Module xmod;
    Ny_Diagnostic_List x86_diags;
    ny_diagnostic_list_init(&x86_diags);
    bool x86_ok = x86_lower_machine_mod(target, &mmod, &xmod, &x86_diags);
    if (!x86_ok) {
        copy_diags_from_list(&res, &x86_diags);
        ny_diagnostic_list_destroy(&x86_diags);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        res.success = false;
        return res;
    }
    ny_diagnostic_list_destroy(&x86_diags);

    if (config->output_kind == NYGEN_OUTPUT_ASM) {
        char *x86_dump = x86_dump_mod(&xmod, &ctx.arena);
        size_t dump_len = strlen(x86_dump);
        res.size = dump_len + 1;
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, x86_dump, res.size);

        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        res.success = true;
        return res;
    }

    X86_Encoded_Module emod;
    Ny_Diagnostic_List enc_diags;
    ny_diagnostic_list_init(&enc_diags);
    bool enc_ok = x86_encode_module(&emod, &xmod, &enc_diags);
    if (!enc_ok) {
        copy_diags_from_list(&res, &enc_diags);
        ny_diagnostic_list_destroy(&enc_diags);
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        res.success = false;
        return res;
    }
    ny_diagnostic_list_destroy(&enc_diags);

    if (config->output_kind == NYGEN_OUTPUT_BYTES) {
        res.size = emod.text_section.count;
        if (res.size > 0) {
            res.data = (uint8_t *)ny_alloc(res.size);
            memcpy(res.data, emod.text_section.bytes, res.size);
        }
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        res.success = true;
        return res;
    }

    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);
    Ny_Diagnostic_List obj_diags;
    ny_diagnostic_list_init(&obj_diags);
    bool obj_ok = ny_emit_object_module(&obj_buf, target, &emod, &obj_diags);
    if (!obj_ok) {
        copy_diags_from_list(&res, &obj_diags);
        ny_diagnostic_list_destroy(&obj_diags);
        ny_obj_buf_destroy(&obj_buf);
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        res.success = false;
        return res;
    }
    ny_diagnostic_list_destroy(&obj_diags);

    res.size = obj_buf.count;
    if (res.size > 0) {
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, obj_buf.bytes, res.size);
    }

    ny_obj_buf_destroy(&obj_buf);
    x86_encoded_mod_destroy(&emod);
    x86_mod_destroy(&xmod);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
    res.success = true;
    return res;
}

bool nygen_link_executable_extra(const char *const *obj_paths, size_t obj_count, const char *out_exe_path, const char *target_triple, Nygen_Result *out_res) {
    if (out_res) memset(out_res, 0, sizeof(*out_res));

    const Ny_Target *target = target_triple ? ny_target_find(target_triple) : ny_target_get_default();
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    bool ok = ny_link_executable_with_extra(obj_paths, obj_count, out_exe_path, target, &diags);
    if (!ok && out_res) {
        copy_diags_from_list(out_res, &diags);
        out_res->success = false;
    } else if (out_res) {
        out_res->success = true;
    }

    ny_diagnostic_list_destroy(&diags);
    return ok;
}

bool nygen_link_executable(const char *obj_path, const char *out_exe_path, const char *target_triple, Nygen_Result *out_res) {
    const char *objs[1] = { obj_path };
    return nygen_link_executable_extra(objs, 1, out_exe_path, target_triple, out_res);
}

bool nygen_compile_to_file(const char *source_text, size_t source_len, const char *out_path, const Nygen_Config *config, Nygen_Result *out_res) {
    if (out_res) memset(out_res, 0, sizeof(*out_res));
    if (!out_path || !config) {
        if (out_res) {
            add_diagnostic(out_res, 0, 0, "invalid null argument to nygen_compile_to_file");
            out_res->success = false;
        }
        return false;
    }

    if (config->output_kind == NYGEN_OUTPUT_EXECUTABLE) {
        Nygen_Config obj_cfg = *config;
        obj_cfg.output_kind = NYGEN_OUTPUT_OBJECT;

        Nygen_Result obj_res = nygen_compile(source_text, source_len, &obj_cfg);
        if (!obj_res.success) {
            if (out_res) *out_res = obj_res;
            else nygen_result_destroy(&obj_res);
            return false;
        }

        char tmp_obj[1024];
        snprintf(tmp_obj, sizeof(tmp_obj), "%s.tmp.obj", out_path);
        FILE *f = fopen(tmp_obj, "wb");
        if (!f) {
            if (out_res) {
                add_diagnostic(out_res, 0, 0, "failed to open temporary object file for writing");
                out_res->success = false;
            }
            nygen_result_destroy(&obj_res);
            return false;
        }
        fwrite(obj_res.data, 1, obj_res.size, f);
        fclose(f);
        nygen_result_destroy(&obj_res);

        bool link_ok = nygen_link_executable(tmp_obj, out_path, config->target_triple, out_res);
        remove(tmp_obj);
        return link_ok;
    }

    Nygen_Result res = nygen_compile(source_text, source_len, config);
    if (!res.success) {
        if (out_res) *out_res = res;
        else nygen_result_destroy(&res);
        return false;
    }

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        if (out_res) {
            add_diagnostic(out_res, 0, 0, "failed to open output file for writing");
            out_res->success = false;
        }
        nygen_result_destroy(&res);
        return false;
    }

    if (res.data && res.size > 0) {
        size_t write_len = res.size;
        if (config->output_kind == NYGEN_OUTPUT_RAW_IR ||
            config->output_kind == NYGEN_OUTPUT_OPT_IR ||
            config->output_kind == NYGEN_OUTPUT_MACHINE_IR ||
            config->output_kind == NYGEN_OUTPUT_ASM) {
            if (write_len > 0 && res.data[write_len - 1] == '\0') {
                write_len--;
            }
        }
        fwrite(res.data, 1, write_len, f);
    }
    fclose(f);

    if (out_res) {
        *out_res = res;
    } else {
        nygen_result_destroy(&res);
    }
    return true;
}
