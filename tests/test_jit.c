// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nyjit/nyjit.h>
#include <nybit/support.h>
#include <string.h>

void test_jit_simple_arithmetic(void) {
    const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %a = const 20;\n"
        "    %b = const 22;\n"
        "    %res = add %a, %b;\n"
        "    @return %res;\n"
        ";;\n";

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    Nygen_Diagnostic *gen_diags = NULL;
    size_t gen_diag_count = 0;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, &gen_diags, &gen_diag_count);
    if (!gen_ok) {
        for (size_t i = 0; i < gen_diag_count; i++) {
            fprintf(stderr, "gen diagnostic: %s\n", gen_diags[i].message);
        }
    }
    TEST_ASSERT(gen_ok);
    TEST_ASSERT_EQ(gen_diag_count, 0);

    Nyjit_Config jit_cfg;
    nyjit_config_init(&jit_cfg);

    Nyjit_Module *jit = nyjit_create(&jit_cfg);
    TEST_ASSERT(jit != NULL);

    Nygen_Diagnostic *jit_diags = NULL;
    size_t jit_diag_count = 0;
    bool link_ok = nyjit_link(jit, &emod, &jit_diags, &jit_diag_count);
    TEST_ASSERT(link_ok);
    TEST_ASSERT_EQ(jit_diag_count, 0);

    typedef int32_t (*MainFn)(void);
    MainFn fn = (MainFn)nyjit_lookup(jit, "main");
    TEST_ASSERT(fn != NULL);

    int32_t result = fn();
    TEST_ASSERT_EQ(result, 42);

    nyjit_destroy(jit);
    nygen_encoded_module_destroy(&emod);
}

void test_jit_multi_function_internal_calls(void) {
    const char *src =
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

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, NULL, NULL);
    TEST_ASSERT(gen_ok);

    Nyjit_Config jit_cfg;
    nyjit_config_init(&jit_cfg);

    Nyjit_Module *jit = nyjit_create(&jit_cfg);
    TEST_ASSERT(jit != NULL);

    bool link_ok = nyjit_link(jit, &emod, NULL, NULL);
    TEST_ASSERT(link_ok);

    typedef int32_t (*MainFn)(void);
    MainFn fn = (MainFn)nyjit_lookup(jit, "main");
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT_EQ(fn(), 42);

    typedef int32_t (*MulFn)(int32_t, int32_t);
    MulFn mul_fn = (MulFn)nyjit_lookup(jit, "helper_mul");
    TEST_ASSERT(mul_fn != NULL);
    TEST_ASSERT_EQ(mul_fn(3, 9), 27);

    nyjit_destroy(jit);
    nygen_encoded_module_destroy(&emod);
}

void test_jit_scalar_arguments_and_return(void) {
    const char *src =
        "@function add_three(%a: i32, %b: i32, %c: i32) -> i32;\n"
        ".entry;\n"
        "    %t = add %a, %b;\n"
        "    %res = add %t, %c;\n"
        "    @return %res;\n"
        ";;\n";

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, NULL, NULL);
    TEST_ASSERT(gen_ok);

    Nyjit_Config jit_cfg;
    nyjit_config_init(&jit_cfg);

    Nyjit_Module *jit = nyjit_create(&jit_cfg);
    TEST_ASSERT(jit != NULL);

    bool link_ok = nyjit_link(jit, &emod, NULL, NULL);
    TEST_ASSERT(link_ok);

    typedef int32_t (*Add3Fn)(int32_t, int32_t, int32_t);
    Add3Fn fn = (Add3Fn)nyjit_lookup(jit, "add_three");
    TEST_ASSERT(fn != NULL);

    TEST_ASSERT_EQ(fn(10, 20, 12), 42);
    TEST_ASSERT_EQ(fn(-5, 5, 0), 0);
    TEST_ASSERT_EQ(fn(100, -50, 25), 75);

    nyjit_destroy(jit);
    nygen_encoded_module_destroy(&emod);
}

void test_jit_globals_read_write(void) {
    const char *src =
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

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, NULL, NULL);
    TEST_ASSERT(gen_ok);

    Nyjit_Config jit_cfg;
    nyjit_config_init(&jit_cfg);

    Nyjit_Module *jit = nyjit_create(&jit_cfg);
    TEST_ASSERT(jit != NULL);

    bool link_ok = nyjit_link(jit, &emod, NULL, NULL);
    TEST_ASSERT(link_ok);

    typedef int32_t (*ComputeFn)(void);
    ComputeFn fn = (ComputeFn)nyjit_lookup(jit, "compute_globals");
    TEST_ASSERT(fn != NULL);

    int32_t *g_accum_ptr = (int32_t *)nyjit_lookup(jit, "g_accum");
    int32_t *g_temp_ptr = (int32_t *)nyjit_lookup(jit, "g_temp");
    TEST_ASSERT(g_accum_ptr != NULL);
    TEST_ASSERT(g_temp_ptr != NULL);
    TEST_ASSERT_EQ(*g_accum_ptr, 12);

    int32_t res1 = fn();
    TEST_ASSERT_EQ(res1, 42);
    TEST_ASSERT_EQ(*g_temp_ptr, 42);

    *g_accum_ptr = 20;
    int32_t res2 = fn();
    TEST_ASSERT_EQ(res2, 50);
    TEST_ASSERT_EQ(*g_temp_ptr, 50);

    nyjit_destroy(jit);
    nygen_encoded_module_destroy(&emod);
}

static int32_t host_mult_add_fn(int32_t a, int32_t b, int32_t c) {
    return (a * b) + c;
}

void test_jit_external_host_function(void) {
    const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %c5 = const 5;\n"
        "    %c8 = const 8;\n"
        "    %c2 = const 2;\n"
        "    %res = call @host_mult_add, %c5, %c8, %c2;\n"
        "    @return %res;\n"
        ";;\n";

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, NULL, NULL);
    TEST_ASSERT(gen_ok);

    Nyjit_Symbol syms[] = {
        { "host_mult_add", (const void *)host_mult_add_fn }
    };

    Nyjit_Config jit_cfg;
    nyjit_config_init(&jit_cfg);
    jit_cfg.symbols = syms;
    jit_cfg.symbol_count = 1;

    Nyjit_Module *jit = nyjit_create(&jit_cfg);
    TEST_ASSERT(jit != NULL);

    bool link_ok = nyjit_link(jit, &emod, NULL, NULL);
    TEST_ASSERT(link_ok);

    typedef int32_t (*MainFn)(void);
    MainFn fn = (MainFn)nyjit_lookup(jit, "main");
    TEST_ASSERT(fn != NULL);

    TEST_ASSERT_EQ(fn(), 42);

    nyjit_destroy(jit);
    nygen_encoded_module_destroy(&emod);
}

void test_jit_lifecycle_and_zero_leak(void) {
    const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %a = const 100;\n"
        "    %b = const 200;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n";

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, NULL, NULL);
    TEST_ASSERT(gen_ok);

    for (int i = 0; i < 50; i++) {
        Nyjit_Config jit_cfg;
        nyjit_config_init(&jit_cfg);

        Nyjit_Module *jit = nyjit_create(&jit_cfg);
        TEST_ASSERT(jit != NULL);

        bool link_ok = nyjit_link(jit, &emod, NULL, NULL);
        TEST_ASSERT(link_ok);

        typedef int32_t (*MainFn)(void);
        MainFn fn = (MainFn)nyjit_lookup(jit, "main");
        TEST_ASSERT(fn != NULL);
        TEST_ASSERT_EQ(fn(), 300);

        nyjit_destroy(jit);
    }

    nygen_encoded_module_destroy(&emod);
}

void test_jit_unresolved_symbol_diagnostic(void) {
    const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %res = call @missing_symbol_xyz;\n"
        "    @return %res;\n"
        ";;\n";

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, NULL, NULL);
    TEST_ASSERT(gen_ok);

    Nyjit_Config jit_cfg;
    nyjit_config_init(&jit_cfg);

    Nyjit_Module *jit = nyjit_create(&jit_cfg);
    TEST_ASSERT(jit != NULL);

    Nygen_Diagnostic *jit_diags = NULL;
    size_t jit_diag_count = 0;
    bool link_ok = nyjit_link(jit, &emod, &jit_diags, &jit_diag_count);
    TEST_ASSERT(!link_ok);
    TEST_ASSERT(jit_diag_count > 0);
    TEST_ASSERT(strstr(jit_diags[0].message, "missing_symbol_xyz") != NULL);

    nyjit_diagnostics_destroy(jit_diags, jit_diag_count);
    nyjit_destroy(jit);
    nygen_encoded_module_destroy(&emod);
}
