// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/regalloc.h"
#include "nybit/target_x86_64.h"
#include "nybit/target_aarch64.h"

bool x86_regalloc_run_impl(Ny_Machine_Function *fn, Ny_Target_ABI abi, Ny_RegAlloc_Result *out_res, Ny_Diagnostic_List *diags);
bool aarch64_regalloc_run_impl(Ny_Machine_Function *fn, Ny_Target_ABI abi, Ny_RegAlloc_Result *out_res, Ny_Diagnostic_List *diags);

bool ny_regalloc_run(Ny_Machine_Function *fn, Ny_Target_ABI abi, Ny_RegAlloc_Result *out_res, Ny_Diagnostic_List *diags) {
    if (abi == NY_ABI_AAPCS64) {
        return aarch64_regalloc_run_impl(fn, abi, out_res, diags);
    }
    return x86_regalloc_run_impl(fn, abi, out_res, diags);
}
