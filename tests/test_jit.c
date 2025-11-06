// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
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

    Ny_JIT_Config cfg;
    ny_jit_config_init(&cfg);

    Ny_JIT_Engine *jit = ny_jit_create(&cfg);
    TEST_ASSERT(jit != NULL);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    bool ok = ny_jit_compile(jit, src, strlen(src), &diags, &diag_count);
    if (!ok) {
        for (size_t i = 0; i < diag_count; i++) {
            fprintf(stderr, "JIT diagnostic: %s\n", diags[i].message);
        }
    }
    TEST_ASSERT(ok);
    TEST_ASSERT_EQ(diag_count, 0);

    typedef int32_t (*MainFn)(void);
    MainFn fn = (MainFn)ny_jit_lookup(jit, "main");
    TEST_ASSERT(fn != NULL);

    int32_t result = fn();
    TEST_ASSERT_EQ(result, 42);

    ny_jit_destroy(jit);
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

    Ny_JIT_Config cfg;
    ny_jit_config_init(&cfg);

    Ny_JIT_Engine *jit = ny_jit_create(&cfg);
    TEST_ASSERT(jit != NULL);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    bool ok = ny_jit_compile(jit, src, strlen(src), &diags, &diag_count);
    TEST_ASSERT(ok);

    typedef int32_t (*MainFn)(void);
    MainFn fn = (MainFn)ny_jit_lookup(jit, "main");
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT_EQ(fn(), 42);

    typedef int32_t (*MulFn)(int32_t, int32_t);
    MulFn mul_fn = (MulFn)ny_jit_lookup(jit, "helper_mul");
    TEST_ASSERT(mul_fn != NULL);
    TEST_ASSERT_EQ(mul_fn(3, 9), 27);

    ny_jit_destroy(jit);
}

void test_jit_scalar_arguments_and_return(void) {
    const char *src =
        "@function add_three(%a: i32, %b: i32, %c: i32) -> i32;\n"
        ".entry;\n"
        "    %t = add %a, %b;\n"
        "    %res = add %t, %c;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_JIT_Config cfg;
    ny_jit_config_init(&cfg);

    Ny_JIT_Engine *jit = ny_jit_create(&cfg);
    TEST_ASSERT(jit != NULL);

    bool ok = ny_jit_compile(jit, src, strlen(src), NULL, NULL);
    TEST_ASSERT(ok);

    typedef int32_t (*Add3Fn)(int32_t, int32_t, int32_t);
    Add3Fn fn = (Add3Fn)ny_jit_lookup(jit, "add_three");
    TEST_ASSERT(fn != NULL);

    TEST_ASSERT_EQ(fn(10, 20, 12), 42);
    TEST_ASSERT_EQ(fn(-5, 5, 0), 0);
    TEST_ASSERT_EQ(fn(100, -50, 25), 75);

    ny_jit_destroy(jit);
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

    Ny_JIT_Config cfg;
    ny_jit_config_init(&cfg);

    Ny_JIT_Engine *jit = ny_jit_create(&cfg);
    TEST_ASSERT(jit != NULL);

    bool ok = ny_jit_compile(jit, src, strlen(src), NULL, NULL);
    TEST_ASSERT(ok);

    typedef int32_t (*ComputeFn)(void);
    ComputeFn fn = (ComputeFn)ny_jit_lookup(jit, "compute_globals");
    TEST_ASSERT(fn != NULL);

    int32_t *g_accum_ptr = (int32_t *)ny_jit_lookup(jit, "g_accum");
    int32_t *g_temp_ptr = (int32_t *)ny_jit_lookup(jit, "g_temp");
    TEST_ASSERT(g_accum_ptr != NULL);
    TEST_ASSERT(g_temp_ptr != NULL);
    TEST_ASSERT_EQ(*g_accum_ptr, 12);

    int32_t res1 = fn();
    TEST_ASSERT_EQ(res1, 42);
    TEST_ASSERT_EQ(*g_temp_ptr, 42);

    /* Mutate mutable global and re-execute */
    *g_accum_ptr = 20;
    int32_t res2 = fn();
    TEST_ASSERT_EQ(res2, 50);
    TEST_ASSERT_EQ(*g_temp_ptr, 50);

    ny_jit_destroy(jit);
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

    Ny_JIT_Symbol syms[] = {
        { "host_mult_add", (const void *)host_mult_add_fn }
    };

    Ny_JIT_Config cfg;
    ny_jit_config_init(&cfg);
    cfg.symbols = syms;
    cfg.symbol_count = 1;

    Ny_JIT_Engine *jit = ny_jit_create(&cfg);
    TEST_ASSERT(jit != NULL);

    bool ok = ny_jit_compile(jit, src, strlen(src), NULL, NULL);
    TEST_ASSERT(ok);

    typedef int32_t (*MainFn)(void);
    MainFn fn = (MainFn)ny_jit_lookup(jit, "main");
    TEST_ASSERT(fn != NULL);

    TEST_ASSERT_EQ(fn(), 42);

    ny_jit_destroy(jit);
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

    for (int i = 0; i < 50; i++) {
        Ny_JIT_Config cfg;
        ny_jit_config_init(&cfg);

        Ny_JIT_Engine *jit = ny_jit_create(&cfg);
        TEST_ASSERT(jit != NULL);

        bool ok = ny_jit_compile(jit, src, strlen(src), NULL, NULL);
        TEST_ASSERT(ok);

        typedef int32_t (*MainFn)(void);
        MainFn fn = (MainFn)ny_jit_lookup(jit, "main");
        TEST_ASSERT(fn != NULL);
        TEST_ASSERT_EQ(fn(), 300);

        ny_jit_destroy(jit);
    }
}

void test_jit_unresolved_symbol_diagnostic(void) {
    const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %res = call @missing_symbol_xyz;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_JIT_Config cfg;
    ny_jit_config_init(&cfg);

    Ny_JIT_Engine *jit = ny_jit_create(&cfg);
    TEST_ASSERT(jit != NULL);

    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    bool ok = ny_jit_compile(jit, src, strlen(src), &diags, &diag_count);
    TEST_ASSERT(!ok);
    TEST_ASSERT(diag_count > 0);
    TEST_ASSERT(strstr(diags[0].message, "missing_symbol_xyz") != NULL);

    ny_jit_diagnostics_destroy(diags, diag_count);

    ny_jit_destroy(jit);
}
