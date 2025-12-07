// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nybit/support.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *s_simple_ny =
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %a = const 40;\n"
    "    %b = const 2;\n"
    "    %r = add %a, %b;\n"
    "    @return %r;\n"
    ";;\n";

static const char *s_syntax_err_ny =
    "@function broken(\n";

void test_nygen_compile_raw_ir(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_RAW_IR;

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(strstr((const char *)res.data, "@function main") != NULL);
    TEST_ASSERT_EQ(res.diagnostic_count, 0);

    nygen_result_destroy(&res);
}

void test_nygen_compile_opt_ir(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OPT_IR;
    cfg.opt_level = NYGEN_OPT_O1;

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(strstr((const char *)res.data, "const 42") != NULL);

    nygen_result_destroy(&res);
}

void test_nygen_compile_machine_ir(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_MACHINE_IR;

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(strstr((const char *)res.data, "machine_function @main") != NULL);

    nygen_result_destroy(&res);
}

void test_nygen_compile_asm(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(strstr((const char *)res.data, "main:") != NULL);
    TEST_ASSERT(strstr((const char *)res.data, "ret") != NULL);

    nygen_result_destroy(&res);
}

void test_nygen_compile_bytes(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_BYTES;

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size > 0);

    nygen_result_destroy(&res);
}

void test_nygen_compile_object_elf_and_coff(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "x86_64-windows";

    Nygen_Result res_coff = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res_coff.success);
    TEST_ASSERT(res_coff.data != NULL);
    TEST_ASSERT(res_coff.size > 0);
    uint16_t machine = *(const uint16_t *)res_coff.data;
    TEST_ASSERT_EQ(machine, 0x8664);
    nygen_result_destroy(&res_coff);

    cfg.target_triple = "x86_64-sysv";
    Nygen_Result res_elf = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res_elf.success);
    TEST_ASSERT(res_elf.data != NULL);
    TEST_ASSERT(res_elf.size >= 4);
    TEST_ASSERT_EQ(res_elf.data[0], 0x7f);
    TEST_ASSERT_EQ(res_elf.data[1], 'E');
    TEST_ASSERT_EQ(res_elf.data[2], 'L');
    TEST_ASSERT_EQ(res_elf.data[3], 'F');
    nygen_result_destroy(&res_elf);
}

void test_nygen_diagnostics_on_error(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;

    Nygen_Result res = nygen_compile(s_syntax_err_ny, strlen(s_syntax_err_ny), &cfg);
    TEST_ASSERT(!res.success);
    TEST_ASSERT(res.diagnostic_count > 0);
    TEST_ASSERT(res.diagnostics != NULL);
    TEST_ASSERT(res.diagnostics[0].message != NULL);
    TEST_ASSERT(strlen(res.diagnostics[0].message) > 0);

    nygen_result_destroy(&res);
}

void test_nygen_compile_executable(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_EXECUTABLE;

    Nygen_Result res;
    bool ok = nygen_compile_to_file(s_simple_ny, strlen(s_simple_ny), "bin/test_nygen_direct.exe", &cfg, &res);
    TEST_ASSERT(ok);
    TEST_ASSERT(res.success);
    nygen_result_destroy(&res);

    int exit_code = ny_test_system("./bin/test_nygen_direct.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove("bin/test_nygen_direct.exe");
}

void test_nygen_cli_integration_e2e(void) {
    FILE *f = fopen("bin/test_cli_tmp.ny", "wb");
    TEST_ASSERT(f != NULL);
    fwrite(s_simple_ny, 1, strlen(s_simple_ny), f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s bin/test_cli_tmp.ny -o bin/test_cli_out.exe", NYBIT_CLI_BIN);
    int cli_ret = ny_test_system(cmd);
    TEST_ASSERT_EQ(cli_ret, 0);

    int run_ret = ny_test_system("./bin/test_cli_out.exe");
    TEST_ASSERT_EQ(run_ret, 42);

    remove("bin/test_cli_tmp.ny");
    remove("bin/test_cli_out.exe");
}
