// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <nygen/nygen.h>
#include <nyjit/nyjit.h>
#include <nybit/support.h>

typedef struct {
    const char *name;
    const char *ir_src;
} Bench_Case;

static const char *s_prog_tiny =
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %a = const 10;\n"
    "    %b = const 20;\n"
    "    %c = add %a, %b;\n"
    "    %d = const 12;\n"
    "    %res = add %c, %d;\n"
    "    @return %res;\n"
    ";;\n";

static const char *s_prog_medium =
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

static const char *s_prog_pressure =
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %v1 = const 1;\n"
    "    %v2 = const 2;\n"
    "    %v3 = const 3;\n"
    "    %v4 = const 4;\n"
    "    %v5 = const 5;\n"
    "    %v6 = const 6;\n"
    "    %v7 = const 7;\n"
    "    %v8 = const 8;\n"
    "    %v9 = const 9;\n"
    "    %v10 = const 10;\n"
    "    %v11 = const 11;\n"
    "    %v12 = const 12;\n"
    "    %v13 = const 13;\n"
    "    %v14 = const 14;\n"
    "    %v15 = const 15;\n"
    "    %v16 = const 16;\n"
    "    %s1 = add %v1, %v2;\n"
    "    %s2 = add %v3, %v4;\n"
    "    %s3 = add %v5, %v6;\n"
    "    %s4 = add %v7, %v8;\n"
    "    %s5 = add %v9, %v10;\n"
    "    %s6 = add %v11, %v12;\n"
    "    %s7 = add %v13, %v14;\n"
    "    %s8 = add %v15, %v16;\n"
    "    %t1 = add %s1, %s2;\n"
    "    %t2 = add %s3, %s4;\n"
    "    %t3 = add %s5, %s6;\n"
    "    %t4 = add %s7, %s8;\n"
    "    %m1 = add %t1, %t2;\n"
    "    %m2 = add %t3, %t4;\n"
    "    %res = add %m1, %m2;\n"
    "    @return %res;\n"
    ";;\n";

static const char *s_prog_module =
    "@global g_val: i32 = 100;\n"
    "\n"
    "@function f1(%x: i32) -> i32;\n"
    ".entry;\n"
    "    %c = const 1;\n"
    "    %r = add %x, %c;\n"
    "    @return %r;\n"
    ";;\n"
    "\n"
    "@function f2(%x: i32) -> i32;\n"
    ".entry;\n"
    "    %c = const 2;\n"
    "    %r = add %x, %c;\n"
    "    @return %r;\n"
    ";;\n"
    "\n"
    "@function f3(%x: i32) -> i32;\n"
    ".entry;\n"
    "    %c = const 3;\n"
    "    %r = add %x, %c;\n"
    "    @return %r;\n"
    ";;\n"
    "\n"
    "@function f4(%x: i32) -> i32;\n"
    ".entry;\n"
    "    %c = const 4;\n"
    "    %r = add %x, %c;\n"
    "    @return %r;\n"
    ";;\n"
    "\n"
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %c0 = const 0;\n"
    "    %a = call @f1, %c0;\n"
    "    %b = call @f2, %a;\n"
    "    %c = call @f3, %b;\n"
    "    %d = call @f4, %c;\n"
    "    @return %d;\n"
    ";;\n";

static double get_time_us(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER freq) {
    return (double)(end.QuadPart - start.QuadPart) * 1000000.0 / (double)freq.QuadPart;
}

static void run_bench_case(const Bench_Case *bc, int iterations) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.opt_level = NYGEN_OPT_O1;

    size_t src_len = strlen(bc->ir_src);

    size_t baseline_peak = g_ny_mem_tracker.peak_allocated;
    size_t out_size = 0;

    double min_us = 1e9;
    double max_us = 0.0;
    double total_us = 0.0;

    for (int i = 0; i < iterations; i++) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        Nygen_Result res = nygen_compile(bc->ir_src, src_len, &cfg);
        QueryPerformanceCounter(&t1);

        if (!res.success) {
            fprintf(stderr, "bench %s failed\n", bc->name);
            exit(1);
        }

        out_size = res.size;
        nygen_result_destroy(&res);

        double us = get_time_us(t0, t1, freq);
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
        total_us += us;
    }

    double avg_us = total_us / iterations;
    size_t peak_diff = g_ny_mem_tracker.peak_allocated > baseline_peak ?
                       (g_ny_mem_tracker.peak_allocated - baseline_peak) : 0;

    printf("%-10s: min=%7.1f us  avg=%7.1f us  max=%7.1f us  peak_mem=%6zu B  obj_size=%zu B\n",
           bc->name, min_us, avg_us, max_us, peak_diff, out_size);
}

static void run_jit_bench_case(const Bench_Case *bc, int iterations) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    size_t src_len = strlen(bc->ir_src);
    double min_us = 1e9;
    double max_us = 0.0;
    double total_us = 0.0;

    for (int i = 0; i < iterations; i++) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        Nygen_Config gen_cfg;
        nygen_config_init(&gen_cfg);

        Nygen_Encoded_Module emod;
        bool gen_ok = nygen_compile_encoded(bc->ir_src, src_len, &gen_cfg, &emod, NULL, NULL);
        if (!gen_ok) exit(1);

        Nyjit_Config jit_cfg;
        nyjit_config_init(&jit_cfg);
        Nyjit_Module *jit = nyjit_create(&jit_cfg);
        if (!jit) exit(1);

        bool link_ok = nyjit_link(jit, &emod, NULL, NULL);
        if (!link_ok) exit(1);

        typedef int32_t (*MainFn)(void);
        MainFn fn = (MainFn)nyjit_lookup(jit, "main");
        if (!fn) exit(1);

        int32_t val = fn();
        (void)val;

        nyjit_destroy(jit);
        nygen_encoded_module_destroy(&emod);

        QueryPerformanceCounter(&t1);
        double us = get_time_us(t0, t1, freq);
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
        total_us += us;
    }

    double avg_us = total_us / iterations;
    printf("%-10s: min=%7.1f us  avg=%7.1f us  max=%7.1f us  (compile+execute latency)\n",
           "jit_medium", min_us, avg_us, max_us);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    Bench_Case cases[] = {
        { "tiny",     s_prog_tiny },
        { "medium",   s_prog_medium },
        { "pressure", s_prog_pressure },
        { "module",   s_prog_module },
    };
    int num_cases = (int)(sizeof(cases) / sizeof(cases[0]));

    int iters = 200;
    for (int i = 0; i < num_cases; i++) {
        run_bench_case(&cases[i], iters);
    }

    run_jit_bench_case(&cases[1], iters);

    return 0;
}
