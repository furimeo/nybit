// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "aarch64_internal.h"
#include <string.h>
#include <stdio.h>

void aarch64_buf_init(AArch64_Code_Buffer *buf) {
    memset(buf, 0, sizeof(*buf));
}

void aarch64_buf_destroy(AArch64_Code_Buffer *buf) {
    if (buf->bytes) {
        ny_free(buf->bytes, buf->capacity * sizeof(uint8_t));
        buf->bytes = nullptr;
    }
    if (buf->fixups) {
        ny_free(buf->fixups, buf->fixup_capacity * sizeof(AArch64_Fixup));
        buf->fixups = nullptr;
    }
    if (buf->relocs) {
        ny_free(buf->relocs, buf->reloc_capacity * sizeof(AArch64_Relocation));
        buf->relocs = nullptr;
    }
    memset(buf, 0, sizeof(*buf));
}

void aarch64_buf_append_bytes(AArch64_Code_Buffer *buf, const uint8_t *src, size_t len) {
    if (len == 0) return;
    ny_buf_grow((void **)&buf->bytes, &buf->capacity, buf->count + len, sizeof(uint8_t));
    memcpy(buf->bytes + buf->count, src, len);
    buf->count += len;
}

void aarch64_buf_append_word(AArch64_Code_Buffer *buf, uint32_t word) {
    uint8_t bytes[4] = {
        (uint8_t)(word & 0xFF),
        (uint8_t)((word >> 8) & 0xFF),
        (uint8_t)((word >> 16) & 0xFF),
        (uint8_t)((word >> 24) & 0xFF)
    };
    aarch64_buf_append_bytes(buf, bytes, 4);
}

void aarch64_buf_append_fixup(AArch64_Code_Buffer *buf, AArch64_Fixup fixup) {
    ny_buf_grow((void **)&buf->fixups, &buf->fixup_capacity, buf->fixup_count, sizeof(AArch64_Fixup));
    buf->fixups[buf->fixup_count++] = fixup;
}

void aarch64_buf_append_reloc(AArch64_Code_Buffer *buf, AArch64_Relocation reloc) {
    ny_buf_grow((void **)&buf->relocs, &buf->reloc_capacity, buf->reloc_count, sizeof(AArch64_Relocation));
    buf->relocs[buf->reloc_count++] = reloc;
}

static uint8_t reg_num(AArch64_Reg r) {
    return r.phys_reg;
}

static uint32_t sf_bit(uint8_t size) {
    return (size == 8) ? (1u << 31) : 0;
}

static bool fits_signed_immediate(int64_t val, uint32_t bits) {
    return val >= -((int64_t)1 << (bits - 1)) && val <= ((int64_t)1 << (bits - 1)) - 1;
}

static void patch_word(AArch64_Code_Buffer *buf, size_t offset, uint32_t word) {
    buf->bytes[offset + 0] = (uint8_t)(word & 0xFF);
    buf->bytes[offset + 1] = (uint8_t)((word >> 8) & 0xFF);
    buf->bytes[offset + 2] = (uint8_t)((word >> 16) & 0xFF);
    buf->bytes[offset + 3] = (uint8_t)((word >> 24) & 0xFF);
}

static uint32_t read_word(const AArch64_Code_Buffer *buf, size_t offset) {
    return (uint32_t)buf->bytes[offset]
         | ((uint32_t)buf->bytes[offset + 1] << 8)
         | ((uint32_t)buf->bytes[offset + 2] << 16)
         | ((uint32_t)buf->bytes[offset + 3] << 24);
}

bool aarch64_validate_function(const AArch64_Function *fn, Ny_Diagnostic_List *diags) {
    (void)fn;
    (void)diags;
    return true;
}

static uint32_t invert_cond(AArch64_Cond cond) {
    switch ((int)cond) {
    case AARCH64_COND_EQ:   return AARCH64_COND_NE;
    case AARCH64_COND_NE:   return AARCH64_COND_EQ;
    case AARCH64_COND_LT_S: return AARCH64_COND_GE_S;
    case AARCH64_COND_LE_S: return AARCH64_COND_GT_S;
    case AARCH64_COND_GT_S: return AARCH64_COND_LE_S;
    case AARCH64_COND_GE_S: return AARCH64_COND_LT_S;
    case AARCH64_COND_LT_U: return AARCH64_COND_GE_U;
    case AARCH64_COND_LE_U: return AARCH64_COND_GT_U;
    case AARCH64_COND_GT_U: return AARCH64_COND_LE_U;
    case AARCH64_COND_GE_U: return AARCH64_COND_LT_U;
    default: return AARCH64_COND_EQ;
    }
}

bool aarch64_encode_instruction(AArch64_Code_Buffer *buf, const AArch64_Instruction *inst, Ny_Diagnostic_List *diags) {
    uint32_t word = 0;
    size_t code_offset = buf->count;

    switch ((AArch64_Opcode)inst->opcode) {
    case AARCH64_OPC_MOV: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src = inst->ops[1].reg;
        word = sf_bit(dst.size) | 0x2A000000u
             | ((uint32_t)reg_num(src) << 16)
             | ((uint32_t)31 << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_MOVZ: {
        AArch64_Reg dst = inst->ops[0].reg;
        uint16_t imm = (uint16_t)inst->ops[1].imm;
        uint8_t hw = inst->shift & 0x3;
        word = sf_bit(dst.size) | 0x52800000u
             | ((uint32_t)hw << 21)
             | ((uint32_t)imm << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_MOVK: {
        AArch64_Reg dst = inst->ops[0].reg;
        uint16_t imm = (uint16_t)inst->ops[1].imm;
        uint8_t hw = inst->shift & 0x3;
        word = sf_bit(dst.size) | 0x72800000u
             | ((uint32_t)hw << 21)
             | ((uint32_t)imm << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_ADD: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Operand rhs = inst->ops[2];
        if (rhs.kind == AARCH64_OP_IMM) {
            uint32_t imm12 = (uint32_t)rhs.imm & 0xFFF;
            uint8_t sh = 0;
            if (rhs.imm > 4095 && (rhs.imm & 0xFFF) == 0 && rhs.imm <= 65535) {
                imm12 = (uint32_t)(rhs.imm >> 12) & 0xFFF;
                sh = 1;
            }
            word = sf_bit(dst.size) | 0x11000000u
                 | ((uint32_t)sh << 22)
                 | (imm12 << 10)
                 | ((uint32_t)reg_num(inst->ops[1].reg) << 5)
                 | (uint32_t)reg_num(dst);
        } else {
            word = sf_bit(dst.size) | 0x0B000000u
                 | ((uint32_t)reg_num(rhs.reg) << 16)
                 | ((uint32_t)reg_num(inst->ops[1].reg) << 5)
                 | (uint32_t)reg_num(dst);
        }
        break;
    }
    case AARCH64_OPC_SUB: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Operand rhs = inst->ops[2];
        if (rhs.kind == AARCH64_OP_IMM) {
            uint32_t imm12 = (uint32_t)rhs.imm & 0xFFF;
            uint8_t sh = 0;
            if (rhs.imm > 4095 && (rhs.imm & 0xFFF) == 0 && rhs.imm <= 65535) {
                imm12 = (uint32_t)(rhs.imm >> 12) & 0xFFF;
                sh = 1;
            }
            word = sf_bit(dst.size) | 0x51000000u
                 | ((uint32_t)sh << 22)
                 | (imm12 << 10)
                 | ((uint32_t)reg_num(inst->ops[1].reg) << 5)
                 | (uint32_t)reg_num(dst);
        } else {
            word = sf_bit(dst.size) | 0x4B000000u
                 | ((uint32_t)reg_num(rhs.reg) << 16)
                 | ((uint32_t)reg_num(inst->ops[1].reg) << 5)
                 | (uint32_t)reg_num(dst);
        }
        break;
    }
    case AARCH64_OPC_CMP: {
        AArch64_Reg lhs = inst->ops[0].reg;
        AArch64_Operand rhs = inst->ops[1];
        if (rhs.kind == AARCH64_OP_IMM) {
            uint32_t imm12 = (uint32_t)rhs.imm & 0xFFF;
            word = sf_bit(lhs.size) | 0x71000000u
                 | (imm12 << 10)
                 | ((uint32_t)reg_num(lhs) << 5)
                 | 31u;
        } else {
            word = sf_bit(lhs.size) | 0x6B000000u
                 | ((uint32_t)reg_num(rhs.reg) << 16)
                 | ((uint32_t)reg_num(lhs) << 5)
                 | 31u;
        }
        break;
    }
    case AARCH64_OPC_MUL: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src1 = inst->ops[1].reg;
        AArch64_Reg src2 = inst->ops[2].reg;
        word = sf_bit(dst.size) | 0x1B007C00u
             | ((uint32_t)reg_num(src2) << 16)
             | ((uint32_t)31 << 10)
             | ((uint32_t)reg_num(src1) << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_SDIV:
    case AARCH64_OPC_UDIV: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src1 = inst->ops[1].reg;
        AArch64_Reg src2 = inst->ops[2].reg;
        uint32_t base = (inst->opcode == AARCH64_OPC_SDIV) ? 0x1AC00C00u : 0x1AC00800u;
        word = sf_bit(dst.size) | base
             | ((uint32_t)reg_num(src2) << 16)
             | ((uint32_t)reg_num(src1) << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_MSUB: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src1 = inst->ops[1].reg;
        AArch64_Reg src2 = inst->ops[2].reg;
        AArch64_Reg src3 = inst->ops[3].reg;
        word = sf_bit(dst.size) | 0x1B008000u
             | ((uint32_t)reg_num(src2) << 16)
             | ((uint32_t)reg_num(src3) << 10)
             | ((uint32_t)reg_num(src1) << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_AND: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src1 = inst->ops[1].reg;
        AArch64_Reg src2 = inst->ops[2].reg;
        word = sf_bit(dst.size) | 0x0A000000u
             | ((uint32_t)reg_num(src2) << 16)
             | ((uint32_t)reg_num(src1) << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_ORR: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src1 = inst->ops[1].reg;
        AArch64_Reg src2 = inst->ops[2].reg;
        word = sf_bit(dst.size) | 0x2A000000u
             | ((uint32_t)reg_num(src2) << 16)
             | ((uint32_t)reg_num(src1) << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_EOR: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src1 = inst->ops[1].reg;
        AArch64_Reg src2 = inst->ops[2].reg;
        word = sf_bit(dst.size) | 0x4A000000u
             | ((uint32_t)reg_num(src2) << 16)
             | ((uint32_t)reg_num(src1) << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_MVN: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src = inst->ops[1].reg;
        word = sf_bit(dst.size) | 0x2A200000u
             | ((uint32_t)reg_num(src) << 16)
             | ((uint32_t)31 << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_LSL:
    case AARCH64_OPC_LSR:
    case AARCH64_OPC_ASR: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg src1 = inst->ops[1].reg;
        uint8_t size = dst.size;
        uint8_t max_shift = (size == 8) ? 63 : 31;

        if (inst->op_count >= 3 && inst->ops[2].kind == AARCH64_OP_REG) {
            AArch64_Reg src2 = inst->ops[2].reg;
            uint32_t base;
            if (inst->opcode == AARCH64_OPC_LSL) base = 0x1AC02000u;
            else if (inst->opcode == AARCH64_OPC_LSR) base = 0x1AC02400u;
            else base = 0x1AC02800u;
            word = sf_bit(size) | base
                 | ((uint32_t)reg_num(src2) << 16)
                 | ((uint32_t)reg_num(src1) << 5)
                 | (uint32_t)reg_num(dst);
        } else {
            uint8_t shift = inst->shift & max_shift;
            uint32_t immr, imms, base;
            if (inst->opcode == AARCH64_OPC_LSL) {
                immr = ((size == 8) ? 64 : 32) - shift;
                imms = ((size == 8) ? 63 : 31) - shift;
                base = (size == 8) ? 0xD3400000u : 0x53000000u;
            } else if (inst->opcode == AARCH64_OPC_LSR) {
                immr = shift;
                imms = (size == 8) ? 63 : 31;
                base = (size == 8) ? 0xD3400000u : 0x53000000u;
            } else {
                immr = shift;
                imms = (size == 8) ? 63 : 31;
                base = (size == 8) ? 0x93400000u : 0x13000000u;
            }
            word = sf_bit(size) | base
                 | ((immr & 0x3F) << 16)
                 | ((imms & 0x3F) << 10)
                 | ((uint32_t)reg_num(src1) << 5)
                 | (uint32_t)reg_num(dst);
        }
        break;
    }
    case AARCH64_OPC_CSET: {
        AArch64_Reg dst = inst->ops[0].reg;
        uint32_t inv = invert_cond((AArch64_Cond)inst->cond);
        word = sf_bit(dst.size) | 0x1A800000u
             | ((uint32_t)31 << 16)
             | ((uint32_t)31 << 5)
             | (uint32_t)reg_num(dst)
             | ((inv & 0xF) << 12)
             | (1u << 10);
        break;
    }
    case AARCH64_OPC_ADRP: {
        AArch64_Reg dst = inst->ops[0].reg;
        word = 0x90000000u | (uint32_t)reg_num(dst);
        aarch64_buf_append_word(buf, word);
        AArch64_Fixup fixup = {
            .kind = AARCH64_FIXUP_ADRP,
            .code_offset = code_offset,
            .target_block = NY_INVALID_BLOCK,
            .symbol_name = inst->ops[1].global_name,
            .addend = 0
        };
        aarch64_buf_append_fixup(buf, fixup);
        return true;
    }
    case AARCH64_OPC_ADD_LO12: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg base = inst->ops[1].reg;
        word = sf_bit(8) | 0x91000000u
             | ((uint32_t)reg_num(base) << 5)
             | (uint32_t)reg_num(dst);
        aarch64_buf_append_word(buf, word);
        AArch64_Fixup fixup = {
            .kind = AARCH64_FIXUP_ADD_LO12,
            .code_offset = code_offset,
            .target_block = NY_INVALID_BLOCK,
            .symbol_name = inst->ops[2].global_name,
            .addend = 0
        };
        aarch64_buf_append_fixup(buf, fixup);
        return true;
    }
    case AARCH64_OPC_LDR: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Mem mem = inst->ops[1].mem;
        uint32_t base = (dst.size == 8) ? 0xF9400000u : 0xB9400000u;
        uint32_t scale = (dst.size == 8) ? 8 : 4;
        uint32_t imm12 = (uint32_t)(mem.disp / (int32_t)scale) & 0xFFF;
        word = base | (imm12 << 10)
             | ((uint32_t)reg_num(mem.base) << 5)
             | (uint32_t)reg_num(dst);
        break;
    }
    case AARCH64_OPC_STR: {
        if (inst->op_count >= 3 && inst->ops[2].kind == AARCH64_OP_GLOBAL) {
            AArch64_Reg src = inst->ops[0].reg;
            AArch64_Reg base_reg = inst->ops[1].reg;
            uint32_t base = (src.size == 8) ? 0xF9000000u : 0xB9000000u;
            word = base
                 | ((uint32_t)reg_num(base_reg) << 5)
                 | (uint32_t)reg_num(src);
            aarch64_buf_append_word(buf, word);
            AArch64_Fixup fixup = {
                .kind = AARCH64_FIXUP_LDST_LO12,
                .code_offset = code_offset,
                .target_block = NY_INVALID_BLOCK,
                .symbol_name = inst->ops[2].global_name,
                .addend = 0
            };
            aarch64_buf_append_fixup(buf, fixup);
            return true;
        }
        AArch64_Reg src = inst->ops[0].reg;
        AArch64_Mem mem = inst->ops[1].mem;
        uint32_t base = (src.size == 8) ? 0xF9000000u : 0xB9000000u;
        uint32_t scale = (src.size == 8) ? 8 : 4;
        uint32_t imm12 = (uint32_t)(mem.disp / (int32_t)scale) & 0xFFF;
        word = base | (imm12 << 10)
             | ((uint32_t)reg_num(mem.base) << 5)
             | (uint32_t)reg_num(src);
        break;
    }
    case AARCH64_OPC_LDR_SYM: {
        AArch64_Reg dst = inst->ops[0].reg;
        AArch64_Reg base_reg = inst->ops[1].reg;
        uint32_t base = (dst.size == 8) ? 0xF9400000u : 0xB9400000u;
        word = base
             | ((uint32_t)reg_num(base_reg) << 5)
             | (uint32_t)reg_num(dst);
        aarch64_buf_append_word(buf, word);
        AArch64_Fixup fixup = {
            .kind = AARCH64_FIXUP_LDST_LO12,
            .code_offset = code_offset,
            .target_block = NY_INVALID_BLOCK,
            .symbol_name = inst->ops[2].global_name,
            .addend = 0
        };
        aarch64_buf_append_fixup(buf, fixup);
        return true;
    }
    case AARCH64_OPC_STP: {
        AArch64_Reg rt1 = inst->ops[0].reg;
        AArch64_Reg rt2 = inst->ops[1].reg;
        AArch64_Mem mem = inst->ops[2].mem;
        int32_t imm7 = mem.disp / 8;
        if (imm7 < -64 || imm7 > 63) {
            if (diags) {
                char buf2[128];
                snprintf(buf2, sizeof(buf2), "stp offset %d out of range for 7-bit signed", mem.disp);
                ny_diagnostic_list_append(diags, buf2);
            }
            return false;
        }
        word = 0xA9000000u
             | (((uint32_t)imm7 & 0x7F) << 15)
             | ((uint32_t)reg_num(rt2) << 10)
             | ((uint32_t)reg_num(mem.base) << 5)
             | (uint32_t)reg_num(rt1);
        break;
    }
    case AARCH64_OPC_LDP: {
        AArch64_Reg rt1 = inst->ops[0].reg;
        AArch64_Reg rt2 = inst->ops[1].reg;
        AArch64_Mem mem = inst->ops[2].mem;
        int32_t imm7 = mem.disp / 8;
        if (imm7 < -64 || imm7 > 63) {
            if (diags) {
                char buf2[128];
                snprintf(buf2, sizeof(buf2), "ldp offset %d out of range for 7-bit signed", mem.disp);
                ny_diagnostic_list_append(diags, buf2);
            }
            return false;
        }
        word = 0xA9400000u
             | (((uint32_t)imm7 & 0x7F) << 15)
             | ((uint32_t)reg_num(rt2) << 10)
             | ((uint32_t)reg_num(mem.base) << 5)
             | (uint32_t)reg_num(rt1);
        break;
    }
    case AARCH64_OPC_B: {
        word = 0x14000000u;
        aarch64_buf_append_word(buf, word);
        AArch64_Fixup fixup = {
            .kind = AARCH64_FIXUP_BRANCH26,
            .code_offset = code_offset,
            .target_block = inst->ops[0].label,
            .symbol_name = {0},
            .addend = 0
        };
        aarch64_buf_append_fixup(buf, fixup);
        return true;
    }
    case AARCH64_OPC_BL: {
        if (inst->ops[0].kind == AARCH64_OP_LABEL) {
            word = 0x94000000u;
            aarch64_buf_append_word(buf, word);
            AArch64_Fixup fixup = {
                .kind = AARCH64_FIXUP_BRANCH26,
                .code_offset = code_offset,
                .target_block = inst->ops[0].label,
                .symbol_name = {0},
                .addend = 0
            };
            aarch64_buf_append_fixup(buf, fixup);
        } else {
            word = 0x94000000u;
            aarch64_buf_append_word(buf, word);
            AArch64_Fixup fixup = {
                .kind = AARCH64_FIXUP_CALL26,
                .code_offset = code_offset,
                .target_block = NY_INVALID_BLOCK,
                .symbol_name = inst->ops[0].global_name,
                .addend = 0
            };
            aarch64_buf_append_fixup(buf, fixup);
        }
        return true;
    }
    case AARCH64_OPC_BCOND: {
        word = 0x54000000u | ((uint32_t)(inst->cond & 0xF));
        aarch64_buf_append_word(buf, word);
        AArch64_Fixup fixup = {
            .kind = AARCH64_FIXUP_BRANCH19,
            .code_offset = code_offset,
            .target_block = inst->ops[0].label,
            .symbol_name = {0},
            .addend = 0
        };
        aarch64_buf_append_fixup(buf, fixup);
        return true;
    }
    case AARCH64_OPC_BR: {
        AArch64_Reg src = inst->ops[0].reg;
        word = 0xD61F0000u
             | ((uint32_t)reg_num(src) << 5);
        break;
    }
    case AARCH64_OPC_BLR: {
        AArch64_Reg src = inst->ops[0].reg;
        word = 0xD63F0000u
             | ((uint32_t)reg_num(src) << 5);
        break;
    }
    case AARCH64_OPC_RET: {
        word = 0xD65F03C0u;
        break;
    }
    case AARCH64_OPC_BRK: {
        uint16_t imm = (uint16_t)inst->ops[0].imm;
        word = 0xD4200000u
             | ((uint32_t)imm << 5);
        break;
    }
    case AARCH64_OPC_NONE:
    default:
        if (diags) {
            char buf2[64];
            snprintf(buf2, sizeof(buf2), "unknown aarch64 opcode %d", inst->opcode);
            ny_diagnostic_list_append(diags, buf2);
        }
        return false;
    }

    aarch64_buf_append_word(buf, word);
    return true;
}

bool aarch64_encode_function(AArch64_Code_Buffer *buf, const AArch64_Function *fn, Ny_Diagnostic_List *diags) {
    size_t fixup_start = buf->fixup_count;

    Ny_Block_ID max_id = 0;
    for (size_t b = 0; b < fn->block_count; b++) {
        if (fn->blocks[b].id > max_id) {
            max_id = fn->blocks[b].id;
        }
    }

    size_t offsets_size = (size_t)(max_id + 1) * sizeof(size_t);
    size_t *block_offsets = (size_t *)ny_alloc_zero(offsets_size);

    for (size_t b = 0; b < fn->block_count; b++) {
        const AArch64_Block *blk = &fn->blocks[b];
        if (blk->id <= max_id) {
            block_offsets[blk->id] = buf->count;
        }
        for (size_t i = 0; i < blk->inst_count; i++) {
            if (!aarch64_encode_instruction(buf, &blk->instructions[i], diags)) {
                ny_free(block_offsets, offsets_size);
                return false;
            }
        }
    }

    for (size_t f = fixup_start; f < buf->fixup_count; f++) {
        AArch64_Fixup *fixup = &buf->fixups[f];
        if (fixup->kind == AARCH64_FIXUP_BRANCH26 || fixup->kind == AARCH64_FIXUP_BRANCH19) {
            if (fixup->target_block > max_id) continue;
            size_t target_offset = block_offsets[fixup->target_block];
            int64_t delta = (int64_t)target_offset - (int64_t)fixup->code_offset;
            int64_t imm = delta >> 2;

            uint32_t word = read_word(buf, fixup->code_offset);
            if (fixup->kind == AARCH64_FIXUP_BRANCH26) {
                if (!fits_signed_immediate(imm, 26)) {
                    if (diags) {
                        ny_diagnostic_list_append(diags, "branch26 offset out of range");
                    }
                    ny_free(block_offsets, offsets_size);
                    return false;
                }
                word = (word & ~0x03FFFFFFu) | ((uint32_t)imm & 0x03FFFFFFu);
            } else {
                if (!fits_signed_immediate(imm, 19)) {
                    if (diags) {
                        ny_diagnostic_list_append(diags, "branch19 offset out of range");
                    }
                    ny_free(block_offsets, offsets_size);
                    return false;
                }
                word = (word & ~0x00FFFFE0u) | (((uint32_t)imm & 0x7FFFFu) << 5);
            }
            patch_word(buf, fixup->code_offset, word);
        }
    }

    ny_free(block_offsets, offsets_size);
    return true;
}

void aarch64_encoded_mod_init(AArch64_Encoded_Module *emod, Ny_String name) {
    memset(emod, 0, sizeof(*emod));
    emod->name = name;
    aarch64_buf_init(&emod->text_section);
    aarch64_buf_init(&emod->rodata_section);
    aarch64_buf_init(&emod->data_section);
}

void aarch64_encoded_mod_destroy(AArch64_Encoded_Module *emod) {
    if (!emod) return;
    aarch64_buf_destroy(&emod->text_section);
    aarch64_buf_destroy(&emod->rodata_section);
    aarch64_buf_destroy(&emod->data_section);
    if (emod->functions) {
        ny_free(emod->functions, emod->function_capacity * sizeof(AArch64_Function_Code));
    }
    if (emod->globals) {
        ny_free(emod->globals, emod->global_capacity * sizeof(AArch64_Encoded_Global));
    }
    memset(emod, 0, sizeof(*emod));
}

bool aarch64_encode_module(AArch64_Encoded_Module *out_mod, const AArch64_Module *mod, Ny_Diagnostic_List *diags) {
    aarch64_encoded_mod_init(out_mod, mod->name);
    out_mod->source_mod = mod;

    out_mod->function_capacity = mod->function_count;
    if (out_mod->function_capacity > 0) {
        out_mod->functions = (AArch64_Function_Code *)ny_alloc_zero(out_mod->function_capacity * sizeof(AArch64_Function_Code));
    }

    for (size_t i = 0; i < mod->function_count; i++) {
        const AArch64_Function *fn = &mod->functions[i];
        size_t fn_start = out_mod->text_section.count;
        if (!aarch64_encode_function(&out_mod->text_section, fn, diags)) {
            return false;
        }
        size_t fn_end = out_mod->text_section.count;
        out_mod->functions[i].name = fn->name;
        out_mod->functions[i].offset = fn_start;
        out_mod->functions[i].size = fn_end - fn_start;
        out_mod->function_count++;
    }

    size_t fixup_start = out_mod->text_section.fixup_count;
    for (size_t f = 0; f < fixup_start; f++) {
        AArch64_Fixup *fixup = &out_mod->text_section.fixups[f];
        if (fixup->kind != AARCH64_FIXUP_CALL26) continue;
        if (fixup->symbol_name.len == 0) continue;

        bool found = false;
        for (size_t i = 0; i < out_mod->function_count; i++) {
            if (ny_str_eq(out_mod->functions[i].name, fixup->symbol_name)) {
                size_t target_offset = out_mod->functions[i].offset;
                int64_t delta = (int64_t)target_offset - (int64_t)fixup->code_offset;
                int64_t imm = delta >> 2;
                if (fits_signed_immediate(imm, 26)) {
                    uint32_t word = read_word(&out_mod->text_section, fixup->code_offset);
                    word = (word & ~0x03FFFFFFu) | ((uint32_t)imm & 0x03FFFFFFu);
                    patch_word(&out_mod->text_section, fixup->code_offset, word);
                    found = true;
                }
                break;
            }
        }

        if (!found) {
            AArch64_Relocation reloc = {
                .kind = fixup->kind,
                .code_offset = fixup->code_offset,
                .symbol_name = fixup->symbol_name,
                .addend = fixup->addend
            };
            aarch64_buf_append_reloc(&out_mod->text_section, reloc);
        }
    }

    for (size_t f = 0; f < fixup_start; f++) {
        AArch64_Fixup *fixup = &out_mod->text_section.fixups[f];
        if (fixup->kind == AARCH64_FIXUP_ADRP ||
            fixup->kind == AARCH64_FIXUP_ADD_LO12 ||
            fixup->kind == AARCH64_FIXUP_LDST_LO12) {
            AArch64_Relocation reloc = {
                .kind = fixup->kind,
                .code_offset = fixup->code_offset,
                .symbol_name = fixup->symbol_name,
                .addend = fixup->addend
            };
            aarch64_buf_append_reloc(&out_mod->text_section, reloc);
        }
    }

    for (size_t f = fixup_start; f < out_mod->text_section.fixup_count; f++) {
        AArch64_Fixup *fixup = &out_mod->text_section.fixups[f];
        if (fixup->kind == AARCH64_FIXUP_CALL26 && fixup->symbol_name.len > 0) {
            bool found = false;
            for (size_t i = 0; i < out_mod->function_count; i++) {
                if (ny_str_eq(out_mod->functions[i].name, fixup->symbol_name)) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                AArch64_Relocation reloc = {
                    .kind = fixup->kind,
                    .code_offset = fixup->code_offset,
                    .symbol_name = fixup->symbol_name,
                    .addend = fixup->addend
                };
                aarch64_buf_append_reloc(&out_mod->text_section, reloc);
            }
        }
    }

    out_mod->global_capacity = mod->global_count;
    if (out_mod->global_capacity > 0) {
        out_mod->globals = (AArch64_Encoded_Global *)ny_alloc_zero(out_mod->global_capacity * sizeof(AArch64_Encoded_Global));
    }

    for (size_t g = 0; g < mod->global_count; g++) {
        const Ny_Machine_Global *mg = &mod->globals[g];
        AArch64_Encoded_Global *eg = &out_mod->globals[g];
        eg->name = mg->name;
        eg->kind = mg->kind;
        eg->align = mg->align > 0 ? mg->align : 1;
        eg->size = mg->data_size;

        if (mg->kind == NY_GLOBAL_CONST) {
            if (eg->align > 1) {
                while (out_mod->rodata_section.count % eg->align != 0) {
                    uint8_t zero = 0;
                    aarch64_buf_append_bytes(&out_mod->rodata_section, &zero, 1);
                }
            }
            eg->offset = out_mod->rodata_section.count;
            if (mg->data && mg->data_size > 0) {
                aarch64_buf_append_bytes(&out_mod->rodata_section, mg->data, mg->data_size);
            }
        } else if (mg->kind == NY_GLOBAL_DATA) {
            if (eg->align > 1) {
                while (out_mod->data_section.count % eg->align != 0) {
                    uint8_t zero = 0;
                    aarch64_buf_append_bytes(&out_mod->data_section, &zero, 1);
                }
            }
            eg->offset = out_mod->data_section.count;
            if (mg->data && mg->data_size > 0) {
                aarch64_buf_append_bytes(&out_mod->data_section, mg->data, mg->data_size);
            }
        } else {
            if (eg->align > out_mod->bss_align) {
                out_mod->bss_align = eg->align;
            }
            while (out_mod->bss_size % eg->align != 0) {
                out_mod->bss_size++;
            }
            eg->offset = out_mod->bss_size;
            out_mod->bss_size += eg->size;
        }
        out_mod->global_count++;
    }

    return true;
}
