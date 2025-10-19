// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/analysis.h>
#include <string.h>

void ny_analysis_manager_init(Ny_Analysis_Manager *am, Ny_Function *fn) {
    memset(am, 0, sizeof(*am));
    am->fn = fn;
}

void ny_analysis_manager_destroy(Ny_Analysis_Manager *am) {
    ny_analysis_invalidate_all(am);
    memset(am, 0, sizeof(*am));
}

void ny_analysis_manager_set_function(Ny_Analysis_Manager *am, Ny_Function *fn) {
    ny_analysis_invalidate_all(am);
    am->fn = fn;
}

void ny_analysis_invalidate_all(Ny_Analysis_Manager *am) {
    if (am->valid_cfg) {
        ny_cfg_info_destroy(&am->cfg);
        am->valid_cfg = false;
    }
    if (am->valid_dom) {
        ny_dominator_tree_destroy(&am->dom);
        am->valid_dom = false;
    }
    if (am->valid_use_def) {
        ny_use_def_destroy(&am->use_def);
        am->valid_use_def = false;
    }
    if (am->valid_liveness) {
        ny_liveness_destroy(&am->liveness);
        am->valid_liveness = false;
    }
}

void ny_analysis_invalidate_cfg(Ny_Analysis_Manager *am) {
    if (am->valid_cfg) {
        ny_cfg_info_destroy(&am->cfg);
        am->valid_cfg = false;
    }
    if (am->valid_dom) {
        ny_dominator_tree_destroy(&am->dom);
        am->valid_dom = false;
    }
    if (am->valid_liveness) {
        ny_liveness_destroy(&am->liveness);
        am->valid_liveness = false;
    }
}

void ny_analysis_invalidate_dom(Ny_Analysis_Manager *am) {
    if (am->valid_dom) {
        ny_dominator_tree_destroy(&am->dom);
        am->valid_dom = false;
    }
}

void ny_analysis_invalidate_use_def(Ny_Analysis_Manager *am) {
    if (am->valid_use_def) {
        ny_use_def_destroy(&am->use_def);
        am->valid_use_def = false;
    }
    if (am->valid_liveness) {
        ny_liveness_destroy(&am->liveness);
        am->valid_liveness = false;
    }
}

void ny_analysis_invalidate_liveness(Ny_Analysis_Manager *am) {
    if (am->valid_liveness) {
        ny_liveness_destroy(&am->liveness);
        am->valid_liveness = false;
    }
}

Ny_CFG_Info *ny_analysis_get_cfg(Ny_Analysis_Manager *am) {
    if (!am->valid_cfg) {
        ny_cfg_info_init(&am->cfg, am->fn);
        am->valid_cfg = true;
    }
    return &am->cfg;
}

Ny_Dominator_Tree *ny_analysis_get_dom(Ny_Analysis_Manager *am) {
    if (!am->valid_dom) {
        ny_dominator_tree_init(&am->dom, am->fn);
        am->valid_dom = true;
    }
    return &am->dom;
}

Ny_Use_Def *ny_analysis_get_use_def(Ny_Analysis_Manager *am) {
    if (!am->valid_use_def) {
        ny_use_def_init(&am->use_def, am->fn);
        am->valid_use_def = true;
    }
    return &am->use_def;
}

Ny_Liveness_Info *ny_analysis_get_liveness(Ny_Analysis_Manager *am) {
    if (!am->valid_liveness) {
        ny_liveness_init(&am->liveness, am->fn);
        am->valid_liveness = true;
    }
    return &am->liveness;
}
