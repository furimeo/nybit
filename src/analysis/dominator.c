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
} Dom_Stack_Frame;

static inline Ny_Block_ID dom_intersect(const Ny_Dominator_Tree *dt, Ny_Block_ID b1, Ny_Block_ID b2) {
    Ny_Block_ID f1 = b1;
    Ny_Block_ID f2 = b2;
    while (f1 != f2) {
        while (dt->rpo[f1] > dt->rpo[f2]) {
            f1 = dt->idom[f1];
        }
        while (dt->rpo[f2] > dt->rpo[f1]) {
            f2 = dt->idom[f2];
        }
    }
    return f1;
}

void ny_dominator_tree_init(Ny_Dominator_Tree *dt, const Ny_Function *fn) {
    memset(dt, 0, sizeof(*dt));
    size_t n = fn->block_count;
    dt->entry = fn->entry_block;
    dt->block_count = n;

    if (n == 0 || fn->entry_block == NY_INVALID_BLOCK) {
        return;
    }

    dt->idom_count = n;
    dt->idom = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * n);
    dt->rpo_count = n;
    dt->rpo = (int32_t *)ny_alloc(sizeof(int32_t) * n);

    for (size_t i = 0; i < n; i++) {
        dt->idom[i] = NY_INVALID_BLOCK;
        dt->rpo[i] = -1;
    }

    bool *visited = (bool *)ny_alloc(sizeof(bool) * n);
    memset(visited, 0, sizeof(bool) * n);

    Ny_Block_ID *post_order = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * n);
    size_t po_count = 0;

    Dom_Stack_Frame *stack = (Dom_Stack_Frame *)ny_alloc(sizeof(Dom_Stack_Frame) * n);
    size_t top = 0;

    uint32_t entry_idx = (uint32_t)fn->entry_block;
    if (entry_idx < n) {
        visited[entry_idx] = true;
        stack[0].blk = fn->entry_block;
        stack[0].succ = 0;
        top = 1;
    }

    while (top > 0) {
        Dom_Stack_Frame *frame = &stack[top - 1];
        const Ny_Block *blk = ny_function_get_block(fn, frame->blk);

        if (blk != nullptr && frame->succ < blk->succ_count) {
            Ny_Block_ID succ_id = blk->succs[frame->succ];
            frame->succ++;
            uint32_t s_idx = (uint32_t)succ_id;
            if (s_idx < n && !visited[s_idx]) {
                visited[s_idx] = true;
                stack[top].blk = succ_id;
                stack[top].succ = 0;
                top++;
            }
        } else {
            post_order[po_count++] = frame->blk;
            top--;
        }
    }

    for (size_t i = 0; i < po_count; i++) {
        Ny_Block_ID b = post_order[po_count - 1 - i];
        dt->rpo[b] = (int32_t)i;
    }

    dt->idom[fn->entry_block] = fn->entry_block;

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 1; i < po_count; i++) {
            Ny_Block_ID b = post_order[po_count - 1 - i];
            const Ny_Block *blk = ny_function_get_block(fn, b);
            if (blk == nullptr) continue;

            Ny_Block_ID new_idom = NY_INVALID_BLOCK;
            for (size_t p_idx = 0; p_idx < blk->pred_count; p_idx++) {
                Ny_Block_ID p = blk->preds[p_idx];
                if ((size_t)p < n && dt->idom[p] != NY_INVALID_BLOCK) {
                    new_idom = p;
                    break;
                }
            }
            if (new_idom == NY_INVALID_BLOCK) continue;

            for (size_t p_idx = 0; p_idx < blk->pred_count; p_idx++) {
                Ny_Block_ID p = blk->preds[p_idx];
                if ((size_t)p < n && p != new_idom && dt->idom[p] != NY_INVALID_BLOCK) {
                    new_idom = dom_intersect(dt, p, new_idom);
                }
            }

            if (dt->idom[b] != new_idom) {
                dt->idom[b] = new_idom;
                changed = true;
            }
        }
    }

    dt->child_start = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
    dt->child_count = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
    memset(dt->child_start, 0, sizeof(uint32_t) * n);
    memset(dt->child_count, 0, sizeof(uint32_t) * n);

    for (size_t b = 0; b < n; b++) {
        Ny_Block_ID b_id = (Ny_Block_ID)b;
        if (b_id != fn->entry_block && dt->idom[b] != NY_INVALID_BLOCK) {
            size_t p = (size_t)dt->idom[b];
            if (p < n) {
                dt->child_count[p]++;
            }
        }
    }

    size_t total_children = 0;
    for (size_t i = 0; i < n; i++) {
        dt->child_start[i] = (uint32_t)total_children;
        total_children += dt->child_count[i];
    }

    dt->children_count = total_children;
    if (total_children > 0) {
        dt->children = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * total_children);
        uint32_t *child_cursor = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
        memset(child_cursor, 0, sizeof(uint32_t) * n);

        for (size_t b = 0; b < n; b++) {
            Ny_Block_ID b_id = (Ny_Block_ID)b;
            if (b_id != fn->entry_block && dt->idom[b] != NY_INVALID_BLOCK) {
                size_t p = (size_t)dt->idom[b];
                if (p < n) {
                    size_t pos = dt->child_start[p] + child_cursor[p];
                    dt->children[pos] = b_id;
                    child_cursor[p]++;
                }
            }
        }
        ny_free(child_cursor, sizeof(uint32_t) * n);
    }

    dt->df_start = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
    dt->df_count = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
    memset(dt->df_start, 0, sizeof(uint32_t) * n);
    memset(dt->df_count, 0, sizeof(uint32_t) * n);

    int32_t *last_visited = (int32_t *)ny_alloc(sizeof(int32_t) * n);
    for (size_t i = 0; i < n; i++) {
        last_visited[i] = -1;
    }

    for (size_t b = 0; b < n; b++) {
        Ny_Block_ID b_id = (Ny_Block_ID)b;
        const Ny_Block *blk = ny_function_get_block(fn, b_id);
        if (blk == nullptr || blk->pred_count < 2) continue;

        Ny_Block_ID idom_b = dt->idom[b];
        for (size_t p_idx = 0; p_idx < blk->pred_count; p_idx++) {
            Ny_Block_ID runner = blk->preds[p_idx];
            while (runner != idom_b && runner != NY_INVALID_BLOCK && (size_t)runner < n) {
                size_t r_idx = (size_t)runner;
                if (last_visited[r_idx] != (int32_t)b) {
                    last_visited[r_idx] = (int32_t)b;
                    dt->df_count[r_idx]++;
                }
                if (runner == fn->entry_block || runner == dt->idom[runner]) break;
                runner = dt->idom[runner];
            }
        }
    }

    size_t total_df = 0;
    for (size_t i = 0; i < n; i++) {
        dt->df_start[i] = (uint32_t)total_df;
        total_df += dt->df_count[i];
    }

    dt->frontiers_count = total_df;
    if (total_df > 0) {
        dt->frontiers = (Ny_Block_ID *)ny_alloc(sizeof(Ny_Block_ID) * total_df);
        uint32_t *df_cursor = (uint32_t *)ny_alloc(sizeof(uint32_t) * n);
        memset(df_cursor, 0, sizeof(uint32_t) * n);
        for (size_t i = 0; i < n; i++) {
            last_visited[i] = -1;
        }

        for (size_t b = 0; b < n; b++) {
            Ny_Block_ID b_id = (Ny_Block_ID)b;
            const Ny_Block *blk = ny_function_get_block(fn, b_id);
            if (blk == nullptr || blk->pred_count < 2) continue;

            Ny_Block_ID idom_b = dt->idom[b];
            for (size_t p_idx = 0; p_idx < blk->pred_count; p_idx++) {
                Ny_Block_ID runner = blk->preds[p_idx];
                while (runner != idom_b && runner != NY_INVALID_BLOCK && (size_t)runner < n) {
                    size_t r_idx = (size_t)runner;
                    if (last_visited[r_idx] != (int32_t)b) {
                        last_visited[r_idx] = (int32_t)b;
                        size_t pos = dt->df_start[r_idx] + df_cursor[r_idx];
                        dt->frontiers[pos] = b_id;
                        df_cursor[r_idx]++;
                    }
                    if (runner == fn->entry_block || runner == dt->idom[runner]) break;
                    runner = dt->idom[runner];
                }
            }
        }
        ny_free(df_cursor, sizeof(uint32_t) * n);
    }

    ny_free(last_visited, sizeof(int32_t) * n);
    ny_free(stack, sizeof(Dom_Stack_Frame) * n);
    ny_free(post_order, sizeof(Ny_Block_ID) * n);
    ny_free(visited, sizeof(bool) * n);
}

void ny_dominator_tree_destroy(Ny_Dominator_Tree *dt) {
    if (dt->idom != nullptr) {
        ny_free(dt->idom, sizeof(Ny_Block_ID) * dt->idom_count);
    }
    if (dt->rpo != nullptr) {
        ny_free(dt->rpo, sizeof(int32_t) * dt->rpo_count);
    }
    if (dt->children != nullptr) {
        ny_free(dt->children, sizeof(Ny_Block_ID) * dt->children_count);
    }
    if (dt->child_start != nullptr) {
        ny_free(dt->child_start, sizeof(uint32_t) * dt->block_count);
    }
    if (dt->child_count != nullptr) {
        ny_free(dt->child_count, sizeof(uint32_t) * dt->block_count);
    }
    if (dt->frontiers != nullptr) {
        ny_free(dt->frontiers, sizeof(Ny_Block_ID) * dt->frontiers_count);
    }
    if (dt->df_start != nullptr) {
        ny_free(dt->df_start, sizeof(uint32_t) * dt->block_count);
    }
    if (dt->df_count != nullptr) {
        ny_free(dt->df_count, sizeof(uint32_t) * dt->block_count);
    }
    memset(dt, 0, sizeof(*dt));
}

[[nodiscard]] bool ny_dominator_tree_dominates(const Ny_Dominator_Tree *dt, Ny_Block_ID a, Ny_Block_ID b) {
    if (a == b) return true;
    if (dt->idom == nullptr || (size_t)a >= dt->idom_count || (size_t)b >= dt->idom_count) return false;
    if (dt->rpo[a] < 0 || dt->rpo[b] < 0) return false;

    Ny_Block_ID curr = b;
    while (curr != a && curr != dt->entry && dt->idom[curr] != NY_INVALID_BLOCK) {
        Ny_Block_ID parent = dt->idom[curr];
        if (parent == curr) break;
        curr = parent;
    }
    return curr == a;
}

[[nodiscard]] bool ny_dominator_tree_strictly_dominates(const Ny_Dominator_Tree *dt, Ny_Block_ID a, Ny_Block_ID b) {
    return a != b && ny_dominator_tree_dominates(dt, a, b);
}

[[nodiscard]] Ny_Block_ID ny_dominator_tree_get_idom(const Ny_Dominator_Tree *dt, Ny_Block_ID blk) {
    size_t idx = (size_t)blk;
    return idx < dt->idom_count ? dt->idom[idx] : NY_INVALID_BLOCK;
}

const Ny_Block_ID *ny_dominator_tree_get_children(const Ny_Dominator_Tree *dt, Ny_Block_ID blk, size_t *out_count) {
    size_t idx = (size_t)blk;
    if (idx >= dt->block_count || dt->child_count == nullptr) {
        if (out_count != nullptr) *out_count = 0;
        return nullptr;
    }
    uint32_t count = dt->child_count[idx];
    if (count == 0) {
        if (out_count != nullptr) *out_count = 0;
        return nullptr;
    }
    if (out_count != nullptr) *out_count = count;
    return &dt->children[dt->child_start[idx]];
}

const Ny_Block_ID *ny_dominator_tree_get_frontier(const Ny_Dominator_Tree *dt, Ny_Block_ID blk, size_t *out_count) {
    size_t idx = (size_t)blk;
    if (idx >= dt->block_count || dt->df_count == nullptr) {
        if (out_count != nullptr) *out_count = 0;
        return nullptr;
    }
    uint32_t count = dt->df_count[idx];
    if (count == 0) {
        if (out_count != nullptr) *out_count = 0;
        return nullptr;
    }
    if (out_count != nullptr) *out_count = count;
    return &dt->frontiers[dt->df_start[idx]];
}
