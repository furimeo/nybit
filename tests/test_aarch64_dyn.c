// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nygen/nygen.h>
#include <nybit/support.h>
#include <nylink/nylink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *s_lib_func =
    "@function exported_func() -> i32;\n"
    ".entry;\n"
    "    %v = const 42;\n"
    "    @return %v;\n"
    ";;\n";

static const char *s_main_import_func =
    "@function main() -> i32;\n"
    ".entry;\n"
    "    %r = call @exported_func;\n"
    "    @return %r;\n"
    ";;\n";

static bool compile_aarch64_obj(const char *src, const char *path) {
    Nygen_Config cfg;
    nygen_config_init(&cfg);
    cfg.output_kind = NYGEN_OUTPUT_OBJECT;
    cfg.target_triple = "aarch64";

    Nygen_Result res = nygen_compile(src, strlen(src), &cfg);
    if (!res.success || !res.data || res.size == 0) {
        nygen_result_destroy(&res);
        return false;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        nygen_result_destroy(&res);
        return false;
    }
    fwrite(res.data, 1, res.size, f);
    fclose(f);
    nygen_result_destroy(&res);
    return true;
}

static bool read_file(const char *path, uint8_t **out_data, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t *)ny_alloc(sz);
    fread(data, 1, sz, f);
    fclose(f);
    *out_data = data;
    *out_size = sz;
    return true;
}

void test_aarch64_shared_emission(void) {
    TEST_ASSERT(compile_aarch64_obj(s_lib_func, "bin/test_aarch64_lib.o"));

    uint8_t *obj_data;
    size_t obj_size;
    TEST_ASSERT(read_file("bin/test_aarch64_lib.o", &obj_data, &obj_size));

    Nylink_Context *ctx = nylink_context_create();
    nylink_context_set_shared(ctx, true);
    nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_SHARED);

    bool added = nylink_add_object(ctx, "lib.o", obj_data, obj_size);
    TEST_ASSERT(added);

    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(resolved);

    Nylink_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.target_format = NYLINK_TARGET_ELF64;
    cfg.output_mode = NYLINK_OUTPUT_SHARED;
    cfg.soname = "libtest_aarch64.so";

    bool laid = nylink_layout(ctx, &cfg);
    TEST_ASSERT(laid);

    bool relocs = nylink_apply_relocations(ctx);
    TEST_ASSERT(relocs);

    bool written = nylink_write_executable(ctx, "bin/libtest_aarch64.so", &cfg);
    TEST_ASSERT(written);

    nylink_context_destroy(ctx);
    ny_free(obj_data, obj_size);
    remove("bin/test_aarch64_lib.o");
}

void test_aarch64_pie_emission(void) {
    TEST_ASSERT(compile_aarch64_obj(s_lib_func, "bin/test_aarch64_pie_lib.o"));
    TEST_ASSERT(compile_aarch64_obj(s_main_import_func, "bin/test_aarch64_pie_main.o"));

    uint8_t *lib_obj_data;
    size_t lib_obj_size;
    TEST_ASSERT(read_file("bin/test_aarch64_pie_lib.o", &lib_obj_data, &lib_obj_size));

    Nylink_Context *so_ctx = nylink_context_create();
    nylink_context_set_shared(so_ctx, true);
    nylink_context_set_output_mode(so_ctx, NYLINK_OUTPUT_SHARED);
    TEST_ASSERT(nylink_add_object(so_ctx, "lib.o", lib_obj_data, lib_obj_size));
    TEST_ASSERT(nylink_resolve_symbols(so_ctx));

    Nylink_Config so_cfg;
    memset(&so_cfg, 0, sizeof(so_cfg));
    so_cfg.target_format = NYLINK_TARGET_ELF64;
    so_cfg.output_mode = NYLINK_OUTPUT_SHARED;
    so_cfg.soname = "libtest_aarch64_pie.so";
    TEST_ASSERT(nylink_layout(so_ctx, &so_cfg));
    TEST_ASSERT(nylink_apply_relocations(so_ctx));
    TEST_ASSERT(nylink_write_executable(so_ctx, "bin/libtest_aarch64_pie.so", &so_cfg));
    nylink_context_destroy(so_ctx);

    uint8_t *so_data;
    size_t so_size;
    TEST_ASSERT(read_file("bin/libtest_aarch64_pie.so", &so_data, &so_size));

    uint8_t *main_obj_data;
    size_t main_obj_size;
    TEST_ASSERT(read_file("bin/test_aarch64_pie_main.o", &main_obj_data, &main_obj_size));

    Nylink_Context *ctx = nylink_context_create();
    nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_PIE);

    bool added_main = nylink_add_object(ctx, "main.o", main_obj_data, main_obj_size);
    TEST_ASSERT(added_main);
    bool added_so = nylink_add_object(ctx, "libtest_aarch64_pie.so", so_data, so_size);
    TEST_ASSERT(added_so);

    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(resolved);

    const char *needed[] = { "libtest_aarch64_pie.so" };
    Nylink_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.target_format = NYLINK_TARGET_ELF64;
    cfg.output_mode = NYLINK_OUTPUT_PIE;
    cfg.entry_point = "main";
    cfg.needed_libs = needed;
    cfg.needed_lib_count = 1;
    cfg.rpath = "$ORIGIN";

    bool laid = nylink_layout(ctx, &cfg);
    TEST_ASSERT(laid);

    bool relocs = nylink_apply_relocations(ctx);
    TEST_ASSERT(relocs);

    bool written = nylink_write_executable(ctx, "bin/test_aarch64_pie.elf", &cfg);
    TEST_ASSERT(written);

    nylink_context_destroy(ctx);
    ny_free(lib_obj_data, lib_obj_size);
    ny_free(so_data, so_size);
    ny_free(main_obj_data, main_obj_size);
    remove("bin/test_aarch64_pie_lib.o");
    remove("bin/test_aarch64_pie_main.o");
    remove("bin/libtest_aarch64_pie.so");
}

void test_aarch64_shared_determinism(void) {
    TEST_ASSERT(compile_aarch64_obj(s_lib_func, "bin/test_aarch64_det_lib.o"));

    uint8_t *obj_data;
    size_t obj_size;
    TEST_ASSERT(read_file("bin/test_aarch64_det_lib.o", &obj_data, &obj_size));

    for (int round = 0; round < 2; round++) {
        Nylink_Context *ctx = nylink_context_create();
        nylink_context_set_shared(ctx, true);
        nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_SHARED);

        nylink_add_object(ctx, "lib.o", obj_data, obj_size);
        nylink_resolve_symbols(ctx);

        Nylink_Config cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.target_format = NYLINK_TARGET_ELF64;
        cfg.output_mode = NYLINK_OUTPUT_SHARED;
        cfg.soname = "libdet.so";

        nylink_layout(ctx, &cfg);
        nylink_apply_relocations(ctx);

        const char *path = (round == 0) ? "bin/test_aarch64_shared_det1.so" : "bin/test_aarch64_shared_det2.so";
        nylink_write_executable(ctx, path, &cfg);

        nylink_context_destroy(ctx);
    }

    FILE *f1 = fopen("bin/test_aarch64_shared_det1.so", "rb");
    FILE *f2 = fopen("bin/test_aarch64_shared_det2.so", "rb");
    TEST_ASSERT(f1 != NULL);
    TEST_ASSERT(f2 != NULL);

    fseek(f1, 0, SEEK_END);
    fseek(f2, 0, SEEK_END);
    long sz1 = ftell(f1);
    long sz2 = ftell(f2);
    TEST_ASSERT_EQ(sz1, sz2);

    fseek(f1, 0, SEEK_SET);
    fseek(f2, 0, SEEK_SET);
    uint8_t buf1[4096];
    uint8_t buf2[4096];
    size_t remaining = (size_t)sz1;
    bool match = true;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buf1) ? sizeof(buf1) : remaining;
        fread(buf1, 1, chunk, f1);
        fread(buf2, 1, chunk, f2);
        if (memcmp(buf1, buf2, chunk) != 0) {
            match = false;
            break;
        }
        remaining -= chunk;
    }
    TEST_ASSERT(match);

    fclose(f1);
    fclose(f2);
    remove("bin/test_aarch64_shared_det1.so");
    remove("bin/test_aarch64_shared_det2.so");
    ny_free(obj_data, obj_size);
    remove("bin/test_aarch64_det_lib.o");
}

void test_aarch64_pie_determinism(void) {
    TEST_ASSERT(compile_aarch64_obj(s_lib_func, "bin/test_aarch64_pie_det_lib.o"));
    TEST_ASSERT(compile_aarch64_obj(s_main_import_func, "bin/test_aarch64_pie_det.o"));

    uint8_t *lib_obj_data;
    size_t lib_obj_size;
    TEST_ASSERT(read_file("bin/test_aarch64_pie_det_lib.o", &lib_obj_data, &lib_obj_size));

    Nylink_Context *so_ctx = nylink_context_create();
    nylink_context_set_shared(so_ctx, true);
    nylink_context_set_output_mode(so_ctx, NYLINK_OUTPUT_SHARED);
    nylink_add_object(so_ctx, "lib.o", lib_obj_data, lib_obj_size);
    nylink_resolve_symbols(so_ctx);

    Nylink_Config so_cfg;
    memset(&so_cfg, 0, sizeof(so_cfg));
    so_cfg.target_format = NYLINK_TARGET_ELF64;
    so_cfg.output_mode = NYLINK_OUTPUT_SHARED;
    so_cfg.soname = "libdet_pie.so";
    nylink_layout(so_ctx, &so_cfg);
    nylink_apply_relocations(so_ctx);
    nylink_write_executable(so_ctx, "bin/libtest_aarch64_pie_det.so", &so_cfg);
    nylink_context_destroy(so_ctx);

    uint8_t *so_data;
    size_t so_size;
    TEST_ASSERT(read_file("bin/libtest_aarch64_pie_det.so", &so_data, &so_size));

    uint8_t *main_obj_data;
    size_t main_obj_size;
    TEST_ASSERT(read_file("bin/test_aarch64_pie_det.o", &main_obj_data, &main_obj_size));

    for (int round = 0; round < 2; round++) {
        Nylink_Context *ctx = nylink_context_create();
        nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_PIE);

        nylink_add_object(ctx, "main.o", main_obj_data, main_obj_size);
        nylink_add_object(ctx, "libdet_pie.so", so_data, so_size);
        nylink_resolve_symbols(ctx);

        const char *needed[] = { "libdet_pie.so" };
        Nylink_Config cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.target_format = NYLINK_TARGET_ELF64;
        cfg.output_mode = NYLINK_OUTPUT_PIE;
        cfg.entry_point = "main";
        cfg.needed_libs = needed;
        cfg.needed_lib_count = 1;

        nylink_layout(ctx, &cfg);
        nylink_apply_relocations(ctx);

        const char *path = (round == 0) ? "bin/test_aarch64_pie_det1.elf" : "bin/test_aarch64_pie_det2.elf";
        nylink_write_executable(ctx, path, &cfg);

        nylink_context_destroy(ctx);
    }

    FILE *f1 = fopen("bin/test_aarch64_pie_det1.elf", "rb");
    FILE *f2 = fopen("bin/test_aarch64_pie_det2.elf", "rb");
    TEST_ASSERT(f1 != NULL);
    TEST_ASSERT(f2 != NULL);

    fseek(f1, 0, SEEK_END);
    fseek(f2, 0, SEEK_END);
    long sz1 = ftell(f1);
    long sz2 = ftell(f2);
    TEST_ASSERT_EQ(sz1, sz2);

    fseek(f1, 0, SEEK_SET);
    fseek(f2, 0, SEEK_SET);
    uint8_t buf1[4096];
    uint8_t buf2[4096];
    size_t remaining = (size_t)sz1;
    bool match = true;
    while (remaining > 0) {
        size_t chunk = remaining > sizeof(buf1) ? sizeof(buf1) : remaining;
        fread(buf1, 1, chunk, f1);
        fread(buf2, 1, chunk, f2);
        if (memcmp(buf1, buf2, chunk) != 0) {
            match = false;
            break;
        }
        remaining -= chunk;
    }
    TEST_ASSERT(match);

    fclose(f1);
    fclose(f2);
    remove("bin/test_aarch64_pie_det1.elf");
    remove("bin/test_aarch64_pie_det2.elf");
    remove("bin/libtest_aarch64_pie_det.so");
    ny_free(lib_obj_data, lib_obj_size);
    ny_free(so_data, so_size);
    ny_free(main_obj_data, main_obj_size);
    remove("bin/test_aarch64_pie_det_lib.o");
    remove("bin/test_aarch64_pie_det.o");
}

void test_aarch64_shared_structural_validation(void) {
    TEST_ASSERT(compile_aarch64_obj(s_lib_func, "bin/test_aarch64_sv.o"));

    uint8_t *obj_data;
    size_t obj_size;
    TEST_ASSERT(read_file("bin/test_aarch64_sv.o", &obj_data, &obj_size));

    Nylink_Context *ctx = nylink_context_create();
    nylink_context_set_shared(ctx, true);
    nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_SHARED);

    nylink_add_object(ctx, "lib.o", obj_data, obj_size);
    nylink_resolve_symbols(ctx);

    Nylink_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.target_format = NYLINK_TARGET_ELF64;
    cfg.output_mode = NYLINK_OUTPUT_SHARED;
    cfg.soname = "libtest.so";

    nylink_layout(ctx, &cfg);
    nylink_apply_relocations(ctx);
    nylink_write_executable(ctx, "bin/test_aarch64_sv.so", &cfg);

    uint8_t *so_data;
    size_t so_size;
    TEST_ASSERT(read_file("bin/test_aarch64_sv.so", &so_data, &so_size));

    TEST_ASSERT(so_size >= 64);
    TEST_ASSERT_EQ(so_data[0], 0x7F);
    TEST_ASSERT_EQ(so_data[1], 'E');
    TEST_ASSERT_EQ(so_data[2], 'L');
    TEST_ASSERT_EQ(so_data[3], 'F');

    uint16_t e_machine = (uint16_t)(so_data[18] | (so_data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    uint16_t e_type = (uint16_t)(so_data[16] | (so_data[17] << 8));
    TEST_ASSERT_EQ(e_type, 3);

    ny_free(so_data, so_size);
    nylink_context_destroy(ctx);
    ny_free(obj_data, obj_size);
    remove("bin/test_aarch64_sv.o");
    remove("bin/test_aarch64_sv.so");
}

void test_aarch64_exec_elf_machine_check(void) {
    TEST_ASSERT(compile_aarch64_obj(s_lib_func, "bin/test_aarch64_mc.o"));

    uint8_t *obj_data;
    size_t obj_size;
    TEST_ASSERT(read_file("bin/test_aarch64_mc.o", &obj_data, &obj_size));

    Nylink_Context *ctx = nylink_context_create();

    bool added = nylink_add_object(ctx, "test.o", obj_data, obj_size);
    TEST_ASSERT(added);

    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(resolved);

    Nylink_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.target_format = NYLINK_TARGET_ELF64;
    cfg.output_mode = NYLINK_OUTPUT_EXECUTABLE;
    cfg.entry_point = "exported_func";

    bool laid = nylink_layout(ctx, &cfg);
    TEST_ASSERT(laid);

    bool relocs = nylink_apply_relocations(ctx);
    TEST_ASSERT(relocs);

    bool written = nylink_write_executable(ctx, "bin/test_aarch64_exec.elf", &cfg);
    TEST_ASSERT(written);

    uint8_t *elf_data;
    size_t elf_size;
    TEST_ASSERT(read_file("bin/test_aarch64_exec.elf", &elf_data, &elf_size));

    uint16_t e_machine = (uint16_t)(elf_data[18] | (elf_data[19] << 8));
    TEST_ASSERT_EQ(e_machine, 183);

    uint16_t e_type = (uint16_t)(elf_data[16] | (elf_data[17] << 8));
    TEST_ASSERT_EQ(e_type, 2);

    ny_free(elf_data, elf_size);
    nylink_context_destroy(ctx);
    ny_free(obj_data, obj_size);
    remove("bin/test_aarch64_mc.o");
    remove("bin/test_aarch64_exec.elf");
}
