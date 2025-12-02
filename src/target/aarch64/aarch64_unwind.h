// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_AARCH64_UNWIND_H
#define NYBIT_TARGET_AARCH64_UNWIND_H

#include "nybit/object.h"
#include "nybit/target_aarch64.h"
#include "nybit/aarch64_encode.h"

bool aarch64_build_eh_frame(Ny_Object_Buffer *out_buf, const AArch64_Module *mod, const AArch64_Encoded_Module *emod);
bool aarch64_build_dwarf_line(Ny_Object_Buffer *out_buf, const AArch64_Encoded_Module *emod);
bool aarch64_build_dwarf_abbrev(Ny_Object_Buffer *out_buf);
bool aarch64_build_dwarf_info(Ny_Object_Buffer *out_buf, const AArch64_Encoded_Module *emod);

#endif
