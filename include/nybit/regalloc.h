// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_REGALLOC_H
#define NYBIT_REGALLOC_H

#include "nybit/support.h"
#include "nybit/machine.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Ny_Live_Interval {
    uint32_t vreg;
    uint32_t start_idx;
    uint32_t end_idx;
    uint16_t reg_class;
    bool is_spilled;
    bool crosses_call;
    uint8_t assigned_phys;
    Ny_Slot_ID stack_slot;
} Ny_Live_Interval;

typedef struct Ny_RegAlloc_Result {
    uint16_t used_callee_saved_mask;
    size_t spilled_count;
} Ny_RegAlloc_Result;

bool ny_regalloc_run(Ny_Machine_Function *fn, Ny_Target_ABI abi, Ny_RegAlloc_Result *out_res, Ny_Diagnostic_List *diags);
bool ny_mfunc_validate_allocated(const Ny_Machine_Function *fn, Ny_Diagnostic_List *diags);

#ifdef __cplusplus
}
#endif

#endif
