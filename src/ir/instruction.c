// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/ir.h"

const char *ny_opcode_name(Ny_Opcode op) {
    switch (op) {
    case NY_OPCODE_NONE:           return "none";
    case NY_OPCODE_CONST:          return "const";
    case NY_OPCODE_CONST_NULL:     return "const_null";
    case NY_OPCODE_ADD:            return "add";
    case NY_OPCODE_SUB:            return "sub";
    case NY_OPCODE_MUL:            return "mul";
    case NY_OPCODE_DIV_S:          return "div.s";
    case NY_OPCODE_DIV_U:          return "div.u";
    case NY_OPCODE_REM_S:          return "rem.s";
    case NY_OPCODE_REM_U:          return "rem.u";
    case NY_OPCODE_NEG:            return "neg";
    case NY_OPCODE_FADD:           return "fadd";
    case NY_OPCODE_FSUB:           return "fsub";
    case NY_OPCODE_FMUL:           return "fmul";
    case NY_OPCODE_FDIV:           return "fdiv";
    case NY_OPCODE_FREM:           return "frem";
    case NY_OPCODE_FNEG:           return "fneg";
    case NY_OPCODE_AND:            return "and";
    case NY_OPCODE_OR:             return "or";
    case NY_OPCODE_XOR:            return "xor";
    case NY_OPCODE_NOT:            return "not";
    case NY_OPCODE_SHL:            return "shl";
    case NY_OPCODE_SHR:            return "shr";
    case NY_OPCODE_SAR:            return "sar";
    case NY_OPCODE_ROTL:           return "rotl";
    case NY_OPCODE_ROTR:           return "rotr";
    case NY_OPCODE_CMP_EQ:         return "cmp.eq";
    case NY_OPCODE_CMP_NE:         return "cmp.ne";
    case NY_OPCODE_CMP_LT_S:       return "cmp.lt.s";
    case NY_OPCODE_CMP_LT_U:       return "cmp.lt.u";
    case NY_OPCODE_CMP_LE_S:       return "cmp.le.s";
    case NY_OPCODE_CMP_LE_U:       return "cmp.le.u";
    case NY_OPCODE_CMP_GT_S:       return "cmp.gt.s";
    case NY_OPCODE_CMP_GT_U:       return "cmp.gt.u";
    case NY_OPCODE_CMP_GE_S:       return "cmp.ge.s";
    case NY_OPCODE_CMP_GE_U:       return "cmp.ge.u";
    case NY_OPCODE_FCMP_EQ:        return "fcmp.eq";
    case NY_OPCODE_FCMP_NE:        return "fcmp.ne";
    case NY_OPCODE_FCMP_LT:        return "fcmp.lt";
    case NY_OPCODE_FCMP_LE:        return "fcmp.le";
    case NY_OPCODE_FCMP_GT:        return "fcmp.gt";
    case NY_OPCODE_FCMP_GE:        return "fcmp.ge";
    case NY_OPCODE_SELECT:         return "select";
    case NY_OPCODE_ADDR:           return "addr";
    case NY_OPCODE_ADDR_OFFSET:    return "addr_offset";
    case NY_OPCODE_GLOBAL_ADDR:    return "global_addr";
    case NY_OPCODE_LOAD:           return "load";
    case NY_OPCODE_STORE:          return "store";
    case NY_OPCODE_STACK_SLOT:     return "stack_slot";
    case NY_OPCODE_STACK_ADDR:     return "stack_addr";
    case NY_OPCODE_ATOMIC_LOAD:    return "atomic_load";
    case NY_OPCODE_ATOMIC_STORE:   return "atomic_store";
    case NY_OPCODE_ATOMIC_RMW:     return "atomic_rmw";
    case NY_OPCODE_ATOMIC_CMPXCHG: return "atomic_cmpxchg";
    case NY_OPCODE_FENCE:          return "fence";
    case NY_OPCODE_CAST:           return "cast";
    case NY_OPCODE_EXTEND:         return "extend";
    case NY_OPCODE_TRUNCATE:       return "truncate";
    case NY_OPCODE_BITCAST:        return "bitcast";
    case NY_OPCODE_SEXT:           return "sext";
    case NY_OPCODE_ZEXT:           return "zext";
    case NY_OPCODE_FEXT:           return "fext";
    case NY_OPCODE_FTRUNC:         return "ftrunc";
    case NY_OPCODE_SITOFP:         return "sitofp";
    case NY_OPCODE_UITOFP:         return "uitofp";
    case NY_OPCODE_FPTOSI:         return "fptosi";
    case NY_OPCODE_FPTOUI:         return "fptoui";
    case NY_OPCODE_VADD:           return "vadd";
    case NY_OPCODE_VSUB:           return "vsub";
    case NY_OPCODE_VMUL:           return "vmul";
    case NY_OPCODE_VDIV:           return "vdiv";
    case NY_OPCODE_VAND:           return "vand";
    case NY_OPCODE_VOR:            return "vor";
    case NY_OPCODE_VXOR:           return "vxor";
    case NY_OPCODE_VSHUFFLE:       return "vshuffle";
    case NY_OPCODE_BRANCH:         return "@branch";
    case NY_OPCODE_BRANCH_IF:      return "@branch_if";
    case NY_OPCODE_SWITCH:         return "@switch";
    case NY_OPCODE_RETURN:         return "@return";
    case NY_OPCODE_TRAP:           return "@trap";
    case NY_OPCODE_CALL:           return "call";
    case NY_OPCODE_CALL_INDIRECT:  return "call_indirect";
    case NY_OPCODE_PHI:            return "phi";
    case NY_OPCODE_COUNT:          return "unknown";
    }
    return "unknown";
}

bool ny_opcode_is_terminator(Ny_Opcode op) {
    switch (op) {
    case NY_OPCODE_BRANCH:
    case NY_OPCODE_BRANCH_IF:
    case NY_OPCODE_SWITCH:
    case NY_OPCODE_RETURN:
    case NY_OPCODE_TRAP:
        return true;
    default:
        return false;
    }
}
