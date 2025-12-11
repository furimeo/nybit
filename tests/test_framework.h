// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NY_TEST_FRAMEWORK_H
#define NY_TEST_FRAMEWORK_H

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/wait.h>
#endif
#include <nybit/support.h>

extern int g_tests_run;
extern int g_tests_failed;
extern int g_tests_skipped;
extern bool g_test_skipped;

#if defined(_WIN32)
#define NYBIT_CLI_BIN "bin\\nybit.exe"
static inline int ny_test_system(const char *cmd) {
    if (cmd && cmd[0] == '.' && cmd[1] == '/') {
        char win_cmd[1024];
        snprintf(win_cmd, sizeof(win_cmd), "%s", cmd + 2);
        for (char *p = win_cmd; *p; p++) {
            if (*p == '/') *p = '\\';
        }
        return system(win_cmd);
    }
    return system(cmd);
}
#else
#define NYBIT_CLI_BIN "./bin/nybit"
static inline int ny_test_system(const char *cmd) {
    int rc = system(cmd);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return rc;
}
#endif

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "%s:%d: assertion failed: %s == %s\n", __FILE__, __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_ASSERT_STR_EQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        fprintf(stderr, "%s:%d: string mismatch: expected '%s', got '%s'\n", __FILE__, __LINE__, (b), (a)); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define TEST_SKIP(reason) do { \
    g_test_skipped = true; \
    (void)(reason); \
    return; \
} while (0)

#define RUN_TEST(fn) do { \
    size_t mem_before = g_ny_mem_tracker.current_allocated; \
    int failed_before = g_tests_failed; \
    g_test_skipped = false; \
    g_tests_run++; \
    fn(); \
    if (g_tests_failed == failed_before) { \
        if (g_test_skipped) { \
            g_tests_skipped++; \
        } else { \
            size_t mem_after = g_ny_mem_tracker.current_allocated; \
            if (mem_after != mem_before) { \
                fprintf(stderr, "%s: leaked %zu bytes\n", #fn, mem_after - mem_before); \
                g_tests_failed++; \
            } else { \
                printf("%s: ok\n", #fn); \
            } \
        } \
    } \
} while (0)

#endif
