// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_AARCH64_INTERNAL_H
#define NYBIT_TARGET_AARCH64_INTERNAL_H

#include "nybit/target_aarch64.h"
#include "nybit/ir.h"

void aarch64_frame_layout(AArch64_Stack_Frame *frame, const Ny_Machine_Function *mfn, Ny_Target_ABI abi, const Ny_Type_Table *tt);
void aarch64_emit_prologue(AArch64_Block *blk, const AArch64_Stack_Frame *frame);
void aarch64_emit_epilogue(AArch64_Block *blk, const AArch64_Stack_Frame *frame);

#endif
