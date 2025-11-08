// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nyjit/nyjit.h>
#include <nybit/support.h>
#include <string.h>

void test_jit_nygen_standalone_encoded(void) {
    const char *src =
        "@function add(%a: i32, %b: i32) -> i32;\n"
        ".entry;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n"
        "\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %c3 = const 3;\n"
        "    %c4 = const 4;\n"
        "    %res = call @add, %c3, %c4;\n"
        "    @return %res;\n"
        ";;\n";

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, NULL, NULL);
    TEST_ASSERT(gen_ok);

    TEST_ASSERT(emod.text != NULL);
    TEST_ASSERT(emod.text_size > 0);
    TEST_ASSERT_EQ(emod.symbol_count, 2);

    bool found_add = false;
    bool found_main = false;
    for (size_t i = 0; i < emod.symbol_count; i++) {
        if (strcmp(emod.symbols[i].name, "add") == 0) {
            TEST_ASSERT_EQ(emod.symbols[i].kind, NYGEN_SYM_FUNCTION);
            found_add = true;
        }
        if (strcmp(emod.symbols[i].name, "main") == 0) {
            TEST_ASSERT_EQ(emod.symbols[i].kind, NYGEN_SYM_FUNCTION);
            found_main = true;
        }
    }
    TEST_ASSERT(found_add);
    TEST_ASSERT(found_main);

    nygen_encoded_module_destroy(&emod);
    TEST_ASSERT(emod.text == NULL);
    TEST_ASSERT_EQ(emod.symbol_count, 0);
}

void test_jit_nygen_to_nyjit_execute(void) {
    const char *src =
        "@function square(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %r = mul %x, %x;\n"
        "    @return %r;\n"
        ";;\n"
        "\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %c6 = const 6;\n"
        "    %sq = call @square, %c6;\n"
        "    %c6b = const 6;\n"
        "    %res = add %sq, %c6b;\n"
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

    typedef int32_t (*MainFn)(void);
    MainFn fn = (MainFn)nyjit_lookup(jit, "main");
    TEST_ASSERT(fn != NULL);
    TEST_ASSERT_EQ(fn(), 42);

    typedef int32_t (*SqFn)(int32_t);
    SqFn sq_fn = (SqFn)nyjit_lookup(jit, "square");
    TEST_ASSERT(sq_fn != NULL);
    TEST_ASSERT_EQ(sq_fn(5), 25);
    TEST_ASSERT_EQ(sq_fn(-7), 49);

    nyjit_destroy(jit);
    nygen_encoded_module_destroy(&emod);
}

void test_jit_nygen_compile_error_diagnostic(void) {
    const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %a = const 10;\n"
        "    @return %a %b;\n"
        ";;\n";

    Nygen_Config gen_cfg;
    nygen_config_init(&gen_cfg);

    Nygen_Encoded_Module emod;
    Nygen_Diagnostic *diags = NULL;
    size_t diag_count = 0;
    bool gen_ok = nygen_compile_encoded(src, strlen(src), &gen_cfg, &emod, &diags, &diag_count);
    TEST_ASSERT(!gen_ok);
    TEST_ASSERT(diag_count > 0);

    nygen_diagnostics_destroy(diags, diag_count);
}
