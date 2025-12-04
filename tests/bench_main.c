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

static const char *s_prog_aggregate =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@type Big = struct { a: i64, b: i64, c: i64 };\n"
    "\n"
    "@function fwd_pair(%p: Pair) -> Pair;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n"
    "\n"
    "@function fwd_big(%b: Big) -> Big;\n"
    ".entry;\n"
    "    @return %b;\n"
    ";;\n"
    "\n"
    "@function main(%p: Pair, %b: Big) -> Pair;\n"
    ".entry;\n"
    "    %p2 = call @fwd_pair, %p;\n"
    "    %b2 = call @fwd_big, %b;\n"
    "    @return %p2;\n"
    ";;\n";

static const char *s_prog_hfa =
    "@type Vec3 = struct { x: f32, y: f32, z: f32 };\n"
    "@type Vec4 = struct { x: f32, y: f32, z: f32, w: f32 };\n"
    "\n"
    "@function fwd_vec3(%v: Vec3) -> Vec3;\n"
    ".entry;\n"
    "    @return %v;\n"
    ";;\n"
    "\n"
    "@function fwd_vec4(%v: Vec4) -> Vec4;\n"
    ".entry;\n"
    "    @return %v;\n"
    ";;\n"
    "\n"
    "@function main(%v3: Vec3, %v4: Vec4) -> Vec3;\n"
    ".entry;\n"
    "    %r3 = call @fwd_vec3, %v3;\n"
    "    %r4 = call @fwd_vec4, %v4;\n"
    "    @return %r3;\n"
    ";;\n";

static double get_time_us(LARGE_INTEGER start, LARGE_INTEGER end, LARGE_INTEGER freq) {
    return (double)(end.QuadPart - start.QuadPart) * 1000000.0 / (double)freq.QuadPart;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void run_bench_case(const Bench_Case *bc, int iterations) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.opt_level = NYGEN_OPT_O1;

    size_t src_len = strlen(bc->ir_src);

    /* Warm-up: 5 iterations */
    for (int w = 0; w < 5; w++) {
        Nygen_Result res = nygen_compile(bc->ir_src, src_len, &cfg);
        nygen_result_destroy(&res);
    }

    size_t baseline_peak = g_ny_mem_tracker.peak_allocated;
    size_t out_size = 0;

    double *samples = (double *)ny_alloc((size_t)iterations * sizeof(double));
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
        samples[i] = us;
        if (us < min_us) min_us = us;
        if (us > max_us) max_us = us;
        total_us += us;
    }

    qsort(samples, (size_t)iterations, sizeof(double), cmp_double);
    int trim = iterations / 20;
    if (trim < 1) trim = 1;
    double trim_total = 0.0;
    int counted = 0;
    for (int i = trim; i < iterations - trim; i++) {
        trim_total += samples[i];
        counted++;
    }
    double avg_us = counted > 0 ? (trim_total / counted) : (total_us / iterations);
    ny_free(samples, (size_t)iterations * sizeof(double));

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

static void run_nyir_bench_case(const Bench_Case *bc, int iterations) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    size_t src_len = strlen(bc->ir_src);

    Nygen_Config cfg;
    nygen_config_init(&cfg);

    uint8_t *nyir_data = NULL;
    size_t nyir_size = 0;
    bool ok = nygen_compile_nyir(bc->ir_src, src_len, &cfg, &nyir_data, &nyir_size, NULL, NULL);
    if (!ok) exit(1);

    double ser_min = 1e9, ser_max = 0, ser_total = 0;
    double deser_min = 1e9, deser_max = 0, deser_total = 0;

    for (int i = 0; i < iterations; i++) {
        uint8_t *data = NULL;
        size_t size = 0;
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        bool s_ok = nygen_compile_nyir(bc->ir_src, src_len, &cfg, &data, &size, NULL, NULL);
        QueryPerformanceCounter(&t1);
        if (!s_ok) exit(1);
        double us = get_time_us(t0, t1, freq);
        if (us < ser_min) ser_min = us;
        if (us > ser_max) ser_max = us;
        ser_total += us;

        QueryPerformanceCounter(&t0);
        Ny_Context *ctx = nygen_load_nyir(data, size, NULL, NULL);
        QueryPerformanceCounter(&t1);
        if (!ctx) exit(1);
        us = get_time_us(t0, t1, freq);
        if (us < deser_min) deser_min = us;
        if (us > deser_max) deser_max = us;
        deser_total += us;

        nygen_ir_destroy(ctx);
        ny_free(data, size);
    }

    double ser_avg = ser_total / iterations;
    double deser_avg = deser_total / iterations;
    printf("%-10s: ser min=%5.1f avg=%5.1f us  deser min=%5.1f avg=%5.1f us  nyir_size=%zu B\n",
           "nyir", ser_min, ser_avg, deser_min, deser_avg, nyir_size);
    ny_free(nyir_data, nyir_size);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    Bench_Case cases[] = {
        { "tiny",      s_prog_tiny },
        { "medium",    s_prog_medium },
        { "pressure",  s_prog_pressure },
        { "module",    s_prog_module },
        { "aggregate", s_prog_aggregate },
        { "hfa",       s_prog_hfa },
    };
    int num_cases = (int)(sizeof(cases) / sizeof(cases[0]));

    int iters = 200;
    for (int i = 0; i < num_cases; i++) {
        run_bench_case(&cases[i], iters);
    }

    run_jit_bench_case(&cases[1], iters);
    run_nyir_bench_case(&cases[1], iters);

    return 0;
}
