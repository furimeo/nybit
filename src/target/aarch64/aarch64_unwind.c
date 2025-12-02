// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "aarch64_unwind.h"
#include "nybit/support.h"
#include <string.h>

/*
 * AAPCS64 DWARF register mapping (DWARF for the Arm 64-bit Architecture §4.1):
 * X0-X30  = 0-30
 * SP      = 31
 * V0-V31  = 64-95
 */
static uint8_t aarch64_dwarf_reg(AArch64_Phys_Reg reg) {
    if (reg < AARCH64_V0) {
        return (uint8_t)reg;
    }
    return (uint8_t)(64 + (reg - AARCH64_V0));
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

/*
 * AAPCS64 frame layout (aarch64_frame.c):
 *   SP after prologue points to bottom of frame.
 *   FP/LR stored at [SP, #stack_size - 16].
 *   Callee-saved at [SP, #stack_size - 16 - (n+1)*8].
 *   CFA = SP + stack_size at function entry (before SUB SP).
 */
bool aarch64_build_eh_frame(Ny_Object_Buffer *out_buf, const AArch64_Module *mod, const AArch64_Encoded_Module *emod) {
    if (!out_buf || !emod) return false;

    Ny_Object_Buffer cie_body;
    ny_obj_buf_init(&cie_body);
    uint32_t cie_id = 0;
    ny_obj_buf_append_bytes(&cie_body, &cie_id, 4);
    ny_obj_buf_append_byte(&cie_body, 3); /* version 3 (ARM .eh_frame convention) */
    ny_obj_buf_append_byte(&cie_body, 0); /* augmentation: empty */
    write_uleb128(&cie_body, 4);          /* code alignment factor = 4 (instruction size) */
    write_sleb128(&cie_body, -8);         /* data alignment factor = -8 */
    ny_obj_buf_append_byte(&cie_body, 30);/* return address register = X30 (LR) */
    /* Initial CFI: CFA = SP + 0 at function entry */
    ny_obj_buf_append_byte(&cie_body, 0x0C); /* DW_CFA_def_cfa: SP(31), offset 0 */
    write_uleb128(&cie_body, 31);
    write_uleb128(&cie_body, 0);
    while (cie_body.count % 4 != 0) {
        ny_obj_buf_append_byte(&cie_body, 0);
    }
    uint32_t cie_len = (uint32_t)cie_body.count;
    ny_obj_buf_append_bytes(out_buf, &cie_len, 4);
    ny_obj_buf_append_bytes(out_buf, cie_body.bytes, cie_body.count);
    ny_obj_buf_destroy(&cie_body);

    for (size_t i = 0; i < emod->function_count; i++) {
        const AArch64_Function_Code *fn = &emod->functions[i];
        Ny_Object_Buffer fde_body;
        ny_obj_buf_init(&fde_body);

        uint32_t cie_ptr = (uint32_t)(fde_body.count + 4 + out_buf->count);
        ny_obj_buf_append_bytes(&fde_body, &cie_ptr, 4);
        uint64_t init_loc = (uint64_t)fn->offset;
        uint64_t addr_range = (uint64_t)fn->size;
        ny_obj_buf_append_bytes(&fde_body, &init_loc, 8);
        ny_obj_buf_append_bytes(&fde_body, &addr_range, 8);

        if (mod && i < mod->function_count) {
            const AArch64_Function *afn = &mod->functions[i];
            uint32_t frame_size = afn->frame.stack_size;

            if (frame_size > 0) {
                /* After SUB SP,SP,#frame_size: CFA = SP + frame_size */
                ny_obj_buf_append_byte(&fde_body, 0x40 | 1); /* advance_loc 1 instruction */
                ny_obj_buf_append_byte(&fde_body, 0x0C); /* DW_CFA_def_cfa SP, frame_size */
                write_uleb128(&fde_body, 31);
                write_uleb128(&fde_body, frame_size);

                /* STP FP,LR,[SP,#off]: FP(29) and LR(30) saved at CFA - 16 and CFA - 8 */
                if (frame_size >= 16) {
                    ny_obj_buf_append_byte(&fde_body, 0x40 | 1); /* advance_loc 1 */
                    ny_obj_buf_append_byte(&fde_body, 0x80 | 29); /* DW_CFA_offset FP */
                    write_uleb128(&fde_body, 2);
                    ny_obj_buf_append_byte(&fde_body, 0x80 | 30); /* DW_CFA_offset LR */
                    write_uleb128(&fde_body, 1);
                }

                /* MOV FP,SP: FP now valid as frame pointer with CFA = FP + frame_size */
                ny_obj_buf_append_byte(&fde_body, 0x40 | 1); /* advance_loc 1 */
                ny_obj_buf_append_byte(&fde_body, 0x0C); /* DW_CFA_def_cfa FP(29), frame_size */
                write_uleb128(&fde_body, 29);
                write_uleb128(&fde_body, frame_size);

                /* Callee-saved registers via STP pairs / single STR */
                uint32_t cs_index = 0;
                bool prev_saved = false;
                AArch64_Phys_Reg prev_reg = AARCH64_NO_REG;

                for (uint32_t r = 0; r < AARCH64_PHYS_REG_COUNT; r++) {
                    if (r == AARCH64_FP || r == AARCH64_LR || r == AARCH64_SP) continue;
                    if (!(afn->frame.callee_saved_mask & ((uint64_t)1 << r))) continue;

                    if (!prev_saved) {
                        prev_reg = (AArch64_Phys_Reg)r;
                        prev_saved = true;
                    } else {
                        ny_obj_buf_append_byte(&fde_body, 0x40 | 1);
                        ny_obj_buf_append_byte(&fde_body, 0x80 | aarch64_dwarf_reg(prev_reg));
                        write_uleb128(&fde_body, cs_index + 4);
                        ny_obj_buf_append_byte(&fde_body, 0x80 | aarch64_dwarf_reg((AArch64_Phys_Reg)r));
                        write_uleb128(&fde_body, cs_index + 3);
                        cs_index += 2;
                        prev_saved = false;
                    }
                }

                if (prev_saved) {
                    ny_obj_buf_append_byte(&fde_body, 0x40 | 1);
                    ny_obj_buf_append_byte(&fde_body, 0x80 | aarch64_dwarf_reg(prev_reg));
                    write_uleb128(&fde_body, cs_index + 3);
                }

                if (afn->frame.need_x8_save) {
                    int32_t x8_off = afn->frame.x8_save_offset;
                    ny_obj_buf_append_byte(&fde_body, 0x40 | 1);
                    ny_obj_buf_append_byte(&fde_body, 0x80 | 8);
                    write_uleb128(&fde_body, (uint64_t)((frame_size - x8_off) / 8));
                }
            }
        }

        while (fde_body.count % 4 != 0) {
            ny_obj_buf_append_byte(&fde_body, 0);
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

bool aarch64_build_dwarf_line(Ny_Object_Buffer *out_buf, const AArch64_Encoded_Module *emod) {
    if (!out_buf || !emod) return false;

    Ny_Object_Buffer body;
    ny_obj_buf_init(&body);

    uint16_t dwarf_version = 4;
    ny_obj_buf_append_bytes(&body, &dwarf_version, 2);

    Ny_Object_Buffer header;
    ny_obj_buf_init(&header);
    ny_obj_buf_append_byte(&header, 4);  /* minimum_instruction_length = 4 (AArch64) */
    ny_obj_buf_append_byte(&header, 1);  /* maximum_operations_per_instruction */
    ny_obj_buf_append_byte(&header, 1);  /* default_is_stmt */
    ny_obj_buf_append_byte(&header, (uint8_t)(int8_t)-5);
    ny_obj_buf_append_byte(&header, 14);
    ny_obj_buf_append_byte(&header, 13);

    uint8_t std_op_lengths[12] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    ny_obj_buf_append_bytes(&header, std_op_lengths, 12);
    ny_obj_buf_append_byte(&header, 0);

    const char *fname = "source.ny";
    ny_obj_buf_append_bytes(&header, fname, strlen(fname) + 1);
    write_uleb128(&header, 0);
    write_uleb128(&header, 0);
    write_uleb128(&header, 0);
    ny_obj_buf_append_byte(&header, 0);

    uint32_t header_len = (uint32_t)header.count;
    ny_obj_buf_append_bytes(&body, &header_len, 4);
    ny_obj_buf_append_bytes(&body, header.bytes, header.count);
    ny_obj_buf_destroy(&header);

    for (size_t i = 0; i < emod->function_count; i++) {
        const AArch64_Function_Code *fn = &emod->functions[i];
        ny_obj_buf_append_byte(&body, 0);
        write_uleb128(&body, 9);
        ny_obj_buf_append_byte(&body, 2);
        uint64_t addr = (uint64_t)fn->offset;
        ny_obj_buf_append_bytes(&body, &addr, 8);
        ny_obj_buf_append_byte(&body, 3);
        write_sleb128(&body, (int64_t)(i + 1));
        ny_obj_buf_append_byte(&body, 1);
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

bool aarch64_build_dwarf_abbrev(Ny_Object_Buffer *out_buf) {
    if (!out_buf) return false;
    write_uleb128(out_buf, 1);
    write_uleb128(out_buf, 0x11);
    ny_obj_buf_append_byte(out_buf, 1);
    write_uleb128(out_buf, 0x03);
    write_uleb128(out_buf, 0x08);
    write_uleb128(out_buf, 0x25);
    write_uleb128(out_buf, 0x08);
    write_uleb128(out_buf, 0x10);
    write_uleb128(out_buf, 0x06);
    write_uleb128(out_buf, 0);
    write_uleb128(out_buf, 0);

    write_uleb128(out_buf, 2);
    write_uleb128(out_buf, 0x2e);
    ny_obj_buf_append_byte(out_buf, 0);
    write_uleb128(out_buf, 0x03);
    write_uleb128(out_buf, 0x08);
    write_uleb128(out_buf, 0x11);
    write_uleb128(out_buf, 0x01);
    write_uleb128(out_buf, 0x12);
    write_uleb128(out_buf, 0x01);
    write_uleb128(out_buf, 0);
    write_uleb128(out_buf, 0);

    write_uleb128(out_buf, 0);
    return true;
}

bool aarch64_build_dwarf_info(Ny_Object_Buffer *out_buf, const AArch64_Encoded_Module *emod) {
    if (!out_buf || !emod) return false;

    Ny_Object_Buffer body;
    ny_obj_buf_init(&body);

    uint16_t version = 4;
    ny_obj_buf_append_bytes(&body, &version, 2);
    uint32_t abbrev_offset = 0;
    ny_obj_buf_append_bytes(&body, &abbrev_offset, 4);
    ny_obj_buf_append_byte(&body, 8);

    write_uleb128(&body, 1);
    const char *cu_name = "nybit_unit";
    ny_obj_buf_append_bytes(&body, cu_name, strlen(cu_name) + 1);
    const char *producer = "nybit 0.1";
    ny_obj_buf_append_bytes(&body, producer, strlen(producer) + 1);
    uint32_t stmt_list = 0;
    ny_obj_buf_append_bytes(&body, &stmt_list, 4);

    for (size_t i = 0; i < emod->function_count; i++) {
        const AArch64_Function_Code *fn = &emod->functions[i];
        write_uleb128(&body, 2);
        ny_obj_buf_append_bytes(&body, fn->name.data, fn->name.len);
        ny_obj_buf_append_byte(&body, 0);
        uint64_t low_pc = (uint64_t)fn->offset;
        uint64_t high_pc = (uint64_t)(fn->offset + fn->size);
        ny_obj_buf_append_bytes(&body, &low_pc, 8);
        ny_obj_buf_append_bytes(&body, &high_pc, 8);
    }
    write_uleb128(&body, 0);

    uint32_t unit_len = (uint32_t)body.count;
    ny_obj_buf_append_bytes(out_buf, &unit_len, 4);
    ny_obj_buf_append_bytes(out_buf, body.bytes, body.count);
    ny_obj_buf_destroy(&body);
    return true;
}
