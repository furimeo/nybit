// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include "nybit/machine.h"
#include "nybit/ir.h"
#include "nybit/opt.h"
#include "../src/target/x86_64/x86_internal.h"
#include <string.h>

void test_x86_register_and_abi_properties(void) {
    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_RAX, 8)), "rax");
    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_RAX, 4)), "eax");
    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_RAX, 2)), "ax");
    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_RAX, 1)), "al");

    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_R8, 8)), "r8");
    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_R8, 4)), "r8d");
    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_R8, 2)), "r8w");
    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_R8, 1)), "r8b");

    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_RSP, 8)), "rsp");
    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_phys(X86_RSP, 4)), "esp");

    TEST_ASSERT_STR_EQ(x86_reg_name(x86_reg_virt(5, 8)), "%v5");

    size_t sysv_count = 0;
    const X86_Phys_Reg *sysv_args = x86_abi_arg_regs(NY_ABI_SYSV_AMD64, &sysv_count);
    TEST_ASSERT_EQ(sysv_count, 6);
    TEST_ASSERT_EQ(sysv_args[0], X86_RDI);
    TEST_ASSERT_EQ(sysv_args[1], X86_RSI);
    TEST_ASSERT_EQ(sysv_args[2], X86_RDX);
    TEST_ASSERT_EQ(sysv_args[3], X86_RCX);
    TEST_ASSERT_EQ(sysv_args[4], X86_R8);
    TEST_ASSERT_EQ(sysv_args[5], X86_R9);

    size_t win_count = 0;
    const X86_Phys_Reg *win_args = x86_abi_arg_regs(NY_ABI_WINDOWS_X64, &win_count);
    TEST_ASSERT_EQ(win_count, 4);
    TEST_ASSERT_EQ(win_args[0], X86_RCX);
    TEST_ASSERT_EQ(win_args[1], X86_RDX);
    TEST_ASSERT_EQ(win_args[2], X86_R8);
    TEST_ASSERT_EQ(win_args[3], X86_R9);

    TEST_ASSERT_EQ(x86_abi_ret_reg(NY_ABI_SYSV_AMD64, 8, false), X86_RAX);
    TEST_ASSERT_EQ(x86_abi_ret_reg(NY_ABI_WINDOWS_X64, 4, false), X86_RAX);
    TEST_ASSERT_EQ(x86_abi_ret_reg(NY_ABI_SYSV_AMD64, 8, true), X86_XMM0);
    TEST_ASSERT_EQ(x86_abi_ret_reg(NY_ABI_WINDOWS_X64, 4, true), X86_XMM0);

    size_t sysv_fp_count = 0;
    const X86_Phys_Reg *sysv_fp_args = x86_abi_fp_arg_regs(NY_ABI_SYSV_AMD64, &sysv_fp_count);
    TEST_ASSERT_EQ(sysv_fp_count, 8);
    TEST_ASSERT_EQ(sysv_fp_args[0], X86_XMM0);
    TEST_ASSERT_EQ(sysv_fp_args[7], X86_XMM7);

    size_t win_fp_count = 0;
    const X86_Phys_Reg *win_fp_args = x86_abi_fp_arg_regs(NY_ABI_WINDOWS_X64, &win_fp_count);
    TEST_ASSERT_EQ(win_fp_count, 4);
    TEST_ASSERT_EQ(win_fp_args[0], X86_XMM0);
    TEST_ASSERT_EQ(win_fp_args[3], X86_XMM3);

    TEST_ASSERT(x86_abi_is_callee_saved(NY_ABI_SYSV_AMD64, X86_RBX));
    TEST_ASSERT(x86_abi_is_callee_saved(NY_ABI_SYSV_AMD64, X86_RBP));
    TEST_ASSERT(x86_abi_is_callee_saved(NY_ABI_SYSV_AMD64, X86_R12));
    TEST_ASSERT(x86_abi_is_callee_saved(NY_ABI_SYSV_AMD64, X86_R15));
    TEST_ASSERT(!x86_abi_is_callee_saved(NY_ABI_SYSV_AMD64, X86_RAX));
    TEST_ASSERT(!x86_abi_is_callee_saved(NY_ABI_SYSV_AMD64, X86_RDI));

    TEST_ASSERT(x86_abi_is_callee_saved(NY_ABI_WINDOWS_X64, X86_RSI));
    TEST_ASSERT(x86_abi_is_callee_saved(NY_ABI_WINDOWS_X64, X86_RDI));
    TEST_ASSERT(!x86_abi_is_callee_saved(NY_ABI_WINDOWS_X64, X86_R10));

    const Ny_Target *t_sysv = ny_target_find("x86_64-sysv");
    TEST_ASSERT(t_sysv != nullptr);
    TEST_ASSERT_EQ(t_sysv->arch, NY_ARCH_X86_64);
    TEST_ASSERT_EQ(t_sysv->abi, NY_ABI_SYSV_AMD64);
    TEST_ASSERT_EQ(t_sysv->info.shadow_space, 0);

    const Ny_Target *t_win = ny_target_find("x86_64-windows");
    TEST_ASSERT(t_win != nullptr);
    TEST_ASSERT_EQ(t_win->arch, NY_ARCH_X86_64);
    TEST_ASSERT_EQ(t_win->abi, NY_ABI_WINDOWS_X64);
    TEST_ASSERT_EQ(t_win->info.shadow_space, 32);

    TEST_ASSERT(ny_target_find("arm64") == nullptr);
    TEST_ASSERT(ny_target_get_default() != nullptr);
}

void test_x86_stack_frame_and_addressing(void) {
    Ny_Machine_Function mfn;
    memset(&mfn, 0, sizeof(mfn));
    mfn.entry_block = NY_INVALID_BLOCK;

    ny_mfunc_create_stack_slot(&mfn, 4, 4);
    ny_mfunc_create_stack_slot(&mfn, 8, 8);

    X86_Stack_Frame frame;
    x86_frame_layout(&frame, &mfn, NY_ABI_SYSV_AMD64);

    TEST_ASSERT_EQ(frame.slot_count, 2);
    TEST_ASSERT_EQ(frame.slot_offsets[0], -4);
    TEST_ASSERT_EQ(frame.slot_offsets[1], -16);
    TEST_ASSERT_EQ(frame.stack_size, 24);

    X86_Block blk;
    memset(&blk, 0, sizeof(blk));
    x86_emit_prologue(&blk, &frame);
    x86_emit_epilogue(&blk, &frame);

    TEST_ASSERT_EQ(blk.inst_count, 5);
    TEST_ASSERT_EQ(blk.instructions[0].opcode, X86_OPC_PUSH);
    TEST_ASSERT_EQ(blk.instructions[1].opcode, X86_OPC_MOV);
    TEST_ASSERT_EQ(blk.instructions[2].opcode, X86_OPC_SUB);
    TEST_ASSERT_EQ(blk.instructions[3].opcode, X86_OPC_MOV);
    TEST_ASSERT_EQ(blk.instructions[4].opcode, X86_OPC_POP);

    if (blk.instructions) {
        ny_free(blk.instructions, blk.inst_capacity * sizeof(X86_Instruction));
    }
    if (frame.slot_offsets) {
        ny_free(frame.slot_offsets, frame.slot_capacity * sizeof(int32_t));
    }
    ny_mfunc_destroy(&mfn);
}

void test_x86_instruction_selection(void) {
    Ny_Machine_Module mmod;
    ny_mmod_init(&mmod, ny_str("test_isel_mod"));

    Ny_Machine_Function *fn = ny_mmod_create_function(&mmod, ny_str("branch_func"), NY_TYPE_I64, NY_CC_DEFAULT);
    Ny_Machine_Reg v0 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Reg v1 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Reg v2 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    Ny_Machine_Reg v3 = ny_mfunc_create_vreg(fn, NY_REG_CLASS_GPR64);
    ny_mfunc_add_param(fn, v0);
    ny_mfunc_add_param(fn, v1);

    Ny_Block_ID b_entry = ny_mfunc_create_block(fn, ny_str("entry"));
    Ny_Block_ID b_then  = ny_mfunc_create_block(fn, ny_str("then"));
    Ny_Block_ID b_else  = ny_mfunc_create_block(fn, ny_str("else"));

    Ny_Machine_Operand add_ops[2] = { ny_mop_reg(v0), ny_mop_reg(v1) };
    ny_mfunc_append_inst(fn, b_entry, NY_MOPC_ADD, v2, add_ops, 2, 0);

    Ny_Machine_Operand cmp_ops[3] = { ny_mop_reg(v2), ny_mop_imm_int(0), ny_mop_cond(NY_MCOND_EQ) };
    ny_mfunc_append_inst(fn, b_entry, NY_MOPC_CMP, v3, cmp_ops, 3, 0);

    Ny_Machine_Operand jcc_ops[2] = { ny_mop_cond(NY_MCOND_EQ), ny_mop_block(b_then) };
    ny_mfunc_append_inst(fn, b_entry, NY_MOPC_JCC, (Ny_Machine_Reg){0}, jcc_ops, 2, NY_MINST_FLAG_BRANCH);

    Ny_Machine_Operand jmp_op = ny_mop_block(b_else);
    ny_mfunc_append_inst(fn, b_entry, NY_MOPC_JMP, (Ny_Machine_Reg){0}, &jmp_op, 1, NY_MINST_FLAG_TERMINATOR | NY_MINST_FLAG_BRANCH);

    Ny_Machine_Operand ret_v2 = ny_mop_reg(v2);
    ny_mfunc_append_inst(fn, b_then, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_v2, 1, NY_MINST_FLAG_TERMINATOR);

    Ny_Machine_Operand ret_v0 = ny_mop_reg(v0);
    ny_mfunc_append_inst(fn, b_else, NY_MOPC_RET, (Ny_Machine_Reg){0}, &ret_v0, 1, NY_MINST_FLAG_TERMINATOR);

    void *target_fn = nullptr;
    bool ok = g_ny_target_x86_64_sysv.lower_function(&g_ny_target_x86_64_sysv, fn, &target_fn, nullptr);
    TEST_ASSERT(ok);
    TEST_ASSERT(target_fn != nullptr);

    X86_Function *xfn = (X86_Function *)target_fn;
    TEST_ASSERT_EQ(xfn->block_count, 3);
    TEST_ASSERT_STR_EQ(xfn->blocks[0].name.data, "entry");
    TEST_ASSERT_STR_EQ(xfn->blocks[1].name.data, "then");
    TEST_ASSERT_STR_EQ(xfn->blocks[2].name.data, "else");

    Ny_Arena scratch;
    ny_arena_init(&scratch, 4096);
    char *asm_text = g_ny_target_x86_64_sysv.dump_function(target_fn, &scratch);
    TEST_ASSERT(strstr(asm_text, ".globl branch_func") != nullptr);
    TEST_ASSERT(strstr(asm_text, "push    rbp") != nullptr);
    TEST_ASSERT(strstr(asm_text, "add") != nullptr);
    TEST_ASSERT(strstr(asm_text, "cmp") != nullptr);
    TEST_ASSERT(strstr(asm_text, "je") != nullptr);
    TEST_ASSERT(strstr(asm_text, "jmp") != nullptr);
    TEST_ASSERT(strstr(asm_text, "ret") != nullptr);

    ny_arena_destroy(&scratch);
    g_ny_target_x86_64_sysv.destroy_function(target_fn);
    ny_mmod_destroy(&mmod);
}

void test_x86_e2e_pipeline(void) {
    Ny_Context ctx;
    ny_context_init(&ctx, "test_x86_e2e");

    Ny_Builder b;
    ny_builder_init(&b, &ctx.module);

    Ny_Function_ID fn_id = ny_builder_add_function(&b, "compute", NY_TYPE_I64);
    Ny_Value_ID a = ny_builder_add_param(&b, "a", NY_TYPE_I64);

    Ny_Block_ID entry = ny_builder_add_block(&b, "entry");
    ny_builder_set_insert_point(&b, fn_id, entry);

    Ny_Value_ID c10 = ny_builder_const_i64(&b, 10, NY_TYPE_I64, "c10");
    Ny_Value_ID c20 = ny_builder_const_i64(&b, 20, NY_TYPE_I64, "c20");
    Ny_Value_ID folded = ny_builder_add(&b, c10, c20, NY_TYPE_I64, "folded");
    Ny_Value_ID res = ny_builder_add(&b, a, folded, NY_TYPE_I64, "res");
    ny_builder_ret(&b, res);

    bool opt_ok = ny_opt_run_module_pipeline(&ctx.module, NY_OPT_O2);
    TEST_ASSERT(opt_ok);

    Ny_Machine_Module mmod;
    bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, nullptr);
    TEST_ASSERT(mir_ok);

    X86_Module xmod;
    bool x86_ok = x86_lower_machine_mod(&g_ny_target_x86_64_sysv, &mmod, &xmod, nullptr);
    TEST_ASSERT(x86_ok);

    Ny_Arena scratch;
    ny_arena_init(&scratch, 4096);
    char *asm_text = x86_dump_mod(&xmod, &scratch);

    TEST_ASSERT(strstr(asm_text, ".intel_syntax noprefix") != nullptr);
    TEST_ASSERT(strstr(asm_text, ".globl compute") != nullptr);
    TEST_ASSERT(strstr(asm_text, "push    rbp") != nullptr);
    TEST_ASSERT(strstr(asm_text, "add") != nullptr);
    TEST_ASSERT(strstr(asm_text, "ret") != nullptr);

    ny_arena_destroy(&scratch);
    x86_mod_destroy(&xmod);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
}

void test_aggregate_type_layout(void) {
    Ny_Type_Table tt;
    ny_type_table_init(&tt);

    /* struct Point { int32_t x; int32_t y; } -> size 8, align 4 */
    Ny_Type_ID f_point[2] = { NY_TYPE_I32, NY_TYPE_I32 };
    Ny_Type_ID t_point = ny_type_table_add_struct(&tt, "Point", f_point, 2);
    TEST_ASSERT(ny_type_is_aggregate(&tt, t_point));
    TEST_ASSERT_EQ(ny_type_size(&tt, t_point), 8);
    TEST_ASSERT_EQ(ny_type_align(&tt, t_point), 4);
    TEST_ASSERT_EQ(ny_type_struct_field_offset(&tt, t_point, 0), 0);
    TEST_ASSERT_EQ(ny_type_struct_field_offset(&tt, t_point, 1), 4);
    TEST_ASSERT_EQ(ny_type_struct_field_type(&tt, t_point, 0), NY_TYPE_I32);
    TEST_ASSERT_EQ(ny_type_struct_field_type(&tt, t_point, 1), NY_TYPE_I32);

    /* struct Mixed { int8_t a; int64_t b; int32_t c; } -> size 24, align 8 */
    Ny_Type_ID f_mixed[3] = { NY_TYPE_I8, NY_TYPE_I64, NY_TYPE_I32 };
    Ny_Type_ID t_mixed = ny_type_table_add_struct(&tt, "Mixed", f_mixed, 3);
    TEST_ASSERT(ny_type_is_aggregate(&tt, t_mixed));
    TEST_ASSERT_EQ(ny_type_size(&tt, t_mixed), 24);
    TEST_ASSERT_EQ(ny_type_align(&tt, t_mixed), 8);
    TEST_ASSERT_EQ(ny_type_struct_field_offset(&tt, t_mixed, 0), 0);
    TEST_ASSERT_EQ(ny_type_struct_field_offset(&tt, t_mixed, 1), 8);
    TEST_ASSERT_EQ(ny_type_struct_field_offset(&tt, t_mixed, 2), 16);

    /* struct PtrField { void *p; int32_t val; } -> size 16, align 8 */
    Ny_Type_ID f_ptr[2] = { NY_TYPE_PTR, NY_TYPE_I32 };
    Ny_Type_ID t_ptr = ny_type_table_add_struct(&tt, "PtrField", f_ptr, 2);
    TEST_ASSERT_EQ(ny_type_size(&tt, t_ptr), 16);
    TEST_ASSERT_EQ(ny_type_align(&tt, t_ptr), 8);
    TEST_ASSERT_EQ(ny_type_struct_field_offset(&tt, t_ptr, 0), 0);
    TEST_ASSERT_EQ(ny_type_struct_field_offset(&tt, t_ptr, 1), 8);

    /* Array [5]i32 -> size 20, align 4 */
    Ny_Type_ID t_arr = ny_type_table_add_array(&tt, NY_TYPE_I32, 5);
    TEST_ASSERT(ny_type_is_aggregate(&tt, t_arr));
    TEST_ASSERT_EQ(ny_type_size(&tt, t_arr), 20);
    TEST_ASSERT_EQ(ny_type_align(&tt, t_arr), 4);

    ny_type_table_destroy(&tt);
}

void test_aggregate_abi_classification(void) {
    Ny_Type_Table tt;
    ny_type_table_init(&tt);

    /* 1. Small integer struct: { i32, i32 } -> 8 bytes */
    Ny_Type_ID f_p2[2] = { NY_TYPE_I32, NY_TYPE_I32 };
    Ny_Type_ID t_p2 = ny_type_table_add_struct(&tt, "Point2D", f_p2, 2);

    Ny_X86_Aggregate_ABI win_p2 = x86_abi_classify_aggregate(&tt, t_p2, NY_ABI_WINDOWS_X64);
    TEST_ASSERT_EQ(win_p2.size, 8);
    TEST_ASSERT(!win_p2.pass_by_ref);
    TEST_ASSERT(!win_p2.return_sret);
    TEST_ASSERT_EQ(win_p2.eightbyte_count, 1);
    TEST_ASSERT_EQ(win_p2.eightbytes[0], NY_X86_CLASS_INTEGER);

    Ny_X86_Aggregate_ABI sysv_p2 = x86_abi_classify_aggregate(&tt, t_p2, NY_ABI_SYSV_AMD64);
    TEST_ASSERT_EQ(sysv_p2.size, 8);
    TEST_ASSERT(!sysv_p2.pass_by_ref);
    TEST_ASSERT(!sysv_p2.return_sret);
    TEST_ASSERT_EQ(sysv_p2.eightbyte_count, 1);
    TEST_ASSERT_EQ(sysv_p2.eightbytes[0], NY_X86_CLASS_INTEGER);

    /* 2. Small float struct: { f32, f32 } -> 8 bytes */
    Ny_Type_ID f_fp2[2] = { NY_TYPE_F32, NY_TYPE_F32 };
    Ny_Type_ID t_fp2 = ny_type_table_add_struct(&tt, "FloatPoint2D", f_fp2, 2);

    Ny_X86_Aggregate_ABI win_fp2 = x86_abi_classify_aggregate(&tt, t_fp2, NY_ABI_WINDOWS_X64);
    TEST_ASSERT(!win_fp2.pass_by_ref);
    TEST_ASSERT(!win_fp2.return_sret);
    TEST_ASSERT_EQ(win_fp2.eightbytes[0], NY_X86_CLASS_INTEGER);

    Ny_X86_Aggregate_ABI sysv_fp2 = x86_abi_classify_aggregate(&tt, t_fp2, NY_ABI_SYSV_AMD64);
    TEST_ASSERT(!sysv_fp2.pass_by_ref);
    TEST_ASSERT(!sysv_fp2.return_sret);
    TEST_ASSERT_EQ(sysv_fp2.eightbyte_count, 1);
    TEST_ASSERT_EQ(sysv_fp2.eightbytes[0], NY_X86_CLASS_SSE);

    /* 3. Small double struct: { f64, f64 } -> 16 bytes */
    Ny_Type_ID f_vec2d[2] = { NY_TYPE_F64, NY_TYPE_F64 };
    Ny_Type_ID t_vec2d = ny_type_table_add_struct(&tt, "Vec2D", f_vec2d, 2);

    Ny_X86_Aggregate_ABI win_vec2d = x86_abi_classify_aggregate(&tt, t_vec2d, NY_ABI_WINDOWS_X64);
    TEST_ASSERT(win_vec2d.pass_by_ref);
    TEST_ASSERT(win_vec2d.return_sret);

    Ny_X86_Aggregate_ABI sysv_vec2d = x86_abi_classify_aggregate(&tt, t_vec2d, NY_ABI_SYSV_AMD64);
    TEST_ASSERT(!sysv_vec2d.pass_by_ref);
    TEST_ASSERT(!sysv_vec2d.return_sret);
    TEST_ASSERT_EQ(sysv_vec2d.eightbyte_count, 2);
    TEST_ASSERT_EQ(sysv_vec2d.eightbytes[0], NY_X86_CLASS_SSE);
    TEST_ASSERT_EQ(sysv_vec2d.eightbytes[1], NY_X86_CLASS_SSE);

    /* 4. Large struct: { i64, i64, i64 } -> 24 bytes */
    Ny_Type_ID f_large[3] = { NY_TYPE_I64, NY_TYPE_I64, NY_TYPE_I64 };
    Ny_Type_ID t_large = ny_type_table_add_struct(&tt, "Large", f_large, 3);

    Ny_X86_Aggregate_ABI win_large = x86_abi_classify_aggregate(&tt, t_large, NY_ABI_WINDOWS_X64);
    TEST_ASSERT(win_large.pass_by_ref);
    TEST_ASSERT(win_large.return_sret);

    Ny_X86_Aggregate_ABI sysv_large = x86_abi_classify_aggregate(&tt, t_large, NY_ABI_SYSV_AMD64);
    TEST_ASSERT(sysv_large.pass_by_ref);
    TEST_ASSERT(sysv_large.return_sret);

    ny_type_table_destroy(&tt);
}
