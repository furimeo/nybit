// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nybit/support.h>
#include <nylink/nylink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool compile_aarch64_obj_raw(const char *src, uint8_t **out_data, size_t *out_size) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    if (!res.success || !res.data || res.size == 0) {
        nygen_result_destroy(&res);
        return false;
    }
    *out_data = (uint8_t *)ny_alloc(res.size);
    memcpy(*out_data, res.data, res.size);
    *out_size = res.size;
    nygen_result_destroy(&res);
    return true;
}

void test_aarch64_malformed_elf_rejected(void) {
    uint8_t bad_elf[64];
    memset(bad_elf, 0, sizeof(bad_elf));
    bad_elf[0] = 0x7F; bad_elf[1] = 'E'; bad_elf[2] = 'L'; bad_elf[3] = 'F';
    bad_elf[4] = 2;
    uint16_t machine = 183;
    memcpy(&bad_elf[18], &machine, 2);
    uint16_t type = 1;
    memcpy(&bad_elf[16], &type, 2);
    bad_elf[58] = 64;

    Nylink_Context *ctx = nylink_context_create();
    bool added = nylink_add_object(ctx, "bad.o", bad_elf, sizeof(bad_elf));
    TEST_ASSERT(!added || nylink_has_errors(ctx));
    nylink_context_destroy(ctx);
}

void test_aarch64_wrong_machine_rejected(void) {
    uint8_t bad_machine[64];
    memset(bad_machine, 0, sizeof(bad_machine));
    bad_machine[0] = 0x7F; bad_machine[1] = 'E'; bad_machine[2] = 'L'; bad_machine[3] = 'F';
    bad_machine[4] = 2;
    uint16_t machine = 999;
    memcpy(&bad_machine[18], &machine, 2);
    uint16_t type = 1;
    memcpy(&bad_machine[16], &type, 2);
    bad_machine[58] = 64;

    Nylink_Context *ctx = nylink_context_create();
    bool added = nylink_add_object(ctx, "bad.o", bad_machine, sizeof(bad_machine));
    TEST_ASSERT(!added || nylink_has_errors(ctx));
    nylink_context_destroy(ctx);
}

void test_aarch64_truncated_elf_rejected(void) {
    uint8_t truncated[20];
    memset(truncated, 0, sizeof(truncated));
    truncated[0] = 0x7F; truncated[1] = 'E'; truncated[2] = 'L'; truncated[3] = 'F';
    truncated[4] = 2;
    uint16_t machine = 183;
    memcpy(&truncated[18], &machine, 2);

    Nylink_Context *ctx = nylink_context_create();
    bool added = nylink_add_object(ctx, "truncated.o", truncated, sizeof(truncated));
    TEST_ASSERT(!added || nylink_has_errors(ctx));
    nylink_context_destroy(ctx);
}

void test_aarch64_empty_object_rejected(void) {
    Nylink_Context *ctx = nylink_context_create();
    bool added = nylink_add_object(ctx, "empty.o", (const uint8_t *)"", 0);
    TEST_ASSERT(!added || nylink_has_errors(ctx));
    nylink_context_destroy(ctx);
}

void test_aarch64_relocation_overflow_diagnostic(void) {
    static const char *overflow_src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %r = call @very_far_away_function;\n"
        "    @return %r;\n"
        ";;\n";

    uint8_t *obj_data;
    size_t obj_size;
    TEST_ASSERT(compile_aarch64_obj_raw(overflow_src, &obj_data, &obj_size));

    Nylink_Context *ctx = nylink_context_create();
    nylink_add_object(ctx, "main.o", obj_data, obj_size);
    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(!resolved || nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
    ny_free(obj_data, obj_size);
}

void test_aarch64_cli_link_executable(void) {
    static const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %a = const 40;\n"
        "    %b = const 2;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n";

    FILE *f = fopen("bin/test_aarch64_cli.ny", "wb");
    TEST_ASSERT(f != NULL);
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --target aarch64 --emit-obj -o bin/test_aarch64_cli.o bin/test_aarch64_cli.ny", NYBIT_CLI_BIN);
    int cli_ret = ny_test_system(cmd);
    TEST_ASSERT_EQ(cli_ret, 0);

    uint8_t *obj_data;
    size_t obj_size;
    FILE *fobj = fopen("bin/test_aarch64_cli.o", "rb");
    TEST_ASSERT(fobj != NULL);
    fseek(fobj, 0, SEEK_END);
    long sz = ftell(fobj);
    fseek(fobj, 0, SEEK_SET);
    obj_data = (uint8_t *)ny_alloc(sz);
    fread(obj_data, 1, sz, fobj);
    obj_size = sz;
    fclose(fobj);

    uint16_t e_machine = (uint16_t)(obj_data[18] | (obj_data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    snprintf(cmd, sizeof(cmd), "%s link --target=elf64-aarch64 --entry=main bin/test_aarch64_cli.o -o bin/test_aarch64_cli.elf", NYBIT_CLI_BIN);
    int link_ret = ny_test_system(cmd);
    TEST_ASSERT_EQ(link_ret, 0);

    FILE *felf = fopen("bin/test_aarch64_cli.elf", "rb");
    TEST_ASSERT(felf != NULL);
    fseek(felf, 0, SEEK_END);
    long elf_sz = ftell(felf);
    fseek(felf, 0, SEEK_SET);
    uint8_t *elf_data = (uint8_t *)ny_alloc(elf_sz);
    fread(elf_data, 1, elf_sz, felf);
    fclose(felf);

    uint16_t elf_machine = (uint16_t)(elf_data[18] | (elf_data[19] << 8));
    TEST_ASSERT_EQ(elf_machine, 183);

    ny_free(elf_data, elf_sz);
    ny_free(obj_data, obj_size);
    remove("bin/test_aarch64_cli.ny");
    remove("bin/test_aarch64_cli.o");
    remove("bin/test_aarch64_cli.elf");
}

void test_aarch64_cli_link_shared(void) {
    static const char *src =
        "@function exported_func() -> i32;\n"
        ".entry;\n"
        "    %v = const 42;\n"
        "    @return %v;\n"
        ";;\n";

    FILE *f = fopen("bin/test_aarch64_cli_so.ny", "wb");
    TEST_ASSERT(f != NULL);
    fwrite(src, 1, strlen(src), f);
    fclose(f);

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s --target aarch64 --emit-obj -o bin/test_aarch64_cli_so.o bin/test_aarch64_cli_so.ny", NYBIT_CLI_BIN);
    int cli_ret = ny_test_system(cmd);
    TEST_ASSERT_EQ(cli_ret, 0);

    snprintf(cmd, sizeof(cmd), "%s link --target=elf64-aarch64 --shared --soname=libtest.so bin/test_aarch64_cli_so.o -o bin/libtest_aarch64_cli.so", NYBIT_CLI_BIN);
    int link_ret = ny_test_system(cmd);
    TEST_ASSERT_EQ(link_ret, 0);

    FILE *fso = fopen("bin/libtest_aarch64_cli.so", "rb");
    TEST_ASSERT(fso != NULL);
    fseek(fso, 0, SEEK_END);
    long so_sz = ftell(fso);
    fseek(fso, 0, SEEK_SET);
    uint8_t *so_data = (uint8_t *)ny_alloc(so_sz);
    fread(so_data, 1, so_sz, fso);
    fclose(fso);

    uint16_t e_type = (uint16_t)(so_data[16] | (so_data[17] << 8));
    TEST_ASSERT_EQ(e_type, 3);

    uint16_t e_machine = (uint16_t)(so_data[18] | (so_data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    ny_free(so_data, so_sz);
    remove("bin/test_aarch64_cli_so.ny");
    remove("bin/test_aarch64_cli_so.o");
    remove("bin/libtest_aarch64_cli.so");
}

void test_aarch64_deterministic_object_across_runs(void) {
    static const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %a = const 40;\n"
        "    %b = const 2;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n";

    uint8_t *obj1;
    size_t size1;
    TEST_ASSERT(compile_aarch64_obj_raw(src, &obj1, &size1));

    uint8_t *obj2;
    size_t size2;
    TEST_ASSERT(compile_aarch64_obj_raw(src, &obj2, &size2));

    TEST_ASSERT_EQ(size1, size2);
    TEST_ASSERT(memcmp(obj1, obj2, size1) == 0);

    ny_free(obj1, size1);
    ny_free(obj2, size2);
}

void test_aarch64_deterministic_executable_across_runs(void) {
    static const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %a = const 40;\n"
        "    %b = const 2;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n";

    uint8_t *obj_data;
    size_t obj_size;
    TEST_ASSERT(compile_aarch64_obj_raw(src, &obj_data, &obj_size));

    char *outputs[2] = {0};
    size_t output_sizes[2] = {0};

    for (int round = 0; round < 2; round++) {
        Nylink_Context *ctx = nylink_context_create();
        nylink_add_object(ctx, "main.o", obj_data, obj_size);
        nylink_resolve_symbols(ctx);

        Nylink_Config cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.target_format = NYLINK_TARGET_ELF64;
        cfg.output_mode = NYLINK_OUTPUT_EXECUTABLE;
        cfg.entry_point = "main";

        nylink_layout(ctx, &cfg);
        nylink_apply_relocations(ctx);

        char path[64];
        snprintf(path, sizeof(path), "bin/test_aarch64_harden_exec%d.elf", round);
        nylink_write_executable(ctx, path, &cfg);

        FILE *f = fopen(path, "rb");
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        outputs[round] = (char *)ny_alloc(sz);
        fread(outputs[round], 1, sz, f);
        output_sizes[round] = sz;
        fclose(f);
        remove(path);

        nylink_context_destroy(ctx);
    }

    TEST_ASSERT_EQ(output_sizes[0], output_sizes[1]);
    TEST_ASSERT(memcmp(outputs[0], outputs[1], output_sizes[0]) == 0);

    ny_free(outputs[0], output_sizes[0]);
    ny_free(outputs[1], output_sizes[1]);
    ny_free(obj_data, obj_size);
}
