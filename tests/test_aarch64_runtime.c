// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nylink/nylink.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#else
#include <unistd.h>
#endif

typedef enum {
    AARCH64_RT_NONE = 0,
    AARCH64_RT_NATIVE,
    AARCH64_RT_QEMU,
} Aarch64_Rt_Kind;

static Aarch64_Rt_Kind g_aarch64_rt = AARCH64_RT_NONE;
static bool g_aarch64_rt_checked = false;
static char g_aarch64_runner[512] = {0};

static void aarch64_rt_detect(void) {
    if (g_aarch64_rt_checked) return;
    g_aarch64_rt_checked = true;

#if defined(_WIN32) || defined(_WIN64)
    FILE *pipe = popen("wsl.exe -d Debian /bin/bash -lc \"which qemu-aarch64-static qemu-aarch64 2>/dev/null | head -1\" 2>nul", "r");
    if (pipe) {
        char buf[512];
        if (fgets(buf, sizeof(buf), pipe)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' || buf[len-1] == ' ')) buf[--len] = '\0';
            if (len > 0 && buf[0] == '/') {
                strncpy(g_aarch64_runner, buf, sizeof(g_aarch64_runner) - 1);
                g_aarch64_rt = AARCH64_RT_QEMU;
            }
        }
        pclose(pipe);
    }
#else
#if defined(__aarch64__)
    g_aarch64_rt = AARCH64_RT_NATIVE;
#else
    FILE *pipe = popen("which qemu-aarch64-static qemu-aarch64 2>/dev/null | head -1", "r");
    if (pipe) {
        char buf[512];
        if (fgets(buf, sizeof(buf), pipe)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' || buf[len-1] == ' ')) buf[--len] = '\0';
            if (len > 0 && buf[0] == '/') {
                strncpy(g_aarch64_runner, buf, sizeof(g_aarch64_runner) - 1);
                g_aarch64_rt = AARCH64_RT_QEMU;
            }
        }
        pclose(pipe);
    }
#endif
#endif
}

static bool aarch64_rt_available(void) {
    aarch64_rt_detect();
    return g_aarch64_rt != AARCH64_RT_NONE;
}

static const char *aarch64_rt_desc(void) {
    aarch64_rt_detect();
    switch (g_aarch64_rt) {
    case AARCH64_RT_NATIVE: return "native";
    case AARCH64_RT_QEMU:  return "qemu";
    default:               return "unavailable";
    }
}

static int aarch64_rt_run(const char *exe_path) {
    aarch64_rt_detect();
    if (g_aarch64_rt == AARCH64_RT_NONE) return -1;

#if defined(_WIN32) || defined(_WIN64)
    char wsl_path[1024];
    char full[1024];
    if (_fullpath(full, exe_path, sizeof(full)) != nullptr) exe_path = full;
    if (exe_path[1] == ':') {
        char drive = (char)tolower((unsigned char)exe_path[0]);
        const char *rest = exe_path + 2;
        snprintf(wsl_path, sizeof(wsl_path), "/mnt/%c%s", drive, rest);
        for (char *p = wsl_path; *p; p++) if (*p == '\\') *p = '/';
    } else {
        strncpy(wsl_path, exe_path, sizeof(wsl_path) - 1);
    }

    char cmd[2048];
    if (g_aarch64_rt == AARCH64_RT_QEMU) {
        snprintf(cmd, sizeof(cmd), "wsl.exe -d Debian /bin/bash -lc \"%s %s; echo EXIT:$?\" 2>nul",
                 g_aarch64_runner, wsl_path);
    } else {
        snprintf(cmd, sizeof(cmd), "wsl.exe -d Debian /bin/bash -lc \"%s; echo EXIT:$?\" 2>nul", wsl_path);
    }
#else
    char cmd[2048];
    if (g_aarch64_rt == AARCH64_RT_QEMU) {
        snprintf(cmd, sizeof(cmd), "%s %s; echo EXIT:$?", g_aarch64_runner, exe_path);
    } else {
        snprintf(cmd, sizeof(cmd), "%s; echo EXIT:$?", exe_path);
    }
#endif

    FILE *pipe = popen(cmd, "r");
    if (!pipe) return -1;
    char buf[256];
    int rc = -1;
    while (fgets(buf, sizeof(buf), pipe)) {
        if (strncmp(buf, "EXIT:", 5) == 0) {
            rc = atoi(buf + 5);
        }
    }
    pclose(pipe);
    return rc;
}

static bool aarch64_rt_link_and_run(const char *ny_source, const char *obj_name,
                                    const char *exe_name, int expected_exit) {
    if (!aarch64_rt_available()) return true;

    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(ny_source, strlen(ny_source), &cfg);
    if (!res.success || !res.data) {
        nygen_result_destroy(&res);
        return false;
    }

    Nylink_Context *ctx = nylink_context_create();
    if (!ctx) { nygen_result_destroy(&res); return false; }

    bool added = nylink_add_object(ctx, obj_name, res.data, res.size);
    if (!added) { nylink_context_destroy(ctx); nygen_result_destroy(&res); return false; }

    bool resolved = nylink_resolve_symbols(ctx);
    if (!resolved) { nylink_context_destroy(ctx); nygen_result_destroy(&res); return false; }

    Nylink_Config link_cfg;
    memset(&link_cfg, 0, sizeof(link_cfg));
    link_cfg.target_format = NYLINK_TARGET_ELF64;
    link_cfg.output_mode = NYLINK_OUTPUT_EXECUTABLE;
    link_cfg.entry_point = "main";

    bool laid = nylink_layout(ctx, &link_cfg);
    bool relocs = nylink_apply_relocations(ctx);
    bool written = nylink_write_executable(ctx, exe_name, &link_cfg);
    nylink_context_destroy(ctx);
    nygen_result_destroy(&res);

    if (!laid || !relocs || !written) return false;

    int rc = aarch64_rt_run(exe_name);
    remove(exe_name);
    return rc == expected_exit;
}

static const char *s_rt_simple_ny =
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %a = const 40;\n"
    "    %b = const 2;\n"
    "    %r = add %a, %b;\n"
    "    @return %r;\n"
    ";;\n";

static const char *s_rt_arith_ny =
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %a = const 10;\n"
    "    %b = const 3;\n"
    "    %m = mul %a, %b;\n"
    "    %c = const 12;\n"
    "    %r = sub %m, %c;\n"
    "    @return %r;\n"
    ";;\n";

static const char *s_rt_call_ny =
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

static const char *s_rt_global_ny =
    "@global @readonly @answer: i32 = 42;\n"
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %p = global_addr @answer;\n"
    "    %v = load %p;\n"
    "    @return %v;\n"
    ";;\n";

static const char *s_rt_pair_ny =
    "@type Pair = struct { a: i32, b: i32 };\n"
    "@function id_pair(%p: Pair) -> Pair;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n"
    "@function fwd_pair(%p: Pair) -> Pair;\n"
    ".entry;\n"
    "    %r = call @id_pair, %p;\n"
    "    @return %r;\n"
    ";;\n";

static const char *s_rt_big_ny =
    "@type Big = struct { a: i64, b: i64, c: i64 };\n"
    "@function id_big(%p: Big) -> Big;\n"
    ".entry;\n"
    "    @return %p;\n"
    ";;\n"
    "@function fwd_big(%p: Big) -> Big;\n"
    ".entry;\n"
    "    %r = call @id_big, %p;\n"
    "    @return %r;\n"
    ";;\n";

void test_aarch64_rt_env_detection(void) {
    aarch64_rt_detect();
    TEST_ASSERT(g_aarch64_rt_checked);
    if (g_aarch64_rt == AARCH64_RT_NONE) {
        printf("test_aarch64_rt_env_detection: ok (aarch64 runtime unavailable)\n");
    }
}

void test_aarch64_rt_trivial_return(void) {
    if (!aarch64_rt_available()) {
        printf("test_aarch64_rt_trivial_return: skipped (aarch64 runtime %s)\n", aarch64_rt_desc());
        return;
    }
    TEST_ASSERT(aarch64_rt_link_and_run(s_rt_simple_ny, "rt_trivial.o", "bin/rt_aarch64_trivial.elf", 42));
}

void test_aarch64_rt_arithmetic(void) {
    if (!aarch64_rt_available()) {
        printf("test_aarch64_rt_arithmetic: skipped (aarch64 runtime %s)\n", aarch64_rt_desc());
        return;
    }
    TEST_ASSERT(aarch64_rt_link_and_run(s_rt_arith_ny, "rt_arith.o", "bin/rt_aarch64_arith.elf", 18));
}

void test_aarch64_rt_function_call(void) {
    if (!aarch64_rt_available()) {
        printf("test_aarch64_rt_function_call: skipped (aarch64 runtime %s)\n", aarch64_rt_desc());
        return;
    }
    TEST_ASSERT(aarch64_rt_link_and_run(s_rt_call_ny, "rt_call.o", "bin/rt_aarch64_call.elf", 42));
}

void test_aarch64_rt_global_data(void) {
    if (!aarch64_rt_available()) {
        printf("test_aarch64_rt_global_data: skipped (aarch64 runtime %s)\n", aarch64_rt_desc());
        return;
    }
    TEST_ASSERT(aarch64_rt_link_and_run(s_rt_global_ny, "rt_global.o", "bin/rt_aarch64_global.elf", 42));
}

void test_aarch64_rt_aggregate_pair(void) {
    if (!aarch64_rt_available()) {
        printf("test_aarch64_rt_aggregate_pair: skipped (aarch64 runtime %s)\n", aarch64_rt_desc());
        return;
    }
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_rt_pair_ny, strlen(s_rt_pair_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

void test_aarch64_rt_aggregate_big_sret(void) {
    if (!aarch64_rt_available()) {
        printf("test_aarch64_rt_aggregate_big_sret: skipped (aarch64 runtime %s)\n", aarch64_rt_desc());
        return;
    }
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";
    Nygen_Result res = nygen_compile(s_rt_big_ny, strlen(s_rt_big_ny), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != NULL);
    nygen_result_destroy(&res);
}

void test_aarch64_rt_determinism(void) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result r1 = nygen_compile(s_rt_pair_ny, strlen(s_rt_pair_ny), &cfg);
    TEST_ASSERT(r1.success);
    Nygen_Result r2 = nygen_compile(s_rt_pair_ny, strlen(s_rt_pair_ny), &cfg);
    TEST_ASSERT(r2.success);
    TEST_ASSERT_EQ(r1.size, r2.size);
    TEST_ASSERT(memcmp(r1.data, r2.data, r1.size) == 0);
    nygen_result_destroy(&r1);
    nygen_result_destroy(&r2);
}
