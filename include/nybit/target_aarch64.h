// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_AARCH64_H
#define NYBIT_TARGET_AARCH64_H

#include "nybit/support.h"
#include "nybit/machine.h"
#include "nybit/target.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AArch64_Phys_Reg {
    AARCH64_X0  = 0,
    AARCH64_X1  = 1,
    AARCH64_X2  = 2,
    AARCH64_X3  = 3,
    AARCH64_X4  = 4,
    AARCH64_X5  = 5,
    AARCH64_X6  = 6,
    AARCH64_X7  = 7,
    AARCH64_X8  = 8,
    AARCH64_X9  = 9,
    AARCH64_X10 = 10,
    AARCH64_X11 = 11,
    AARCH64_X12 = 12,
    AARCH64_X13 = 13,
    AARCH64_X14 = 14,
    AARCH64_X15 = 15,
    AARCH64_X16 = 16,
    AARCH64_X17 = 17,
    AARCH64_X18 = 18,
    AARCH64_X19 = 19,
    AARCH64_X20 = 20,
    AARCH64_X21 = 21,
    AARCH64_X22 = 22,
    AARCH64_X23 = 23,
    AARCH64_X24 = 24,
    AARCH64_X25 = 25,
    AARCH64_X26 = 26,
    AARCH64_X27 = 27,
    AARCH64_X28 = 28,
    AARCH64_FP  = 29,
    AARCH64_LR  = 30,
    AARCH64_SP  = 31,
    AARCH64_GPR_COUNT = 31,
    AARCH64_V0  = 32,
    AARCH64_V1  = 33,
    AARCH64_V2  = 34,
    AARCH64_V3  = 35,
    AARCH64_V4  = 36,
    AARCH64_V5  = 37,
    AARCH64_V6  = 38,
    AARCH64_V7  = 39,
    AARCH64_V8  = 40,
    AARCH64_V9  = 41,
    AARCH64_V10 = 42,
    AARCH64_V11 = 43,
    AARCH64_V12 = 44,
    AARCH64_V13 = 45,
    AARCH64_V14 = 46,
    AARCH64_V15 = 47,
    AARCH64_V16 = 48,
    AARCH64_V17 = 49,
    AARCH64_V18 = 50,
    AARCH64_V19 = 51,
    AARCH64_V20 = 52,
    AARCH64_V21 = 53,
    AARCH64_V22 = 54,
    AARCH64_V23 = 55,
    AARCH64_V24 = 56,
    AARCH64_V25 = 57,
    AARCH64_V26 = 58,
    AARCH64_V27 = 59,
    AARCH64_V28 = 60,
    AARCH64_V29 = 61,
    AARCH64_V30 = 62,
    AARCH64_V31 = 63,
    AARCH64_FP_REG_COUNT = 32,
    AARCH64_PHYS_REG_COUNT = 64,
    AARCH64_NO_REG = 255,
} AArch64_Phys_Reg;

typedef struct AArch64_Reg {
    uint32_t id;
    uint8_t size;
    bool is_virtual;
    uint8_t phys_reg;
} AArch64_Reg;

static inline AArch64_Reg aarch64_reg_phys(AArch64_Phys_Reg phys, uint8_t size) {
    return (AArch64_Reg){ .id = (uint32_t)phys, .size = size, .is_virtual = false, .phys_reg = (uint8_t)phys };
}

static inline AArch64_Reg aarch64_reg_virt(uint32_t vreg, uint8_t size) {
    return (AArch64_Reg){ .id = vreg, .size = size, .is_virtual = true, .phys_reg = 0 };
}

static inline bool aarch64_reg_is_valid(AArch64_Reg r) {
    return r.size > 0 && (r.is_virtual || r.phys_reg != AARCH64_NO_REG);
}

const char *aarch64_reg_name(AArch64_Reg reg);

typedef struct AArch64_Mem {
    AArch64_Reg base;
    int32_t disp;
    Ny_String symbol;
    bool is_symbol;
} AArch64_Mem;

typedef enum AArch64_Cond {
    AARCH64_COND_NONE = 0,
    AARCH64_COND_EQ,
    AARCH64_COND_NE,
    AARCH64_COND_LT_S,
    AARCH64_COND_LE_S,
    AARCH64_COND_GT_S,
    AARCH64_COND_GE_S,
    AARCH64_COND_LT_U,
    AARCH64_COND_LE_U,
    AARCH64_COND_GT_U,
    AARCH64_COND_GE_U,
} AArch64_Cond;

const char *aarch64_cond_name(AArch64_Cond cond);

typedef enum AArch64_Op_Kind {
    AARCH64_OP_NONE = 0,
    AARCH64_OP_REG,
    AARCH64_OP_IMM,
    AARCH64_OP_MEM,
    AARCH64_OP_LABEL,
    AARCH64_OP_GLOBAL,
} AArch64_Op_Kind;

typedef struct AArch64_Operand {
    AArch64_Op_Kind kind;
    union {
        AArch64_Reg reg;
        int64_t imm;
        AArch64_Mem mem;
        Ny_Block_ID label;
        Ny_String global_name;
    };
} AArch64_Operand;

static inline AArch64_Operand aarch64_op_reg(AArch64_Reg reg) {
    AArch64_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = AARCH64_OP_REG;
    op.reg = reg;
    return op;
}

static inline AArch64_Operand aarch64_op_imm(int64_t imm) {
    AArch64_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = AARCH64_OP_IMM;
    op.imm = imm;
    return op;
}

static inline AArch64_Operand aarch64_op_mem(AArch64_Mem mem) {
    AArch64_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = AARCH64_OP_MEM;
    op.mem = mem;
    return op;
}

static inline AArch64_Operand aarch64_op_label(Ny_Block_ID label) {
    AArch64_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = AARCH64_OP_LABEL;
    op.label = label;
    return op;
}

static inline AArch64_Operand aarch64_op_global(Ny_String name) {
    AArch64_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = AARCH64_OP_GLOBAL;
    op.global_name = name;
    return op;
}

typedef enum AArch64_Opcode {
    AARCH64_OPC_NONE = 0,
    AARCH64_OPC_MOV,
    AARCH64_OPC_MOVZ,
    AARCH64_OPC_MOVK,
    AARCH64_OPC_ADD,
    AARCH64_OPC_SUB,
    AARCH64_OPC_SUBS,
    AARCH64_OPC_MUL,
    AARCH64_OPC_SDIV,
    AARCH64_OPC_UDIV,
    AARCH64_OPC_MSUB,
    AARCH64_OPC_AND,
    AARCH64_OPC_ORR,
    AARCH64_OPC_EOR,
    AARCH64_OPC_MVN,
    AARCH64_OPC_LSL,
    AARCH64_OPC_LSR,
    AARCH64_OPC_ASR,
    AARCH64_OPC_CMP,
    AARCH64_OPC_CSET,
    AARCH64_OPC_ADRP,
    AARCH64_OPC_ADD_LO12,
    AARCH64_OPC_LDR,
    AARCH64_OPC_STR,
    AARCH64_OPC_LDR_SYM,
    AARCH64_OPC_STP,
    AARCH64_OPC_LDP,
    AARCH64_OPC_B,
    AARCH64_OPC_BL,
    AARCH64_OPC_BCOND,
    AARCH64_OPC_CBZ,
    AARCH64_OPC_CBNZ,
    AARCH64_OPC_BR,
    AARCH64_OPC_BLR,
    AARCH64_OPC_RET,
    AARCH64_OPC_BRK,
    AARCH64_OPC_COUNT,
} AArch64_Opcode;

const char *aarch64_opcode_mnemonic(AArch64_Opcode opc);

typedef struct AArch64_Instruction {
    uint16_t opcode;
    uint8_t size;
    uint8_t cond;
    uint8_t op_count;
    uint8_t shift;
    AArch64_Operand ops[4];
} AArch64_Instruction;

typedef struct AArch64_Stack_Frame {
    uint32_t stack_size;
    int32_t *slot_offsets;
    size_t slot_count;
    size_t slot_capacity;
    bool has_call;
    uint64_t callee_saved_mask;
    uint32_t num_callee_saved;
    bool need_x8_save;
    int32_t x8_save_offset;
} AArch64_Stack_Frame;

typedef struct AArch64_Block {
    Ny_Block_ID id;
    Ny_String name;
    AArch64_Instruction *instructions;
    size_t inst_count;
    size_t inst_capacity;
} AArch64_Block;

typedef struct AArch64_Function {
    Ny_String name;
    Ny_Target_ABI abi;
    AArch64_Block *blocks;
    size_t block_count;
    size_t block_capacity;
    AArch64_Stack_Frame frame;
} AArch64_Function;

typedef struct AArch64_Module {
    Ny_String name;
    Ny_Target_ABI abi;
    AArch64_Function *functions;
    size_t function_count;
    size_t function_capacity;
    Ny_Machine_Global *globals;
    size_t global_count;
    size_t global_capacity;
} AArch64_Module;

void aarch64_func_init(AArch64_Function *fn, Ny_String name, Ny_Target_ABI abi);
void aarch64_func_destroy(AArch64_Function *fn);
void aarch64_block_append_inst(AArch64_Block *blk, AArch64_Instruction inst);
void aarch64_mod_init(AArch64_Module *mod, Ny_String name, Ny_Target_ABI abi);
void aarch64_mod_destroy(AArch64_Module *mod);

const AArch64_Phys_Reg *aarch64_abi_arg_regs(Ny_Target_ABI abi, size_t *out_count);
const AArch64_Phys_Reg *aarch64_abi_fp_arg_regs(Ny_Target_ABI abi, size_t *out_count);
AArch64_Phys_Reg aarch64_abi_ret_reg(Ny_Target_ABI abi, uint8_t size, bool is_fp);
bool aarch64_abi_is_callee_saved(Ny_Target_ABI abi, AArch64_Phys_Reg reg);

/* AAPCS64 aggregate ABI classification */
typedef enum {
    NY_AAPCS64_SCALAR = 0,
    NY_AAPCS64_GPR_AGG,
    NY_AAPCS64_HFA,
    NY_AAPCS64_INDIRECT,
} Ny_AAPCS64_Kind;

typedef struct Ny_AAPCS64_ABI {
    Ny_AAPCS64_Kind kind;
    uint32_t size;
    uint8_t reg_count;
    Ny_Type_ID hfa_member;
} Ny_AAPCS64_ABI;

Ny_AAPCS64_ABI aarch64_abi_classify_aggregate(const Ny_Type_Table *tt, Ny_Type_ID ty);

bool aarch64_lower_machine_func(const Ny_Target *target, const Ny_Machine_Function *mfn, void **out_target_fn, Ny_Diagnostic_List *diags);
bool aarch64_lower_machine_mod(const Ny_Target *target, const Ny_Machine_Module *mmod, AArch64_Module *out_mod, Ny_Diagnostic_List *diags);

char *aarch64_dump_func(const void *target_fn, Ny_Arena *scratch);
char *aarch64_dump_mod(const AArch64_Module *mod, Ny_Arena *scratch);

#include "nybit/aarch64_encode.h"

#ifdef __cplusplus
}
#endif

#endif
