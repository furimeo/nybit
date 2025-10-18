// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_IR_H
#define NYBIT_IR_H

#include "nybit/support.h"

/* Types */

enum {
    NY_TYPE_VOID = 0,
    NY_TYPE_I8   = 1,
    NY_TYPE_I16  = 2,
    NY_TYPE_I32  = 3,
    NY_TYPE_I64  = 4,
    NY_TYPE_I128 = 5,
    NY_TYPE_F16  = 6,
    NY_TYPE_F32  = 7,
    NY_TYPE_F64  = 8,
    NY_TYPE_PTR  = 9,
    NY_PRIMITIVE_TYPE_COUNT = 10,
};

typedef enum Ny_Type_Kind {
    NY_TYPE_KIND_PRIMITIVE,
    NY_TYPE_KIND_POINTER,
    NY_TYPE_KIND_VECTOR,
    NY_TYPE_KIND_ARRAY,
    NY_TYPE_KIND_STRUCT,
} Ny_Type_Kind;

typedef struct Ny_Type {
    Ny_Type_Kind kind;
    uint32_t size;
    uint32_t align;
    Ny_Type_ID elem_type;
    uint16_t vector_lanes;
    uint32_t array_count;
    Ny_Type_ID *field_types;
    size_t field_count;
    Ny_String name;
} Ny_Type;

typedef struct Ny_Type_Table {
    Ny_Type *types;
    size_t count;
    size_t capacity;
} Ny_Type_Table;

void ny_type_table_init(Ny_Type_Table *tt);
void ny_type_table_destroy(Ny_Type_Table *tt);
Ny_Type_ID ny_type_table_add_vector(Ny_Type_Table *tt, Ny_Type_ID elem, uint16_t lanes);
Ny_Type_ID ny_type_table_add_pointer(Ny_Type_Table *tt, Ny_Type_ID pointee);
Ny_Type_ID ny_type_table_add_array(Ny_Type_Table *tt, Ny_Type_ID elem, uint32_t count);
const Ny_Type *ny_type_get(const Ny_Type_Table *tt, Ny_Type_ID id);
const char *ny_type_name(const Ny_Type_Table *tt, Ny_Type_ID id);
uint32_t ny_type_size(const Ny_Type_Table *tt, Ny_Type_ID id);
bool ny_type_is_integer(Ny_Type_ID id);
bool ny_type_is_float(Ny_Type_ID id);
bool ny_type_is_vector(const Ny_Type_Table *tt, Ny_Type_ID id);

/* Values & Operands */

typedef enum Ny_Value_Kind {
    NY_VAL_INSTRUCTION,
    NY_VAL_ARGUMENT,
    NY_VAL_CONSTANT,
} Ny_Value_Kind;

typedef struct Ny_Value {
    Ny_Value_ID id;
    Ny_Type_ID type;
    Ny_Value_Kind kind;
    Ny_Inst_ID def;
    uint32_t index;
    Ny_String name;
} Ny_Value;

typedef enum Ny_Operand_Kind {
    NY_OP_NONE = 0,
    NY_OP_VALUE,
    NY_OP_BLOCK,
    NY_OP_IMM_INT,
    NY_OP_IMM_FLOAT,
    NY_OP_FUNCTION,
    NY_OP_SYMBOL,
    NY_OP_TYPE,
} Ny_Operand_Kind;

typedef struct Ny_Operand {
    uint8_t kind;
    union {
        Ny_Value_ID val;
        Ny_Block_ID blk;
        int64_t imm_int;
        double imm_float;
        Ny_Function_ID fn_id;
        Ny_Symbol_ID sym_id;
        Ny_Type_ID type_id;
    };
} Ny_Operand;

static inline Ny_Operand ny_operand_value(Ny_Value_ID val) {
    Ny_Operand op;
    op.kind = NY_OP_VALUE;
    op.val = val;
    return op;
}

static inline Ny_Operand ny_operand_block(Ny_Block_ID blk) {
    Ny_Operand op;
    op.kind = NY_OP_BLOCK;
    op.blk = blk;
    return op;
}

static inline Ny_Operand ny_operand_int(int64_t imm) {
    Ny_Operand op;
    op.kind = NY_OP_IMM_INT;
    op.imm_int = imm;
    return op;
}

static inline Ny_Operand ny_operand_float(double imm) {
    Ny_Operand op;
    op.kind = NY_OP_IMM_FLOAT;
    op.imm_float = imm;
    return op;
}

static inline Ny_Operand ny_operand_function(Ny_Function_ID fn_id) {
    Ny_Operand op;
    op.kind = NY_OP_FUNCTION;
    op.fn_id = fn_id;
    return op;
}

static inline Ny_Operand ny_operand_symbol(Ny_Symbol_ID sym_id) {
    Ny_Operand op;
    op.kind = NY_OP_SYMBOL;
    op.sym_id = sym_id;
    return op;
}

static inline Ny_Operand ny_operand_type(Ny_Type_ID type_id) {
    Ny_Operand op;
    op.kind = NY_OP_TYPE;
    op.type_id = type_id;
    return op;
}

/* Opcodes & Instructions */

typedef enum Ny_Opcode {
    NY_OPCODE_NONE = 0,
    NY_OPCODE_CONST,
    NY_OPCODE_CONST_NULL,
    NY_OPCODE_ADD,
    NY_OPCODE_SUB,
    NY_OPCODE_MUL,
    NY_OPCODE_DIV_S,
    NY_OPCODE_DIV_U,
    NY_OPCODE_REM_S,
    NY_OPCODE_REM_U,
    NY_OPCODE_NEG,
    NY_OPCODE_FADD,
    NY_OPCODE_FSUB,
    NY_OPCODE_FMUL,
    NY_OPCODE_FDIV,
    NY_OPCODE_FREM,
    NY_OPCODE_FNEG,
    NY_OPCODE_AND,
    NY_OPCODE_OR,
    NY_OPCODE_XOR,
    NY_OPCODE_NOT,
    NY_OPCODE_SHL,
    NY_OPCODE_SHR,
    NY_OPCODE_SAR,
    NY_OPCODE_ROTL,
    NY_OPCODE_ROTR,
    NY_OPCODE_CMP_EQ,
    NY_OPCODE_CMP_NE,
    NY_OPCODE_CMP_LT_S,
    NY_OPCODE_CMP_LT_U,
    NY_OPCODE_CMP_LE_S,
    NY_OPCODE_CMP_LE_U,
    NY_OPCODE_CMP_GT_S,
    NY_OPCODE_CMP_GT_U,
    NY_OPCODE_CMP_GE_S,
    NY_OPCODE_CMP_GE_U,
    NY_OPCODE_FCMP_EQ,
    NY_OPCODE_FCMP_NE,
    NY_OPCODE_FCMP_LT,
    NY_OPCODE_FCMP_LE,
    NY_OPCODE_FCMP_GT,
    NY_OPCODE_FCMP_GE,
    NY_OPCODE_SELECT,
    NY_OPCODE_ADDR,
    NY_OPCODE_ADDR_OFFSET,
    NY_OPCODE_LOAD,
    NY_OPCODE_STORE,
    NY_OPCODE_STACK_SLOT,
    NY_OPCODE_STACK_ADDR,
    NY_OPCODE_ATOMIC_LOAD,
    NY_OPCODE_ATOMIC_STORE,
    NY_OPCODE_ATOMIC_RMW,
    NY_OPCODE_ATOMIC_CMPXCHG,
    NY_OPCODE_FENCE,
    NY_OPCODE_CAST,
    NY_OPCODE_EXTEND,
    NY_OPCODE_TRUNCATE,
    NY_OPCODE_BITCAST,
    NY_OPCODE_SEXT,
    NY_OPCODE_ZEXT,
    NY_OPCODE_FEXT,
    NY_OPCODE_FTRUNC,
    NY_OPCODE_SITOFP,
    NY_OPCODE_UITOFP,
    NY_OPCODE_FPTOSI,
    NY_OPCODE_FPTOUI,
    NY_OPCODE_VADD,
    NY_OPCODE_VSUB,
    NY_OPCODE_VMUL,
    NY_OPCODE_VDIV,
    NY_OPCODE_VAND,
    NY_OPCODE_VOR,
    NY_OPCODE_VXOR,
    NY_OPCODE_VSHUFFLE,
    NY_OPCODE_BRANCH,
    NY_OPCODE_BRANCH_IF,
    NY_OPCODE_SWITCH,
    NY_OPCODE_RETURN,
    NY_OPCODE_TRAP,
    NY_OPCODE_CALL,
    NY_OPCODE_CALL_INDIRECT,
    NY_OPCODE_PHI,
    NY_OPCODE_COUNT,
} Ny_Opcode;

enum {
    NY_FLAG_VOLATILE = 1 << 0,
    NY_FLAG_ATOMIC   = 1 << 1,
    NY_FLAG_EXACT    = 1 << 2,
    NY_FLAG_NSW      = 1 << 3,
    NY_FLAG_NUW      = 1 << 4,
    NY_FLAG_FAST_MATH= 1 << 5,
};

typedef struct Ny_Instruction {
    Ny_Inst_ID id;
    uint16_t opcode;
    uint16_t flags;
    Ny_Value_ID result;
    uint32_t op_start;
    uint16_t op_count;
    Ny_Block_ID block;
    Ny_Inst_ID prev;
    Ny_Inst_ID next;
} Ny_Instruction;

const char *ny_opcode_name(Ny_Opcode op);
bool ny_opcode_is_terminator(Ny_Opcode op);

/* Blocks */

typedef struct Ny_Block {
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
} Ny_Block;

void ny_block_add_edge(Ny_Block *pred, Ny_Block *succ);
void ny_block_remove_edge(Ny_Block *pred, Ny_Block *succ);

/* Functions */

typedef enum Ny_Calling_Convention {
    NY_CC_DEFAULT = 0,
    NY_CC_C,
    NY_CC_SYSTEM,
    NY_CC_FAST,
} Ny_Calling_Convention;

typedef struct Ny_Function {
    Ny_Function_ID id;
    Ny_String name;
    Ny_Type_ID return_type;
    Ny_Type_ID *param_types;
    Ny_String *param_names;
    size_t param_count;
    size_t param_capacity;
    uint8_t call_conv;
    bool is_variadic;
    Ny_Block_ID entry_block;

    Ny_Block *blocks;
    size_t block_count;
    size_t block_capacity;

    Ny_Instruction *instructions;
    size_t inst_count;
    size_t inst_capacity;

    Ny_Value *values;
    size_t val_count;
    size_t val_capacity;

    Ny_Operand *operands;
    size_t op_count;
    size_t op_capacity;
} Ny_Function;

void ny_function_init(Ny_Function *fn, Ny_Function_ID id, Ny_String name, Ny_Type_ID ret_type, uint8_t call_conv);
void ny_function_destroy(Ny_Function *fn);
void ny_function_add_param(Ny_Function *fn, Ny_Type_ID type, Ny_String name);

Ny_Block *ny_function_get_block(const Ny_Function *fn, Ny_Block_ID id);
Ny_Instruction *ny_function_get_instruction(const Ny_Function *fn, Ny_Inst_ID id);
Ny_Value *ny_function_get_value(const Ny_Function *fn, Ny_Value_ID id);
Ny_Operand *ny_function_get_operands(const Ny_Function *fn, const Ny_Instruction *inst);

Ny_Block_ID ny_function_create_block(Ny_Function *fn, Ny_String name);
Ny_Value_ID ny_function_create_value(Ny_Function *fn, Ny_Type_ID type, Ny_Value_Kind kind, Ny_Inst_ID def, uint32_t index, Ny_String name);
Ny_Inst_ID ny_function_append_instruction(Ny_Function *fn, Ny_Block_ID block_id, Ny_Opcode opcode, Ny_Value_ID result, const Ny_Operand *ops, size_t op_count, uint16_t flags);

void ny_instruction_replace_operand(Ny_Function *fn, Ny_Inst_ID inst_id, size_t op_idx, Ny_Operand new_op);
void ny_function_replace_all_uses(Ny_Function *fn, Ny_Value_ID old_val, Ny_Value_ID new_val);
void ny_function_remove_instruction(Ny_Function *fn, Ny_Inst_ID inst_id);
void ny_function_remove_block(Ny_Function *fn, Ny_Block_ID block_id);
bool ny_function_split_block(Ny_Function *fn, Ny_Block_ID block_id, Ny_Inst_ID split_before_inst, Ny_Block_ID *out_new_block_id);
bool ny_function_merge_blocks(Ny_Function *fn, Ny_Block_ID first_id, Ny_Block_ID second_id);
void ny_function_remove_phi_incoming(Ny_Function *fn, Ny_Block_ID blk_id, Ny_Block_ID pred_id);

/* Module & Context */

typedef struct Ny_Module {
    Ny_String name;
    Ny_Type_Table types;
    Ny_Function *functions;
    size_t function_count;
    size_t function_capacity;
} Ny_Module;

typedef struct Ny_Context {
    Ny_Arena arena;
    Ny_Module module;
} Ny_Context;

void ny_context_init(Ny_Context *ctx, const char *name);
void ny_context_destroy(Ny_Context *ctx);

void ny_module_init(Ny_Module *mod, Ny_String name);
void ny_module_destroy(Ny_Module *mod);
Ny_Function_ID ny_module_create_function(Ny_Module *mod, Ny_String name, Ny_Type_ID ret_type, uint8_t call_conv);
Ny_Function *ny_module_get_function(Ny_Module *mod, Ny_Function_ID id);
Ny_Function *ny_module_get_function_by_name(Ny_Module *mod, const char *name);

/* Builder */

typedef struct Ny_Builder {
    Ny_Module *module;
    Ny_Function_ID cur_fn;
    Ny_Block_ID cur_block;
} Ny_Builder;

void ny_builder_init(Ny_Builder *b, Ny_Module *mod);
void ny_builder_set_insert_point(Ny_Builder *b, Ny_Function_ID fn_id, Ny_Block_ID block_id);
Ny_Function *ny_builder_current_function(Ny_Builder *b);

Ny_Function_ID ny_builder_add_function(Ny_Builder *b, const char *name, Ny_Type_ID ret_type);
Ny_Value_ID ny_builder_add_param(Ny_Builder *b, const char *name, Ny_Type_ID type);
Ny_Block_ID ny_builder_add_block(Ny_Builder *b, const char *name);

Ny_Value_ID ny_builder_const_i64(Ny_Builder *b, int64_t val, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_const_f64(Ny_Builder *b, double val, Ny_Type_ID type, const char *name);

Ny_Value_ID ny_builder_binary(Ny_Builder *b, Ny_Opcode op, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_add(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_sub(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_mul(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_div_s(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_div_u(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_rem_s(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_rem_u(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_neg(Ny_Builder *b, Ny_Value_ID val, Ny_Type_ID type, const char *name);

Ny_Value_ID ny_builder_and(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_or(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_xor(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_not(Ny_Builder *b, Ny_Value_ID val, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_shl(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_shr(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);
Ny_Value_ID ny_builder_sar(Ny_Builder *b, Ny_Value_ID lhs, Ny_Value_ID rhs, Ny_Type_ID type, const char *name);

Ny_Value_ID ny_builder_cmp(Ny_Builder *b, Ny_Opcode op, Ny_Value_ID lhs, Ny_Value_ID rhs, const char *name);
Ny_Value_ID ny_builder_select(Ny_Builder *b, Ny_Value_ID cond, Ny_Value_ID true_val, Ny_Value_ID false_val, Ny_Type_ID type, const char *name);

Ny_Value_ID ny_builder_load(Ny_Builder *b, Ny_Value_ID ptr, Ny_Type_ID val_type, const char *name);
Ny_Inst_ID ny_builder_store(Ny_Builder *b, Ny_Value_ID ptr, Ny_Value_ID val);

Ny_Inst_ID ny_builder_branch(Ny_Builder *b, Ny_Block_ID target);
Ny_Inst_ID ny_builder_branch_if(Ny_Builder *b, Ny_Value_ID cond, Ny_Block_ID true_blk, Ny_Block_ID false_blk);
Ny_Inst_ID ny_builder_ret(Ny_Builder *b, Ny_Value_ID val);
Ny_Inst_ID ny_builder_trap(Ny_Builder *b);
Ny_Value_ID ny_builder_call(Ny_Builder *b, Ny_Function_ID callee, const Ny_Value_ID *args, size_t arg_count, Ny_Type_ID ret_type, const char *name);

/* Validation & Dump */

typedef struct Ny_Diagnostic_Item {
    char *message;
} Ny_Diagnostic_Item;

typedef struct Ny_Diagnostic_List {
    Ny_Diagnostic_Item *items;
    size_t count;
    size_t capacity;
} Ny_Diagnostic_List;

void ny_diagnostic_list_init(Ny_Diagnostic_List *list);
void ny_diagnostic_list_destroy(Ny_Diagnostic_List *list);
void ny_diagnostic_list_append(Ny_Diagnostic_List *list, const char *msg);

bool ny_validate_module(const Ny_Module *mod, Ny_Diagnostic_List *out_diags);
bool ny_validate_function(const Ny_Module *mod, const Ny_Function *fn, Ny_Diagnostic_List *out_diags);

char *ny_dump_module(const Ny_Module *mod, Ny_Arena *scratch);

/* Dominator Tree */

typedef struct Ny_Core_Dominator_Tree {
    Ny_Block_ID *idom;
    int *rpo;
    Ny_Block_ID entry;
    size_t block_count;
} Ny_Core_Dominator_Tree;

void ny_core_dominator_tree_init(Ny_Core_Dominator_Tree *dt, const Ny_Function *fn);
void ny_core_dominator_tree_destroy(Ny_Core_Dominator_Tree *dt);
bool ny_core_dominator_tree_dominates(const Ny_Core_Dominator_Tree *dt, Ny_Block_ID a, Ny_Block_ID b);

#endif // NYBIT_IR_H
