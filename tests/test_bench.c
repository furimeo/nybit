// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nybit/support.h>
#include <string.h>

static const char *s_bench_prog =
    "@function helper_add(%x: i32, %y: i32) -> i32;\n"
    ".entry;\n"
    "    %sum = add %x, %y;\n"
    "    @return %sum;\n"
    ";;\n"
    "\n"
    "@function helper_mul(%x: i32, %y: i32) -> i32;\n"
    ".entry;\n"
    "    %prod = mul %x, %y;\n"
    "    @return %prod;\n"
    ";;\n"
    "\n"
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %c1 = const 5;\n"
    "    %c2 = const 7;\n"
    "    %a = call @helper_add, %c1, %c2;\n"
    "    %c3 = const 3;\n"
    "    %b = call @helper_mul, %a, %c3;\n"
    "    %c6 = const 6;\n"
    "    %res = add %b, %c6;\n"
    "    @return %res;\n"
    ";;\n";

void test_benchmark_medium_repeat(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.opt_level = NYGEN_OPT_O1;

    size_t src_len = strlen(s_bench_prog);
    size_t expected_size = 0;

    for (int i = 0; i < 50; i++) {
        Nygen_Result res = nygen_compile(s_bench_prog, src_len, &cfg);
        TEST_ASSERT(res.success);
        TEST_ASSERT(res.size > 0);
        TEST_ASSERT(res.data != NULL);

        if (i == 0) {
            expected_size = res.size;
        } else {
            TEST_ASSERT_EQ(res.size, expected_size);
        }

        nygen_result_destroy(&res);
    }
}
