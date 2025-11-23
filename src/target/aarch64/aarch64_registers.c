// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "aarch64_internal.h"
#include <stdio.h>
#include <string.h>

static const char *s_reg_names_64[31] = {
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
    "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
    "x24", "x25", "x26", "x27", "x28", "fp", "lr"
};

static const char *s_reg_names_32[31] = {
    "w0", "w1", "w2", "w3", "w4", "w5", "w6", "w7",
    "w8", "w9", "w10", "w11", "w12", "w13", "w14", "w15",
    "w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
    "w24", "w25", "w26", "w27", "w28", "w29", "w30"
};

static const char *s_reg_names_v[32] = {
    "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
    "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
    "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
    "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
};

const char *aarch64_reg_name(AArch64_Reg reg) {
    if (reg.is_virtual) {
        static _Thread_local char vbufs[8][32];
        static _Thread_local size_t vidx = 0;
        char *buf = vbufs[vidx++ & 7];
        snprintf(buf, 32, "%%v%u", reg.id);
        return buf;
    }

    if (reg.phys_reg == AARCH64_SP) {
        return (reg.size == 4) ? "wsp" : "sp";
    }

    if (reg.phys_reg >= AARCH64_V0) {
        return s_reg_names_v[reg.phys_reg - AARCH64_V0];
    }

    if (reg.phys_reg >= AARCH64_GPR_COUNT) {
        return "<invalid_reg>";
    }

    return (reg.size == 4) ? s_reg_names_32[reg.phys_reg] : s_reg_names_64[reg.phys_reg];
}

const char *aarch64_cond_name(AArch64_Cond cond) {
    switch (cond) {
    case AARCH64_COND_EQ:   return "eq";
    case AARCH64_COND_NE:   return "ne";
    case AARCH64_COND_LT_S: return "lt";
    case AARCH64_COND_LE_S: return "le";
    case AARCH64_COND_GT_S: return "gt";
    case AARCH64_COND_GE_S: return "ge";
    case AARCH64_COND_LT_U: return "lo";
    case AARCH64_COND_LE_U: return "ls";
    case AARCH64_COND_GT_U: return "hi";
    case AARCH64_COND_GE_U: return "hs";
    case AARCH64_COND_NONE:
    default:
        return "";
    }
}

const char *aarch64_opcode_mnemonic(AArch64_Opcode opc) {
    switch (opc) {
    case AARCH64_OPC_MOV:    return "mov";
    case AARCH64_OPC_MOVZ:   return "movz";
    case AARCH64_OPC_MOVK:   return "movk";
    case AARCH64_OPC_ADD:    return "add";
    case AARCH64_OPC_SUB:    return "sub";
    case AARCH64_OPC_SUBS:   return "subs";
    case AARCH64_OPC_MUL:    return "mul";
    case AARCH64_OPC_SDIV:   return "sdiv";
    case AARCH64_OPC_UDIV:   return "udiv";
    case AARCH64_OPC_MSUB:   return "msub";
    case AARCH64_OPC_AND:    return "and";
    case AARCH64_OPC_ORR:    return "orr";
    case AARCH64_OPC_EOR:    return "eor";
    case AARCH64_OPC_MVN:    return "mvn";
    case AARCH64_OPC_LSL:    return "lsl";
    case AARCH64_OPC_LSR:    return "lsr";
    case AARCH64_OPC_ASR:    return "asr";
    case AARCH64_OPC_CMP:    return "cmp";
    case AARCH64_OPC_CSET:   return "cset";
    case AARCH64_OPC_ADRP:   return "adrp";
    case AARCH64_OPC_ADD_LO12: return "add";
    case AARCH64_OPC_LDR:    return "ldr";
    case AARCH64_OPC_STR:    return "str";
    case AARCH64_OPC_LDR_SYM: return "ldr";
    case AARCH64_OPC_STP:    return "stp";
    case AARCH64_OPC_LDP:    return "ldp";
    case AARCH64_OPC_B:      return "b";
    case AARCH64_OPC_BL:     return "bl";
    case AARCH64_OPC_BCOND:  return "b.";
    case AARCH64_OPC_CBZ:    return "cbz";
    case AARCH64_OPC_CBNZ:   return "cbnz";
    case AARCH64_OPC_BR:     return "br";
    case AARCH64_OPC_BLR:    return "blr";
    case AARCH64_OPC_RET:    return "ret";
    case AARCH64_OPC_BRK:    return "brk";
    case AARCH64_OPC_NONE:
    default:
        return "unknown";
    }
}

static const AArch64_Phys_Reg s_aapcs64_arg_regs[] = {
    AARCH64_X0, AARCH64_X1, AARCH64_X2, AARCH64_X3,
    AARCH64_X4, AARCH64_X5, AARCH64_X6, AARCH64_X7
};

static const AArch64_Phys_Reg s_aapcs64_fp_arg_regs[] = {
    AARCH64_V0, AARCH64_V1, AARCH64_V2, AARCH64_V3,
    AARCH64_V4, AARCH64_V5, AARCH64_V6, AARCH64_V7
};

const AArch64_Phys_Reg *aarch64_abi_arg_regs(Ny_Target_ABI abi, size_t *out_count) {
    (void)abi;
    *out_count = sizeof(s_aapcs64_arg_regs) / sizeof(s_aapcs64_arg_regs[0]);
    return s_aapcs64_arg_regs;
}

const AArch64_Phys_Reg *aarch64_abi_fp_arg_regs(Ny_Target_ABI abi, size_t *out_count) {
    (void)abi;
    *out_count = sizeof(s_aapcs64_fp_arg_regs) / sizeof(s_aapcs64_fp_arg_regs[0]);
    return s_aapcs64_fp_arg_regs;
}

AArch64_Phys_Reg aarch64_abi_ret_reg(Ny_Target_ABI abi, uint8_t size, bool is_fp) {
    (void)abi;
    (void)size;
    if (is_fp) {
        return AARCH64_V0;
    }
    return AARCH64_X0;
}

bool aarch64_abi_is_callee_saved(Ny_Target_ABI abi, AArch64_Phys_Reg reg) {
    (void)abi;
    if (reg >= AARCH64_X19 && reg <= AARCH64_X28) {
        return true;
    }
    if (reg == AARCH64_FP || reg == AARCH64_LR) {
        return true;
    }
    if (reg >= AARCH64_V8 && reg <= AARCH64_V15) {
        return true;
    }
    return false;
}

void aarch64_func_init(AArch64_Function *fn, Ny_String name, Ny_Target_ABI abi) {
    memset(fn, 0, sizeof(*fn));
    fn->name = name;
    fn->abi = abi;
}

void aarch64_func_destroy(AArch64_Function *fn) {
    if (!fn) return;
    for (size_t i = 0; i < fn->block_count; i++) {
        AArch64_Block *blk = &fn->blocks[i];
        if (blk->instructions) {
            ny_free(blk->instructions, blk->inst_capacity * sizeof(AArch64_Instruction));
            blk->instructions = nullptr;
        }
    }
    if (fn->blocks) {
        ny_free(fn->blocks, fn->block_capacity * sizeof(AArch64_Block));
        fn->blocks = nullptr;
    }
    if (fn->frame.slot_offsets) {
        ny_free(fn->frame.slot_offsets, fn->frame.slot_capacity * sizeof(int32_t));
        fn->frame.slot_offsets = nullptr;
    }
    memset(fn, 0, sizeof(*fn));
}

void aarch64_block_append_inst(AArch64_Block *blk, AArch64_Instruction inst) {
    ny_buf_grow((void **)&blk->instructions, &blk->inst_capacity, blk->inst_count, sizeof(AArch64_Instruction));
    blk->instructions[blk->inst_count++] = inst;
}

void aarch64_mod_init(AArch64_Module *mod, Ny_String name, Ny_Target_ABI abi) {
    memset(mod, 0, sizeof(*mod));
    mod->name = name;
    mod->abi = abi;
}

void aarch64_mod_destroy(AArch64_Module *mod) {
    if (!mod) return;
    for (size_t i = 0; i < mod->function_count; i++) {
        aarch64_func_destroy(&mod->functions[i]);
    }
    if (mod->functions) {
        ny_free(mod->functions, mod->function_capacity * sizeof(AArch64_Function));
        mod->functions = nullptr;
    }
    for (size_t i = 0; i < mod->global_count; i++) {
        if (mod->globals[i].data) {
            ny_free(mod->globals[i].data, mod->globals[i].data_size);
        }
    }
    if (mod->globals) {
        ny_free(mod->globals, mod->global_capacity * sizeof(Ny_Machine_Global));
        mod->globals = nullptr;
    }
    memset(mod, 0, sizeof(*mod));
}
