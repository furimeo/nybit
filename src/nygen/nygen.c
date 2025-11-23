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
#include "nybit/target_aarch64.h"
#include "nybit/regalloc.h"
#include "nybit/object.h"
#include "nybit/x86_encode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern bool nygen_serialize_nyir(const Ny_Module *mod, uint8_t **out_data, size_t *out_size);
extern Ny_Context *nygen_deserialize_nyir(const uint8_t *data, size_t size,
                                           Nygen_Diagnostic **out_diags, size_t *out_diag_count);

void nygen_config_init(Nygen_Config *config) {
    if (!config) return;
    memset(config, 0, sizeof(*config));
    config->target_triple = nullptr;
    config->opt_level = NYGEN_OPT_O1;
    config->output_kind = NYGEN_OUTPUT_OPT_IR;
    config->run_analysis = false;
    config->module_name = "nygen_module";
    config->debug_info = false;
    config->emit_unwind = true;
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

static Nygen_Result run_backend(Ny_Context *ctx, const Nygen_Config *config) {
    Nygen_Result res;
    memset(&res, 0, sizeof(res));

    if (config->output_kind == NYGEN_OUTPUT_RAW_IR) {
        char *raw_dump = ny_dump_module(&ctx->module, &ctx->arena);
        size_t dump_len = strlen(raw_dump);
        res.size = dump_len + 1;
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, raw_dump, res.size);
        if (config->run_analysis) {
            res.analysis_report = build_analysis_report(&ctx->module, &ctx->arena);
        }
        res.success = true;
        return res;
    }

    if (config->output_kind == NYGEN_OUTPUT_NYIR) {
        uint8_t *nyir_data = nullptr;
        size_t nyir_size = 0;
        if (!nygen_serialize_nyir(&ctx->module, &nyir_data, &nyir_size)) {
            add_diagnostic(&res, 0, 0, "failed to serialize IR to .nyir");
            res.success = false;
            return res;
        }
        res.data = nyir_data;
        res.size = nyir_size;
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
        ny_opt_run_module_pipeline(&ctx->module, opt_lvl);

        Ny_Diagnostic_List val_diags;
        ny_diagnostic_list_init(&val_diags);
        bool post_valid = ny_validate_module(&ctx->module, &val_diags);
        if (!post_valid) {
            copy_diags_from_list(&res, &val_diags);
            ny_diagnostic_list_destroy(&val_diags);
            res.success = false;
            return res;
        }
        ny_diagnostic_list_destroy(&val_diags);
    }

    if (config->run_analysis) {
        res.analysis_report = build_analysis_report(&ctx->module, &ctx->arena);
    }

    if (config->output_kind == NYGEN_OUTPUT_OPT_IR) {
        char *opt_dump = ny_dump_module(&ctx->module, &ctx->arena);
        size_t dump_len = strlen(opt_dump);
        res.size = dump_len + 1;
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, opt_dump, res.size);
        res.success = true;
        return res;
    }

    if (config->output_kind == NYGEN_OUTPUT_MACHINE_IR) {
        Ny_Machine_Module mmod;
        Ny_Diagnostic_List mir_diags;
        ny_diagnostic_list_init(&mir_diags);
        bool mir_ok = ny_ir_lower_to_mir(&ctx->module, &mmod, &mir_diags);
        if (!mir_ok) {
            copy_diags_from_list(&res, &mir_diags);
            ny_diagnostic_list_destroy(&mir_diags);
            ny_mmod_destroy(&mmod);
            res.success = false;
            return res;
        }
        ny_diagnostic_list_destroy(&mir_diags);

        char *mir_dump = ny_mir_dump_module(&mmod, &ctx->arena);
        size_t dump_len = strlen(mir_dump);
        res.size = dump_len + 1;
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, mir_dump, res.size);

        ny_mmod_destroy(&mmod);
        res.success = true;
        return res;
    }

    const Ny_Target *target = config->target_triple ? ny_target_find(config->target_triple) : ny_target_get_default();
    if (!target) {
        add_diagnostic(&res, 0, 0, "unknown target specified");
        res.success = false;
        return res;
    }

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List mir_diags;
    ny_diagnostic_list_init(&mir_diags);
    bool mir_ok = ny_ir_lower_to_mir(&ctx->module, &mmod, &mir_diags);
    if (!mir_ok) {
        copy_diags_from_list(&res, &mir_diags);
        ny_diagnostic_list_destroy(&mir_diags);
        ny_mmod_destroy(&mmod);
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
            res.success = false;
            return res;
        }
        bool val_ok = ny_mfunc_validate_allocated(&mmod.functions[f], &ra_diags);
        if (!val_ok) {
            copy_diags_from_list(&res, &ra_diags);
            ny_diagnostic_list_destroy(&ra_diags);
            ny_mmod_destroy(&mmod);
            res.success = false;
            return res;
        }
        ny_diagnostic_list_destroy(&ra_diags);
    }

    X86_Module xmod;
    AArch64_Module aarch64_mod;
    Ny_Diagnostic_List x86_diags;
    ny_diagnostic_list_init(&x86_diags);
    bool x86_ok = false;
    bool use_aarch64 = (target->arch == NY_ARCH_AARCH64);

    if (use_aarch64) {
        bool ok = aarch64_lower_machine_mod(target, &mmod, &aarch64_mod, &x86_diags);
        if (!ok) {
            copy_diags_from_list(&res, &x86_diags);
            ny_diagnostic_list_destroy(&x86_diags);
            aarch64_mod_destroy(&aarch64_mod);
            ny_mmod_destroy(&mmod);
            res.success = false;
            return res;
        }
    } else {
        x86_ok = x86_lower_machine_mod(target, &mmod, &xmod, &x86_diags);
        if (!x86_ok) {
            copy_diags_from_list(&res, &x86_diags);
            ny_diagnostic_list_destroy(&x86_diags);
            x86_mod_destroy(&xmod);
            ny_mmod_destroy(&mmod);
            res.success = false;
            return res;
        }
    }
    ny_diagnostic_list_destroy(&x86_diags);

    if (config->output_kind == NYGEN_OUTPUT_ASM) {
        char *asm_dump = use_aarch64 ? aarch64_dump_mod(&aarch64_mod, &ctx->arena) : x86_dump_mod(&xmod, &ctx->arena);
        size_t dump_len = strlen(asm_dump);
        res.size = dump_len + 1;
        res.data = (uint8_t *)ny_alloc(res.size);
        memcpy(res.data, asm_dump, res.size);

        if (use_aarch64) aarch64_mod_destroy(&aarch64_mod);
        else x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        res.success = true;
        return res;
    }

    X86_Encoded_Module emod;
    AArch64_Encoded_Module aarch64_emod;
    Ny_Diagnostic_List enc_diags;
    ny_diagnostic_list_init(&enc_diags);
    bool enc_ok = false;

    if (use_aarch64) {
        enc_ok = aarch64_encode_module(&aarch64_emod, &aarch64_mod, &enc_diags);
    } else {
        enc_ok = x86_encode_module(&emod, &xmod, &enc_diags);
    }
    if (!enc_ok) {
        copy_diags_from_list(&res, &enc_diags);
        ny_diagnostic_list_destroy(&enc_diags);
        if (use_aarch64) {
            aarch64_encoded_mod_destroy(&aarch64_emod);
            aarch64_mod_destroy(&aarch64_mod);
        } else {
            x86_encoded_mod_destroy(&emod);
            x86_mod_destroy(&xmod);
        }
        ny_mmod_destroy(&mmod);
        res.success = false;
        return res;
    }
    ny_diagnostic_list_destroy(&enc_diags);
    if (use_aarch64) {
        aarch64_emod.debug_info = config->debug_info;
        aarch64_emod.emit_unwind = config->emit_unwind;
    } else {
        emod.debug_info = config->debug_info;
        emod.emit_unwind = config->emit_unwind;
    }

    if (config->output_kind == NYGEN_OUTPUT_BYTES) {
        if (use_aarch64) {
            res.size = aarch64_emod.text_section.count;
            if (res.size > 0) {
                res.data = (uint8_t *)ny_alloc(res.size);
                memcpy(res.data, aarch64_emod.text_section.bytes, res.size);
            }
        } else {
            res.size = emod.text_section.count;
            if (res.size > 0) {
                res.data = (uint8_t *)ny_alloc(res.size);
                memcpy(res.data, emod.text_section.bytes, res.size);
            }
        }
        if (use_aarch64) {
            aarch64_encoded_mod_destroy(&aarch64_emod);
            aarch64_mod_destroy(&aarch64_mod);
        } else {
            x86_encoded_mod_destroy(&emod);
            x86_mod_destroy(&xmod);
        }
        ny_mmod_destroy(&mmod);
        res.success = true;
        return res;
    }

    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);
    Ny_Diagnostic_List obj_diags;
    ny_diagnostic_list_init(&obj_diags);
    bool obj_ok = false;
    if (use_aarch64) {
        obj_ok = ny_emit_object_module(&obj_buf, target, &aarch64_emod, &obj_diags);
    } else {
        obj_ok = ny_emit_object_module(&obj_buf, target, &emod, &obj_diags);
    }
    if (!obj_ok) {
        copy_diags_from_list(&res, &obj_diags);
        ny_diagnostic_list_destroy(&obj_diags);
        ny_obj_buf_destroy(&obj_buf);
        if (use_aarch64) {
            aarch64_encoded_mod_destroy(&aarch64_emod);
            aarch64_mod_destroy(&aarch64_mod);
        } else {
            x86_encoded_mod_destroy(&emod);
            x86_mod_destroy(&xmod);
        }
        ny_mmod_destroy(&mmod);
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
    if (use_aarch64) {
        aarch64_encoded_mod_destroy(&aarch64_emod);
        aarch64_mod_destroy(&aarch64_mod);
    } else {
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
    }
    ny_mmod_destroy(&mmod);
    res.success = true;
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

    res = run_backend(&ctx, config);
    ny_context_destroy(&ctx);
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

void nygen_diagnostics_destroy(Nygen_Diagnostic *diags, size_t count) {
    if (!diags || count == 0) return;
    for (size_t i = 0; i < count; i++) {
        if (diags[i].message) {
            ny_free((void *)diags[i].message, strlen(diags[i].message) + 1);
        }
    }
    ny_free(diags, count * sizeof(Nygen_Diagnostic));
}

typedef struct {
    uint8_t *text;
    size_t text_size;
    uint8_t *rodata;
    size_t rodata_size;
    uint8_t *data;
    size_t data_size;
    Nygen_JIT_Symbol *symbols;
    size_t symbol_count;
    Nygen_JIT_Reloc *relocs;
    size_t reloc_count;
    char **names;
    size_t name_count;
} Nygen_Encoded_Owner;

static void add_diag_out(Nygen_Diagnostic **out_diags, size_t *out_count, const char *msg, uint32_t line, uint32_t col) {
    if (!out_diags || !out_count) return;
    size_t old = *out_count;
    size_t new_count = old + 1;
    *out_diags = (Nygen_Diagnostic *)ny_realloc(*out_diags, old * sizeof(Nygen_Diagnostic), new_count * sizeof(Nygen_Diagnostic));
    size_t len = strlen(msg);
    char *copy = (char *)ny_alloc(len + 1);
    memcpy(copy, msg, len + 1);
    (*out_diags)[old].message = copy;
    (*out_diags)[old].line = line;
    (*out_diags)[old].col = col;
    *out_count = new_count;
}

static void copy_diags_out(Nygen_Diagnostic **out_diags, size_t *out_count, const Ny_Diagnostic_List *list) {
    for (size_t i = 0; i < list->count; i++) {
        add_diag_out(out_diags, out_count, list->items[i].message, 0, 0);
    }
}

static char *dup_ny_string(Ny_String s) {
    char *name = (char *)ny_alloc(s.len + 1);
    memcpy(name, s.data, s.len);
    name[s.len] = '\0';
    return name;
}

bool nygen_compile_encoded(const char *source_text, size_t source_len, const Nygen_Config *config,
                           Nygen_Encoded_Module *out_module,
                           Nygen_Diagnostic **out_diags, size_t *out_diag_count) {
    if (out_module) memset(out_module, 0, sizeof(*out_module));
    if (out_diags) *out_diags = nullptr;
    if (out_diag_count) *out_diag_count = 0;

    if (!source_text || source_len == 0 || !config || !out_module) {
        add_diag_out(out_diags, out_diag_count, "invalid null argument to nygen_compile_encoded", 0, 0);
        return false;
    }

    const char *mod_name = config->module_name ? config->module_name : "nygen_module";
    Ny_Context ctx;
    ny_context_init(&ctx, mod_name);

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, source_text, source_len, &ctx.arena);
    bool parsed = ny_parse_module(&parser);
    if (!parsed) {
        for (size_t i = 0; i < parser.diag_count; i++) {
            add_diag_out(out_diags, out_diag_count, parser.diagnostics[i].message, parser.diagnostics[i].line, parser.diagnostics[i].col);
        }
        ny_parser_destroy(&parser);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_parser_destroy(&parser);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_validate_module(&ctx.module, &val_diags);
    if (!valid) {
        copy_diags_out(out_diags, out_diag_count, &val_diags);
        ny_diagnostic_list_destroy(&val_diags);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&val_diags);

    if (config->opt_level != NYGEN_OPT_O0) {
        Ny_Opt_Level opt_lvl = (config->opt_level == NYGEN_OPT_O2) ? NY_OPT_O2 : NY_OPT_O1;
        ny_opt_run_module_pipeline(&ctx.module, opt_lvl);
    }

    const Ny_Target *target = config->target_triple ? ny_target_find(config->target_triple) : ny_target_get_default();
    if (!target) {
        add_diag_out(out_diags, out_diag_count, "target not found for compilation", 0, 0);
        ny_context_destroy(&ctx);
        return false;
    }

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List mir_diags;
    ny_diagnostic_list_init(&mir_diags);
    bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &mir_diags);
    if (!mir_ok) {
        copy_diags_out(out_diags, out_diag_count, &mir_diags);
        ny_diagnostic_list_destroy(&mir_diags);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&mir_diags);

    for (size_t f = 0; f < mmod.function_count; f++) {
        Ny_Diagnostic_List ra_diags;
        ny_diagnostic_list_init(&ra_diags);
        bool ra_ok = ny_regalloc_run(&mmod.functions[f], target->abi, nullptr, &ra_diags);
        if (!ra_ok) {
            copy_diags_out(out_diags, out_diag_count, &ra_diags);
            ny_diagnostic_list_destroy(&ra_diags);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            return false;
        }
        ny_diagnostic_list_destroy(&ra_diags);
    }

    X86_Module xmod;
    AArch64_Module aarch64_mod;
    Ny_Diagnostic_List x86_diags;
    ny_diagnostic_list_init(&x86_diags);
    bool use_aarch64_enc = (target->arch == NY_ARCH_AARCH64);

    if (use_aarch64_enc) {
        bool ok = aarch64_lower_machine_mod(target, &mmod, &aarch64_mod, &x86_diags);
        if (!ok) {
            copy_diags_out(out_diags, out_diag_count, &x86_diags);
            ny_diagnostic_list_destroy(&x86_diags);
            aarch64_mod_destroy(&aarch64_mod);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            return false;
        }
    } else {
        bool ok = x86_lower_machine_mod(target, &mmod, &xmod, &x86_diags);
        if (!ok) {
            copy_diags_out(out_diags, out_diag_count, &x86_diags);
            ny_diagnostic_list_destroy(&x86_diags);
            x86_mod_destroy(&xmod);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            return false;
        }
    }
    ny_diagnostic_list_destroy(&x86_diags);

    X86_Encoded_Module emod;
    AArch64_Encoded_Module aarch64_emod;
    Ny_Diagnostic_List enc_diags;
    ny_diagnostic_list_init(&enc_diags);
    bool enc_ok = false;

    if (use_aarch64_enc) {
        enc_ok = aarch64_encode_module(&aarch64_emod, &aarch64_mod, &enc_diags);
    } else {
        enc_ok = x86_encode_module(&emod, &xmod, &enc_diags);
    }
    if (!enc_ok) {
        copy_diags_out(out_diags, out_diag_count, &enc_diags);
        ny_diagnostic_list_destroy(&enc_diags);
        if (use_aarch64_enc) {
            aarch64_encoded_mod_destroy(&aarch64_emod);
            aarch64_mod_destroy(&aarch64_mod);
        } else {
            x86_encoded_mod_destroy(&emod);
            x86_mod_destroy(&xmod);
        }
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&enc_diags);

    Nygen_Encoded_Owner *owner = (Nygen_Encoded_Owner *)ny_alloc_zero(sizeof(Nygen_Encoded_Owner));

    if (use_aarch64_enc) {
        owner->text_size = aarch64_emod.text_section.count;
        if (owner->text_size > 0) {
            owner->text = (uint8_t *)ny_alloc(owner->text_size);
            memcpy(owner->text, aarch64_emod.text_section.bytes, owner->text_size);
        }
        owner->rodata_size = aarch64_emod.rodata_section.count;
        if (owner->rodata_size > 0) {
            owner->rodata = (uint8_t *)ny_alloc(owner->rodata_size);
            memcpy(owner->rodata, aarch64_emod.rodata_section.bytes, owner->rodata_size);
        }
        owner->data_size = aarch64_emod.data_section.count;
        if (owner->data_size > 0) {
            owner->data = (uint8_t *)ny_alloc(owner->data_size);
            memcpy(owner->data, aarch64_emod.data_section.bytes, owner->data_size);
        }
    } else {
        owner->text_size = emod.text_section.count;
        if (owner->text_size > 0) {
            owner->text = (uint8_t *)ny_alloc(owner->text_size);
            memcpy(owner->text, emod.text_section.bytes, owner->text_size);
        }
        owner->rodata_size = emod.rodata_section.count;
        if (owner->rodata_size > 0) {
            owner->rodata = (uint8_t *)ny_alloc(owner->rodata_size);
            memcpy(owner->rodata, emod.rodata_section.bytes, owner->rodata_size);
        }
        owner->data_size = emod.data_section.count;
        if (owner->data_size > 0) {
            owner->data = (uint8_t *)ny_alloc(owner->data_size);
            memcpy(owner->data, emod.data_section.bytes, owner->data_size);
        }
    }

    size_t total_symbols = use_aarch64_enc ? (aarch64_emod.function_count + aarch64_emod.global_count)
                                           : (emod.function_count + emod.global_count);
    size_t total_relocs = use_aarch64_enc ? aarch64_emod.text_section.reloc_count : emod.text_section.reloc_count;

    owner->symbol_count = total_symbols;
    owner->name_count = total_symbols + total_relocs;

    if (total_symbols > 0) {
        owner->symbols = (Nygen_JIT_Symbol *)ny_alloc_zero(total_symbols * sizeof(Nygen_JIT_Symbol));
    }
    if (owner->name_count > 0) {
        owner->names = (char **)ny_alloc_zero(owner->name_count * sizeof(char *));
    }

    size_t sym_idx = 0;
    size_t name_idx = 0;

    if (use_aarch64_enc) {
        for (size_t i = 0; i < aarch64_emod.function_count; i++) {
            const AArch64_Function_Code *fn = &aarch64_emod.functions[i];
            char *name = dup_ny_string(fn->name);
            owner->names[name_idx++] = name;
            owner->symbols[sym_idx].name = name;
            owner->symbols[sym_idx].kind = NYGEN_SYM_FUNCTION;
            owner->symbols[sym_idx].offset = fn->offset;
            owner->symbols[sym_idx].size = fn->size;
            sym_idx++;
        }

        for (size_t g = 0; g < aarch64_emod.global_count; g++) {
            const AArch64_Encoded_Global *eg = &aarch64_emod.globals[g];
            char *name = dup_ny_string(eg->name);
            owner->names[name_idx++] = name;
            owner->symbols[sym_idx].name = name;
            if (eg->kind == NY_GLOBAL_CONST) owner->symbols[sym_idx].kind = NYGEN_SYM_RODATA;
            else if (eg->kind == NY_GLOBAL_DATA) owner->symbols[sym_idx].kind = NYGEN_SYM_DATA;
            else owner->symbols[sym_idx].kind = NYGEN_SYM_BSS;
            owner->symbols[sym_idx].offset = eg->offset;
            owner->symbols[sym_idx].size = eg->size;
            sym_idx++;
        }
    } else {
        for (size_t i = 0; i < emod.function_count; i++) {
            const X86_Function_Code *fn = &emod.functions[i];
            char *name = dup_ny_string(fn->name);
            owner->names[name_idx++] = name;
            owner->symbols[sym_idx].name = name;
            owner->symbols[sym_idx].kind = NYGEN_SYM_FUNCTION;
            owner->symbols[sym_idx].offset = fn->offset;
            owner->symbols[sym_idx].size = fn->size;
            sym_idx++;
        }

        for (size_t g = 0; g < emod.global_count; g++) {
            const X86_Encoded_Global *eg = &emod.globals[g];
            char *name = dup_ny_string(eg->name);
            owner->names[name_idx++] = name;
            owner->symbols[sym_idx].name = name;
            if (eg->kind == NY_GLOBAL_CONST) owner->symbols[sym_idx].kind = NYGEN_SYM_RODATA;
            else if (eg->kind == NY_GLOBAL_DATA) owner->symbols[sym_idx].kind = NYGEN_SYM_DATA;
            else owner->symbols[sym_idx].kind = NYGEN_SYM_BSS;
            owner->symbols[sym_idx].offset = eg->offset;
            owner->symbols[sym_idx].size = eg->size;
            sym_idx++;
        }
    }

    owner->reloc_count = total_relocs;
    if (owner->reloc_count > 0) {
        owner->relocs = (Nygen_JIT_Reloc *)ny_alloc_zero(owner->reloc_count * sizeof(Nygen_JIT_Reloc));
    }

    if (use_aarch64_enc) {
        for (size_t r = 0; r < aarch64_emod.text_section.reloc_count; r++) {
            const AArch64_Relocation *reloc = &aarch64_emod.text_section.relocs[r];
            char *name = dup_ny_string(reloc->symbol_name);
            owner->names[name_idx++] = name;
            Nygen_Reloc_Kind rk;
            switch (reloc->kind) {
            case AARCH64_FIXUP_CALL26: rk = NYGEN_RELOC_AARCH64_CALL26; break;
            case AARCH64_FIXUP_ADRP: rk = NYGEN_RELOC_AARCH64_ADRP; break;
            case AARCH64_FIXUP_ADD_LO12: rk = NYGEN_RELOC_AARCH64_ADD_LO12; break;
            case AARCH64_FIXUP_LDST_LO12: rk = NYGEN_RELOC_AARCH64_LDST_LO12; break;
            default: rk = NYGEN_RELOC_AARCH64_CALL26; break;
            }
            owner->relocs[r].kind = rk;
            owner->relocs[r].code_offset = reloc->code_offset;
            owner->relocs[r].symbol_name = name;
            owner->relocs[r].addend = reloc->addend;
        }
    } else {
        for (size_t r = 0; r < emod.text_section.reloc_count; r++) {
            const X86_Relocation *reloc = &emod.text_section.relocs[r];
            char *name = dup_ny_string(reloc->symbol_name);
            owner->names[name_idx++] = name;
            owner->relocs[r].kind = (reloc->kind == X86_FIXUP_CALL_REL32) ? NYGEN_RELOC_CALL_REL32 : NYGEN_RELOC_GLOBAL_REL32;
            owner->relocs[r].code_offset = reloc->code_offset;
            owner->relocs[r].symbol_name = name;
            owner->relocs[r].addend = reloc->addend;
        }
    }

    out_module->text = owner->text;
    out_module->text_size = owner->text_size;
    out_module->rodata = owner->rodata;
    out_module->rodata_size = owner->rodata_size;
    out_module->data = owner->data;
    out_module->data_size = owner->data_size;
    out_module->bss_size = use_aarch64_enc ? aarch64_emod.bss_size : emod.bss_size;
    out_module->symbols = owner->symbols;
    out_module->symbol_count = owner->symbol_count;
    out_module->relocs = owner->relocs;
    out_module->reloc_count = owner->reloc_count;
    out_module->internal = owner;

    if (use_aarch64_enc) {
        aarch64_encoded_mod_destroy(&aarch64_emod);
        aarch64_mod_destroy(&aarch64_mod);
    } else {
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
    }
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
    return true;
}

void nygen_encoded_module_destroy(Nygen_Encoded_Module *module) {
    if (!module || !module->internal) return;
    Nygen_Encoded_Owner *owner = (Nygen_Encoded_Owner *)module->internal;
    if (owner->text) ny_free(owner->text, owner->text_size);
    if (owner->rodata) ny_free(owner->rodata, owner->rodata_size);
    if (owner->data) ny_free(owner->data, owner->data_size);
    for (size_t i = 0; i < owner->name_count; i++) {
        if (owner->names[i]) {
            size_t name_len = strlen(owner->names[i]);
            ny_free(owner->names[i], name_len + 1);
        }
    }
    if (owner->names) ny_free(owner->names, owner->name_count * sizeof(char *));
    if (owner->symbols) ny_free(owner->symbols, owner->symbol_count * sizeof(Nygen_JIT_Symbol));
    if (owner->relocs) ny_free(owner->relocs, owner->reloc_count * sizeof(Nygen_JIT_Reloc));
    ny_free(owner, sizeof(Nygen_Encoded_Owner));
    memset(module, 0, sizeof(*module));
}

bool nygen_compile_nyir(const char *source_text, size_t source_len, const Nygen_Config *config,
                        uint8_t **out_data, size_t *out_size,
                        Nygen_Diagnostic **out_diags, size_t *out_diag_count) {
    if (out_data) *out_data = nullptr;
    if (out_size) *out_size = 0;
    if (out_diags) *out_diags = nullptr;
    if (out_diag_count) *out_diag_count = 0;

    if (!source_text || source_len == 0 || !config || !out_data || !out_size) {
        if (out_diags && out_diag_count) {
            *out_diags = (Nygen_Diagnostic *)ny_alloc_zero(sizeof(Nygen_Diagnostic));
            const char *msg = "invalid null argument to nygen_compile_nyir";
            size_t mlen = strlen(msg);
            char *copy = (char *)ny_alloc(mlen + 1);
            memcpy(copy, msg, mlen + 1);
            (*out_diags)[0].message = copy;
            (*out_diags)[0].line = 0;
            (*out_diags)[0].col = 0;
            *out_diag_count = 1;
        }
        return false;
    }

    const char *mod_name = config->module_name ? config->module_name : "nygen_module";
    Ny_Context ctx;
    ny_context_init(&ctx, mod_name);

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, source_text, source_len, &ctx.arena);
    bool parse_ok = ny_parse_module(&parser);
    if (!parse_ok) {
        for (size_t i = 0; i < parser.diag_count; i++) {
            if (out_diags && out_diag_count) {
                size_t old = *out_diag_count;
                size_t new_count = old + 1;
                *out_diags = (Nygen_Diagnostic *)ny_realloc(*out_diags, old * sizeof(Nygen_Diagnostic), new_count * sizeof(Nygen_Diagnostic));
                size_t mlen = strlen(parser.diagnostics[i].message);
                char *copy = (char *)ny_alloc(mlen + 1);
                memcpy(copy, parser.diagnostics[i].message, mlen + 1);
                (*out_diags)[old].message = copy;
                (*out_diags)[old].line = parser.diagnostics[i].line;
                (*out_diags)[old].col = parser.diagnostics[i].col;
                *out_diag_count = new_count;
            }
        }
        ny_parser_destroy(&parser);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_parser_destroy(&parser);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_validate_module(&ctx.module, &val_diags);
    if (!valid) {
        for (size_t i = 0; i < val_diags.count; i++) {
            if (out_diags && out_diag_count) {
                size_t old = *out_diag_count;
                size_t new_count = old + 1;
                *out_diags = (Nygen_Diagnostic *)ny_realloc(*out_diags, old * sizeof(Nygen_Diagnostic), new_count * sizeof(Nygen_Diagnostic));
                size_t mlen = strlen(val_diags.items[i].message);
                char *copy = (char *)ny_alloc(mlen + 1);
                memcpy(copy, val_diags.items[i].message, mlen + 1);
                (*out_diags)[old].message = copy;
                (*out_diags)[old].line = 0;
                (*out_diags)[old].col = 0;
                *out_diag_count = new_count;
            }
        }
        ny_diagnostic_list_destroy(&val_diags);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&val_diags);

    if (config->opt_level != NYGEN_OPT_O0) {
        Ny_Opt_Level opt_lvl = (config->opt_level == NYGEN_OPT_O2) ? NY_OPT_O2 : NY_OPT_O1;
        ny_opt_run_module_pipeline(&ctx.module, opt_lvl);
    }

    bool ok = nygen_serialize_nyir(&ctx.module, out_data, out_size);
    ny_context_destroy(&ctx);
    return ok;
}

Ny_Context *nygen_load_nyir(const uint8_t *data, size_t size,
                            Nygen_Diagnostic **out_diags, size_t *out_diag_count) {
    return nygen_deserialize_nyir(data, size, out_diags, out_diag_count);
}

Nygen_Result nygen_compile_ir(Ny_Context *ctx, const Nygen_Config *config) {
    if (!ctx || !config) {
        Nygen_Result res;
        memset(&res, 0, sizeof(res));
        add_diagnostic(&res, 0, 0, "invalid null argument to nygen_compile_ir");
        res.success = false;
        return res;
    }
    return run_backend(ctx, config);
}

void nygen_ir_destroy(Ny_Context *ctx) {
    if (!ctx) return;
    ny_context_destroy(ctx);
    ny_free(ctx, sizeof(Ny_Context));
}
