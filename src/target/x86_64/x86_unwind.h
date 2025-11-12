// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_X86_64_UNWIND_H
#define NYBIT_TARGET_X86_64_UNWIND_H

#include "nybit/object.h"
#include "nybit/target_x86_64.h"

typedef struct X86_Unwind_Options {
    bool emit_unwind;
    bool debug_info;
} X86_Unwind_Options;

/* Build ELF .eh_frame CFI section */
bool x86_build_eh_frame(Ny_Object_Buffer *out_buf, const X86_Module *mod, const X86_Encoded_Module *emod);

/* Build ELF DWARF debug sections: .debug_line, .debug_info, .debug_abbrev */
bool x86_build_dwarf_line(Ny_Object_Buffer *out_buf, const X86_Module *mod, const X86_Encoded_Module *emod);
bool x86_build_dwarf_abbrev(Ny_Object_Buffer *out_buf);
bool x86_build_dwarf_info(Ny_Object_Buffer *out_buf, const X86_Module *mod, const X86_Encoded_Module *emod);

/* Build Windows x64 .pdata and .xdata sections */
bool x86_build_coff_pdata_xdata(Ny_Object_Buffer *pdata_buf, Ny_Object_Buffer *xdata_buf,
                               const X86_Module *mod, const X86_Encoded_Module *emod,
                               uint32_t xdata_rva_base);

/* Build Windows COFF CodeView line table (.debug$S) */
bool x86_build_codeview_debug_s(Ny_Object_Buffer *out_buf, const X86_Module *mod, const X86_Encoded_Module *emod);

#endif
