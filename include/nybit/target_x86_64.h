// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_X86_64_H
#define NYBIT_TARGET_X86_64_H

#include "nybit/support.h"
#include "nybit/machine.h"
#include "nybit/target.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum X86_Phys_Reg {
    X86_RAX = 0,
    X86_RCX = 1,
    X86_RDX = 2,
    X86_RBX = 3,
    X86_RSP = 4,
    X86_RBP = 5,
    X86_RSI = 6,
    X86_RDI = 7,
    X86_R8  = 8,
    X86_R9  = 9,
    X86_R10 = 10,
    X86_R11 = 11,
    X86_R12 = 12,
    X86_R13 = 13,
    X86_R14 = 14,
    X86_R15 = 15,
    X86_GPR_COUNT = 16,
    X86_NO_REG = 255,
} X86_Phys_Reg;

typedef struct X86_Reg {
    uint32_t id;
    uint8_t size;
    bool is_virtual;
    uint8_t phys_reg;
} X86_Reg;

static inline X86_Reg x86_reg_phys(X86_Phys_Reg phys, uint8_t size) {
    return (X86_Reg){ .id = (uint32_t)phys, .size = size, .is_virtual = false, .phys_reg = (uint8_t)phys };
}

static inline X86_Reg x86_reg_virt(uint32_t vreg, uint8_t size) {
    return (X86_Reg){ .id = vreg, .size = size, .is_virtual = true, .phys_reg = 0 };
}

static inline bool x86_reg_is_valid(X86_Reg r) {
    return r.size > 0 && (r.is_virtual || r.phys_reg != X86_NO_REG);
}

const char *x86_reg_name(X86_Reg reg);

typedef struct X86_Mem {
    X86_Reg base;
    X86_Reg index;
    uint8_t scale;
    int32_t disp;
    bool is_rip_relative;
    Ny_String symbol;
} X86_Mem;

typedef enum X86_Cond {
    X86_COND_NONE = 0,
    X86_COND_E,
    X86_COND_NE,
    X86_COND_L,
    X86_COND_LE,
    X86_COND_G,
    X86_COND_GE,
    X86_COND_B,
    X86_COND_BE,
    X86_COND_A,
    X86_COND_AE,
} X86_Cond;

const char *x86_cond_suffix(X86_Cond cond);

typedef enum X86_Op_Kind {
    X86_OP_NONE = 0,
    X86_OP_REG,
    X86_OP_IMM,
    X86_OP_MEM,
    X86_OP_LABEL,
    X86_OP_GLOBAL,
} X86_Op_Kind;

typedef struct X86_Operand {
    X86_Op_Kind kind;
    union {
        X86_Reg reg;
        int64_t imm;
        X86_Mem mem;
        Ny_Block_ID label;
        Ny_String global_name;
    };
} X86_Operand;

static inline X86_Operand x86_op_reg(X86_Reg reg) {
    X86_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = X86_OP_REG;
    op.reg = reg;
    return op;
}

static inline X86_Operand x86_op_imm(int64_t imm) {
    X86_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = X86_OP_IMM;
    op.imm = imm;
    return op;
}

static inline X86_Operand x86_op_mem(X86_Mem mem) {
    X86_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = X86_OP_MEM;
    op.mem = mem;
    return op;
}

static inline X86_Operand x86_op_label(Ny_Block_ID label) {
    X86_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = X86_OP_LABEL;
    op.label = label;
    return op;
}

static inline X86_Operand x86_op_global(Ny_String name) {
    X86_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = X86_OP_GLOBAL;
    op.global_name = name;
    return op;
}

typedef enum X86_Opcode {
    X86_OPC_NONE = 0,
    X86_OPC_MOV,
    X86_OPC_LEA,
    X86_OPC_ADD,
    X86_OPC_SUB,
    X86_OPC_IMUL,
    X86_OPC_IDIV,
    X86_OPC_NEG,
    X86_OPC_AND,
    X86_OPC_OR,
    X86_OPC_XOR,
    X86_OPC_NOT,
    X86_OPC_SHL,
    X86_OPC_SHR,
    X86_OPC_SAR,
    X86_OPC_CMP,
    X86_OPC_TEST,
    X86_OPC_JMP,
    X86_OPC_JCC,
    X86_OPC_SETCC,
    X86_OPC_PUSH,
    X86_OPC_POP,
    X86_OPC_CALL,
    X86_OPC_RET,
    X86_OPC_CDQ,
    X86_OPC_CQO,
    X86_OPC_UD2,
    X86_OPC_COUNT,
} X86_Opcode;

const char *x86_opcode_mnemonic(X86_Opcode opc);

typedef struct X86_Instruction {
    uint16_t opcode;
    uint8_t size;
    uint8_t cond;
    uint8_t op_count;
    X86_Operand ops[3];
} X86_Instruction;

typedef struct X86_Stack_Frame {
    uint32_t stack_size;
    int32_t *slot_offsets;
    size_t slot_count;
    size_t slot_capacity;
    bool has_call;
    uint16_t callee_saved_mask;
} X86_Stack_Frame;

typedef struct X86_Block {
    Ny_Block_ID id;
    Ny_String name;
    X86_Instruction *instructions;
    size_t inst_count;
    size_t inst_capacity;
} X86_Block;

typedef struct X86_Function {
    Ny_String name;
    Ny_Target_ABI abi;
    X86_Block *blocks;
    size_t block_count;
    size_t block_capacity;
    X86_Stack_Frame frame;
} X86_Function;

typedef struct X86_Module {
    Ny_String name;
    Ny_Target_ABI abi;
    X86_Function *functions;
    size_t function_count;
    size_t function_capacity;
    Ny_Machine_Global *globals;
    size_t global_count;
    size_t global_capacity;
} X86_Module;

void x86_func_init(X86_Function *fn, Ny_String name, Ny_Target_ABI abi);
void x86_func_destroy(X86_Function *fn);

void x86_block_append_inst(X86_Block *blk, X86_Instruction inst);

void x86_mod_init(X86_Module *mod, Ny_String name, Ny_Target_ABI abi);
void x86_mod_destroy(X86_Module *mod);

/* ABI queries */
const X86_Phys_Reg *x86_abi_arg_regs(Ny_Target_ABI abi, size_t *out_count);
X86_Phys_Reg x86_abi_ret_reg(Ny_Target_ABI abi, uint8_t size);
bool x86_abi_is_callee_saved(Ny_Target_ABI abi, X86_Phys_Reg reg);

/* Lowering */
bool x86_lower_machine_func(const Ny_Target *target, const Ny_Machine_Function *mfn, void **out_target_fn, Ny_Diagnostic_List *diags);
bool x86_lower_machine_mod(const Ny_Target *target, const Ny_Machine_Module *mmod, X86_Module *out_x86_mod, Ny_Diagnostic_List *diags);

/* Dumper */
char *x86_dump_func(const void *target_fn, Ny_Arena *scratch);
char *x86_dump_mod(const X86_Module *mod, Ny_Arena *scratch);

#include "nybit/x86_encode.h"

#ifdef __cplusplus
}
#endif

#endif
