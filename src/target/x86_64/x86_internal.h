// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_X86_64_INTERNAL_H
#define NYBIT_TARGET_X86_64_INTERNAL_H

#include "nybit/target_x86_64.h"

void x86_frame_layout(X86_Stack_Frame *frame, const Ny_Machine_Function *mfn, Ny_Target_ABI abi);
void x86_emit_prologue(X86_Block *blk, const X86_Stack_Frame *frame);
void x86_emit_epilogue(X86_Block *blk, const X86_Stack_Frame *frame);

#endif
