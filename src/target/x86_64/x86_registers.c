// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/target_x86_64.h"
#include <stdio.h>
#include <string.h>

static const char *s_reg_names_64[X86_GPR_COUNT] = {
    "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"
};

static const char *s_reg_names_32[X86_GPR_COUNT] = {
    "eax",  "ecx",  "edx",  "ebx",  "esp",  "ebp",  "esi",  "edi",
    "r8d",  "r9d",  "r10d", "r11d", "r12d", "r13d", "r14d", "r15d"
};

static const char *s_reg_names_16[X86_GPR_COUNT] = {
    "ax",   "cx",   "dx",   "bx",   "sp",   "bp",   "si",   "di",
    "r8w",  "r9w",  "r10w", "r11w", "r12w", "r13w", "r14w", "r15w"
};

static const char *s_reg_names_8[X86_GPR_COUNT] = {
    "al",   "cl",   "dl",   "bl",   "spl",  "bpl",  "sil",  "dil",
    "r8b",  "r9b",  "r10b", "r11b", "r12b", "r13b", "r14b", "r15b"
};

const char *x86_reg_name(X86_Reg reg) {
    if (reg.is_virtual) {
        static _Thread_local char vbufs[8][32];
        static _Thread_local size_t vidx = 0;
        char *buf = vbufs[vidx++ & 7];
        snprintf(buf, 32, "%%v%u", reg.id);
        return buf;
    }

    if (reg.phys_reg >= X86_GPR_COUNT) {
        return "<invalid_reg>";
    }

    switch (reg.size) {
    case 1:  return s_reg_names_8[reg.phys_reg];
    case 2:  return s_reg_names_16[reg.phys_reg];
    case 4:  return s_reg_names_32[reg.phys_reg];
    case 8:  return s_reg_names_64[reg.phys_reg];
    default: return s_reg_names_64[reg.phys_reg];
    }
}

const char *x86_cond_suffix(X86_Cond cond) {
    switch (cond) {
    case X86_COND_E:  return "e";
    case X86_COND_NE: return "ne";
    case X86_COND_L:  return "l";
    case X86_COND_LE: return "le";
    case X86_COND_G:  return "g";
    case X86_COND_GE: return "ge";
    case X86_COND_B:  return "b";
    case X86_COND_BE: return "be";
    case X86_COND_A:  return "a";
    case X86_COND_AE: return "ae";
    case X86_COND_NONE:
    default:
        return "";
    }
}

const char *x86_opcode_mnemonic(X86_Opcode opc) {
    switch (opc) {
    case X86_OPC_MOV:   return "mov";
    case X86_OPC_LEA:   return "lea";
    case X86_OPC_ADD:   return "add";
    case X86_OPC_SUB:   return "sub";
    case X86_OPC_IMUL:  return "imul";
    case X86_OPC_IDIV:  return "idiv";
    case X86_OPC_NEG:   return "neg";
    case X86_OPC_AND:   return "and";
    case X86_OPC_OR:    return "or";
    case X86_OPC_XOR:   return "xor";
    case X86_OPC_NOT:   return "not";
    case X86_OPC_SHL:   return "shl";
    case X86_OPC_SHR:   return "shr";
    case X86_OPC_SAR:   return "sar";
    case X86_OPC_CMP:   return "cmp";
    case X86_OPC_TEST:  return "test";
    case X86_OPC_JMP:   return "jmp";
    case X86_OPC_JCC:   return "j";
    case X86_OPC_SETCC: return "set";
    case X86_OPC_PUSH:  return "push";
    case X86_OPC_POP:   return "pop";
    case X86_OPC_CALL:  return "call";
    case X86_OPC_RET:   return "ret";
    case X86_OPC_CDQ:   return "cdq";
    case X86_OPC_CQO:   return "cqo";
    case X86_OPC_UD2:   return "ud2";
    case X86_OPC_NONE:
    default:
        return "unknown";
    }
}

static const X86_Phys_Reg s_sysv_arg_regs[] = {
    X86_RDI, X86_RSI, X86_RDX, X86_RCX, X86_R8, X86_R9
};

static const X86_Phys_Reg s_win64_arg_regs[] = {
    X86_RCX, X86_RDX, X86_R8, X86_R9
};

const X86_Phys_Reg *x86_abi_arg_regs(Ny_Target_ABI abi, size_t *out_count) {
    if (abi == NY_ABI_WINDOWS_X64) {
        *out_count = sizeof(s_win64_arg_regs) / sizeof(s_win64_arg_regs[0]);
        return s_win64_arg_regs;
    }
    *out_count = sizeof(s_sysv_arg_regs) / sizeof(s_sysv_arg_regs[0]);
    return s_sysv_arg_regs;
}

X86_Phys_Reg x86_abi_ret_reg(Ny_Target_ABI abi, uint8_t size) {
    (void)abi;
    (void)size;
    return X86_RAX;
}

bool x86_abi_is_callee_saved(Ny_Target_ABI abi, X86_Phys_Reg reg) {
    if (abi == NY_ABI_WINDOWS_X64) {
        return reg == X86_RBX || reg == X86_RBP || reg == X86_RDI ||
               reg == X86_RSI || reg == X86_RSP ||
               (reg >= X86_R12 && reg <= X86_R15);
    }
    return reg == X86_RBX || reg == X86_RSP || reg == X86_RBP ||
           (reg >= X86_R12 && reg <= X86_R15);
}

void x86_func_init(X86_Function *fn, Ny_String name, Ny_Target_ABI abi) {
    memset(fn, 0, sizeof(*fn));
    fn->name = name;
    fn->abi = abi;
}

void x86_func_destroy(X86_Function *fn) {
    if (!fn) return;
    for (size_t i = 0; i < fn->block_count; i++) {
        X86_Block *blk = &fn->blocks[i];
        if (blk->instructions) {
            ny_free(blk->instructions, blk->inst_capacity * sizeof(X86_Instruction));
            blk->instructions = NULL;
        }
    }
    if (fn->blocks) {
        ny_free(fn->blocks, fn->block_capacity * sizeof(X86_Block));
        fn->blocks = NULL;
    }
    if (fn->frame.slot_offsets) {
        ny_free(fn->frame.slot_offsets, fn->frame.slot_capacity * sizeof(int32_t));
        fn->frame.slot_offsets = NULL;
    }
    memset(fn, 0, sizeof(*fn));
}

void x86_block_append_inst(X86_Block *blk, X86_Instruction inst) {
    ny_buf_grow((void **)&blk->instructions, &blk->inst_capacity, blk->inst_count, sizeof(X86_Instruction));
    blk->instructions[blk->inst_count++] = inst;
}

void x86_mod_init(X86_Module *mod, Ny_String name, Ny_Target_ABI abi) {
    memset(mod, 0, sizeof(*mod));
    mod->name = name;
    mod->abi = abi;
}

void x86_mod_destroy(X86_Module *mod) {
    if (!mod) return;
    for (size_t i = 0; i < mod->function_count; i++) {
        x86_func_destroy(&mod->functions[i]);
    }
    if (mod->functions) {
        ny_free(mod->functions, mod->function_capacity * sizeof(X86_Function));
        mod->functions = NULL;
    }
    for (size_t i = 0; i < mod->global_count; i++) {
        if (mod->globals[i].data) {
            ny_free(mod->globals[i].data, mod->globals[i].data_size);
        }
    }
    if (mod->globals) {
        ny_free(mod->globals, mod->global_capacity * sizeof(Ny_Machine_Global));
        mod->globals = NULL;
    }
    memset(mod, 0, sizeof(*mod));
}
