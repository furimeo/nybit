// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nybit/support.h>
#include <nybit/ir.h>
#include <nybit/target_aarch64.h>
#include <nygen/nygen.h>
#include <string.h>

void test_aapcs64_struct_i32(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[1] = { NY_TYPE_I32 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 1);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 4);
    TEST_ASSERT_EQ(cls.reg_count, 1);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_struct_i64(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[1] = { NY_TYPE_I64 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 1);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 8);
    TEST_ASSERT_EQ(cls.reg_count, 1);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_struct_i32_i32(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[2] = { NY_TYPE_I32, NY_TYPE_I32 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 8);
    TEST_ASSERT_EQ(cls.reg_count, 1);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_struct_i64_i64(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[2] = { NY_TYPE_I64, NY_TYPE_I64 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 16);
    TEST_ASSERT_EQ(cls.reg_count, 2);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_struct_i32_i64(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[2] = { NY_TYPE_I32, NY_TYPE_I64 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 16);
    TEST_ASSERT_EQ(cls.reg_count, 2);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_struct_ptr_i64(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[2] = { NY_TYPE_PTR, NY_TYPE_I64 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 16);
    TEST_ASSERT_EQ(cls.reg_count, 2);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_large_struct_indirect(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[3] = { NY_TYPE_I64, NY_TYPE_I64, NY_TYPE_I64 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 3);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_INDIRECT);
    TEST_ASSERT_EQ(cls.size, 24);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_hfa_f32_f32(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[2] = { NY_TYPE_F32, NY_TYPE_F32 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_HFA);
    TEST_ASSERT_EQ(cls.reg_count, 2);
    TEST_ASSERT_EQ(cls.hfa_member, NY_TYPE_F32);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_hfa_f64_f64(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[2] = { NY_TYPE_F64, NY_TYPE_F64 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_HFA);
    TEST_ASSERT_EQ(cls.reg_count, 2);
    TEST_ASSERT_EQ(cls.hfa_member, NY_TYPE_F64);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_hfa_four_f32(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[4] = { NY_TYPE_F32, NY_TYPE_F32, NY_TYPE_F32, NY_TYPE_F32 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 4);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_HFA);
    TEST_ASSERT_EQ(cls.reg_count, 4);
    TEST_ASSERT_EQ(cls.hfa_member, NY_TYPE_F32);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_hfa_five_f32_indirect(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[5] = { NY_TYPE_F32, NY_TYPE_F32, NY_TYPE_F32, NY_TYPE_F32, NY_TYPE_F32 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 5);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_INDIRECT);
    TEST_ASSERT_EQ(cls.size, 20);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_mixed_int_float_not_hfa(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[2] = { NY_TYPE_I32, NY_TYPE_F32 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 8);
    TEST_ASSERT_EQ(cls.reg_count, 1);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_nested_struct(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID inner_f[2] = { NY_TYPE_I32, NY_TYPE_I32 };
    Ny_Type_ID inner = ny_type_table_add_struct(&tt, "Inner", inner_f, 2);
    Ny_Type_ID outer_f[2] = { inner, NY_TYPE_I32 };
    Ny_Type_ID outer = ny_type_table_add_struct(&tt, "Outer", outer_f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, outer);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 12);
    TEST_ASSERT_EQ(cls.reg_count, 2);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_array_f32_hfa(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID t = ny_type_table_add_array(&tt, NY_TYPE_F32, 4);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_HFA);
    TEST_ASSERT_EQ(cls.reg_count, 4);
    TEST_ASSERT_EQ(cls.hfa_member, NY_TYPE_F32);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_array_i32_large_indirect(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID t = ny_type_table_add_array(&tt, NY_TYPE_I32, 5);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_INDIRECT);
    TEST_ASSERT_EQ(cls.size, 20);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_scalar_not_aggregate(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, NY_TYPE_I64);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_SCALAR);
    ny_type_table_destroy(&tt);
}

static const char *s_id_pair_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@function id_pair(%p: Pair) -> Pair;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n";

static const char *s_fwd_pair_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@function id_pair(%p: Pair) -> Pair;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n"
    "@function fwd_pair(%p: Pair) -> Pair;\n"
    ".entry;\n"
    "    %r = call @id_pair, %p;\n"
    "    @return %r;\n"
    ";;\n";

static const char *s_id_big_ny =
    "@type Big = struct { a: i64, b: i64, c: i64 };\n"
    "@function id_big(%p: Big) -> Big;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n";

static const char *s_fwd_big_ny =
    "@type Big = struct { a: i64, b: i64, c: i64 };\n"
    "@function id_big(%p: Big) -> Big;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n"
    "@function fwd_big(%p: Big) -> Big;\n"
    ".entry;\n"
    "    %r = call @id_big, %p;\n"
    "    @return %r;\n"
    ";;\n";

void test_aapcs64_lower_small_struct_param(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_id_pair_ny, strlen(s_id_pair_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    const char *asm_text = (const char *)res.data;
    TEST_ASSERT(strstr(asm_text, "id_pair:") != NULL);
    TEST_ASSERT(strstr(asm_text, "ret") != NULL);
    nygen_result_destroy(&res);
}

void test_aapcs64_lower_small_struct_call(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_fwd_pair_ny, strlen(s_fwd_pair_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    const char *asm_text = (const char *)res.data;
    TEST_ASSERT(strstr(asm_text, "fwd_pair:") != NULL);
    TEST_ASSERT(strstr(asm_text, "bl") != NULL);
    nygen_result_destroy(&res);
}

void test_aapcs64_lower_large_struct_param(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_id_big_ny, strlen(s_id_big_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

void test_aapcs64_lower_large_struct_call_sret(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_fwd_big_ny, strlen(s_fwd_big_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    const char *asm_text = (const char *)res.data;
    TEST_ASSERT(strstr(asm_text, "fwd_big:") != NULL);
    nygen_result_destroy(&res);
}

void test_aapcs64_lower_small_struct_elf(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_id_pair_ny, strlen(s_id_pair_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);
    TEST_ASSERT_EQ(res.data[0], 0x7F);
    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);
    nygen_result_destroy(&res);
}

void test_aapcs64_lower_large_struct_elf(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_fwd_big_ny, strlen(s_fwd_big_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);
    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);
    nygen_result_destroy(&res);
}

void test_aapcs64_empty_struct(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "Empty", NULL, 0);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.size, 0);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_SCALAR);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_struct_i8(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[1] = { NY_TYPE_I8 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 1);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 1);
    TEST_ASSERT_EQ(cls.reg_count, 1);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_struct_ptr_ptr(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[2] = { NY_TYPE_PTR, NY_TYPE_PTR };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 2);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_GPR_AGG);
    TEST_ASSERT_EQ(cls.size, 16);
    TEST_ASSERT_EQ(cls.reg_count, 2);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_nested_hfa_exceeds_four(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID inner_f[2] = { NY_TYPE_F32, NY_TYPE_F32 };
    Ny_Type_ID inner = ny_type_table_add_struct(&tt, "F2", inner_f, 2);
    Ny_Type_ID outer_f[3] = { inner, inner, NY_TYPE_F32 };
    Ny_Type_ID outer = ny_type_table_add_struct(&tt, "Outer", outer_f, 3);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, outer);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_INDIRECT);
    ny_type_table_destroy(&tt);
}

void test_aapcs64_hfa_f64_f64_f64(void) {
    Ny_Type_Table tt; ny_type_table_init(&tt);
    Ny_Type_ID f[3] = { NY_TYPE_F64, NY_TYPE_F64, NY_TYPE_F64 };
    Ny_Type_ID t = ny_type_table_add_struct(&tt, "S", f, 3);
    Ny_AAPCS64_ABI cls = aarch64_abi_classify_aggregate(&tt, t);
    TEST_ASSERT_EQ(cls.kind, NY_AAPCS64_HFA);
    TEST_ASSERT_EQ(cls.reg_count, 3);
    TEST_ASSERT_EQ(cls.hfa_member, NY_TYPE_F64);
    ny_type_table_destroy(&tt);
}

static const char *s_mixed_args_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@function mixed_args(%s: i64, %p: Pair, %v: i32) -> i32;\n"
    ".entry;\n"
    "    @return %v;\n"
    ";;\n";

void test_aapcs64_lower_mixed_args(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_mixed_args_ny, strlen(s_mixed_args_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

static const char *s_multi_agg_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@function multi_agg(%p1: Pair, %p2: Pair) -> Pair;\n"
    ".entry;\n"
    "    @return %p1;\n"
    ";;\n";

void test_aapcs64_lower_multi_aggregate(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_multi_agg_ny, strlen(s_multi_agg_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

static const char *s_reg_pressure_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@function reg_pressure(%a: i64, %b: i64, %c: i64, %d: i64, %e: i64, %f: i64, %g: i64, %h: i64, %p: Pair) -> i32;\n"
    ".entry;\n"
    "    @return %a;\n"
    ";;\n";

void test_aapcs64_lower_reg_pressure_stack(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_reg_pressure_ny, strlen(s_reg_pressure_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

static const char *s_stack_agg_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@function callee_stack(%a: i64, %b: i64, %c: i64, %d: i64, %e: i64, %f: i64, %g: i64, %h: i64, %p: Pair) -> Pair;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n"
    "@function caller_stack(%a: i64, %b: i64, %c: i64, %d: i64, %e: i64, %f: i64, %g: i64, %h: i64, %p: Pair) -> Pair;\n"
    ".entry;\n"
    "    %r = call @callee_stack, %a, %b, %c, %d, %e, %f, %g, %h, %p;\n"
    "    @return %r;\n"
    ";;\n";

void test_aapcs64_lower_stack_aggregate(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_stack_agg_ny, strlen(s_stack_agg_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    const char *asm_text = (const char *)res.data;
    TEST_ASSERT(strstr(asm_text, "caller_stack:") != NULL);
    TEST_ASSERT(strstr(asm_text, "bl") != NULL);
    nygen_result_destroy(&res);
}

static const char *s_two_indirect_ny =
    "@type Big = struct { a: i64, b: i64, c: i64 };\n"
    "@function callee_two_big(%b1: Big, %b2: Big) -> Big;\n"
    ".entry;\n"
    "    @return %b1;\n"
    ";;\n"
    "@function caller_two_big(%b1: Big, %b2: Big) -> Big;\n"
    ".entry;\n"
    "    %r = call @callee_two_big, %b1, %b2;\n"
    "    @return %r;\n"
    ";;\n";

void test_aapcs64_lower_two_indirect_aggregate(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_two_indirect_ny, strlen(s_two_indirect_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

static const char *s_mixed_agg_scalar_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@type Big = struct { a: i64, b: i64, c: i64 };\n"
    "@function callee_mixed(%x: i32, %p: Pair, %y: i64, %b: Big) -> Pair;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n"
    "@function caller_mixed(%x: i32, %p: Pair, %y: i64, %b: Big) -> Pair;\n"
    ".entry;\n"
    "    %r = call @callee_mixed, %x, %p, %y, %b;\n"
    "    @return %r;\n"
    ";;\n";

void test_aapcs64_lower_mixed_agg_scalar(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_mixed_agg_scalar_ny, strlen(s_mixed_agg_scalar_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

static const char *s_hfa_vec3_ny =
    "@type Vec3 = struct { x: f32, y: f32, z: f32 };\n"
    "@function hfa_param(%v: Vec3) -> Vec3;\n"
    ".entry;\n"
    "    @return %v;\n"
    ";;\n";

void test_aapcs64_lower_hfa_param(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_hfa_vec3_ny, strlen(s_hfa_vec3_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    const char *asm_text = (const char *)res.data;
    TEST_ASSERT(strstr(asm_text, "hfa_param:") != NULL);
    TEST_ASSERT(strstr(asm_text, "ret") != NULL);
    nygen_result_destroy(&res);
}

static const char *s_hfa_call_ny =
    "@type Vec3 = struct { x: f32, y: f32, z: f32 };\n"
    "@function hfa_id(%v: Vec3) -> Vec3;\n"
    ".entry;\n"
    "    @return %v;\n"
    ";;\n"
    "@function hfa_caller(%v: Vec3) -> Vec3;\n"
    ".entry;\n"
    "    %r = call @hfa_id, %v;\n"
    "    @return %r;\n"
    ";;\n";

void test_aapcs64_lower_hfa_call(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_hfa_call_ny, strlen(s_hfa_call_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    const char *asm_text = (const char *)res.data;
    TEST_ASSERT(strstr(asm_text, "hfa_caller:") != NULL);
    TEST_ASSERT(strstr(asm_text, "bl") != NULL);
    nygen_result_destroy(&res);
}

void test_aapcs64_lower_hfa_elf(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_hfa_call_ny, strlen(s_hfa_call_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);
    TEST_ASSERT_EQ(res.data[0], 0x7F);
    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);
    nygen_result_destroy(&res);
}

static const char *s_nested_sret_ny =
    "@type Big = struct { a: i64, b: i64, c: i64 };\n"
    "@function inner_sret(%b: Big) -> Big;\n"
    ".entry;\n"
    "    @return %b;\n"
    ";;\n"
    "@function outer_sret(%b: Big) -> Big;\n"
    ".entry;\n"
    "    %r1 = call @inner_sret, %b;\n"
    "    %r2 = call @inner_sret, %r1;\n"
    "    @return %r2;\n"
    ";;\n";

void test_aapcs64_lower_nested_sret(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_nested_sret_ny, strlen(s_nested_sret_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    const char *asm_text = (const char *)res.data;
    TEST_ASSERT(strstr(asm_text, "outer_sret:") != NULL);
    TEST_ASSERT(strstr(asm_text, "bl") != NULL);
    nygen_result_destroy(&res);
}

static const char *s_leaf_sret_ny =
    "@type Big = struct { a: i64, b: i64, c: i64 };\n"
    "@function leaf_sret(%b: Big) -> Big;\n"
    ".entry;\n"
    "    @return %b;\n"
    ";;\n";

void test_aapcs64_lower_sret_no_call(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_leaf_sret_ny, strlen(s_leaf_sret_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    const char *asm_text = (const char *)res.data;
    TEST_ASSERT(strstr(asm_text, "leaf_sret:") != NULL);
    TEST_ASSERT(strstr(asm_text, "ret") != NULL);
    nygen_result_destroy(&res);
}

static const char *s_deep_nested_ny =
    "@type Inner = struct { x: i32, y: i32 };\n"
    "@type Outer = struct { a: Inner, b: Inner };\n"
    "@function deep_nested(%o: Outer) -> Outer;\n"
    ".entry;\n"
    "    @return %o;\n"
    ";;\n";

void test_aapcs64_lower_deep_nested(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_deep_nested_ny, strlen(s_deep_nested_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

static const char *s_exhaust_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@function exhaust(%a1: i64, %a2: i64, %a3: i64, %a4: i64, %a5: i64, %a6: i64, %a7: i64, %a8: i64, %p1: Pair, %p2: Pair) -> Pair;\n"
    ".entry;\n"
    "    @return %p1;\n"
    ";;\n"
    "@function call_exhaust(%a1: i64, %a2: i64, %a3: i64, %a4: i64, %a5: i64, %a6: i64, %a7: i64, %a8: i64, %p1: Pair, %p2: Pair) -> Pair;\n"
    ".entry;\n"
    "    %r = call @exhaust, %a1, %a2, %a3, %a4, %a5, %a6, %a7, %a8, %p1, %p2;\n"
    "    @return %r;\n"
    ";;\n";

void test_aapcs64_lower_full_reg_exhaustion(void) {
    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_exhaust_ny, strlen(s_exhaust_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);
    nygen_result_destroy(&res);
}

void test_aarch64_unwind_eh_frame(void) {
    const char *src =
        "@function square(%n: i32) -> i32;\n"
        ".entry;\n"
        "    %sq = mul %n, %n;\n"
        "    @return %sq;\n"
        ";;\n";

    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.target_triple = "aarch64";
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.emit_unwind = true;
    cfg.debug_info = false;

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);
    TEST_ASSERT(res.data[0] == 0x7F && res.data[1] == 'E' && res.data[2] == 'L' && res.data[3] == 'F');
    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    bool found_eh = false;
    for (size_t i = 0; i + 9 <= res.size; i++) {
        if (memcmp(res.data + i, ".eh_frame", 9) == 0) found_eh = true;
    }
    TEST_ASSERT(found_eh);

    nygen_result_destroy(&res);
}

void test_aarch64_unwind_dwarf_debug(void) {
    const char *src =
        "@function compute(%a: i32, %b: i32) -> i32;\n"
        ".entry;\n"
        "    %sum = add %a, %b;\n"
        "    @return %sum;\n"
        ";;\n";

    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.target_triple = "aarch64";
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.emit_unwind = true;
    cfg.debug_info = true;

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);

    bool found_eh = false;
    bool found_line = false;
    bool found_info = false;
    bool found_abbrev = false;
    for (size_t i = 0; i + 13 <= res.size; i++) {
        if (memcmp(res.data + i, ".eh_frame", 9) == 0) found_eh = true;
        if (memcmp(res.data + i, ".debug_line", 11) == 0) found_line = true;
        if (memcmp(res.data + i, ".debug_info", 11) == 0) found_info = true;
        if (memcmp(res.data + i, ".debug_abbrev", 13) == 0) found_abbrev = true;
    }
    TEST_ASSERT(found_eh);
    TEST_ASSERT(found_line);
    TEST_ASSERT(found_info);
    TEST_ASSERT(found_abbrev);

    nygen_result_destroy(&res);
}

void test_aarch64_unwind_disabled(void) {
    const char *src =
        "@function simple(%x: i32) -> i32;\n"
        ".entry;\n"
        "    @return %x;\n"
        ";;\n";

    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.target_triple = "aarch64";
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.emit_unwind = false;
    cfg.debug_info = false;

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);

    bool found_eh = false;
    bool found_debug = false;
    for (size_t i = 0; i + 9 <= res.size; i++) {
        if (memcmp(res.data + i, ".eh_frame", 9) == 0) found_eh = true;
        if (memcmp(res.data + i, ".debug_", 7) == 0) found_debug = true;
    }
    TEST_ASSERT(!found_eh);
    TEST_ASSERT(!found_debug);

    nygen_result_destroy(&res);
}

void test_aarch64_unwind_determinism(void) {
    const char *src =
        "@type Pair = struct { a: i32, b: i32 };\n"
        "@function det_fn(%p: Pair) -> Pair;\n"
        ".entry;\n"
        "    @return %p;\n"
        ";;\n";

    Nygen_Config cfg; nygen_config_init(&cfg);
    cfg.target_triple = "aarch64";
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.emit_unwind = true;
    cfg.debug_info = true;

    Nygen_Result res1 = nygen_compile(src, strlen(src), &cfg);
    Nygen_Result res2 = nygen_compile(src, strlen(src), &cfg);
    TEST_ASSERT(res1.success);
    TEST_ASSERT(res2.success);
    TEST_ASSERT_EQ(res1.size, res2.size);
    TEST_ASSERT(memcmp(res1.data, res2.data, res1.size) == 0);

    nygen_result_destroy(&res1);
    nygen_result_destroy(&res2);
}

