// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NY_TEST_FRAMEWORK_H
#define NY_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <nybit/support.h>

extern int g_tests_run;
extern int g_tests_failed;

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

#define RUN_TEST(fn) do { \
    size_t mem_before = g_ny_mem_tracker.current_allocated; \
    int failed_before = g_tests_failed; \
    g_tests_run++; \
    fn(); \
    if (g_tests_failed == failed_before) { \
        size_t mem_after = g_ny_mem_tracker.current_allocated; \
        if (mem_after != mem_before) { \
            fprintf(stderr, "%s: leaked %zu bytes\n", #fn, mem_after - mem_before); \
            g_tests_failed++; \
        } else { \
            printf("%s: ok\n", #fn); \
        } \
    } \
} while (0)

#endif
