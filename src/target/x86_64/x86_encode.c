// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/x86_encode.h"
#include "nybit/ir.h"
#include <string.h>

void x86_buf_init(X86_Code_Buffer *buf) {
    memset(buf, 0, sizeof(*buf));
}

void x86_buf_destroy(X86_Code_Buffer *buf) {
    if (buf->bytes) {
        ny_free(buf->bytes, buf->capacity * sizeof(uint8_t));
    }
    if (buf->fixups) {
        ny_free(buf->fixups, buf->fixup_capacity * sizeof(X86_Fixup));
    }
    if (buf->relocs) {
        ny_free(buf->relocs, buf->reloc_capacity * sizeof(X86_Relocation));
    }
    memset(buf, 0, sizeof(*buf));
}

void x86_buf_append_byte(X86_Code_Buffer *buf, uint8_t byte) {
    ny_buf_grow((void **)&buf->bytes, &buf->capacity, buf->count, sizeof(uint8_t));
    buf->bytes[buf->count++] = byte;
}

void x86_buf_append_bytes(X86_Code_Buffer *buf, const uint8_t *src, size_t len) {
    if (len == 0) return;
    ny_buf_grow((void **)&buf->bytes, &buf->capacity, buf->count + len - 1, sizeof(uint8_t));
    memcpy(buf->bytes + buf->count, src, len);
    buf->count += len;
}

void x86_buf_append_i32(X86_Code_Buffer *buf, int32_t val) {
    uint8_t b[4];
    b[0] = (uint8_t)(val & 0xFF);
    b[1] = (uint8_t)((val >> 8) & 0xFF);
    b[2] = (uint8_t)((val >> 16) & 0xFF);
    b[3] = (uint8_t)((val >> 24) & 0xFF);
    x86_buf_append_bytes(buf, b, 4);
}

void x86_buf_append_i64(X86_Code_Buffer *buf, int64_t val) {
    uint8_t b[8];
    for (size_t i = 0; i < 8; i++) {
        b[i] = (uint8_t)((val >> (i * 8)) & 0xFF);
    }
    x86_buf_append_bytes(buf, b, 8);
}

void x86_buf_append_fixup(X86_Code_Buffer *buf, X86_Fixup fixup) {
    ny_buf_grow((void **)&buf->fixups, &buf->fixup_capacity, buf->fixup_count, sizeof(X86_Fixup));
    buf->fixups[buf->fixup_count++] = fixup;
}

void x86_buf_append_reloc(X86_Code_Buffer *buf, X86_Relocation reloc) {
    ny_buf_grow((void **)&buf->relocs, &buf->reloc_capacity, buf->reloc_count, sizeof(X86_Relocation));
    buf->relocs[buf->reloc_count++] = reloc;
}

void x86_encoded_mod_init(X86_Encoded_Module *emod, Ny_String name) {
    memset(emod, 0, sizeof(*emod));
    emod->name = name;
    x86_buf_init(&emod->text_section);
    x86_buf_init(&emod->rodata_section);
    x86_buf_init(&emod->data_section);
}

void x86_encoded_mod_destroy(X86_Encoded_Module *emod) {
    x86_buf_destroy(&emod->text_section);
    x86_buf_destroy(&emod->rodata_section);
    x86_buf_destroy(&emod->data_section);
    if (emod->functions) {
        ny_free(emod->functions, emod->function_capacity * sizeof(X86_Function_Code));
    }
    if (emod->globals) {
        ny_free(emod->globals, emod->global_capacity * sizeof(X86_Encoded_Global));
    }
    memset(emod, 0, sizeof(*emod));
}

static inline bool is_byte_rex_reg(uint8_t r) {
    return r >= 4 && r <= 7;
}

static uint8_t cond_to_tttn(X86_Cond cond) {
    switch (cond) {
    case X86_COND_E:  return 0x4;
    case X86_COND_NE: return 0x5;
    case X86_COND_B:  return 0x2;
    case X86_COND_AE: return 0x3;
    case X86_COND_BE: return 0x6;
    case X86_COND_A:  return 0x7;
    case X86_COND_L:  return 0xC;
    case X86_COND_GE: return 0xD;
    case X86_COND_LE: return 0xE;
    case X86_COND_G:  return 0xF;
    default:          return 0x4;
    }
}

static void encode_modrm_op(X86_Code_Buffer *buf,
                            const uint8_t *opc_bytes, size_t opc_len,
                            bool reg_is_ext, uint8_t reg_or_ext,
                            const X86_Operand *rm_op,
                            uint8_t size, bool rex_w) {
    if (size == 2) {
        x86_buf_append_byte(buf, 0x66);
    }

    bool w = rex_w;
    bool r = false;
    bool x = false;
    bool b = false;
    bool force_rex = false;

    if (!reg_is_ext) {
        if (reg_or_ext >= X86_XMM0 && reg_or_ext < X86_PHYS_REG_COUNT) {
            reg_or_ext = reg_or_ext - X86_XMM0;
        } else if (reg_or_ext >= 8 && reg_or_ext < X86_GPR_COUNT) {
            r = true;
        } else if (size == 1 && is_byte_rex_reg(reg_or_ext)) {
            force_rex = true;
        }
    }

    if (rm_op->kind == X86_OP_REG) {
        uint8_t rm_phys = rm_op->reg.phys_reg;
        if (rm_phys >= X86_XMM0 && rm_phys < X86_PHYS_REG_COUNT) {
            // XMM0-XMM7 do not need REX.B
        } else if (rm_phys >= 8 && rm_phys < X86_GPR_COUNT) {
            b = true;
        } else if (size == 1 && is_byte_rex_reg(rm_phys)) {
            force_rex = true;
        }
    } else if (rm_op->kind == X86_OP_MEM) {
        const X86_Mem *m = &rm_op->mem;
        if (!m->is_rip_relative && m->base.phys_reg != X86_NO_REG) {
            if (m->base.phys_reg >= 8 && m->base.phys_reg < X86_GPR_COUNT) {
                b = true;
            }
        }
        if (x86_reg_is_valid(m->index) && m->index.phys_reg != X86_NO_REG) {
            if (m->index.phys_reg >= 8 && m->index.phys_reg < X86_GPR_COUNT) {
                x = true;
            }
        }
    }

    if (w || r || x || b || force_rex) {
        uint8_t rex = 0x40 | (w ? 0x08 : 0) | (r ? 0x04 : 0) | (x ? 0x02 : 0) | (b ? 0x01 : 0);
        x86_buf_append_byte(buf, rex);
    }

    for (size_t i = 0; i < opc_len; i++) {
        x86_buf_append_byte(buf, opc_bytes[i]);
    }

    uint8_t reg_bits = reg_or_ext & 7;

    if (rm_op->kind == X86_OP_REG) {
        uint8_t mod = 0b11;
        uint8_t rm_phys = rm_op->reg.phys_reg;
        if (rm_phys >= X86_XMM0 && rm_phys < X86_PHYS_REG_COUNT) {
            rm_phys = rm_phys - X86_XMM0;
        }
        uint8_t rm_bits = rm_phys & 7;
        x86_buf_append_byte(buf, (uint8_t)((mod << 6) | (reg_bits << 3) | rm_bits));
    } else if (rm_op->kind == X86_OP_MEM) {
        const X86_Mem *m = &rm_op->mem;
        if (m->is_rip_relative || m->base.phys_reg == X86_NO_REG) {
            x86_buf_append_byte(buf, (uint8_t)((0b00 << 6) | (reg_bits << 3) | 0b101));
            x86_buf_append_i32(buf, m->disp);
            if (m->symbol.len > 0) {
                X86_Fixup fixup = {
                    .kind = X86_FIXUP_GLOBAL_REL32,
                    .code_offset = buf->count - 4,
                    .symbol_name = m->symbol,
                    .addend = 0,
                };
                x86_buf_append_fixup(buf, fixup);
            }
        } else {
            uint8_t base_phys = m->base.phys_reg;
            uint8_t base_low = base_phys & 7;
            bool has_idx = x86_reg_is_valid(m->index) && m->index.phys_reg != X86_NO_REG;
            bool use_sib = has_idx || (base_low == 4);

            uint8_t mod = 0;
            int disp_len = 0;
            if (base_low == 5) {
                if (m->disp >= -128 && m->disp <= 127) {
                    mod = 0b01;
                    disp_len = 1;
                } else {
                    mod = 0b10;
                    disp_len = 4;
                }
            } else {
                if (m->disp == 0) {
                    mod = 0b00;
                    disp_len = 0;
                } else if (m->disp >= -128 && m->disp <= 127) {
                    mod = 0b01;
                    disp_len = 1;
                } else {
                    mod = 0b10;
                    disp_len = 4;
                }
            }

            if (!use_sib) {
                x86_buf_append_byte(buf, (uint8_t)((mod << 6) | (reg_bits << 3) | base_low));
            } else {
                x86_buf_append_byte(buf, (uint8_t)((mod << 6) | (reg_bits << 3) | 0b100));
                uint8_t scale_val = (m->scale == 8) ? 3 : (m->scale == 4) ? 2 : (m->scale == 2) ? 1 : 0;
                uint8_t idx_val = has_idx ? (m->index.phys_reg & 7) : 0b100;
                uint8_t sib = (uint8_t)((scale_val << 6) | (idx_val << 3) | base_low);
                x86_buf_append_byte(buf, sib);
            }

            if (disp_len == 1) {
                x86_buf_append_byte(buf, (uint8_t)(int8_t)m->disp);
            } else if (disp_len == 4) {
                x86_buf_append_i32(buf, m->disp);
            }
        }
    }
}

bool x86_validate_instruction(const X86_Instruction *inst, Ny_Diagnostic_List *diags) {
    if (inst->opcode <= X86_OPC_NONE || inst->opcode >= X86_OPC_COUNT) {
        if (diags) ny_diagnostic_list_append(diags, "invalid x86 opcode");
        return false;
    }

    for (size_t i = 0; i < inst->op_count; i++) {
        const X86_Operand *op = &inst->ops[i];
        if (op->kind == X86_OP_REG) {
            if (op->reg.is_virtual || op->reg.phys_reg >= X86_PHYS_REG_COUNT) {
                if (diags) ny_diagnostic_list_append(diags, "unallocated virtual or invalid register in machine encoder");
                return false;
            }
        } else if (op->kind == X86_OP_MEM) {
            if (x86_reg_is_valid(op->mem.base)) {
                if (op->mem.base.is_virtual || op->mem.base.phys_reg >= X86_GPR_COUNT) {
                    if (diags) ny_diagnostic_list_append(diags, "invalid base register in memory operand");
                    return false;
                }
            }
            if (x86_reg_is_valid(op->mem.index)) {
                if (op->mem.index.is_virtual || op->mem.index.phys_reg >= X86_GPR_COUNT || op->mem.index.phys_reg == X86_RSP) {
                    if (diags) ny_diagnostic_list_append(diags, "invalid index register (RSP cannot be index)");
                    return false;
                }
            }
            if (op->mem.scale != 0 && op->mem.scale != 1 && op->mem.scale != 2 && op->mem.scale != 4 && op->mem.scale != 8) {
                if (diags) ny_diagnostic_list_append(diags, "invalid SIB scale");
                return false;
            }
        }
    }

    if (inst->op_count >= 2 && inst->ops[0].kind == X86_OP_MEM && inst->ops[1].kind == X86_OP_MEM) {
        if (diags) ny_diagnostic_list_append(diags, "x86 does not support memory-to-memory operations");
        return false;
    }

    return true;
}

bool x86_validate_function(const X86_Function *fn, Ny_Diagnostic_List *diags) {
    for (size_t b = 0; b < fn->block_count; b++) {
        const X86_Block *blk = &fn->blocks[b];
        for (size_t i = 0; i < blk->inst_count; i++) {
            if (!x86_validate_instruction(&blk->instructions[i], diags)) {
                return false;
            }
        }
    }
    return true;
}

static bool encode_arith_binop(X86_Code_Buffer *buf, uint8_t base_opc, uint8_t ext, const X86_Instruction *inst) {
    uint8_t sz = inst->size;
    const X86_Operand *dst = &inst->ops[0];
    const X86_Operand *src = &inst->ops[1];

    if (src->kind == X86_OP_REG) {
        uint8_t opc = (sz == 1) ? (uint8_t)(base_opc - 1) : base_opc;
        encode_modrm_op(buf, &opc, 1, false, src->reg.phys_reg, dst, sz, sz == 8);
        return true;
    }

    if (src->kind == X86_OP_MEM && dst->kind == X86_OP_REG) {
        uint8_t opc = (sz == 1) ? (uint8_t)(base_opc + 1) : (uint8_t)(base_opc + 2);
        encode_modrm_op(buf, &opc, 1, false, dst->reg.phys_reg, src, sz, sz == 8);
        return true;
    }

    if (src->kind == X86_OP_IMM) {
        int64_t imm = src->imm;
        if (sz > 1 && imm >= -128 && imm <= 127) {
            uint8_t opc = 0x83;
            encode_modrm_op(buf, &opc, 1, true, ext, dst, sz, sz == 8);
            x86_buf_append_byte(buf, (uint8_t)(int8_t)imm);
            return true;
        }
        if (sz > 1) {
            uint8_t opc = 0x81;
            encode_modrm_op(buf, &opc, 1, true, ext, dst, sz, sz == 8);
            x86_buf_append_i32(buf, (int32_t)imm);
            return true;
        }
        uint8_t opc = 0x80;
        encode_modrm_op(buf, &opc, 1, true, ext, dst, 1, false);
        x86_buf_append_byte(buf, (uint8_t)(int8_t)imm);
        return true;
    }

    return false;
}

bool x86_encode_instruction(X86_Code_Buffer *buf, const X86_Instruction *inst, Ny_Diagnostic_List *diags) {
    if (!x86_validate_instruction(inst, diags)) {
        return false;
    }

    switch ((X86_Opcode)inst->opcode) {
    case X86_OPC_MOV: {
        const X86_Operand *dst = &inst->ops[0];
        const X86_Operand *src = &inst->ops[1];
        uint8_t sz = inst->size;

        if (dst->kind == X86_OP_REG && src->kind == X86_OP_REG) {
            uint8_t opc = (sz == 1) ? 0x88 : 0x89;
            encode_modrm_op(buf, &opc, 1, false, src->reg.phys_reg, dst, sz, sz == 8);
            return true;
        }
        if (dst->kind == X86_OP_REG && src->kind == X86_OP_MEM) {
            uint8_t opc = (sz == 1) ? 0x8A : 0x8B;
            encode_modrm_op(buf, &opc, 1, false, dst->reg.phys_reg, src, sz, sz == 8);
            return true;
        }
        if (dst->kind == X86_OP_MEM && src->kind == X86_OP_REG) {
            uint8_t opc = (sz == 1) ? 0x88 : 0x89;
            encode_modrm_op(buf, &opc, 1, false, src->reg.phys_reg, dst, sz, sz == 8);
            return true;
        }
        if (dst->kind == X86_OP_REG && src->kind == X86_OP_IMM) {
            int64_t imm = src->imm;
            if (sz == 8) {
                if (imm < INT32_MIN || imm > INT32_MAX) {
                    uint8_t rex = 0x48 | ((dst->reg.phys_reg >= 8) ? 0x01 : 0x00);
                    x86_buf_append_byte(buf, rex);
                    x86_buf_append_byte(buf, (uint8_t)(0xB8 + (dst->reg.phys_reg & 7)));
                    x86_buf_append_i64(buf, imm);
                    return true;
                }
                uint8_t opc = 0xC7;
                encode_modrm_op(buf, &opc, 1, true, 0, dst, 8, true);
                x86_buf_append_i32(buf, (int32_t)imm);
                return true;
            }
            if (sz == 4) {
                if (dst->reg.phys_reg >= 8) {
                    x86_buf_append_byte(buf, 0x41);
                }
                x86_buf_append_byte(buf, (uint8_t)(0xB8 + (dst->reg.phys_reg & 7)));
                x86_buf_append_i32(buf, (int32_t)imm);
                return true;
            }
            if (sz == 2) {
                x86_buf_append_byte(buf, 0x66);
                if (dst->reg.phys_reg >= 8) {
                    x86_buf_append_byte(buf, 0x41);
                }
                x86_buf_append_byte(buf, (uint8_t)(0xB8 + (dst->reg.phys_reg & 7)));
                x86_buf_append_byte(buf, (uint8_t)(imm & 0xFF));
                x86_buf_append_byte(buf, (uint8_t)((imm >> 8) & 0xFF));
                return true;
            }
            if (sz == 1) {
                if (dst->reg.phys_reg >= 8) {
                    x86_buf_append_byte(buf, 0x41);
                } else if (is_byte_rex_reg(dst->reg.phys_reg)) {
                    x86_buf_append_byte(buf, 0x40);
                }
                x86_buf_append_byte(buf, (uint8_t)(0xB0 + (dst->reg.phys_reg & 7)));
                x86_buf_append_byte(buf, (uint8_t)(imm & 0xFF));
                return true;
            }
        }
        if (dst->kind == X86_OP_MEM && src->kind == X86_OP_IMM) {
            uint8_t opc = (sz == 1) ? 0xC6 : 0xC7;
            encode_modrm_op(buf, &opc, 1, true, 0, dst, sz, sz == 8);
            if (sz == 8 || sz == 4) {
                x86_buf_append_i32(buf, (int32_t)src->imm);
            } else if (sz == 2) {
                x86_buf_append_byte(buf, (uint8_t)(src->imm & 0xFF));
                x86_buf_append_byte(buf, (uint8_t)((src->imm >> 8) & 0xFF));
            } else {
                x86_buf_append_byte(buf, (uint8_t)(src->imm & 0xFF));
            }
            return true;
        }
        break;
    }
    case X86_OPC_LEA: {
        uint8_t opc = 0x8D;
        encode_modrm_op(buf, &opc, 1, false, inst->ops[0].reg.phys_reg, &inst->ops[1], inst->size, inst->size == 8);
        return true;
    }
    case X86_OPC_ADD: return encode_arith_binop(buf, 0x01, 0, inst);
    case X86_OPC_OR:  return encode_arith_binop(buf, 0x09, 1, inst);
    case X86_OPC_AND: return encode_arith_binop(buf, 0x21, 4, inst);
    case X86_OPC_SUB: return encode_arith_binop(buf, 0x29, 5, inst);
    case X86_OPC_XOR: return encode_arith_binop(buf, 0x31, 6, inst);
    case X86_OPC_CMP: return encode_arith_binop(buf, 0x39, 7, inst);
    case X86_OPC_IMUL: {
        uint8_t sz = inst->size;
        if (inst->op_count == 2 && inst->ops[1].kind != X86_OP_IMM) {
            uint8_t opc[2] = { 0x0F, 0xAF };
            encode_modrm_op(buf, opc, 2, false, inst->ops[0].reg.phys_reg, &inst->ops[1], sz, sz == 8);
            return true;
        }
        if (inst->op_count == 3 || (inst->op_count == 2 && inst->ops[1].kind == X86_OP_IMM)) {
            const X86_Operand *rm_src = (inst->op_count == 3) ? &inst->ops[1] : &inst->ops[0];
            int64_t imm = (inst->op_count == 3) ? inst->ops[2].imm : inst->ops[1].imm;
            if (imm >= -128 && imm <= 127) {
                uint8_t opc = 0x6B;
                encode_modrm_op(buf, &opc, 1, false, inst->ops[0].reg.phys_reg, rm_src, sz, sz == 8);
                x86_buf_append_byte(buf, (uint8_t)(int8_t)imm);
                return true;
            }
            uint8_t opc = 0x69;
            encode_modrm_op(buf, &opc, 1, false, inst->ops[0].reg.phys_reg, rm_src, sz, sz == 8);
            x86_buf_append_i32(buf, (int32_t)imm);
            return true;
        }
        if (inst->op_count == 1) {
            uint8_t opc = (sz == 1) ? 0xF6 : 0xF7;
            encode_modrm_op(buf, &opc, 1, true, 5, &inst->ops[0], sz, sz == 8);
            return true;
        }
        break;
    }
    case X86_OPC_IDIV: {
        uint8_t sz = inst->size;
        uint8_t opc = (sz == 1) ? 0xF6 : 0xF7;
        encode_modrm_op(buf, &opc, 1, true, 7, &inst->ops[0], sz, sz == 8);
        return true;
    }
    case X86_OPC_NEG: {
        uint8_t sz = inst->size;
        uint8_t opc = (sz == 1) ? 0xF6 : 0xF7;
        encode_modrm_op(buf, &opc, 1, true, 3, &inst->ops[0], sz, sz == 8);
        return true;
    }
    case X86_OPC_NOT: {
        uint8_t sz = inst->size;
        uint8_t opc = (sz == 1) ? 0xF6 : 0xF7;
        encode_modrm_op(buf, &opc, 1, true, 2, &inst->ops[0], sz, sz == 8);
        return true;
    }
    case X86_OPC_SHL:
    case X86_OPC_SHR:
    case X86_OPC_SAR: {
        uint8_t ext = (inst->opcode == X86_OPC_SHL) ? 4 : (inst->opcode == X86_OPC_SHR) ? 5 : 7;
        uint8_t sz = inst->size;
        if (inst->op_count == 1 || (inst->ops[1].kind == X86_OP_IMM && inst->ops[1].imm == 1)) {
            uint8_t opc = (sz == 1) ? 0xD0 : 0xD1;
            encode_modrm_op(buf, &opc, 1, true, ext, &inst->ops[0], sz, sz == 8);
            return true;
        }
        if (inst->ops[1].kind == X86_OP_IMM) {
            uint8_t opc = (sz == 1) ? 0xC0 : 0xC1;
            encode_modrm_op(buf, &opc, 1, true, ext, &inst->ops[0], sz, sz == 8);
            x86_buf_append_byte(buf, (uint8_t)inst->ops[1].imm);
            return true;
        }
        if (inst->ops[1].kind == X86_OP_REG) {
            uint8_t opc = (sz == 1) ? 0xD2 : 0xD3;
            encode_modrm_op(buf, &opc, 1, true, ext, &inst->ops[0], sz, sz == 8);
            return true;
        }
        break;
    }
    case X86_OPC_TEST: {
        uint8_t sz = inst->size;
        if (inst->ops[1].kind == X86_OP_REG) {
            uint8_t opc = (sz == 1) ? 0x84 : 0x85;
            encode_modrm_op(buf, &opc, 1, false, inst->ops[1].reg.phys_reg, &inst->ops[0], sz, sz == 8);
            return true;
        }
        if (inst->ops[1].kind == X86_OP_IMM) {
            uint8_t opc = (sz == 1) ? 0xF6 : 0xF7;
            encode_modrm_op(buf, &opc, 1, true, 0, &inst->ops[0], sz, sz == 8);
            if (sz == 1) {
                x86_buf_append_byte(buf, (uint8_t)(int8_t)inst->ops[1].imm);
            } else {
                x86_buf_append_i32(buf, (int32_t)inst->ops[1].imm);
            }
            return true;
        }
        break;
    }
    case X86_OPC_SETCC: {
        uint8_t tttn = cond_to_tttn((X86_Cond)inst->cond);
        uint8_t opc[2] = { 0x0F, (uint8_t)(0x90 + tttn) };
        encode_modrm_op(buf, opc, 2, true, 0, &inst->ops[0], 1, false);
        return true;
    }
    case X86_OPC_JMP: {
        if (inst->ops[0].kind == X86_OP_LABEL) {
            x86_buf_append_byte(buf, 0xE9);
            x86_buf_append_i32(buf, 0);
            X86_Fixup fixup = {
                .kind = X86_FIXUP_BRANCH_REL32,
                .code_offset = buf->count - 4,
                .target_block = inst->ops[0].label,
                .symbol_name = (Ny_String){0},
                .addend = 0,
            };
            x86_buf_append_fixup(buf, fixup);
            return true;
        }
        if (inst->ops[0].kind == X86_OP_REG || inst->ops[0].kind == X86_OP_MEM) {
            uint8_t opc = 0xFF;
            encode_modrm_op(buf, &opc, 1, true, 4, &inst->ops[0], 8, false);
            return true;
        }
        break;
    }
    case X86_OPC_JCC: {
        uint8_t tttn = cond_to_tttn((X86_Cond)inst->cond);
        x86_buf_append_byte(buf, 0x0F);
        x86_buf_append_byte(buf, (uint8_t)(0x80 + tttn));
        x86_buf_append_i32(buf, 0);
        X86_Fixup fixup = {
            .kind = X86_FIXUP_BRANCH_REL32,
            .code_offset = buf->count - 4,
            .target_block = inst->ops[0].label,
            .symbol_name = (Ny_String){0},
            .addend = 0,
        };
        x86_buf_append_fixup(buf, fixup);
        return true;
    }
    case X86_OPC_CALL: {
        if (inst->ops[0].kind == X86_OP_GLOBAL || inst->ops[0].kind == X86_OP_LABEL) {
            x86_buf_append_byte(buf, 0xE8);
            x86_buf_append_i32(buf, 0);
            X86_Fixup fixup = {
                .kind = X86_FIXUP_CALL_REL32,
                .code_offset = buf->count - 4,
                .target_block = (inst->ops[0].kind == X86_OP_LABEL) ? inst->ops[0].label : NY_INVALID_BLOCK,
                .symbol_name = (inst->ops[0].kind == X86_OP_GLOBAL) ? inst->ops[0].global_name : (Ny_String){0},
                .addend = 0,
            };
            x86_buf_append_fixup(buf, fixup);
            return true;
        }
        if (inst->ops[0].kind == X86_OP_REG || inst->ops[0].kind == X86_OP_MEM) {
            uint8_t opc = 0xFF;
            encode_modrm_op(buf, &opc, 1, true, 2, &inst->ops[0], 8, false);
            return true;
        }
        break;
    }
    case X86_OPC_RET: {
        if (inst->op_count == 0) {
            x86_buf_append_byte(buf, 0xC3);
            return true;
        }
        x86_buf_append_byte(buf, 0xC2);
        x86_buf_append_byte(buf, (uint8_t)(inst->ops[0].imm & 0xFF));
        x86_buf_append_byte(buf, (uint8_t)((inst->ops[0].imm >> 8) & 0xFF));
        return true;
    }
    case X86_OPC_PUSH: {
        if (inst->ops[0].kind == X86_OP_REG) {
            uint8_t r = inst->ops[0].reg.phys_reg;
            if (r >= 8) {
                x86_buf_append_byte(buf, 0x41);
            }
            x86_buf_append_byte(buf, (uint8_t)(0x50 + (r & 7)));
            return true;
        }
        if (inst->ops[0].kind == X86_OP_IMM) {
            int64_t imm = inst->ops[0].imm;
            if (imm >= -128 && imm <= 127) {
                x86_buf_append_byte(buf, 0x6A);
                x86_buf_append_byte(buf, (uint8_t)(int8_t)imm);
                return true;
            }
            x86_buf_append_byte(buf, 0x68);
            x86_buf_append_i32(buf, (int32_t)imm);
            return true;
        }
        if (inst->ops[0].kind == X86_OP_MEM) {
            uint8_t opc = 0xFF;
            encode_modrm_op(buf, &opc, 1, true, 6, &inst->ops[0], 8, false);
            return true;
        }
        break;
    }
    case X86_OPC_POP: {
        if (inst->ops[0].kind == X86_OP_REG) {
            uint8_t r = inst->ops[0].reg.phys_reg;
            if (r >= 8) {
                x86_buf_append_byte(buf, 0x41);
            }
            x86_buf_append_byte(buf, (uint8_t)(0x58 + (r & 7)));
            return true;
        }
        if (inst->ops[0].kind == X86_OP_MEM) {
            uint8_t opc = 0x8F;
            encode_modrm_op(buf, &opc, 1, true, 0, &inst->ops[0], 8, false);
            return true;
        }
        break;
    }
    case X86_OPC_CDQ: {
        x86_buf_append_byte(buf, 0x99);
        return true;
    }
    case X86_OPC_CQO: {
        x86_buf_append_byte(buf, 0x48);
        x86_buf_append_byte(buf, 0x99);
        return true;
    }
    case X86_OPC_UD2: {
        x86_buf_append_byte(buf, 0x0F);
        x86_buf_append_byte(buf, 0x0B);
        return true;
    }
    case X86_OPC_MOVSS:
    case X86_OPC_MOVSD: {
        const X86_Operand *dst = &inst->ops[0];
        const X86_Operand *src = &inst->ops[1];
        uint8_t prefix = (inst->opcode == X86_OPC_MOVSS) ? 0xF3 : 0xF2;
        x86_buf_append_byte(buf, prefix);

        if (dst->kind == X86_OP_REG) {
            uint8_t opc[2] = { 0x0F, 0x10 };
            encode_modrm_op(buf, opc, 2, false, dst->reg.phys_reg, src, inst->size, false);
            return true;
        }
        if (dst->kind == X86_OP_MEM && src->kind == X86_OP_REG) {
            uint8_t opc[2] = { 0x0F, 0x11 };
            encode_modrm_op(buf, opc, 2, false, src->reg.phys_reg, dst, inst->size, false);
            return true;
        }
        break;
    }
    case X86_OPC_ADDSS:
    case X86_OPC_ADDSD:
    case X86_OPC_SUBSS:
    case X86_OPC_SUBSD:
    case X86_OPC_MULSS:
    case X86_OPC_MULSD:
    case X86_OPC_DIVSS:
    case X86_OPC_DIVSD: {
        const X86_Operand *dst = &inst->ops[0];
        const X86_Operand *src = &inst->ops[1];
        bool is_single = (inst->opcode == X86_OPC_ADDSS || inst->opcode == X86_OPC_SUBSS ||
                          inst->opcode == X86_OPC_MULSS || inst->opcode == X86_OPC_DIVSS);
        uint8_t prefix = is_single ? 0xF3 : 0xF2;
        x86_buf_append_byte(buf, prefix);

        uint8_t byte2 = 0x58;
        if (inst->opcode == X86_OPC_ADDSS || inst->opcode == X86_OPC_ADDSD) byte2 = 0x58;
        else if (inst->opcode == X86_OPC_SUBSS || inst->opcode == X86_OPC_SUBSD) byte2 = 0x5C;
        else if (inst->opcode == X86_OPC_MULSS || inst->opcode == X86_OPC_MULSD) byte2 = 0x59;
        else if (inst->opcode == X86_OPC_DIVSS || inst->opcode == X86_OPC_DIVSD) byte2 = 0x5E;

        uint8_t opc[2] = { 0x0F, byte2 };
        encode_modrm_op(buf, opc, 2, false, dst->reg.phys_reg, src, inst->size, false);
        return true;
    }
    case X86_OPC_CVTSI2SS:
    case X86_OPC_CVTSI2SD: {
        const X86_Operand *dst = &inst->ops[0];
        const X86_Operand *src = &inst->ops[1];
        uint8_t prefix = (inst->opcode == X86_OPC_CVTSI2SS) ? 0xF3 : 0xF2;
        x86_buf_append_byte(buf, prefix);
        uint8_t opc[2] = { 0x0F, 0x2A };
        bool rex_w = (src->kind == X86_OP_REG && src->reg.size == 8);
        encode_modrm_op(buf, opc, 2, false, dst->reg.phys_reg, src, inst->size, rex_w);
        return true;
    }
    case X86_OPC_CVTTSS2SI:
    case X86_OPC_CVTTSD2SI: {
        const X86_Operand *dst = &inst->ops[0];
        const X86_Operand *src = &inst->ops[1];
        uint8_t prefix = (inst->opcode == X86_OPC_CVTTSS2SI) ? 0xF3 : 0xF2;
        x86_buf_append_byte(buf, prefix);
        uint8_t opc[2] = { 0x0F, 0x2C };
        bool rex_w = (dst->kind == X86_OP_REG && dst->reg.size == 8);
        encode_modrm_op(buf, opc, 2, false, dst->reg.phys_reg, src, inst->size, rex_w);
        return true;
    }
    case X86_OPC_CVTSS2SD: {
        const X86_Operand *dst = &inst->ops[0];
        const X86_Operand *src = &inst->ops[1];
        x86_buf_append_byte(buf, 0xF3);
        uint8_t opc[2] = { 0x0F, 0x5A };
        encode_modrm_op(buf, opc, 2, false, dst->reg.phys_reg, src, inst->size, false);
        return true;
    }
    case X86_OPC_CVTSD2SS: {
        const X86_Operand *dst = &inst->ops[0];
        const X86_Operand *src = &inst->ops[1];
        x86_buf_append_byte(buf, 0xF2);
        uint8_t opc[2] = { 0x0F, 0x5A };
        encode_modrm_op(buf, opc, 2, false, dst->reg.phys_reg, src, inst->size, false);
        return true;
    }
    case X86_OPC_UCOMISS:
    case X86_OPC_UCOMISD: {
        const X86_Operand *dst = &inst->ops[0];
        const X86_Operand *src = &inst->ops[1];
        if (inst->opcode == X86_OPC_UCOMISD) {
            x86_buf_append_byte(buf, 0x66);
        }
        uint8_t opc[2] = { 0x0F, 0x2E };
        encode_modrm_op(buf, opc, 2, false, dst->reg.phys_reg, src, inst->size, false);
        return true;
    }
    default:
        break;
    }

    if (diags) {
        ny_diagnostic_list_append(diags, "unsupported operand combination for x86 instruction");
    }
    return false;
}

bool x86_encode_function(X86_Code_Buffer *buf, const X86_Function *fn, Ny_Diagnostic_List *diags) {
    if (!x86_validate_function(fn, diags)) {
        return false;
    }

    size_t fixup_start_idx = buf->fixup_count;

    uint32_t max_id = 0;
    for (size_t b = 0; b < fn->block_count; b++) {
        if (fn->blocks[b].id > max_id) {
            max_id = fn->blocks[b].id;
        }
    }

    size_t offsets_size = (max_id + 1) * sizeof(size_t);
    size_t *block_offsets = (size_t *)ny_alloc_zero(offsets_size);

    for (size_t b = 0; b < fn->block_count; b++) {
        const X86_Block *blk = &fn->blocks[b];
        block_offsets[blk->id] = buf->count;

        for (size_t i = 0; i < blk->inst_count; i++) {
            if (!x86_encode_instruction(buf, &blk->instructions[i], diags)) {
                ny_free(block_offsets, offsets_size);
                return false;
            }
        }
    }

    for (size_t f = fixup_start_idx; f < buf->fixup_count; f++) {
        X86_Fixup *fixup = &buf->fixups[f];
        if (fixup->kind == X86_FIXUP_BRANCH_REL32) {
            if (fixup->target_block <= max_id) {
                size_t target_offset = block_offsets[fixup->target_block];
                size_t next_inst_offset = fixup->code_offset + 4;
                int32_t disp = (int32_t)((int64_t)target_offset - (int64_t)next_inst_offset + fixup->addend);

                buf->bytes[fixup->code_offset + 0] = (uint8_t)(disp & 0xFF);
                buf->bytes[fixup->code_offset + 1] = (uint8_t)((disp >> 8) & 0xFF);
                buf->bytes[fixup->code_offset + 2] = (uint8_t)((disp >> 16) & 0xFF);
                buf->bytes[fixup->code_offset + 3] = (uint8_t)((disp >> 24) & 0xFF);
            }
        }
    }

    ny_free(block_offsets, offsets_size);
    return true;
}

bool x86_encode_module(X86_Encoded_Module *out_mod, const X86_Module *mod, Ny_Diagnostic_List *diags) {
    x86_encoded_mod_init(out_mod, mod->name);

    if (mod->function_count > 0) {
        out_mod->function_capacity = mod->function_count;
        out_mod->functions = (X86_Function_Code *)ny_alloc_zero(out_mod->function_capacity * sizeof(X86_Function_Code));
    }

    for (size_t i = 0; i < mod->function_count; i++) {
        const X86_Function *fn = &mod->functions[i];
        size_t fn_start = out_mod->text_section.count;

        if (!x86_encode_function(&out_mod->text_section, fn, diags)) {
            return false;
        }

        size_t fn_end = out_mod->text_section.count;
        out_mod->functions[out_mod->function_count++] = (X86_Function_Code){
            .name = fn->name,
            .offset = fn_start,
            .size = fn_end - fn_start,
        };
    }

    for (size_t f = 0; f < out_mod->text_section.fixup_count; f++) {
        X86_Fixup *fixup = &out_mod->text_section.fixups[f];
        if (fixup->kind == X86_FIXUP_CALL_REL32 || fixup->kind == X86_FIXUP_GLOBAL_REL32) {
            bool resolved = false;
            for (size_t i = 0; i < out_mod->function_count; i++) {
                if (ny_str_eq(out_mod->functions[i].name, fixup->symbol_name)) {
                    size_t target_offset = out_mod->functions[i].offset;
                    size_t next_inst_offset = fixup->code_offset + 4;
                    int32_t disp = (int32_t)((int64_t)target_offset - (int64_t)next_inst_offset + fixup->addend);

                    out_mod->text_section.bytes[fixup->code_offset + 0] = (uint8_t)(disp & 0xFF);
                    out_mod->text_section.bytes[fixup->code_offset + 1] = (uint8_t)((disp >> 8) & 0xFF);
                    out_mod->text_section.bytes[fixup->code_offset + 2] = (uint8_t)((disp >> 16) & 0xFF);
                    out_mod->text_section.bytes[fixup->code_offset + 3] = (uint8_t)((disp >> 24) & 0xFF);
                    resolved = true;
                    break;
                }
            }
            if (!resolved) {
                X86_Relocation reloc = {
                    .kind = fixup->kind,
                    .code_offset = fixup->code_offset,
                    .symbol_name = fixup->symbol_name,
                    .addend = fixup->addend,
                };
                x86_buf_append_reloc(&out_mod->text_section, reloc);
            }
        }
    }

    /* Encode Globals */
    out_mod->global_capacity = mod->global_count;
    if (out_mod->global_capacity > 0) {
        out_mod->globals = (X86_Encoded_Global *)ny_alloc_zero(out_mod->global_capacity * sizeof(X86_Encoded_Global));
    }

    for (size_t g = 0; g < mod->global_count; g++) {
        const Ny_Machine_Global *mg = &mod->globals[g];
        X86_Encoded_Global *eg = &out_mod->globals[out_mod->global_count++];
        eg->name = mg->name;
        eg->kind = mg->kind;
        eg->align = mg->align > 0 ? mg->align : 1;
        eg->size = mg->data_size;

        if (mg->kind == NY_GLOBAL_CONST) {
            while ((out_mod->rodata_section.count % eg->align) != 0) {
                x86_buf_append_byte(&out_mod->rodata_section, 0);
            }
            eg->offset = out_mod->rodata_section.count;
            if (mg->data && mg->data_size > 0) {
                x86_buf_append_bytes(&out_mod->rodata_section, mg->data, mg->data_size);
            }
        } else if (mg->kind == NY_GLOBAL_DATA) {
            while ((out_mod->data_section.count % eg->align) != 0) {
                x86_buf_append_byte(&out_mod->data_section, 0);
            }
            eg->offset = out_mod->data_section.count;
            if (mg->data && mg->data_size > 0) {
                x86_buf_append_bytes(&out_mod->data_section, mg->data, mg->data_size);
            }
        } else if (mg->kind == NY_GLOBAL_BSS) {
            if (eg->align > out_mod->bss_align) {
                out_mod->bss_align = eg->align;
            }
            while ((out_mod->bss_size % eg->align) != 0) {
                out_mod->bss_size++;
            }
            eg->offset = out_mod->bss_size;
            out_mod->bss_size += eg->size;
        }
    }

    return true;
}
