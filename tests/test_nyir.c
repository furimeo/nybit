// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nybit/support.h>
#include <string.h>

static const char *s_simple =
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %a = const 20;\n"
    "    %b = const 22;\n"
    "    %res = add %a, %b;\n"
    "    @return %res;\n"
    ";;\n";

static const char *s_multi_fn =
    "@function helper_mul(%x: i32, %y: i32) -> i32;\n"
    ".entry;\n"
    "    %res = mul %x, %y;\n"
    "    @return %res;\n"
    ";;\n"
    "\n"
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %c6 = const 6;\n"
    "    %c7 = const 7;\n"
    "    %prod = call @helper_mul, %c6, %c7;\n"
    "    @return %prod;\n"
    ";;\n";

static const char *s_globals =
    "@global @readonly @k_base: i32 = 20;\n"
    "@global @g_accum: i32 = 12;\n"
    "@global @g_temp: i32;\n"
    "\n"
    "@function compute_globals() -> i32;\n"
    ".entry;\n"
    "    %p_base = global_addr @k_base;\n"
    "    %base = load %p_base;\n"
    "    %p_accum = global_addr @g_accum;\n"
    "    %accum = load %p_accum;\n"
    "    %sum = add %base, %accum;\n"
    "    %c10 = const 10;\n"
    "    %total = add %sum, %c10;\n"
    "    %p_temp = global_addr @g_temp;\n"
    "    store %p_temp, %total;\n"
    "    %res = load %p_temp;\n"
    "    @return %res;\n"
    ";;\n";

static const char *s_branch =
    "@function abs_val(%x: i32) -> i32;\n"
    ".entry;\n"
    "    %zero = const 0;\n"
    "    %cond = cmp.lt.s %x, %zero;\n"
    "    @branch_if %cond, .neg, .pos;\n"
    ".neg;\n"
    "    %r1 = neg %x;\n"
    "    @return %r1;\n"
    ".pos;\n"
    "    @return %x;\n"
    ";;\n";

void test_nyir_roundtrip_simple(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_simple, strlen(s_simple), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);
    TEST_ASSERT(nyir_data != NULL);
    TEST_ASSERT(nyir_size >= 12);

    TEST_ASSERT_EQ(nyir_data[0], NYIR_MAGIC0);
    TEST_ASSERT_EQ(nyir_data[1], NYIR_MAGIC1);
    TEST_ASSERT_EQ(nyir_data[2], NYIR_MAGIC2);
    TEST_ASSERT_EQ(nyir_data[3], NYIR_MAGIC3);

    uint16_t version = (uint16_t)(nyir_data[4] | (nyir_data[5] << 8));
    TEST_ASSERT_EQ(version, NYIR_VERSION);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size, &diags, &diag_count);
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT_EQ(diag_count, 0);

    Nygen_Config obj_cfg;
    nygen_config_init(&obj_cfg);
    obj_cfg.output_kind = NYGEN_OUTPUT_OBJECT;

    Nygen_Result res = nygen_compile_ir(ctx, &obj_cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size > 0);

    nygen_result_destroy(&res);
    nygen_ir_destroy(ctx);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_roundtrip_multi_fn(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_multi_fn, strlen(s_multi_fn), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size, &diags, &diag_count);
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT_EQ(diag_count, 0);

    Nygen_Config obj_cfg;
    nygen_config_init(&obj_cfg);
    obj_cfg.output_kind = NYGEN_OUTPUT_OBJECT;

    Nygen_Result res = nygen_compile_ir(ctx, &obj_cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.size > 0);

    nygen_result_destroy(&res);
    nygen_ir_destroy(ctx);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_roundtrip_globals(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_globals, strlen(s_globals), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size, &diags, &diag_count);
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT_EQ(diag_count, 0);

    Nygen_Config obj_cfg;
    nygen_config_init(&obj_cfg);
    obj_cfg.output_kind = NYGEN_OUTPUT_OBJECT;

    Nygen_Result res = nygen_compile_ir(ctx, &obj_cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.size > 0);

    nygen_result_destroy(&res);
    nygen_ir_destroy(ctx);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_roundtrip_branch(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_branch, strlen(s_branch), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size, &diags, &diag_count);
    TEST_ASSERT(ctx != NULL);
    TEST_ASSERT_EQ(diag_count, 0);

    Nygen_Config obj_cfg;
    nygen_config_init(&obj_cfg);
    obj_cfg.output_kind = NYGEN_OUTPUT_OBJECT;

    Nygen_Result res = nygen_compile_ir(ctx, &obj_cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.size > 0);

    nygen_result_destroy(&res);
    nygen_ir_destroy(ctx);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_deterministic_output(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *data1 = NULL;
    size_t size1 = 0;
    bool ok1 = nygen_compile_nyir(s_multi_fn, strlen(s_multi_fn), &cfg, &data1, &size1, NULL, NULL);
    TEST_ASSERT(ok1);
    TEST_ASSERT(size1 > 0);

    uint8_t *data2 = NULL;
    size_t size2 = 0;
    bool ok2 = nygen_compile_nyir(s_multi_fn, strlen(s_multi_fn), &cfg, &data2, &size2, NULL, NULL);
    TEST_ASSERT(ok2);

    TEST_ASSERT_EQ(size1, size2);
    TEST_ASSERT(memcmp(data1, data2, size1) == 0);

    ny_free(data1, size1);
    ny_free(data2, size2);
}

void test_nyir_backend_matches_direct(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.opt_level = NYGEN_OPT_O0;

    Nygen_Result direct_res = nygen_compile(s_multi_fn, strlen(s_multi_fn), &cfg);
    TEST_ASSERT(direct_res.success);
    TEST_ASSERT(direct_res.size > 0);

    Nygen_Config nyir_cfg;
    nygen_config_init(&nyir_cfg);
    nyir_cfg.opt_level = NYGEN_OPT_O0;

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_multi_fn, strlen(s_multi_fn), &nyir_cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size, NULL, NULL);
    TEST_ASSERT(ctx != NULL);

    Nygen_Config ir_cfg;
    nygen_config_init(&ir_cfg);
    ir_cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    ir_cfg.opt_level = NYGEN_OPT_O0;

    Nygen_Result ir_res = nygen_compile_ir(ctx, &ir_cfg);
    TEST_ASSERT(ir_res.success);

    TEST_ASSERT_EQ(direct_res.size, ir_res.size);
    TEST_ASSERT(memcmp(direct_res.data, ir_res.data, direct_res.size) == 0);

    nygen_result_destroy(&ir_res);
    nygen_ir_destroy(ctx);
    ny_free(nyir_data, nyir_size);
    nygen_result_destroy(&direct_res);
}

void test_nyir_corrupt_truncated(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_simple, strlen(s_simple), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size / 2, &diags, &diag_count);
    TEST_ASSERT(ctx == NULL);
    TEST_ASSERT(diag_count > 0);
    TEST_ASSERT(strstr(diags[0].message, "truncated") != NULL || strstr(diags[0].message, "too small") != NULL);

    nygen_diagnostics_destroy(diags, diag_count);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_corrupt_bad_magic(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_simple, strlen(s_simple), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    nyir_data[0] ^= 0xFF;

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size, &diags, &diag_count);
    TEST_ASSERT(ctx == NULL);
    TEST_ASSERT(diag_count > 0);
    TEST_ASSERT(strstr(diags[0].message, "magic") != NULL);

    nygen_diagnostics_destroy(diags, diag_count);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_corrupt_bad_version(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_simple, strlen(s_simple), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    uint16_t bad_version = NYIR_VERSION + 1;
    nyir_data[4] = (uint8_t)(bad_version & 0xFF);
    nyir_data[5] = (uint8_t)((bad_version >> 8) & 0xFF);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size, &diags, &diag_count);
    TEST_ASSERT(ctx == NULL);
    TEST_ASSERT(diag_count > 0);
    TEST_ASSERT(strstr(diags[0].message, "version") != NULL);

    nygen_diagnostics_destroy(diags, diag_count);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_corrupt_bad_opcode(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_simple, strlen(s_simple), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    /* Locate first instruction opcode in s_simple:
       magic(4) + ver(2) + res(2) = 8
       mod_name len(4) + "nygen_module"(12) = 16 (total 24)
       types count(4) = 4 (total 28)
       globals count(4) = 4 (total 32)
       functions count(4) = 4 (total 36)
       fn name len(4) + "main"(4) = 8 (total 44)
       ret_type(4) + call_conv(1) + is_var(1) + entry_block(4) = 10 (total 54)
       param_count(4) = 4 (total 58)
       block_count(4) = 4 (total 62)
       blk name len(4) + "entry"(5) = 9 (total 71)
       first_inst(4) + last_inst(4) + inst_count(4) + pred_count(4) + succ_count(4) = 20 (total 91)
       inst_count(4) = 4 (total 95)
       inst 0: opcode(2) at byte offset 95
    */
    size_t op_offset = 95;
    TEST_ASSERT(op_offset + 2 <= nyir_size);
    uint16_t bad_op = 0xFFFE;
    nyir_data[op_offset] = (uint8_t)(bad_op & 0xFF);
    nyir_data[op_offset + 1] = (uint8_t)((bad_op >> 8) & 0xFF);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(nyir_data, nyir_size, &diags, &diag_count);
    TEST_ASSERT(ctx == NULL);
    TEST_ASSERT(diag_count > 0);
    TEST_ASSERT(strstr(diags[0].message, "opcode") != NULL);

    nygen_diagnostics_destroy(diags, diag_count);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_corrupt_trailing_data(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_simple, strlen(s_simple), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);

    uint8_t *padded = (uint8_t *)ny_alloc(nyir_size + 16);
    memcpy(padded, nyir_data, nyir_size);
    memset(padded + nyir_size, 0xFF, 16);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    Ny_Context *ctx = nygen_load_nyir(padded, nyir_size + 16, &diags, &diag_count);
    TEST_ASSERT(ctx == NULL);
    TEST_ASSERT(diag_count > 0);
    TEST_ASSERT(strstr(diags[0].message, "trailing") != NULL);

    nygen_diagnostics_destroy(diags, diag_count);
    ny_free(padded, nyir_size + 16);
    ny_free(nyir_data, nyir_size);
}

void test_nyir_nygen_standalone(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(s_multi_fn, strlen(s_multi_fn), &cfg, &nyir_data, &nyir_size, NULL, NULL);
    TEST_ASSERT(ok);
    TEST_ASSERT(nyir_data != NULL);
    TEST_ASSERT(nyir_size > 12);

    TEST_ASSERT_EQ(nyir_data[0], NYIR_MAGIC0);
    TEST_ASSERT_EQ(nyir_data[1], NYIR_MAGIC1);
    TEST_ASSERT_EQ(nyir_data[2], NYIR_MAGIC2);
    TEST_ASSERT_EQ(nyir_data[3], NYIR_MAGIC3);

    uint16_t version = (uint16_t)(nyir_data[4] | (nyir_data[5] << 8));
    TEST_ASSERT_EQ(version, NYIR_VERSION);

    ny_free(nyir_data, nyir_size);
}
