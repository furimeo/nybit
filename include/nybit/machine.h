// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_MACHINE_H
#define NYBIT_MACHINE_H

#include "nybit/ir.h"
#include "nybit/support.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Register Classes */

typedef enum Ny_Reg_Class {
    NY_REG_CLASS_NONE = 0,
    NY_REG_CLASS_GPR32,
    NY_REG_CLASS_GPR64,
    NY_REG_CLASS_FP32,
    NY_REG_CLASS_FP64,
    NY_REG_CLASS_VEC128,
    NY_REG_CLASS_FLAGS,
} Ny_Reg_Class;

const char *ny_reg_class_name(Ny_Reg_Class rc);

/* Machine Register */

typedef struct Ny_Machine_Reg {
    uint32_t id;
    uint16_t reg_class;
    bool is_virtual;
    uint8_t phys_reg;
} Ny_Machine_Reg;

static inline Ny_Machine_Reg ny_mreg_vreg(uint32_t id, Ny_Reg_Class rc) {
    return (Ny_Machine_Reg){ .id = id, .reg_class = (uint16_t)rc, .is_virtual = true, .phys_reg = 0 };
}

static inline Ny_Machine_Reg ny_mreg_preg(uint8_t phys_id, Ny_Reg_Class rc) {
    return (Ny_Machine_Reg){ .id = phys_id, .reg_class = (uint16_t)rc, .is_virtual = false, .phys_reg = phys_id };
}

static inline bool ny_mreg_is_valid(Ny_Machine_Reg reg) {
    return reg.reg_class != NY_REG_CLASS_NONE;
}

static inline bool ny_mreg_eq(Ny_Machine_Reg a, Ny_Machine_Reg b) {
    return a.id == b.id && a.reg_class == b.reg_class && a.is_virtual == b.is_virtual && a.phys_reg == b.phys_reg;
}

/* Addressing Mode & Memory Operands */

typedef struct Ny_Machine_Mem_Op {
    Ny_Machine_Reg base;
    Ny_Machine_Reg index;
    uint8_t scale;
    int32_t disp;
    Ny_Slot_ID stack_slot;
    Ny_Symbol_ID symbol;
    Ny_String symbol_name;
} Ny_Machine_Mem_Op;

/* Condition Codes */

typedef enum Ny_Machine_Cond {
    NY_MCOND_NONE = 0,
    NY_MCOND_EQ,
    NY_MCOND_NE,
    NY_MCOND_LT_S,
    NY_MCOND_LT_U,
    NY_MCOND_LE_S,
    NY_MCOND_LE_U,
    NY_MCOND_GT_S,
    NY_MCOND_GT_U,
    NY_MCOND_GE_S,
    NY_MCOND_GE_U,
} Ny_Machine_Cond;

const char *ny_mcond_name(Ny_Machine_Cond cond);

/* Machine Operand */

typedef enum Ny_Machine_Op_Kind {
    NY_MOP_KIND_NONE = 0,
    NY_MOP_KIND_REG,
    NY_MOP_KIND_IMM_INT,
    NY_MOP_KIND_IMM_FLOAT,
    NY_MOP_KIND_MEM,
    NY_MOP_KIND_BLOCK,
    NY_MOP_KIND_SYMBOL,
    NY_MOP_KIND_COND,
} Ny_Machine_Op_Kind;

typedef struct Ny_Machine_Operand {
    Ny_Machine_Op_Kind kind;
    Ny_Type_ID type;
    union {
        Ny_Machine_Reg reg;
        int64_t imm_int;
        double imm_float;
        Ny_Machine_Mem_Op mem;
        Ny_Block_ID block;
        struct {
            Ny_String name;
            Ny_Symbol_ID id;
        } symbol;
        Ny_Machine_Cond cond;
    };
} Ny_Machine_Operand;

static inline Ny_Machine_Operand ny_mop_reg(Ny_Machine_Reg reg) {
    Ny_Machine_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = NY_MOP_KIND_REG;
    op.reg = reg;
    return op;
}

static inline Ny_Machine_Operand ny_mop_imm_int(int64_t val) {
    Ny_Machine_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = NY_MOP_KIND_IMM_INT;
    op.imm_int = val;
    return op;
}

static inline Ny_Machine_Operand ny_mop_imm_float(double val) {
    Ny_Machine_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = NY_MOP_KIND_IMM_FLOAT;
    op.imm_float = val;
    return op;
}

static inline Ny_Machine_Operand ny_mop_mem(Ny_Machine_Mem_Op mem) {
    Ny_Machine_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = NY_MOP_KIND_MEM;
    op.mem = mem;
    return op;
}

static inline Ny_Machine_Operand ny_mop_block(Ny_Block_ID blk) {
    Ny_Machine_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = NY_MOP_KIND_BLOCK;
    op.block = blk;
    return op;
}

static inline Ny_Machine_Operand ny_mop_symbol(Ny_String name, Ny_Symbol_ID id) {
    Ny_Machine_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = NY_MOP_KIND_SYMBOL;
    op.symbol.name = name;
    op.symbol.id = id;
    return op;
}

static inline Ny_Machine_Operand ny_mop_cond(Ny_Machine_Cond cond) {
    Ny_Machine_Operand op;
    memset(&op, 0, sizeof(op));
    op.kind = NY_MOP_KIND_COND;
    op.cond = cond;
    return op;
}

/* Machine Opcodes */

typedef enum Ny_Machine_Opcode {
    NY_MOPC_NONE = 0,

    NY_MOPC_COPY,
    NY_MOPC_LOAD,
    NY_MOPC_STORE,
    NY_MOPC_LEA,

    NY_MOPC_ADD,
    NY_MOPC_SUB,
    NY_MOPC_IMUL,
    NY_MOPC_IDIV,
    NY_MOPC_UDIV,
    NY_MOPC_IREM,
    NY_MOPC_UREM,
    NY_MOPC_NEG,

    NY_MOPC_AND,
    NY_MOPC_OR,
    NY_MOPC_XOR,
    NY_MOPC_NOT,
    NY_MOPC_SHL,
    NY_MOPC_SHR,
    NY_MOPC_SAR,

    NY_MOPC_FADD,
    NY_MOPC_FSUB,
    NY_MOPC_FMUL,
    NY_MOPC_FDIV,
    NY_MOPC_FNEG,

    NY_MOPC_CMP,
    NY_MOPC_TEST,
    NY_MOPC_FCMP,
    NY_MOPC_SELECT,

    NY_MOPC_JMP,
    NY_MOPC_JCC,
    NY_MOPC_CALL,
    NY_MOPC_CALL_IND,
    NY_MOPC_RET,
    NY_MOPC_TRAP,

    NY_MOPC_PHI,
    NY_MOPC_STACK_ALLOC,

    NY_MOPC_COUNT,
} Ny_Machine_Opcode;

const char *ny_mopc_name(Ny_Machine_Opcode opc);

/* Instruction Flags */

enum {
    NY_MINST_FLAG_TERMINATOR  = 1 << 0,
    NY_MINST_FLAG_BRANCH      = 1 << 1,
    NY_MINST_FLAG_CALL        = 1 << 2,
    NY_MINST_FLAG_SIDE_EFFECT = 1 << 3,
    NY_MINST_FLAG_COMMUTATIVE = 1 << 4,
};

/* Machine Instruction */

typedef struct Ny_Machine_Instruction {
    Ny_Inst_ID id;
    uint16_t opcode;
    uint16_t flags;
    Ny_Machine_Reg def_reg;
    uint32_t op_start;
    uint16_t op_count;
    Ny_Block_ID block;
    Ny_Inst_ID prev;
    Ny_Inst_ID next;
} Ny_Machine_Instruction;

/* Stack Slot */

typedef struct Ny_Machine_Stack_Slot {
    Ny_Slot_ID id;
    uint32_t size;
    uint32_t align;
    int32_t frame_offset;
} Ny_Machine_Stack_Slot;

/* Machine Block */

typedef struct Ny_Machine_Block {
    Ny_Block_ID id;
    Ny_String name;
    Ny_Inst_ID first_inst;
    Ny_Inst_ID last_inst;
    uint32_t inst_count;
    Ny_Block_ID *preds;
    size_t pred_count;
    size_t pred_capacity;
    Ny_Block_ID *succs;
    size_t succ_count;
    size_t succ_capacity;
} Ny_Machine_Block;

void ny_mblock_add_edge(Ny_Machine_Block *pred, Ny_Machine_Block *succ);
void ny_mblock_remove_edge(Ny_Machine_Block *pred, Ny_Machine_Block *succ);

/* Machine Function */

typedef struct Ny_Machine_Function {
    Ny_Function_ID id;
    Ny_String name;
    uint8_t call_conv;
    Ny_Type_ID return_type;

    Ny_Reg_Class *vreg_classes;
    size_t vreg_count;
    size_t vreg_capacity;

    Ny_Machine_Reg *param_regs;
    size_t param_count;
    size_t param_capacity;
    Ny_Type_ID *param_types;
    Ny_Slot_ID *param_slots;

    Ny_Machine_Stack_Slot *stack_slots;
    size_t stack_slot_count;
    size_t stack_slot_capacity;

    Ny_Machine_Block *blocks;
    size_t block_count;
    size_t block_capacity;
    Ny_Block_ID entry_block;

    Ny_Machine_Instruction *instructions;
    size_t inst_count;
    size_t inst_capacity;
    Ny_Loc *inst_locs;
    size_t inst_loc_capacity;
    Ny_Type_ID *inst_ret_types;
    size_t inst_ret_type_capacity;
    Ny_Slot_ID *inst_result_slots;
    size_t inst_result_slot_capacity;

    Ny_Machine_Operand *operands;
    size_t op_count;
    size_t op_capacity;
} Ny_Machine_Function;

void ny_mfunc_init(Ny_Machine_Function *fn, Ny_Function_ID id, Ny_String name, Ny_Type_ID ret_type, uint8_t call_conv);
void ny_mfunc_destroy(Ny_Machine_Function *fn);

Ny_Machine_Reg ny_mfunc_create_vreg(Ny_Machine_Function *fn, Ny_Reg_Class rc);
void ny_mfunc_add_param(Ny_Machine_Function *fn, Ny_Machine_Reg reg);
void ny_mfunc_add_param_typed(Ny_Machine_Function *fn, Ny_Machine_Reg reg, Ny_Type_ID type, Ny_Slot_ID slot);
Ny_Slot_ID ny_mfunc_create_stack_slot(Ny_Machine_Function *fn, uint32_t size, uint32_t align);

Ny_Block_ID ny_mfunc_create_block(Ny_Machine_Function *fn, Ny_String name);
Ny_Machine_Block *ny_mfunc_get_block(const Ny_Machine_Function *fn, Ny_Block_ID id);

Ny_Inst_ID ny_mfunc_append_inst(Ny_Machine_Function *fn, Ny_Block_ID block_id, Ny_Machine_Opcode opcode, Ny_Machine_Reg def_reg, const Ny_Machine_Operand *ops, size_t op_count, uint16_t flags);
Ny_Inst_ID ny_mfunc_insert_before(Ny_Machine_Function *fn, Ny_Inst_ID before_inst_id, Ny_Machine_Opcode opcode, Ny_Machine_Reg def_reg, const Ny_Machine_Operand *ops, size_t op_count, uint16_t flags);
Ny_Inst_ID ny_mfunc_insert_after(Ny_Machine_Function *fn, Ny_Inst_ID after_inst_id, Ny_Machine_Opcode opcode, Ny_Machine_Reg def_reg, const Ny_Machine_Operand *ops, size_t op_count, uint16_t flags);
Ny_Machine_Instruction *ny_mfunc_get_inst(const Ny_Machine_Function *fn, Ny_Inst_ID id);
Ny_Machine_Operand *ny_mfunc_get_operands(const Ny_Machine_Function *fn, const Ny_Machine_Instruction *inst);
void ny_mfunc_set_inst_loc(Ny_Machine_Function *fn, Ny_Inst_ID inst_id, Ny_Loc loc);
Ny_Loc ny_mfunc_get_inst_loc(const Ny_Machine_Function *fn, Ny_Inst_ID inst_id);
void ny_mfunc_set_inst_ret_type(Ny_Machine_Function *fn, Ny_Inst_ID inst_id, Ny_Type_ID type);
Ny_Type_ID ny_mfunc_get_inst_ret_type(const Ny_Machine_Function *fn, Ny_Inst_ID inst_id);
void ny_mfunc_set_inst_result_slot(Ny_Machine_Function *fn, Ny_Inst_ID inst_id, Ny_Slot_ID slot);
Ny_Slot_ID ny_mfunc_get_inst_result_slot(const Ny_Machine_Function *fn, Ny_Inst_ID inst_id);

/* Machine Global */

typedef struct Ny_Machine_Global {
    Ny_String name;
    Ny_Global_Kind kind;
    uint32_t align;
    uint8_t *data;
    size_t data_size;
} Ny_Machine_Global;

/* Machine Module */

typedef struct Ny_Machine_Module {
    Ny_String name;
    const Ny_Type_Table *types;
    Ny_Machine_Function *functions;
    size_t function_count;
    size_t function_capacity;
    Ny_Machine_Global *globals;
    size_t global_count;
    size_t global_capacity;
} Ny_Machine_Module;

void ny_mmod_init(Ny_Machine_Module *mod, Ny_String name);
void ny_mmod_destroy(Ny_Machine_Module *mod);
Ny_Machine_Function *ny_mmod_create_function(Ny_Machine_Module *mod, Ny_String name, Ny_Type_ID ret_type, uint8_t call_conv);
Ny_Machine_Function *ny_mmod_get_function(const Ny_Machine_Module *mod, Ny_Function_ID id);
void ny_mmod_add_global(Ny_Machine_Module *mod, Ny_String name, Ny_Global_Kind kind, uint32_t align, const void *data, size_t data_size);

/* Lowering */

bool ny_ir_lower_to_mir(const Ny_Module *ir_mod, Ny_Machine_Module *out_mmod, Ny_Diagnostic_List *diags);

/* Dumper */

char *ny_mir_dump_module(const Ny_Machine_Module *mod, Ny_Arena *scratch);

#ifdef __cplusplus
}
#endif

#endif
