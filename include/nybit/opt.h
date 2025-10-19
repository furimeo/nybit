// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_OPT_H
#define NYBIT_OPT_H

#include <nybit/analysis.h>
#include <nybit/ir.h>
#include <nybit/support.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NY_OPT_O0 = 0,
    NY_OPT_O1,
    NY_OPT_O2,
} Ny_Opt_Level;

uint32_t ny_opt_get_type_bit_width(Ny_Type_ID type_id);
int64_t ny_opt_mask_to_width(int64_t val, uint32_t width);
int64_t ny_opt_sign_extend_width(int64_t val, uint32_t width);
bool ny_opt_get_operand_const_int(const Ny_Function *fn, Ny_Operand op, int64_t *out_val);
bool ny_opt_get_operand_const_float(const Ny_Function *fn, Ny_Operand op, double *out_val);
bool ny_opt_is_commutative(Ny_Opcode op);

bool ny_fold_int_unary(Ny_Opcode op, int64_t a, uint32_t width, int64_t *out_val);
bool ny_fold_int_binary(Ny_Opcode op, int64_t a, int64_t b, uint32_t width, int64_t *out_val);
bool ny_fold_int_cmp(Ny_Opcode op, int64_t a, int64_t b, uint32_t width, int64_t *out_val);
bool ny_fold_float_unary(Ny_Opcode op, double a, double *out_val);
bool ny_fold_float_binary(Ny_Opcode op, double a, double b, double *out_val);
bool ny_fold_float_cmp(Ny_Opcode op, double a, double b, int64_t *out_val);

bool ny_canonicalize_instructions(Ny_Module *mod, Ny_Function *fn);
bool ny_canonicalize_cfg(Ny_Module *mod, Ny_Function *fn);
bool ny_canonicalize_function(Ny_Module *mod, Ny_Function *fn);
bool ny_canonicalize_module(Ny_Module *mod);

bool ny_opt_pass_const_fold(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am);
bool ny_opt_pass_sccp(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am);
bool ny_opt_pass_dce(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am);
bool ny_opt_pass_copy_prop(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am);
bool ny_opt_pass_gvn(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am);
bool ny_opt_pass_cfg_simplify(Ny_Module *mod, Ny_Function *fn, Ny_Analysis_Manager *am);

bool ny_opt_run_pipeline(Ny_Module *mod, Ny_Function *fn, Ny_Opt_Level level);
bool ny_opt_run_module_pipeline(Ny_Module *mod, Ny_Opt_Level level);

#ifdef __cplusplus
}
#endif

#endif
