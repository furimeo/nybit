// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nybit/regalloc.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include "nybit/machine.h"
#include "nybit/ir.h"
#include "nybit/parser.h"
#include "nybit/opt.h"
#include <string.h>

void test_regalloc_arithmetic_no_spill(void) {
    Ny_Machine_Module mmod;
    ny_mmod_init(&mmod, ny_str("test_ra_mod"));

    Ny_Machine_Function *fn = ny_mmod_create_function(&mmod, ny_str("arith"), NY_TYPE_I64, NY_CC_DEFAULT);
    Ny_Machine_Reg v0 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Reg v1 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Reg v2 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Reg v3 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    ny_mfunc_add_param(fn, v0);
    ny_mfunc_add_param(fn, v1);

    Ny_Block_ID entry = ny_mfunc_create_block(fn, ny_str("entry"));
    Ny_Machine_Operand add_ops[2] = { ny_mop_reg(v0), ny_mop_reg(v1) };
    ny_mfunc_append_inst(fn, entry, NY_MOPC_ADD, v2, add_ops, 2, 0);

    Ny_Machine_Operand sub_ops[2] = { ny_mop_reg(v2), ny_mop_imm_int(5) };
    ny_mfunc_append_inst(fn, entry, NY_MOPC_SUB, v3, sub_ops, 2, 0);

    Ny_Machine_Operand ret_op = ny_mop_reg(v3);
    ny_mfunc_append_inst(fn, entry, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_op, 1, NY_MINST_FLAG_TERMINATOR);

    Ny_RegAlloc_Result res;
    bool ok = ny_regalloc_run(fn, NY_ABI_SYSV_AMD64, &res, nullptr);
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(res.spilled_count, 0);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_mfunc_validate_allocated(fn, &val_diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(val_diags.count, 0);
    ny_diagnostic_list_destroy(&val_diags);

    for (size_t i = 0; i < fn->inst_count; i++) {
        Ny_Machine_Instruction *inst = &fn->instructions[i];
        if (ny_mreg_is_valid(inst->def_reg)) {
            TEST_ASSERT(!inst->def_reg.is_virtual);
        }
        Ny_Machine_Operand *ops = ny_mfunc_get_operands(fn, inst);
        for (size_t j = 0; j < inst->op_count; j++) {
            if (ops[j].kind == NY_MOP_KIND_REG) {
                TEST_ASSERT(!ops[j].reg.is_virtual);
            }
        }
    }

    ny_mmod_destroy(&mmod);
}

void test_regalloc_high_pressure_and_spill(void) {
    Ny_Machine_Module mmod;
    ny_mmod_init(&mmod, ny_str("test_ra_spill_mod"));

    Ny_Machine_Function *fn = ny_mmod_create_function(&mmod, ny_str("pressure"), NY_TYPE_I64, NY_CC_DEFAULT);
    Ny_Block_ID entry = ny_mfunc_create_block(fn, ny_str("entry"));

    const size_t NUM_VALUES = 20;
    Ny_Machine_Reg vregs[20];
    for (size_t i = 0; i < NUM_VALUES; i++) {
        vregs[i] = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
        Ny_Machine_Operand imm_op = ny_mop_imm_int((int64_t)(i + 1));
        ny_mfunc_append_inst(fn, entry, NY_MOPC_COPY, vregs[i], &imm_op, 1, 0);
    }

    Ny_Machine_Reg sum = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Operand first_ops[2] = { ny_mop_reg(vregs[0]), ny_mop_reg(vregs[1]) };
    ny_mfunc_append_inst(fn, entry, NY_MOPC_ADD, sum, first_ops, 2, 0);

    for (size_t i = 2; i < NUM_VALUES; i++) {
        Ny_Machine_Operand add_ops[2] = { ny_mop_reg(sum), ny_mop_reg(vregs[i]) };
        ny_mfunc_append_inst(fn, entry, NY_MOPC_ADD, sum, add_ops, 2, 0);
    }

    Ny_Machine_Operand ret_op = ny_mop_reg(sum);
    ny_mfunc_append_inst(fn, entry, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_op, 1, NY_MINST_FLAG_TERMINATOR);

    Ny_RegAlloc_Result res;
    bool ok = ny_regalloc_run(fn, NY_ABI_SYSV_AMD64, &res, nullptr);
    TEST_ASSERT(ok);
    TEST_ASSERT(res.spilled_count > 0);
    TEST_ASSERT(fn->stack_slot_count >= res.spilled_count);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_mfunc_validate_allocated(fn, &val_diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(val_diags.count, 0);
    ny_diagnostic_list_destroy(&val_diags);

    ny_mmod_destroy(&mmod);
}

void test_regalloc_loops(void) {
    Ny_Machine_Module mmod;
    ny_mmod_init(&mmod, ny_str("test_ra_loop_mod"));

    Ny_Machine_Function *fn = ny_mmod_create_function(&mmod, ny_str("loop_func"), NY_TYPE_I64, NY_CC_DEFAULT);
    Ny_Block_ID b_entry = ny_mfunc_create_block(fn, ny_str("entry"));
    Ny_Block_ID b_loop  = ny_mfunc_create_block(fn, ny_str("loop"));
    Ny_Block_ID b_exit  = ny_mfunc_create_block(fn, ny_str("exit"));

    ny_mblock_add_edge(&fn->blocks[b_entry], &fn->blocks[b_loop]);
    ny_mblock_add_edge(&fn->blocks[b_loop], &fn->blocks[b_loop]);
    ny_mblock_add_edge(&fn->blocks[b_loop], &fn->blocks[b_exit]);

    Ny_Machine_Reg i_var = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Reg sum_var = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);

    Ny_Machine_Operand zero = ny_mop_imm_int(0);
    ny_mfunc_append_inst(fn, b_entry, NY_MOPC_COPY, i_var, &zero, 1, 0);
    ny_mfunc_append_inst(fn, b_entry, NY_MOPC_COPY, sum_var, &zero, 1, 0);
    Ny_Machine_Operand jmp_loop = ny_mop_block(b_loop);
    ny_mfunc_append_inst(fn, b_entry, NY_MOPC_JMP, (Ny_Machine_Reg){0}, &jmp_loop, 1, NY_MINST_FLAG_TERMINATOR | NY_MINST_FLAG_BRANCH);

    Ny_Machine_Operand add_sum_ops[2] = { ny_mop_reg(sum_var), ny_mop_reg(i_var) };
    ny_mfunc_append_inst(fn, b_loop, NY_MOPC_ADD, sum_var, add_sum_ops, 2, 0);

    Ny_Machine_Operand add_i_ops[2] = { ny_mop_reg(i_var), ny_mop_imm_int(1) };
    ny_mfunc_append_inst(fn, b_loop, NY_MOPC_ADD, i_var, add_i_ops, 2, 0);

    Ny_Machine_Reg cmp_res = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Operand cmp_ops[3] = { ny_mop_reg(i_var), ny_mop_imm_int(100), ny_mop_cond(NY_MCOND_LT_S) };
    ny_mfunc_append_inst(fn, b_loop, NY_MOPC_CMP, cmp_res, cmp_ops, 3, 0);

    Ny_Machine_Operand jcc_ops[2] = { ny_mop_cond(NY_MCOND_LT_S), ny_mop_block(b_loop) };
    ny_mfunc_append_inst(fn, b_loop, NY_MOPC_JCC, (Ny_Machine_Reg){0}, jcc_ops, 2, NY_MINST_FLAG_BRANCH);

    Ny_Machine_Operand jmp_exit = ny_mop_block(b_exit);
    ny_mfunc_append_inst(fn, b_loop, NY_MOPC_JMP, (Ny_Machine_Reg){0}, &jmp_exit, 1, NY_MINST_FLAG_TERMINATOR | NY_MINST_FLAG_BRANCH);

    Ny_Machine_Operand ret_op = ny_mop_reg(sum_var);
    ny_mfunc_append_inst(fn, b_exit, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_op, 1, NY_MINST_FLAG_TERMINATOR);

    Ny_RegAlloc_Result res;
    bool ok = ny_regalloc_run(fn, NY_ABI_SYSV_AMD64, &res, nullptr);
    TEST_ASSERT(ok);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_mfunc_validate_allocated(fn, &val_diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(val_diags.count, 0);
    ny_diagnostic_list_destroy(&val_diags);

    ny_mmod_destroy(&mmod);
}

void test_regalloc_calls_and_callee_saved(void) {
    Ny_Machine_Module mmod;
    ny_mmod_init(&mmod, ny_str("test_ra_call_mod"));

    Ny_Machine_Function *fn = ny_mmod_create_function(&mmod, ny_str("caller"), NY_TYPE_I64, NY_CC_DEFAULT);
    Ny_Block_ID entry = ny_mfunc_create_block(fn, ny_str("entry"));

    Ny_Machine_Reg live_across = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Operand imm42 = ny_mop_imm_int(42);
    ny_mfunc_append_inst(fn, entry, NY_MOPC_COPY, live_across, &imm42, 1, 0);

    Ny_Machine_Reg call_res = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Operand callee_sym = ny_mop_symbol(ny_str("helper"), 1);
    ny_mfunc_append_inst(fn, entry, NY_MOPC_CALL, call_res, &callee_sym, 1, NY_MINST_FLAG_CALL);

    Ny_Machine_Reg total = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Operand add_ops[2] = { ny_mop_reg(live_across), ny_mop_reg(call_res) };
    ny_mfunc_append_inst(fn, entry, NY_MOPC_ADD, total, add_ops, 2, 0);

    Ny_Machine_Operand ret_op = ny_mop_reg(total);
    ny_mfunc_append_inst(fn, entry, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_op, 1, NY_MINST_FLAG_TERMINATOR);

    Ny_RegAlloc_Result res;
    bool ok = ny_regalloc_run(fn, NY_ABI_SYSV_AMD64, &res, nullptr);
    TEST_ASSERT(ok);

    TEST_ASSERT(res.used_callee_saved_mask != 0 || res.spilled_count > 0);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    bool valid = ny_mfunc_validate_allocated(fn, &val_diags);
    TEST_ASSERT(valid);
    TEST_ASSERT_EQ(val_diags.count, 0);
    ny_diagnostic_list_destroy(&val_diags);

    ny_mmod_destroy(&mmod);
}

void test_regalloc_abi_sysv_vs_win64(void) {
    Ny_Machine_Module mmod_sysv;
    ny_mmod_init(&mmod_sysv, ny_str("sysv_mod"));
    Ny_Machine_Function *fn_sysv = ny_mmod_create_function(&mmod_sysv, ny_str("f_sysv"), NY_TYPE_I64, NY_CC_DEFAULT);
    Ny_Block_ID entry_sysv = ny_mfunc_create_block(fn_sysv, ny_str("entry"));
    Ny_Machine_Reg params_sysv[6];
    for (size_t i = 0; i < 6; i++) {
        params_sysv[i] = ny_mfunc_create_vreg(fn_sysv, NY_REG_CLASS_GPR64);
        ny_mfunc_add_param(fn_sysv, params_sysv[i]);
    }
    Ny_Machine_Operand ret_sysv = ny_mop_reg(params_sysv[0]);
    ny_mfunc_append_inst(fn_sysv, entry_sysv, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_sysv, 1, NY_MINST_FLAG_TERMINATOR);

    Ny_RegAlloc_Result res_sysv;
    bool ok_sysv = ny_regalloc_run(fn_sysv, NY_ABI_SYSV_AMD64, &res_sysv, nullptr);
    TEST_ASSERT(ok_sysv);
    TEST_ASSERT(ny_mfunc_validate_allocated(fn_sysv, nullptr));
    ny_mmod_destroy(&mmod_sysv);

    Ny_Machine_Module mmod_win;
    ny_mmod_init(&mmod_win, ny_str("win_mod"));
    Ny_Machine_Function *fn_win = ny_mmod_create_function(&mmod_win, ny_str("f_win"), NY_TYPE_I64, NY_CC_DEFAULT);
    Ny_Block_ID entry_win = ny_mfunc_create_block(fn_win, ny_str("entry"));
    Ny_Machine_Reg params_win[4];
    for (size_t i = 0; i < 4; i++) {
        params_win[i] = ny_mfunc_create_vreg(fn_win, NY_REG_CLASS_GPR64);
        ny_mfunc_add_param(fn_win, params_win[i]);
    }
    Ny_Machine_Operand ret_win = ny_mop_reg(params_win[0]);
    ny_mfunc_append_inst(fn_win, entry_win, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_win, 1, NY_MINST_FLAG_TERMINATOR);

    Ny_RegAlloc_Result res_win;
    bool ok_win = ny_regalloc_run(fn_win, NY_ABI_WINDOWS_X64, &res_win, nullptr);
    TEST_ASSERT(ok_win);
    TEST_ASSERT(ny_mfunc_validate_allocated(fn_win, nullptr));
    ny_mmod_destroy(&mmod_win);
}

void test_regalloc_validation_pass(void) {
    Ny_Machine_Module mmod;
    ny_mmod_init(&mmod, ny_str("test_val_mod"));

    Ny_Machine_Function *fn = ny_mmod_create_function(&mmod, ny_str("unalloc"), NY_TYPE_I64, NY_CC_DEFAULT);
    Ny_Block_ID entry = ny_mfunc_create_block(fn, ny_str("entry"));
    Ny_Machine_Reg v0 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);

    Ny_Machine_Operand op = ny_mop_imm_int(123);
    ny_mfunc_append_inst(fn, entry, NY_MOPC_COPY, v0, &op, 1, 0);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool valid_before = ny_mfunc_validate_allocated(fn, &diags);
    TEST_ASSERT(!valid_before);
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    bool ra_ok = ny_regalloc_run(fn, NY_ABI_SYSV_AMD64, nullptr, nullptr);
    TEST_ASSERT(ra_ok);

    ny_diagnostic_list_init(&diags);
    bool valid_after = ny_mfunc_validate_allocated(fn, &diags);
    TEST_ASSERT(valid_after);
    TEST_ASSERT_EQ(diags.count, 0);
    ny_diagnostic_list_destroy(&diags);

    ny_mmod_destroy(&mmod);
}

void test_regalloc_e2e_pure_asm(void) {
    const char *src =
        "@function factorial(%n: i32) -> i32;\n"
        ".entry;\n"
        "    %zero = const 1;\n"
        "    %cond = cmp.le.s %n, %zero;\n"
        "    @branch_if %cond, .base, .recurse;\n"
        ".base;\n"
        "    %one = const 1;\n"
        "    @return %one;\n"
        ".recurse;\n"
        "    %c1 = const 1;\n"
        "    %sub = sub %n, %c1;\n"
        "    %rec = call @factorial, %sub;\n"
        "    %ans = mul %n, %rec;\n"
        "    @return %ans;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_fact");

    Ny_Parser p;
    ny_parser_init(&p, &ctx.module, src, strlen(src), &ctx.arena);
    bool parse_ok = ny_parse_module(&p);
    TEST_ASSERT(parse_ok);

    bool opt_ok = ny_opt_run_module_pipeline(&ctx.module, NY_OPT_O2);
    (void)opt_ok;

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &diags);
    TEST_ASSERT(mir_ok);
    TEST_ASSERT_EQ(diags.count, 0);

    for (size_t i = 0; i < mmod.function_count; i++) {
        Ny_RegAlloc_Result res;
        bool ra_ok = ny_regalloc_run(&mmod.functions[i], NY_ABI_SYSV_AMD64, &res, &diags);
        TEST_ASSERT(ra_ok);
        TEST_ASSERT_EQ(diags.count, 0);

        bool val_ok = ny_mfunc_validate_allocated(&mmod.functions[i], &diags);
        TEST_ASSERT(val_ok);
        TEST_ASSERT_EQ(diags.count, 0);
    }

    X86_Module xmod;
    bool x86_ok = x86_lower_machine_mod(&g_ny_target_x86_64_sysv, &mmod, &xmod, &diags);
    TEST_ASSERT(x86_ok);
    TEST_ASSERT_EQ(diags.count, 0);

    Ny_Arena scratch;
    ny_arena_init(&scratch, 4096);
    char *asm_text = x86_dump_mod(&xmod, &scratch);

    TEST_ASSERT(strstr(asm_text, "%v") == nullptr);
    TEST_ASSERT(strstr(asm_text, ".globl factorial") != nullptr);
    TEST_ASSERT(strstr(asm_text, "push    rbp") != nullptr);
    TEST_ASSERT(strstr(asm_text, "call") != nullptr);
    TEST_ASSERT(strstr(asm_text, "ret") != nullptr);

    ny_arena_destroy(&scratch);
    x86_mod_destroy(&xmod);
    ny_diagnostic_list_destroy(&diags);
    ny_mmod_destroy(&mmod);
    ny_parser_destroy(&p);
    ny_context_destroy(&ctx);
}
