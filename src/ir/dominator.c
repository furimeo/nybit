// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"

void ny_core_dominator_tree_init(Ny_Core_Dominator_Tree *dt, const Ny_Function *fn) {
    size_t n = fn->block_count;
    dt->block_count = n;
    dt->entry = fn->entry_block;

    if (n == 0 || fn->entry_block == NY_INVALID_BLOCK) {
        dt->idom = NULL;
        dt->rpo = NULL;
        return;
    }

    dt->idom = (Ny_Block_ID *)ny_alloc(n * sizeof(Ny_Block_ID));
    dt->rpo = (int *)ny_alloc(n * sizeof(int));
    for (size_t i = 0; i < n; i++) {
        dt->idom[i] = NY_INVALID_BLOCK;
        dt->rpo[i] = -1;
    }

    // Iterative DFS stack for post-order traversal
    bool *visited = (bool *)ny_alloc_zero(n * sizeof(bool));
    Ny_Block_ID *post_order = (Ny_Block_ID *)ny_alloc(n * sizeof(Ny_Block_ID));
    size_t po_count = 0;

    typedef struct Stack_Frame {
        Ny_Block_ID blk;
        size_t succ;
    } Stack_Frame;

    Stack_Frame *stack = (Stack_Frame *)ny_alloc(n * sizeof(Stack_Frame));
    size_t top = 0;

    stack[0].blk = fn->entry_block;
    stack[0].succ = 0;
    top = 1;
    visited[fn->entry_block] = true;

    while (top > 0) {
        Stack_Frame *frame = &stack[top - 1];
        Ny_Block *blk = ny_function_get_block((Ny_Function *)fn, frame->blk);

        if (blk && frame->succ < blk->succ_count) {
            Ny_Block_ID succ_id = blk->succs[frame->succ];
            frame->succ++;
            if ((size_t)succ_id < n && !visited[succ_id]) {
                visited[succ_id] = true;
                stack[top].blk = succ_id;
                stack[top].succ = 0;
                top++;
            }
        } else {
            post_order[po_count++] = frame->blk;
            top--;
        }
    }

    // Reverse Post-Order (RPO)
    for (size_t i = 0; i < po_count; i++) {
        Ny_Block_ID b = post_order[po_count - 1 - i];
        dt->rpo[b] = (int)i;
    }

    dt->idom[fn->entry_block] = fn->entry_block;

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 1; i < po_count; i++) {
            Ny_Block_ID b = post_order[po_count - 1 - i];
            Ny_Block *blk = ny_function_get_block((Ny_Function *)fn, b);
            if (!blk) continue;

            Ny_Block_ID new_idom = NY_INVALID_BLOCK;
            for (size_t p = 0; p < blk->pred_count; p++) {
                Ny_Block_ID pred_id = blk->preds[p];
                if (dt->idom[pred_id] != NY_INVALID_BLOCK) {
                    new_idom = pred_id;
                    break;
                }
            }
            if (new_idom == NY_INVALID_BLOCK) continue;

            for (size_t p = 0; p < blk->pred_count; p++) {
                Ny_Block_ID pred_id = blk->preds[p];
                if (pred_id != new_idom && dt->idom[pred_id] != NY_INVALID_BLOCK) {
                    // Intersect
                    Ny_Block_ID f1 = pred_id;
                    Ny_Block_ID f2 = new_idom;
                    while (f1 != f2) {
                        while (dt->rpo[f1] > dt->rpo[f2]) f1 = dt->idom[f1];
                        while (dt->rpo[f2] > dt->rpo[f1]) f2 = dt->idom[f2];
                    }
                    new_idom = f1;
                }
            }

            if (dt->idom[b] != new_idom) {
                dt->idom[b] = new_idom;
                changed = true;
            }
        }
    }

    ny_free(visited, n * sizeof(bool));
    ny_free(post_order, n * sizeof(Ny_Block_ID));
    ny_free(stack, n * sizeof(Stack_Frame));
}

void ny_core_dominator_tree_destroy(Ny_Core_Dominator_Tree *dt) {
    if (dt->idom) {
        ny_free(dt->idom, dt->block_count * sizeof(Ny_Block_ID));
    }
    if (dt->rpo) {
        ny_free(dt->rpo, dt->block_count * sizeof(int));
    }
    memset(dt, 0, sizeof(Ny_Core_Dominator_Tree));
}

bool ny_core_dominator_tree_dominates(const Ny_Core_Dominator_Tree *dt, Ny_Block_ID a, Ny_Block_ID b) {
    if (a == b) return true;
    if (!dt->idom || (size_t)a >= dt->block_count || (size_t)b >= dt->block_count) return false;
    if (dt->rpo[a] < 0 || dt->rpo[b] < 0) return false;

    Ny_Block_ID curr = b;
    while (curr != a && curr != dt->entry && dt->idom[curr] != NY_INVALID_BLOCK) {
        Ny_Block_ID parent = dt->idom[curr];
        if (parent == curr) break;
        curr = parent;
    }
    return curr == a;
}
