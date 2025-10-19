// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_ANALYSIS_H
#define NYBIT_ANALYSIS_H

#include <nybit/ir.h>
#include <nybit/support.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    Ny_Block_ID from;
    Ny_Block_ID to;
} Ny_CFG_Edge;

typedef struct {
    Ny_Block_ID *rpo;
    size_t rpo_count;
    Ny_Block_ID *post_order;
    size_t po_count;
    int32_t *rpo_index;
    size_t rpo_index_count;
    bool *is_reachable;
    size_t is_reachable_count;
    bool *loop_headers;
    size_t loop_headers_count;
    Ny_CFG_Edge *back_edges;
    size_t back_edges_count;
} Ny_CFG_Info;

void ny_cfg_info_init(Ny_CFG_Info *info, const Ny_Function *fn);
void ny_cfg_info_destroy(Ny_CFG_Info *info);
bool ny_cfg_is_reachable(const Ny_CFG_Info *info, Ny_Block_ID blk);
bool ny_cfg_is_loop_header(const Ny_CFG_Info *info, Ny_Block_ID blk);
bool ny_cfg_is_back_edge(const Ny_CFG_Info *info, Ny_Block_ID from, Ny_Block_ID to);
int32_t ny_cfg_rpo_index(const Ny_CFG_Info *info, Ny_Block_ID blk);
const Ny_Block_ID *ny_cfg_get_successors(const Ny_Function *fn, Ny_Block_ID blk, size_t *out_count);
const Ny_Block_ID *ny_cfg_get_predecessors(const Ny_Function *fn, Ny_Block_ID blk, size_t *out_count);

typedef struct {
    Ny_Block_ID *idom;
    size_t idom_count;
    int32_t *rpo;
    size_t rpo_count;
    Ny_Block_ID entry;

    Ny_Block_ID *children;
    size_t children_count;
    uint32_t *child_start;
    uint32_t *child_count;
    size_t block_count;

    Ny_Block_ID *frontiers;
    size_t frontiers_count;
    uint32_t *df_start;
    uint32_t *df_count;
} Ny_Dominator_Tree;

void ny_dominator_tree_init(Ny_Dominator_Tree *dt, const Ny_Function *fn);
void ny_dominator_tree_destroy(Ny_Dominator_Tree *dt);
bool ny_dominator_tree_dominates(const Ny_Dominator_Tree *dt, Ny_Block_ID a, Ny_Block_ID b);
bool ny_dominator_tree_strictly_dominates(const Ny_Dominator_Tree *dt, Ny_Block_ID a, Ny_Block_ID b);
Ny_Block_ID ny_dominator_tree_get_idom(const Ny_Dominator_Tree *dt, Ny_Block_ID blk);
const Ny_Block_ID *ny_dominator_tree_get_children(const Ny_Dominator_Tree *dt, Ny_Block_ID blk, size_t *out_count);
const Ny_Block_ID *ny_dominator_tree_get_frontier(const Ny_Dominator_Tree *dt, Ny_Block_ID blk, size_t *out_count);

typedef struct {
    Ny_Inst_ID inst;
    uint16_t op_idx;
} Ny_Use;

typedef struct {
    Ny_Inst_ID *def_inst;
    size_t def_inst_count;
    uint32_t *use_start;
    uint32_t *use_count;
    size_t val_count;
    Ny_Use *uses;
    size_t total_uses;
} Ny_Use_Def;

void ny_use_def_init(Ny_Use_Def *ud, const Ny_Function *fn);
void ny_use_def_destroy(Ny_Use_Def *ud);
Ny_Inst_ID ny_use_def_get_def(const Ny_Use_Def *ud, Ny_Value_ID val);
const Ny_Use *ny_use_def_get_uses(const Ny_Use_Def *ud, Ny_Value_ID val, size_t *out_count);
uint32_t ny_use_def_use_count(const Ny_Use_Def *ud, Ny_Value_ID val);
bool ny_use_def_has_uses(const Ny_Use_Def *ud, Ny_Value_ID val);
bool ny_use_def_has_single_use(const Ny_Use_Def *ud, Ny_Value_ID val);

typedef struct {
    size_t num_blocks;
    size_t words_per_block;
    uint64_t *live_in;
    uint64_t *live_out;
    size_t total_words;
} Ny_Liveness_Info;

void ny_liveness_init(Ny_Liveness_Info *liv, const Ny_Function *fn);
void ny_liveness_destroy(Ny_Liveness_Info *liv);
bool ny_liveness_is_live_in(const Ny_Liveness_Info *liv, Ny_Block_ID blk, Ny_Value_ID val);
bool ny_liveness_is_live_out(const Ny_Liveness_Info *liv, Ny_Block_ID blk, Ny_Value_ID val);
const uint64_t *ny_liveness_get_live_in_slice(const Ny_Liveness_Info *liv, Ny_Block_ID blk, size_t *out_words);
const uint64_t *ny_liveness_get_live_out_slice(const Ny_Liveness_Info *liv, Ny_Block_ID blk, size_t *out_words);

typedef struct {
    Ny_Function *fn;
    Ny_CFG_Info cfg;
    Ny_Dominator_Tree dom;
    Ny_Use_Def use_def;
    Ny_Liveness_Info liveness;

    bool valid_cfg;
    bool valid_dom;
    bool valid_use_def;
    bool valid_liveness;
} Ny_Analysis_Manager;

void ny_analysis_manager_init(Ny_Analysis_Manager *am, Ny_Function *fn);
void ny_analysis_manager_destroy(Ny_Analysis_Manager *am);
void ny_analysis_manager_set_function(Ny_Analysis_Manager *am, Ny_Function *fn);
void ny_analysis_invalidate_all(Ny_Analysis_Manager *am);
void ny_analysis_invalidate_cfg(Ny_Analysis_Manager *am);
void ny_analysis_invalidate_dom(Ny_Analysis_Manager *am);
void ny_analysis_invalidate_use_def(Ny_Analysis_Manager *am);
void ny_analysis_invalidate_liveness(Ny_Analysis_Manager *am);
Ny_CFG_Info *ny_analysis_get_cfg(Ny_Analysis_Manager *am);
Ny_Dominator_Tree *ny_analysis_get_dom(Ny_Analysis_Manager *am);
Ny_Use_Def *ny_analysis_get_use_def(Ny_Analysis_Manager *am);
Ny_Liveness_Info *ny_analysis_get_liveness(Ny_Analysis_Manager *am);

#ifdef __cplusplus
}
#endif

#endif
