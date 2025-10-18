// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"

void ny_block_add_edge(Ny_Block *pred, Ny_Block *succ) {
    bool has_succ = false;
    for (size_t i = 0; i < pred->succ_count; i++) {
        if (pred->succs[i] == succ->id) {
            has_succ = true;
            break;
        }
    }
    if (!has_succ) {
        ny_buf_grow((void **)&pred->succs, &pred->succ_capacity, pred->succ_count, sizeof(Ny_Block_ID));
        pred->succs[pred->succ_count++] = succ->id;
    }

    bool has_pred = false;
    for (size_t i = 0; i < succ->pred_count; i++) {
        if (succ->preds[i] == pred->id) {
            has_pred = true;
            break;
        }
    }
    if (!has_pred) {
        ny_buf_grow((void **)&succ->preds, &succ->pred_capacity, succ->pred_count, sizeof(Ny_Block_ID));
        succ->preds[succ->pred_count++] = pred->id;
    }
}

void ny_block_remove_edge(Ny_Block *pred, Ny_Block *succ) {
    for (size_t i = 0; i < pred->succ_count; i++) {
        if (pred->succs[i] == succ->id) {
            pred->succs[i] = pred->succs[pred->succ_count - 1];
            pred->succ_count--;
            break;
        }
    }
    for (size_t i = 0; i < succ->pred_count; i++) {
        if (succ->preds[i] == pred->id) {
            succ->preds[i] = succ->preds[succ->pred_count - 1];
            succ->pred_count--;
            break;
        }
    }
}
