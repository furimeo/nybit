// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nybit/object.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include "nybit/x86_encode.h"
#include "nygen/nygen.h"
#include <string.h>

void test_debug_source_loc_propagation(void) {
    const char *src =
        "@function test_loc(%a: i32, %b: i32) -> i32;\n"
        ".entry;\n"
        "    %r = add %a, %b;\n"
        "    @return %r;\n"
        ";;\n";

    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_MACHINE_IR;
    cfg.debug_info = true;

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != nullptr);
    nygen_result_destroy(&res);
}

void test_debug_dwarf_elf_emission(void) {
    const char *src =
        "@function calc(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %two = const 2;\n"
        "    %res = mul %x, %two;\n"
        "    @return %res;\n"
        ";;\n";

    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.target_triple = "x86_64-linux";
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.debug_info = true;
    cfg.emit_unwind = true;

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != nullptr);
    TEST_ASSERT(res.size > 0);

    /* Verify ELF header */
    TEST_ASSERT(res.data[0] == 0x7F && res.data[1] == 'E' && res.data[2] == 'L' && res.data[3] == 'F');

    /* Verify presence of .eh_frame and .debug sections in string payload */
    bool found_eh = false;
    bool found_debug_line = false;
    bool found_debug_info = false;
    for (size_t i = 0; i + 9 < res.size; i++) {
        if (memcmp(res.data + i, ".eh_frame", 9) == 0) found_eh = true;
        if (memcmp(res.data + i, ".debug_line", 11) == 0) found_debug_line = true;
        if (memcmp(res.data + i, ".debug_info", 11) == 0) found_debug_info = true;
    }
    TEST_ASSERT(found_eh);
    TEST_ASSERT(found_debug_line);
    TEST_ASSERT(found_debug_info);

    nygen_result_destroy(&res);
}

void test_debug_coff_codeview_emission(void) {
    const char *src =
        "@function square(%n: i32) -> i32;\n"
        ".entry;\n"
        "    %sq = mul %n, %n;\n"
        "    @return %sq;\n"
        ";;\n";

    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.target_triple = "x86_64-windows";
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.debug_info = true;
    cfg.emit_unwind = true;

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != nullptr);
    TEST_ASSERT(res.size > 0);

    /* Verify presence of .pdata, .xdata, and .debug in COFF object */
    bool found_pdata = false;
    bool found_xdata = false;
    bool found_debug_s = false;
    for (size_t i = 0; i + 8 <= res.size; i++) {
        if (memcmp(res.data + i, ".pdata\0\0", 8) == 0) found_pdata = true;
        if (memcmp(res.data + i, ".xdata\0\0", 8) == 0) found_xdata = true;
        if (memcmp(res.data + i, ".debug$S", 8) == 0) found_debug_s = true;
    }
    TEST_ASSERT(found_pdata);
    TEST_ASSERT(found_xdata);
    TEST_ASSERT(found_debug_s);

    nygen_result_destroy(&res);
}

void test_unwind_disabled_flag(void) {
    const char *src =
        "@function no_unwind_fn(%a: i32) -> i32;\n"
        ".entry;\n"
        "    @return %a;\n"
        ";;\n";

    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.target_triple = "x86_64-linux";
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.debug_info = false;
    cfg.emit_unwind = false;

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    TEST_ASSERT(res.success);
    TEST_ASSERT(res.data != nullptr);

    bool found_eh = false;
    for (size_t i = 0; i + 9 < res.size; i++) {
        if (memcmp(res.data + i, ".eh_frame", 9) == 0) found_eh = true;
    }
    TEST_ASSERT(!found_eh);

    nygen_result_destroy(&res);
}
