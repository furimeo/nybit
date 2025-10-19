// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Ny_Block_ID blk;
    size_t succ;
} CFG_Stack_Frame;

void ny_cfg_info_init(Ny_CFG_Info *info, const Ny_Function *fn) {
    memset(info, 0, sizeof(*info));
    size_t n = fn->block_count;
    if (n == 0 || fn->entry_block == NY_INVALID_BLOCK) {
        return;
    }

    info->rpo_index_count = n;
    info->rpo_index = (int32_t *)ny_alloc(sizeof(int32_t) * n);
    for (size_t i = 0; i < n; i++) {
        info->rpo_index[i] = -1;
    }

    info->is_reachable_count = n;
    info->is_reachable = (bool *)ny_alloc(sizeof(bool) * n);
    memset(info->is_reachable, 0, sizeof(bool) * n);

    info->loop_headers_count = n;
    info->loop_headers = (bool *)ny_alloc(sizeof(bool) * n);
    memset(info->loop_headers, 0, sizeof(bool) * n);

    uint8_t *visited = (uint8_t *)ny_alloc(sizeof(uint8_t) * n);
    memset(visited, 0, sizeof(uint8_t) * n);

    CFG_Stack_Frame *stack = (CFG_Stack_Frame *)ny_alloc(sizeof(CFG_Stack_Frame) * n);
    size_t top = 0;

    Ny_Block_ID *temp_po = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * n);
    size_t po_count = 0;

    Ny_CFG_Edge *temp_back_edges = nullptr;
    size_t back_count = 0;
    size_t back_cap = 0;

    uint32_t entry_idx = (uint32_t)fn->entry_block;
    if (entry_idx < n) {
        visited[entry_idx] = 1;
        stack[0].blk = fn->entry_block;
        stack[0].succ = 0;
        top = 1;
    }

    while (top > 0) {
        CFG_Stack_Frame *frame = &stack[top - 1];
        const Ny_Block *blk = ny_function_get_block(fn, frame->blk);

        if (blk != nullptr && frame->succ < blk->succ_count) {
            Ny_Block_ID succ_id = blk->succs[frame->succ];
            frame->succ++;
            uint32_t s_idx = (uint32_t)succ_id;
            if (s_idx < n) {
                if (visited[s_idx] == 1) {
                    ny_buf_grow((void **)&temp_back_edges, &back_cap, back_count, sizeof(Ny_CFG_Edge));
                    temp_back_edges[back_count].from = frame->blk;
                    temp_back_edges[back_count].to = succ_id;
                    back_count++;
                    info->loop_headers[s_idx] = true;
                } else if (visited[s_idx] == 0) {
                    visited[s_idx] = 1;
                    stack[top].blk = succ_id;
                    stack[top].succ = 0;
                    top++;
                }
            }
        } else {
            visited[frame->blk] = 2;
            temp_po[po_count++] = frame->blk;
            top--;
        }
    }

    info->po_count = po_count;
    info->rpo_count = po_count;
    if (po_count > 0) {
        info->post_order = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * po_count);
        info->rpo = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * po_count);

        for (size_t i = 0; i < po_count; i++) {
            info->post_order[i] = temp_po[i];
            Ny_Block_ID rpo_b = temp_po[po_count - 1 - i];
            info->rpo[i] = rpo_b;
            if ((size_t)rpo_b < n) {
                info->rpo_index[rpo_b] = (int32_t)i;
                info->is_reachable[rpo_b] = true;
            }
        }
    }

    info->back_edges_count = back_count;
    if (back_count > 0) {
        info->back_edges = (Ny_CFG_Edge *)ny_alloc(sizeof(Ny_CFG_Edge) * back_count);
        memcpy(info->back_edges, temp_back_edges, sizeof(Ny_CFG_Edge) * back_count);
    }

    if (temp_back_edges != nullptr) {
        ny_free(temp_back_edges, sizeof(Ny_CFG_Edge) * back_cap);
    }
    ny_free(temp_po, sizeof(Ny_Block_ID) * n);
    ny_free(stack, sizeof(CFG_Stack_Frame) * n);
    ny_free(visited, sizeof(uint8_t) * n);
}

void ny_cfg_info_destroy(Ny_CFG_Info *info) {
    if (info->rpo != nullptr) {
        ny_free(info->rpo, sizeof(Ny_Block_ID) * info->rpo_count);
    }
    if (info->post_order != nullptr) {
        ny_free(info->post_order, sizeof(Ny_Block_ID) * info->po_count);
    }
    if (info->rpo_index != nullptr) {
        ny_free(info->rpo_index, sizeof(int32_t) * info->rpo_index_count);
    }
    if (info->is_reachable != nullptr) {
        ny_free(info->is_reachable, sizeof(bool) * info->is_reachable_count);
    }
    if (info->loop_headers != nullptr) {
        ny_free(info->loop_headers, sizeof(bool) * info->loop_headers_count);
    }
    if (info->back_edges != nullptr) {
        ny_free(info->back_edges, sizeof(Ny_CFG_Edge) * info->back_edges_count);
    }
    memset(info, 0, sizeof(*info));
}

[[nodiscard]] bool ny_cfg_is_reachable(const Ny_CFG_Info *info, Ny_Block_ID blk) {
    size_t idx = (size_t)blk;
    return idx < info->is_reachable_count && info->is_reachable[idx];
}

[[nodiscard]] bool ny_cfg_is_loop_header(const Ny_CFG_Info *info, Ny_Block_ID blk) {
    size_t idx = (size_t)blk;
    return idx < info->loop_headers_count && info->loop_headers[idx];
}

[[nodiscard]] bool ny_cfg_is_back_edge(const Ny_CFG_Info *info, Ny_Block_ID from, Ny_Block_ID to) {
    for (size_t i = 0; i < info->back_edges_count; i++) {
        if (info->back_edges[i].from == from && info->back_edges[i].to == to) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] int32_t ny_cfg_rpo_index(const Ny_CFG_Info *info, Ny_Block_ID blk) {
    size_t idx = (size_t)blk;
    return idx < info->rpo_index_count ? info->rpo_index[idx] : -1;
}

const Ny_Block_ID *ny_cfg_get_successors(const Ny_Function *fn, Ny_Block_ID blk, size_t *out_count) {
    const Ny_Block *b = ny_function_get_block(fn, blk);
    if (b != nullptr) {
        if (out_count != nullptr) *out_count = b->succ_count;
        return b->succs;
    }
    if (out_count != nullptr) *out_count = 0;
    return nullptr;
}

const Ny_Block_ID *ny_cfg_get_predecessors(const Ny_Function *fn, Ny_Block_ID blk, size_t *out_count) {
    const Ny_Block *b = ny_function_get_block(fn, blk);
    if (b != nullptr) {
        if (out_count != nullptr) *out_count = b->pred_count;
        return b->preds;
    }
    if (out_count != nullptr) *out_count = 0;
    return nullptr;
}
