// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "x86_unwind.h"
#include "nybit/support.h"
#include <string.h>

static uint8_t x86_dwarf_reg(X86_Phys_Reg reg) {
    switch (reg) {
    case X86_RAX: return 0;
    case X86_RDX: return 1;
    case X86_RCX: return 2;
    case X86_RBX: return 3;
    case X86_RSI: return 4;
    case X86_RDI: return 5;
    case X86_RBP: return 6;
    case X86_RSP: return 7;
    case X86_R8:  return 8;
    case X86_R9:  return 9;
    case X86_R10: return 10;
    case X86_R11: return 11;
    case X86_R12: return 12;
    case X86_R13: return 13;
    case X86_R14: return 14;
    case X86_R15: return 15;
    default:      return 0;
    }
}

static void write_uleb128(Ny_Object_Buffer *buf, uint64_t val) {
    do {
        uint8_t byte = (uint8_t)(val & 0x7F);
        val >>= 7;
        if (val != 0) byte |= 0x80;
        ny_obj_buf_append_byte(buf, byte);
    } while (val != 0);
}

static void write_sleb128(Ny_Object_Buffer *buf, int64_t val) {
    bool more = true;
    while (more) {
        uint8_t byte = (uint8_t)(val & 0x7F);
        val >>= 7;
        if ((val == 0 && (byte & 0x40) == 0) || (val == -1 && (byte & 0x40) != 0)) {
            more = false;
        } else {
            byte |= 0x80;
        }
        ny_obj_buf_append_byte(buf, byte);
    }
}

bool x86_build_eh_frame(Ny_Object_Buffer *out_buf, const X86_Module *mod, const X86_Encoded_Module *emod) {
    (void)mod;
    if (!out_buf || !emod) return false;

    Ny_Object_Buffer cie_body;
    ny_obj_buf_init(&cie_body);
    uint32_t cie_id = 0;
    ny_obj_buf_append_bytes(&cie_body, &cie_id, 4);
    ny_obj_buf_append_byte(&cie_body, 1); /* version 1 */
    ny_obj_buf_append_byte(&cie_body, 0); /* augmentation: empty string */
    write_uleb128(&cie_body, 1);          /* code alignment factor */
    write_sleb128(&cie_body, -8);         /* data alignment factor */
    ny_obj_buf_append_byte(&cie_body, 16);/* return address register (16 on x86_64) */
    ny_obj_buf_append_byte(&cie_body, 0x0C); /* DW_CFA_def_cfa: r7 (RSP), offset 8 */
    write_uleb128(&cie_body, 7);
    write_uleb128(&cie_body, 8);
    ny_obj_buf_append_byte(&cie_body, 0x80 | 16); /* DW_CFA_offset r16, 1 (-8) */
    write_uleb128(&cie_body, 1);
    while (cie_body.count % 4 != 0) {
        ny_obj_buf_append_byte(&cie_body, 0); /* DW_CFA_nop */
    }
    uint32_t cie_len = (uint32_t)cie_body.count;
    ny_obj_buf_append_bytes(out_buf, &cie_len, 4);
    ny_obj_buf_append_bytes(out_buf, cie_body.bytes, cie_body.count);
    ny_obj_buf_destroy(&cie_body);

    for (size_t i = 0; i < emod->function_count; i++) {
        const X86_Function_Code *fn = &emod->functions[i];
        Ny_Object_Buffer fde_body;
        ny_obj_buf_init(&fde_body);
        uint32_t cie_ptr = (uint32_t)(fde_body.count + 4 + out_buf->count);
        ny_obj_buf_append_bytes(&fde_body, &cie_ptr, 4);
        uint64_t init_loc = (uint64_t)fn->offset;
        uint64_t addr_range = (uint64_t)fn->size;
        ny_obj_buf_append_bytes(&fde_body, &init_loc, 8);
        ny_obj_buf_append_bytes(&fde_body, &addr_range, 8);

        uint64_t advance = 1;
        ny_obj_buf_append_byte(&fde_body, 0x40 | (uint8_t)advance); /* DW_CFA_advance_loc 1 */
        ny_obj_buf_append_byte(&fde_body, 0x0E);                    /* DW_CFA_def_cfa_offset 16 */
        write_uleb128(&fde_body, 16);
        ny_obj_buf_append_byte(&fde_body, 0x80 | 6);                 /* DW_CFA_offset r6 (RBP), 2 (-16) */
        write_uleb128(&fde_body, 2);

        if (mod && i < mod->function_count) {
            const X86_Function *xfn = &mod->functions[i];
            uint32_t offset = 16;
            for (size_t r = 0; r < X86_GPR_COUNT; r++) {
                if (xfn->frame.callee_saved_mask & (1 << r)) {
                    offset += 8;
                    ny_obj_buf_append_byte(&fde_body, 0x40 | 1);     /* advance 1 byte push */
                    ny_obj_buf_append_byte(&fde_body, 0x0E);         /* DW_CFA_def_cfa_offset offset */
                    write_uleb128(&fde_body, offset);
                    ny_obj_buf_append_byte(&fde_body, 0x80 | x86_dwarf_reg((X86_Phys_Reg)r));
                    write_uleb128(&fde_body, offset / 8);
                }
            }
        }

        while (fde_body.count % 4 != 0) {
            ny_obj_buf_append_byte(&fde_body, 0);                    /* DW_CFA_nop */
        }
        uint32_t fde_len = (uint32_t)fde_body.count;
        ny_obj_buf_append_bytes(out_buf, &fde_len, 4);
        ny_obj_buf_append_bytes(out_buf, fde_body.bytes, fde_body.count);
        ny_obj_buf_destroy(&fde_body);
    }

    uint32_t zero_term = 0;
    ny_obj_buf_append_bytes(out_buf, &zero_term, 4);
    return true;
}

bool x86_build_dwarf_line(Ny_Object_Buffer *out_buf, const X86_Module *mod, const X86_Encoded_Module *emod) {
    (void)mod;
    if (!out_buf || !emod) return false;

    Ny_Object_Buffer body;
    ny_obj_buf_init(&body);

    uint16_t dwarf_version = 4;
    ny_obj_buf_append_bytes(&body, &dwarf_version, 2);

    Ny_Object_Buffer header;
    ny_obj_buf_init(&header);
    ny_obj_buf_append_byte(&header, 1);  /* minimum_instruction_length */
    ny_obj_buf_append_byte(&header, 1);  /* maximum_operations_per_instruction */
    ny_obj_buf_append_byte(&header, 1);  /* default_is_stmt */
    ny_obj_buf_append_byte(&header, (uint8_t)(int8_t)-5); /* line_base */
    ny_obj_buf_append_byte(&header, 14); /* line_range */
    ny_obj_buf_append_byte(&header, 13); /* opcode_base */

    uint8_t std_op_lengths[12] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    ny_obj_buf_append_bytes(&header, std_op_lengths, 12);
    ny_obj_buf_append_byte(&header, 0);  /* include_directories terminator */

    const char *fname = "source.ny";
    ny_obj_buf_append_bytes(&header, fname, strlen(fname) + 1);
    write_uleb128(&header, 0); /* dir index */
    write_uleb128(&header, 0); /* time */
    write_uleb128(&header, 0); /* size */
    ny_obj_buf_append_byte(&header, 0);  /* file table terminator */

    uint32_t header_len = (uint32_t)header.count;
    ny_obj_buf_append_bytes(&body, &header_len, 4);
    ny_obj_buf_append_bytes(&body, header.bytes, header.count);
    ny_obj_buf_destroy(&header);

    for (size_t i = 0; i < emod->function_count; i++) {
        const X86_Function_Code *fn = &emod->functions[i];
        /* DW_LNE_set_address */
        ny_obj_buf_append_byte(&body, 0);
        write_uleb128(&body, 9);
        ny_obj_buf_append_byte(&body, 2);
        uint64_t addr = (uint64_t)fn->offset;
        ny_obj_buf_append_bytes(&body, &addr, 8);

        /* DW_LNS_advance_line */
        ny_obj_buf_append_byte(&body, 3);
        write_sleb128(&body, (int64_t)(i + 1));

        /* DW_LNS_copy */
        ny_obj_buf_append_byte(&body, 1);

        /* DW_LNE_end_sequence */
        ny_obj_buf_append_byte(&body, 0);
        write_uleb128(&body, 1);
        ny_obj_buf_append_byte(&body, 1);
    }

    uint32_t total_len = (uint32_t)body.count;
    ny_obj_buf_append_bytes(out_buf, &total_len, 4);
    ny_obj_buf_append_bytes(out_buf, body.bytes, body.count);
    ny_obj_buf_destroy(&body);
    return true;
}

bool x86_build_dwarf_abbrev(Ny_Object_Buffer *out_buf) {
    if (!out_buf) return false;
    /* Abbrev 1: DW_TAG_compile_unit */
    write_uleb128(out_buf, 1);
    write_uleb128(out_buf, 0x11); /* DW_TAG_compile_unit */
    ny_obj_buf_append_byte(out_buf, 1); /* DW_CHILDREN_yes */
    write_uleb128(out_buf, 0x03); /* DW_AT_name */
    write_uleb128(out_buf, 0x08); /* DW_FORM_string */
    write_uleb128(out_buf, 0x25); /* DW_AT_producer */
    write_uleb128(out_buf, 0x08); /* DW_FORM_string */
    write_uleb128(out_buf, 0x10); /* DW_AT_stmt_list */
    write_uleb128(out_buf, 0x06); /* DW_FORM_data4 */
    write_uleb128(out_buf, 0);
    write_uleb128(out_buf, 0);

    /* Abbrev 2: DW_TAG_subprogram */
    write_uleb128(out_buf, 2);
    write_uleb128(out_buf, 0x2e); /* DW_TAG_subprogram */
    ny_obj_buf_append_byte(out_buf, 0); /* DW_CHILDREN_no */
    write_uleb128(out_buf, 0x03); /* DW_AT_name */
    write_uleb128(out_buf, 0x08); /* DW_FORM_string */
    write_uleb128(out_buf, 0x11); /* DW_AT_low_pc */
    write_uleb128(out_buf, 0x01); /* DW_FORM_addr */
    write_uleb128(out_buf, 0x12); /* DW_AT_high_pc */
    write_uleb128(out_buf, 0x01); /* DW_FORM_addr */
    write_uleb128(out_buf, 0);
    write_uleb128(out_buf, 0);

    /* Terminator */
    write_uleb128(out_buf, 0);
    return true;
}

bool x86_build_dwarf_info(Ny_Object_Buffer *out_buf, const X86_Module *mod, const X86_Encoded_Module *emod) {
    (void)mod;
    if (!out_buf || !emod) return false;

    Ny_Object_Buffer body;
    ny_obj_buf_init(&body);

    uint16_t version = 4;
    ny_obj_buf_append_bytes(&body, &version, 2);
    uint32_t abbrev_offset = 0;
    ny_obj_buf_append_bytes(&body, &abbrev_offset, 4);
    ny_obj_buf_append_byte(&body, 8); /* address size */

    /* Entry 1: compile unit */
    write_uleb128(&body, 1);
    const char *cu_name = "nybit_unit";
    ny_obj_buf_append_bytes(&body, cu_name, strlen(cu_name) + 1);
    const char *producer = "nybit 0.1";
    ny_obj_buf_append_bytes(&body, producer, strlen(producer) + 1);
    uint32_t stmt_list = 0;
    ny_obj_buf_append_bytes(&body, &stmt_list, 4);

    /* Subprograms */
    for (size_t i = 0; i < emod->function_count; i++) {
        const X86_Function_Code *fn = &emod->functions[i];
        write_uleb128(&body, 2);
        ny_obj_buf_append_bytes(&body, fn->name.data, fn->name.len);
        ny_obj_buf_append_byte(&body, 0);
        uint64_t low_pc = (uint64_t)fn->offset;
        uint64_t high_pc = (uint64_t)(fn->offset + fn->size);
        ny_obj_buf_append_bytes(&body, &low_pc, 8);
        ny_obj_buf_append_bytes(&body, &high_pc, 8);
    }
    write_uleb128(&body, 0); /* end children of compile_unit */

    uint32_t unit_len = (uint32_t)body.count;
    ny_obj_buf_append_bytes(out_buf, &unit_len, 4);
    ny_obj_buf_append_bytes(out_buf, body.bytes, body.count);
    ny_obj_buf_destroy(&body);
    return true;
}

bool x86_build_coff_pdata_xdata(Ny_Object_Buffer *pdata_buf, Ny_Object_Buffer *xdata_buf,
                               const X86_Module *mod, const X86_Encoded_Module *emod,
                               uint32_t xdata_rva_base) {
    if (!pdata_buf || !xdata_buf || !emod) return false;

    for (size_t i = 0; i < emod->function_count; i++) {
        const X86_Function_Code *fn = &emod->functions[i];
        uint32_t xdata_offset = (uint32_t)xdata_buf->count;
        uint32_t unwind_info_rva = xdata_rva_base + xdata_offset;

        /* RUNTIME_FUNCTION */
        uint32_t begin_addr = (uint32_t)fn->offset;
        uint32_t end_addr = (uint32_t)(fn->offset + fn->size);
        ny_obj_buf_append_bytes(pdata_buf, &begin_addr, 4);
        ny_obj_buf_append_bytes(pdata_buf, &end_addr, 4);
        ny_obj_buf_append_bytes(pdata_buf, &unwind_info_rva, 4);

        /* Emit UNWIND_INFO */
        uint8_t ver_flags = 1; /* Version 1, Flags 0 */
        uint8_t size_prologue = 4;
        uint8_t count_codes = 2; /* UWOP_SET_FPREG (2 slots) */
        uint8_t frame_reg = 5;   /* RBP */
        uint8_t frame_offset = 0;
        uint8_t frame_reg_offset = (frame_offset << 4) | (frame_reg & 0x0F);

        if (mod && i < mod->function_count) {
            const X86_Function *xfn = &mod->functions[i];
            for (size_t r = 0; r < X86_GPR_COUNT; r++) {
                if (xfn->frame.callee_saved_mask & (1 << r)) {
                    count_codes++; /* UWOP_PUSH_NONVOL */
                    size_prologue += 2;
                }
            }
            if (xfn->frame.stack_size > 0) {
                if (xfn->frame.stack_size <= 512 * 1024 - 8) {
                    count_codes += 2;
                } else {
                    count_codes += 3;
                }
                size_prologue += 4;
            }
        }

        ny_obj_buf_append_byte(xdata_buf, ver_flags);
        ny_obj_buf_append_byte(xdata_buf, size_prologue);
        ny_obj_buf_append_byte(xdata_buf, count_codes);
        ny_obj_buf_append_byte(xdata_buf, frame_reg_offset);

        /* UWOP_SET_FPREG */
        uint8_t code_off = 4;
        uint8_t op_info = 3; /* UWOP_SET_FPREG */
        ny_obj_buf_append_byte(xdata_buf, code_off);
        ny_obj_buf_append_byte(xdata_buf, op_info);
        uint16_t reserved = 0;
        ny_obj_buf_append_bytes(xdata_buf, &reserved, 2);

        if (mod && i < mod->function_count) {
            const X86_Function *xfn = &mod->functions[i];
            for (size_t r = 0; r < X86_GPR_COUNT; r++) {
                if (xfn->frame.callee_saved_mask & (1 << r)) {
                    ny_obj_buf_append_byte(xdata_buf, 2);
                    ny_obj_buf_append_byte(xdata_buf, (uint8_t)((r << 4) | 0)); /* UWOP_PUSH_NONVOL */
                }
            }
            if (xfn->frame.stack_size > 0) {
                uint32_t slots = xfn->frame.stack_size / 8;
                if (xfn->frame.stack_size <= 512 * 1024 - 8) {
                    ny_obj_buf_append_byte(xdata_buf, size_prologue);
                    ny_obj_buf_append_byte(xdata_buf, 1); /* UWOP_ALLOC_LARGE, op_info = 0 */
                    uint16_t alloc_val = (uint16_t)slots;
                    ny_obj_buf_append_bytes(xdata_buf, &alloc_val, 2);
                }
            }
        }

        while (xdata_buf->count % 4 != 0) {
            ny_obj_buf_append_byte(xdata_buf, 0);
        }
    }
    return true;
}

bool x86_build_codeview_debug_s(Ny_Object_Buffer *out_buf, const X86_Module *mod, const X86_Encoded_Module *emod) {
    (void)mod;
    if (!out_buf || !emod) return false;

    uint32_t cv_magic = 4; /* CV_SIGNATURE_C13 */
    ny_obj_buf_append_bytes(out_buf, &cv_magic, 4);

    for (size_t i = 0; i < emod->function_count; i++) {
        const X86_Function_Code *fn = &emod->functions[i];
        Ny_Object_Buffer subsection;
        ny_obj_buf_init(&subsection);

        /* DEBUG_S_LINES */
        uint32_t fn_offset = (uint32_t)fn->offset;
        uint16_t sec_idx = 1;
        uint16_t flags = 0;
        uint32_t code_size = (uint32_t)fn->size;
        ny_obj_buf_append_bytes(&subsection, &fn_offset, 4);
        ny_obj_buf_append_bytes(&subsection, &sec_idx, 2);
        ny_obj_buf_append_bytes(&subsection, &flags, 2);
        ny_obj_buf_append_bytes(&subsection, &code_size, 4);

        uint32_t file_offset = 0;
        uint32_t line_count = 1;
        uint32_t block_size = 12 + line_count * 8;
        ny_obj_buf_append_bytes(&subsection, &file_offset, 4);
        ny_obj_buf_append_bytes(&subsection, &line_count, 4);
        ny_obj_buf_append_bytes(&subsection, &block_size, 4);

        uint32_t inst_offset = 0;
        uint32_t line_num = 0x80000000 | (uint32_t)(i + 1);
        ny_obj_buf_append_bytes(&subsection, &inst_offset, 4);
        ny_obj_buf_append_bytes(&subsection, &line_num, 4);

        uint32_t sub_type = 0xF2; /* DEBUG_S_LINES */
        uint32_t sub_len = (uint32_t)subsection.count;
        ny_obj_buf_append_bytes(out_buf, &sub_type, 4);
        ny_obj_buf_append_bytes(out_buf, &sub_len, 4);
        ny_obj_buf_append_bytes(out_buf, subsection.bytes, subsection.count);
        ny_obj_buf_destroy(&subsection);
    }
    return true;
}
