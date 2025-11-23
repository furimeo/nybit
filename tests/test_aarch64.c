// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nybit/support.h>
#include <nybit/object.h>
#include <nybit/target.h>
#include <nybit/target_aarch64.h>
#include <nylink/nylink.h>
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

static const char *s_call_ny =
    "@function helper() -> i32;\n"
    ".entry;\n"
    "    %v = const 42;\n"
    "    @return %v;\n"
    ";;\n"
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %r = call @helper;\n"
    "    @return %r;\n"
    ";;\n";

static const char *s_global_ny =
    "@global @readonly @answer: i32 = 42;\n"
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %p = global_addr @answer;\n"
    "    %v = load %p;\n"
    "    @return %v;\n"
    ";;\n";

static const char *s_branch_ny =
    "@function main(%n: i32) -> i32;\n"
    ".entry;\n"
    "    %z = const 0;\n"
    "    %c = cmp.eq %n, %z;\n"
    "    @branch_if %c, .zero, .nonzero;\n"
    ".zero;\n"
    "    @return %n;\n"
    ".nonzero;\n"
    "    %one = const 1;\n"
    "    %r = sub %n, %one;\n"
    "    @return %r;\n"
    ";;\n";

void test_aarch64_target_registration(void) {
    const Ny_Target *t = ny_target_find("aarch64");
    TEST_ASSERT(t != NULL);
    TEST_ASSERT_EQ(t->arch, NY_ARCH_AARCH64);
    TEST_ASSERT_EQ(t->abi, NY_ABI_AAPCS64);
    TEST_ASSERT_EQ(t->info.ptr_size, 8);

    const Ny_Target *t2 = ny_target_find("aarch64-linux");
    TEST_ASSERT(t2 != NULL);
    TEST_ASSERT(t2 == t);

    const Ny_Target *t3 = ny_target_find("aarch64-sysv");
    TEST_ASSERT(t3 != NULL);
    TEST_ASSERT(t3 == t);
}

void test_aarch64_compile_asm(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_ASM;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(strstr((const char *)res.data, "main:") != NULL);
    TEST_ASSERT(strstr((const char *)res.data, "ret") != NULL);

    nygen_result_destroy(&res);
}

void test_aarch64_compile_bytes(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_BYTES;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size > 0);
    TEST_ASSERT_EQ(res.size % 4, 0);

    nygen_result_destroy(&res);
}

void test_aarch64_compile_elf_object(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);

    TEST_ASSERT_EQ(res.data[0], 0x7F);
    TEST_ASSERT_EQ(res.data[1], 'E');
    TEST_ASSERT_EQ(res.data[2], 'L');
    TEST_ASSERT_EQ(res.data[3], 'F');
    TEST_ASSERT_EQ(res.data[4], 2);

    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    uint16_t e_type = (uint16_t)(res.data[16] | (res.data[17] << 8));
    TEST_ASSERT_EQ(e_type, 1);

    nygen_result_destroy(&res);
}

void test_aarch64_compile_call_elf(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_call_ny, strlen(s_call_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);

    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    nygen_result_destroy(&res);
}

void test_aarch64_compile_global_elf(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_global_ny, strlen(s_global_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);

    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    nygen_result_destroy(&res);
}

void test_aarch64_compile_branch_elf(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_branch_ny, strlen(s_branch_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size >= 64);

    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    nygen_result_destroy(&res);
}

void test_aarch64_object_determinism(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res1 = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res1.success);
    TEST_ASSERT(res1.size > 0);

    Nygen_Result res2 = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res2.success);
    TEST_ASSERT(res2.size > 0);

    TEST_ASSERT_EQ(res1.size, res2.size);
    TEST_ASSERT(memcmp(res1.data, res2.data, res1.size) == 0);

    nygen_result_destroy(&res1);
    nygen_result_destroy(&res2);
}

void test_aarch64_bytes_determinism(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_BYTES;
    cfg.target_triple = "aarch64";

    Nygen_Result res1 = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    Nygen_Result res2 = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);

    TEST_ASSERT(res1.success);
    TEST_ASSERT(res2.success);
    TEST_ASSERT_EQ(res1.size, res2.size);
    TEST_ASSERT(memcmp(res1.data, res2.data, res1.size) == 0);

    nygen_result_destroy(&res1);
    nygen_result_destroy(&res2);
}

void test_aarch64_call_determinism(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res1 = nygen_compile(s_call_ny, strlen(s_call_ny), &cfg);
    Nygen_Result res2 = nygen_compile(s_call_ny, strlen(s_call_ny), &cfg);

    TEST_ASSERT(res1.success);
    TEST_ASSERT(res2.success);
    TEST_ASSERT_EQ(res1.size, res2.size);
    TEST_ASSERT(memcmp(res1.data, res2.data, res1.size) == 0);

    nygen_result_destroy(&res1);
    nygen_result_destroy(&res2);
}

void test_aarch64_machine_ir(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_MACHINE_IR;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(strstr((const char *)res.data, "machine_function") != NULL);

    nygen_result_destroy(&res);
}

void test_aarch64_regalloc_spill(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    const char *many_vars =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %a = const 1;\n"
        "    %b = const 2;\n"
        "    %c = const 3;\n"
        "    %d = const 4;\n"
        "    %e = const 5;\n"
        "    %f = const 6;\n"
        "    %g = const 7;\n"
        "    %h = const 8;\n"
        "    %i = const 9;\n"
        "    %j = const 10;\n"
        "    %k = const 11;\n"
        "    %l = const 12;\n"
        "    %m = const 13;\n"
        "    %n = const 14;\n"
        "    %o = const 15;\n"
        "    %p = const 16;\n"
        "    %q = const 17;\n"
        "    %r = const 18;\n"
        "    %s = const 19;\n"
        "    %t = const 20;\n"
        "    %u = const 21;\n"
        "    %v = const 22;\n"
        "    %w = const 23;\n"
        "    %x = const 24;\n"
        "    %y = const 25;\n"
        "    %z = const 26;\n"
        "    %aa = const 27;\n"
        "    %bb = const 28;\n"
        "    %cc = const 29;\n"
        "    %dd = const 30;\n"
        "    %ee = const 31;\n"
        "    %ff = const 32;\n"
        "    %gg = const 33;\n"
        "    %hh = const 34;\n"
        "    %ii = const 35;\n"
        "    %jj = const 36;\n"
        "    %kk = const 37;\n"
        "    %ll = const 38;\n"
        "    %mm = const 39;\n"
        "    %nn = const 40;\n"
        "    %oo = const 41;\n"
        "    %pp = const 42;\n"
        "    %qq = add %a, %b;\n"
        "    %rr = add %qq, %c;\n"
        "    @return %rr;\n"
        ";;\n";

    Nygen_Result res = nygen_compile(many_vars, strlen(many_vars), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size > 0);

    uint16_t e_machine = (uint16_t)(res.data[18] | (res.data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    nygen_result_destroy(&res);
}

void test_aarch64_nylink_static_link(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    TEST_ASSERT(res.size > 0);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(ctx != NULL);

    bool added = nylink_add_object(ctx, "aarch64_test.o", res.data, res.size);
    TEST_ASSERT(added);

    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(resolved);
    TEST_ASSERT(!nylink_has_errors(ctx));

    Nylink_Config link_cfg;
    memset(&link_cfg, 0, sizeof(link_cfg));
    link_cfg.target_format = NYLINK_TARGET_ELF64;
    link_cfg.output_mode = NYLINK_OUTPUT_EXECUTABLE;
    link_cfg.entry_point = "main";

    bool laid = nylink_layout(ctx, &link_cfg);
    TEST_ASSERT(laid);

    bool relocs = nylink_apply_relocations(ctx);
    TEST_ASSERT(relocs);

    bool written = nylink_write_executable(ctx, "bin/test_aarch64_static.elf", &link_cfg);
    TEST_ASSERT(written);

    nylink_context_destroy(ctx);
    nygen_result_destroy(&res);
}

void test_aarch64_nylink_multi_object(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    static const char *helper_src =
        "@function helper() -> i32;\n"
        ".entry;\n"
        "    %v = const 42;\n"
        "    @return %v;\n"
        ";;\n";

    static const char *caller_src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %r = call @helper;\n"
        "    @return %r;\n"
        ";;\n";

    Nygen_Result res1 = nygen_compile(helper_src, strlen(helper_src), &cfg);
    TEST_ASSERT(res1.success);

    Nygen_Result res2 = nygen_compile(caller_src, strlen(caller_src), &cfg);
    TEST_ASSERT(res2.success);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(ctx != NULL);

    bool added1 = nylink_add_object(ctx, "helper.o", res1.data, res1.size);
    TEST_ASSERT(added1);

    bool added2 = nylink_add_object(ctx, "caller.o", res2.data, res2.size);
    TEST_ASSERT(added2);

    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(resolved);
    TEST_ASSERT(!nylink_has_errors(ctx));

    Nylink_Config link_cfg;
    memset(&link_cfg, 0, sizeof(link_cfg));
    link_cfg.target_format = NYLINK_TARGET_ELF64;
    link_cfg.output_mode = NYLINK_OUTPUT_EXECUTABLE;
    link_cfg.entry_point = "main";

    bool laid = nylink_layout(ctx, &link_cfg);
    TEST_ASSERT(laid);

    bool relocs = nylink_apply_relocations(ctx);
    TEST_ASSERT(relocs);

    bool written = nylink_write_executable(ctx, "bin/test_aarch64_multi.elf", &link_cfg);
    TEST_ASSERT(written);

    nylink_context_destroy(ctx);
    nygen_result_destroy(&res1);
    nygen_result_destroy(&res2);
}

void test_aarch64_nylink_determinism(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res.success);

    for (int round = 0; round < 2; round++) {
        Nylink_Context *ctx = nylink_context_create();
        TEST_ASSERT(ctx != NULL);

        bool added = nylink_add_object(ctx, "aarch64_test.o", res.data, res.size);
        TEST_ASSERT(added);

        bool resolved = nylink_resolve_symbols(ctx);
        TEST_ASSERT(resolved);

        Nylink_Config link_cfg;
        memset(&link_cfg, 0, sizeof(link_cfg));
        link_cfg.target_format = NYLINK_TARGET_ELF64;
        link_cfg.output_mode = NYLINK_OUTPUT_EXECUTABLE;
        link_cfg.entry_point = "main";

        bool laid = nylink_layout(ctx, &link_cfg);
        TEST_ASSERT(laid);

        bool relocs = nylink_apply_relocations(ctx);
        TEST_ASSERT(relocs);

        const char *out_path = (round == 0) ? "bin/test_aarch64_det1.elf" : "bin/test_aarch64_det2.elf";
        bool written = nylink_write_executable(ctx, out_path, &link_cfg);
        TEST_ASSERT(written);

        nylink_context_destroy(ctx);
    }

    FILE *f1 = fopen("bin/test_aarch64_det1.elf", "rb");
    FILE *f2 = fopen("bin/test_aarch64_det2.elf", "rb");
    TEST_ASSERT(f1 != NULL);
    TEST_ASSERT(f2 != NULL);

    fseek(f1, 0, SEEK_END);
    fseek(f2, 0, SEEK_END);
    long sz1 = ftell(f1);
    long sz2 = ftell(f2);
    TEST_ASSERT_EQ(sz1, sz2);

    fseek(f1, 0, SEEK_SET);
    fseek(f2, 0, SEEK_SET);
    uint8_t buf1[4096];
    uint8_t buf2[4096];
    size_t remaining = (size_t)sz1;
    bool match = true;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buf1) ? sizeof(buf1) : remaining;
        size_t r1 = fread(buf1, 1, chunk, f1);
        size_t r2 = fread(buf2, 1, chunk, f2);
        if (r1 != chunk || r2 != chunk || memcmp(buf1, buf2, chunk) != 0) {
            match = false;
            break;
        }
        remaining -= chunk;
    }
    TEST_ASSERT(match);

    fclose(f1);
    fclose(f2);
    remove("bin/test_aarch64_det1.elf");
    remove("bin/test_aarch64_det2.elf");

    nygen_result_destroy(&res);
}

void test_aarch64_nylink_unresolved_symbol(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    const char *unresolved =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %r = call @missing;\n"
        "    @return %r;\n"
        ";;\n";

    Nygen_Result res = nygen_compile(unresolved, strlen(unresolved), &cfg);
    TEST_ASSERT(res.success);

    Nylink_Context *ctx = nylink_context_create();
    bool added = nylink_add_object(ctx, "test.o", res.data, res.size);
    TEST_ASSERT(added);

    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(!resolved || nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
    nygen_result_destroy(&res);
}

void test_aarch64_nylink_arch_mismatch(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "x86_64-sysv";

    Nygen_Result res_x86 = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res_x86.success);

    cfg.target_triple = "aarch64";
    Nygen_Result res_aarch64 = nygen_compile(s_simple_ny, strlen(s_simple_ny), &cfg);
    TEST_ASSERT(res_aarch64.success);

    Nylink_Context *ctx = nylink_context_create();
    nylink_add_object(ctx, "x86.o", res_x86.data, res_x86.size);
    bool added2 = nylink_add_object(ctx, "aarch64.o", res_aarch64.data, res_aarch64.size);
    TEST_ASSERT(!added2);

    nylink_context_destroy(ctx);
    nygen_result_destroy(&res_x86);
    nygen_result_destroy(&res_aarch64);
}

void test_aarch64_jit_compile_encoded(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.target_triple = "aarch64";

    Nygen_Encoded_Module mod;
    Nygen_Diagnostic *diags = nullptr;
    size_t diag_count = 0;

    bool ok = nygen_compile_encoded(s_simple_ny, strlen(s_simple_ny), &cfg, &mod, &diags, &diag_count);
    TEST_ASSERT(ok);
    TEST_ASSERT(mod.text != nullptr);
    TEST_ASSERT(mod.text_size > 0);
    TEST_ASSERT_EQ(mod.text_size % 4, 0);
    TEST_ASSERT(mod.symbol_count > 0);

    if (diags) nygen_diagnostics_destroy(diags, diag_count);
    nygen_encoded_module_destroy(&mod);
}

void test_aarch64_jit_determinism(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.target_triple = "aarch64";

    Nygen_Encoded_Module mod1, mod2;
    Nygen_Diagnostic *diags1 = nullptr;
    Nygen_Diagnostic *diags2 = nullptr;
    size_t diag_count1 = 0;
    size_t diag_count2 = 0;

    bool ok1 = nygen_compile_encoded(s_simple_ny, strlen(s_simple_ny), &cfg, &mod1, &diags1, &diag_count1);
    bool ok2 = nygen_compile_encoded(s_simple_ny, strlen(s_simple_ny), &cfg, &mod2, &diags2, &diag_count2);

    TEST_ASSERT(ok1);
    TEST_ASSERT(ok2);
    TEST_ASSERT_EQ(mod1.text_size, mod2.text_size);
    TEST_ASSERT(memcmp(mod1.text, mod2.text, mod1.text_size) == 0);

    if (diags1) nygen_diagnostics_destroy(diags1, diag_count1);
    if (diags2) nygen_diagnostics_destroy(diags2, diag_count2);
    nygen_encoded_module_destroy(&mod1);
    nygen_encoded_module_destroy(&mod2);
}
