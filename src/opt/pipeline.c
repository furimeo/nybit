// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/opt.h>
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>

bool ny_opt_run_pipeline(Ny_Module *mod, Ny_Function *fn, Ny_Opt_Level level) {
    if (fn->block_count == 0) return false;

    if (level == NY_OPT_O0) {
        return ny_canonicalize_function(mod, fn);
    }

    Ny_Analysis_Manager am;
    ny_analysis_manager_init(&am, fn);

    bool changed_any = false;
    int max_iters = (level == NY_OPT_O2) ? 4 : 1;

    for (int iter = 0; iter < max_iters; iter++) {
        bool changed_iter = false;

        if (ny_canonicalize_function(mod, fn)) {
            changed_iter = true;
            ny_analysis_invalidate_cfg(&am);
        }

        if (ny_opt_pass_cfg_simplify(mod, fn, &am)) {
            changed_iter = true;
        }

        if (ny_opt_pass_sccp(mod, fn, &am)) {
            changed_iter = true;
        }

        if (ny_opt_pass_dce(mod, fn, &am)) {
            changed_iter = true;
        }

        if (ny_opt_pass_copy_prop(mod, fn, &am)) {
            changed_iter = true;
        }

        if (ny_opt_pass_const_fold(mod, fn, &am)) {
            changed_iter = true;
        }

        if (ny_opt_pass_gvn(mod, fn, &am)) {
            changed_iter = true;
        }

        if (ny_opt_pass_cfg_simplify(mod, fn, &am)) {
            changed_iter = true;
        }

        if (!changed_iter) break;
        changed_any = true;
    }

    ny_analysis_manager_destroy(&am);
    return changed_any;
}

bool ny_opt_run_module_pipeline(Ny_Module *mod, Ny_Opt_Level level) {
    bool changed = false;
    for (size_t i = 0; i < mod->function_count; i++) {
        if (ny_opt_run_pipeline(mod, &mod->functions[i], level)) {
            changed = true;
        }
    }
    return changed;
}
