// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/machine.h"

const char *ny_reg_class_name(Ny_Reg_Class rc) {
    switch (rc) {
    case NY_REG_CLASS_NONE:    return "none";
    case NY_REG_CLASS_GPR32:   return "gpr32";
    case NY_REG_CLASS_GPR64:   return "gpr64";
    case NY_REG_CLASS_FP32:    return "fp32";
    case NY_REG_CLASS_FP64:    return "fp64";
    case NY_REG_CLASS_VEC128:  return "vec128";
    case NY_REG_CLASS_FLAGS:   return "flags";
    }
    return "unknown";
}

const char *ny_mcond_name(Ny_Machine_Cond cond) {
    switch (cond) {
    case NY_MCOND_NONE: return "none";
    case NY_MCOND_EQ:   return "eq";
    case NY_MCOND_NE:   return "ne";
    case NY_MCOND_LT_S: return "lt_s";
    case NY_MCOND_LT_U: return "lt_u";
    case NY_MCOND_LE_S: return "le_s";
    case NY_MCOND_LE_U: return "le_u";
    case NY_MCOND_GT_S: return "gt_s";
    case NY_MCOND_GT_U: return "gt_u";
    case NY_MCOND_GE_S: return "ge_s";
    case NY_MCOND_GE_U: return "ge_u";
    }
    return "unknown";
}

const char *ny_mopc_name(Ny_Machine_Opcode opc) {
    switch (opc) {
    case NY_MOPC_NONE:        return "none";
    case NY_MOPC_COPY:        return "mov";
    case NY_MOPC_LOAD:        return "load";
    case NY_MOPC_STORE:       return "store";
    case NY_MOPC_LEA:         return "lea";
    case NY_MOPC_ADD:         return "add";
    case NY_MOPC_SUB:         return "sub";
    case NY_MOPC_IMUL:        return "imul";
    case NY_MOPC_IDIV:        return "idiv";
    case NY_MOPC_UDIV:        return "udiv";
    case NY_MOPC_IREM:        return "irem";
    case NY_MOPC_UREM:        return "urem";
    case NY_MOPC_NEG:         return "neg";
    case NY_MOPC_AND:         return "and";
    case NY_MOPC_OR:          return "or";
    case NY_MOPC_XOR:         return "xor";
    case NY_MOPC_NOT:         return "not";
    case NY_MOPC_SHL:         return "shl";
    case NY_MOPC_SHR:         return "shr";
    case NY_MOPC_SAR:         return "sar";
    case NY_MOPC_FADD:        return "fadd";
    case NY_MOPC_FSUB:        return "fsub";
    case NY_MOPC_FMUL:        return "fmul";
    case NY_MOPC_FDIV:        return "fdiv";
    case NY_MOPC_FNEG:        return "fneg";
    case NY_MOPC_CMP:         return "cmp";
    case NY_MOPC_TEST:        return "test";
    case NY_MOPC_FCMP:        return "fcmp";
    case NY_MOPC_SELECT:      return "select";
    case NY_MOPC_JMP:         return "jmp";
    case NY_MOPC_JCC:         return "jcc";
    case NY_MOPC_CALL:        return "call";
    case NY_MOPC_CALL_IND:    return "call_ind";
    case NY_MOPC_RET:         return "ret";
    case NY_MOPC_TRAP:        return "trap";
    case NY_MOPC_PHI:         return "mphi";
    case NY_MOPC_STACK_ALLOC: return "stack_alloc";
    case NY_MOPC_COUNT:       return "unknown";
    }
    return "unknown";
}

void ny_mblock_add_edge(Ny_Machine_Block *pred, Ny_Machine_Block *succ) {
    for (size_t i = 0; i < pred->succ_count; i++) {
        if (pred->succs[i] == succ->id) return;
    }
    ny_buf_grow((void **)&pred->succs, &pred->succ_capacity, pred->succ_count, sizeof(Ny_Block_ID));
    pred->succs[pred->succ_count++] = succ->id;

    for (size_t i = 0; i < succ->pred_count; i++) {
        if (succ->preds[i] == pred->id) return;
    }
    ny_buf_grow((void **)&succ->preds, &succ->pred_capacity, succ->pred_count, sizeof(Ny_Block_ID));
    succ->preds[succ->pred_count++] = pred->id;
}

void ny_mblock_remove_edge(Ny_Machine_Block *pred, Ny_Machine_Block *succ) {
    for (size_t i = 0; i < pred->succ_count; i++) {
        if (pred->succs[i] == succ->id) {
            pred->succs[i] = pred->succs[pred->succ_count - 1];
            pred->succ_count--;
            break;
        }
    }
    for (size_t i = 0; i < succ->pred_count; i++) {
        if (succ->preds[i] == pred->id) {
            succ->preds[i] = succ->preds[succ->pred_count - 1];
            succ->pred_count--;
            break;
        }
    }
}

void ny_mfunc_init(Ny_Machine_Function *fn, Ny_Function_ID id, Ny_String name, Ny_Type_ID ret_type, uint8_t call_conv) {
    memset(fn, 0, sizeof(Ny_Machine_Function));
    fn->id = id;
    fn->name = name;
    fn->return_type = ret_type;
    fn->call_conv = call_conv;
    fn->entry_block = NY_INVALID_BLOCK;
}

void ny_mfunc_destroy(Ny_Machine_Function *fn) {
    if (fn->vreg_classes) {
        ny_free(fn->vreg_classes, fn->vreg_capacity * sizeof(Ny_Reg_Class));
    }
    if (fn->param_regs) {
        ny_free(fn->param_regs, fn->param_capacity * sizeof(Ny_Machine_Reg));
    }
    if (fn->stack_slots) {
        ny_free(fn->stack_slots, fn->stack_slot_capacity * sizeof(Ny_Machine_Stack_Slot));
    }
    for (size_t i = 0; i < fn->block_count; i++) {
        Ny_Machine_Block *b = &fn->blocks[i];
        if (b->preds) {
            ny_free(b->preds, b->pred_capacity * sizeof(Ny_Block_ID));
        }
        if (b->succs) {
            ny_free(b->succs, b->succ_capacity * sizeof(Ny_Block_ID));
        }
    }
    if (fn->blocks) {
        ny_free(fn->blocks, fn->block_capacity * sizeof(Ny_Machine_Block));
    }
    if (fn->instructions) {
        ny_free(fn->instructions, fn->inst_capacity * sizeof(Ny_Machine_Instruction));
    }
    if (fn->operands) {
        ny_free(fn->operands, fn->op_capacity * sizeof(Ny_Machine_Operand));
    }
    memset(fn, 0, sizeof(Ny_Machine_Function));
    fn->entry_block = NY_INVALID_BLOCK;
}

Ny_Machine_Reg ny_mfunc_create_vreg(Ny_Machine_Function *fn, Ny_Reg_Class rc) {
    uint32_t id = (uint32_t)fn->vreg_count;
    ny_buf_grow((void **)&fn->vreg_classes, &fn->vreg_capacity, fn->vreg_count, sizeof(Ny_Reg_Class));
    fn->vreg_classes[fn->vreg_count++] = rc;
    return ny_mreg_vreg(id, rc);
}

void ny_mfunc_add_param(Ny_Machine_Function *fn, Ny_Machine_Reg reg) {
    ny_buf_grow((void **)&fn->param_regs, &fn->param_capacity, fn->param_count, sizeof(Ny_Machine_Reg));
    fn->param_regs[fn->param_count++] = reg;
}

Ny_Slot_ID ny_mfunc_create_stack_slot(Ny_Machine_Function *fn, uint32_t size, uint32_t align) {
    Ny_Slot_ID id = (Ny_Slot_ID)fn->stack_slot_count;
    ny_buf_grow((void **)&fn->stack_slots, &fn->stack_slot_capacity, fn->stack_slot_count, sizeof(Ny_Machine_Stack_Slot));
    Ny_Machine_Stack_Slot *s = &fn->stack_slots[fn->stack_slot_count++];
    s->id = id;
    s->size = size;
    s->align = align > 0 ? align : 4;
    s->frame_offset = 0;
    return id;
}

Ny_Block_ID ny_mfunc_create_block(Ny_Machine_Function *fn, Ny_String name) {
    Ny_Block_ID id = (Ny_Block_ID)fn->block_count;
    ny_buf_grow((void **)&fn->blocks, &fn->block_capacity, fn->block_count, sizeof(Ny_Machine_Block));
    Ny_Machine_Block *b = &fn->blocks[fn->block_count++];
    memset(b, 0, sizeof(Ny_Machine_Block));
    b->id = id;
    b->name = name;
    b->first_inst = NY_INVALID_INST;
    b->last_inst = NY_INVALID_INST;

    if (fn->entry_block == NY_INVALID_BLOCK) {
        fn->entry_block = id;
    }
    return id;
}

Ny_Machine_Block *ny_mfunc_get_block(const Ny_Machine_Function *fn, Ny_Block_ID id) {
    if (id >= fn->block_count) return nullptr;
    return &fn->blocks[id];
}

static uint32_t mfunc_push_operands(Ny_Machine_Function *fn, const Ny_Machine_Operand *ops, size_t op_count) {
    uint32_t op_start = (uint32_t)fn->op_count;
    if (op_count > 0) {
        while (fn->op_count + op_count > fn->op_capacity) {
            size_t old_cap = fn->op_capacity;
            size_t new_cap = old_cap == 0 ? 16 : old_cap * 2;
            while (new_cap < fn->op_count + op_count) new_cap *= 2;
            fn->operands = (Ny_Machine_Operand *)ny_realloc(fn->operands, old_cap * sizeof(Ny_Machine_Operand), new_cap * sizeof(Ny_Machine_Operand));
            fn->op_capacity = new_cap;
        }
        memcpy(&fn->operands[fn->op_count], ops, op_count * sizeof(Ny_Machine_Operand));
        fn->op_count += op_count;
    }
    return op_start;
}

Ny_Inst_ID ny_mfunc_append_inst(Ny_Machine_Function *fn, Ny_Block_ID block_id, Ny_Machine_Opcode opcode, Ny_Machine_Reg def_reg, const Ny_Machine_Operand *ops, size_t op_count, uint16_t flags) {
    Ny_Machine_Block *blk = ny_mfunc_get_block(fn, block_id);
    assert(blk != nullptr);

    Ny_Inst_ID inst_id = (Ny_Inst_ID)fn->inst_count;
    ny_buf_grow((void **)&fn->instructions, &fn->inst_capacity, fn->inst_count, sizeof(Ny_Machine_Instruction));
    fn->inst_count++;

    uint32_t op_start = mfunc_push_operands(fn, ops, op_count);

    Ny_Machine_Instruction *inst = &fn->instructions[inst_id];
    inst->id = inst_id;
    inst->opcode = (uint16_t)opcode;
    inst->flags = flags;
    inst->def_reg = def_reg;
    inst->op_start = op_start;
    inst->op_count = (uint16_t)op_count;
    inst->block = block_id;
    inst->prev = blk->last_inst;
    inst->next = NY_INVALID_INST;

    if (blk->first_inst == NY_INVALID_INST) {
        blk->first_inst = inst_id;
    } else {
        fn->instructions[blk->last_inst].next = inst_id;
    }
    blk->last_inst = inst_id;
    blk->inst_count++;

    return inst_id;
}

Ny_Inst_ID ny_mfunc_insert_before(Ny_Machine_Function *fn, Ny_Inst_ID before_inst_id, Ny_Machine_Opcode opcode, Ny_Machine_Reg def_reg, const Ny_Machine_Operand *ops, size_t op_count, uint16_t flags) {
    assert(before_inst_id < fn->inst_count);
    Ny_Block_ID block_id = fn->instructions[before_inst_id].block;
    Ny_Machine_Block *blk = ny_mfunc_get_block(fn, block_id);
    assert(blk != nullptr);

    Ny_Inst_ID inst_id = (Ny_Inst_ID)fn->inst_count;
    ny_buf_grow((void **)&fn->instructions, &fn->inst_capacity, fn->inst_count, sizeof(Ny_Machine_Instruction));
    fn->inst_count++;

    uint32_t op_start = mfunc_push_operands(fn, ops, op_count);

    Ny_Inst_ID prev_id = fn->instructions[before_inst_id].prev;

    Ny_Machine_Instruction *inst = &fn->instructions[inst_id];
    inst->id = inst_id;
    inst->opcode = (uint16_t)opcode;
    inst->flags = flags;
    inst->def_reg = def_reg;
    inst->op_start = op_start;
    inst->op_count = (uint16_t)op_count;
    inst->block = block_id;
    inst->prev = prev_id;
    inst->next = before_inst_id;

    fn->instructions[before_inst_id].prev = inst_id;
    if (prev_id != NY_INVALID_INST) {
        fn->instructions[prev_id].next = inst_id;
    } else {
        blk->first_inst = inst_id;
    }
    blk->inst_count++;

    return inst_id;
}

Ny_Inst_ID ny_mfunc_insert_after(Ny_Machine_Function *fn, Ny_Inst_ID after_inst_id, Ny_Machine_Opcode opcode, Ny_Machine_Reg def_reg, const Ny_Machine_Operand *ops, size_t op_count, uint16_t flags) {
    assert(after_inst_id < fn->inst_count);
    Ny_Block_ID block_id = fn->instructions[after_inst_id].block;
    Ny_Machine_Block *blk = ny_mfunc_get_block(fn, block_id);
    assert(blk != nullptr);

    Ny_Inst_ID inst_id = (Ny_Inst_ID)fn->inst_count;
    ny_buf_grow((void **)&fn->instructions, &fn->inst_capacity, fn->inst_count, sizeof(Ny_Machine_Instruction));
    fn->inst_count++;

    uint32_t op_start = mfunc_push_operands(fn, ops, op_count);

    Ny_Inst_ID next_id = fn->instructions[after_inst_id].next;

    Ny_Machine_Instruction *inst = &fn->instructions[inst_id];
    inst->id = inst_id;
    inst->opcode = (uint16_t)opcode;
    inst->flags = flags;
    inst->def_reg = def_reg;
    inst->op_start = op_start;
    inst->op_count = (uint16_t)op_count;
    inst->block = block_id;
    inst->prev = after_inst_id;
    inst->next = next_id;

    fn->instructions[after_inst_id].next = inst_id;
    if (next_id != NY_INVALID_INST) {
        fn->instructions[next_id].prev = inst_id;
    } else {
        blk->last_inst = inst_id;
    }
    blk->inst_count++;

    return inst_id;
}

Ny_Machine_Instruction *ny_mfunc_get_inst(const Ny_Machine_Function *fn, Ny_Inst_ID id) {
    if (id >= fn->inst_count) return nullptr;
    return &fn->instructions[id];
}

Ny_Machine_Operand *ny_mfunc_get_operands(const Ny_Machine_Function *fn, const Ny_Machine_Instruction *inst) {
    if (inst->op_count == 0) return nullptr;
    return &fn->operands[inst->op_start];
}

void ny_mmod_init(Ny_Machine_Module *mod, Ny_String name) {
    memset(mod, 0, sizeof(Ny_Machine_Module));
    mod->name = name;
}

void ny_mmod_destroy(Ny_Machine_Module *mod) {
    for (size_t i = 0; i < mod->function_count; i++) {
        ny_mfunc_destroy(&mod->functions[i]);
    }
    if (mod->functions) {
        ny_free(mod->functions, mod->function_capacity * sizeof(Ny_Machine_Function));
    }
    for (size_t i = 0; i < mod->global_count; i++) {
        if (mod->globals[i].data) {
            ny_free(mod->globals[i].data, mod->globals[i].data_size);
        }
    }
    if (mod->globals) {
        ny_free(mod->globals, mod->global_capacity * sizeof(Ny_Machine_Global));
    }
    memset(mod, 0, sizeof(Ny_Machine_Module));
}

Ny_Machine_Function *ny_mmod_create_function(Ny_Machine_Module *mod, Ny_String name, Ny_Type_ID ret_type, uint8_t call_conv) {
    Ny_Function_ID id = (Ny_Function_ID)mod->function_count;
    ny_buf_grow((void **)&mod->functions, &mod->function_capacity, mod->function_count, sizeof(Ny_Machine_Function));
    Ny_Machine_Function *fn = &mod->functions[mod->function_count++];
    ny_mfunc_init(fn, id, name, ret_type, call_conv);
    return fn;
}

Ny_Machine_Function *ny_mmod_get_function(const Ny_Machine_Module *mod, Ny_Function_ID id) {
    if (id >= mod->function_count) return nullptr;
    return &mod->functions[id];
}

void ny_mmod_add_global(Ny_Machine_Module *mod, Ny_String name, Ny_Global_Kind kind, uint32_t align, const void *data, size_t data_size) {
    ny_buf_grow((void **)&mod->globals, &mod->global_capacity, mod->global_count, sizeof(Ny_Machine_Global));
    Ny_Machine_Global *g = &mod->globals[mod->global_count++];
    memset(g, 0, sizeof(*g));
    g->name = name;
    g->kind = kind;
    g->align = align > 0 ? align : 1;
    g->data_size = data_size;
    if (data && data_size > 0) {
        g->data = (uint8_t *)ny_alloc(data_size);
        memcpy(g->data, data, data_size);
    }
}
