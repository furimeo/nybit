// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nybit/object.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define mkdir_compat(dir) _mkdir(dir)
#else
#include <sys/stat.h>
#define mkdir_compat(dir) mkdir(dir, 0755)
#endif

static void emit_cli_dummy_coff(Ny_Object_Buffer *obj_buf, const char *fn_name, bool add_reloc, const char *reloc_target) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("dummy_coff"));

    const uint8_t code[] = { 0xe8, 0x00, 0x00, 0x00, 0x00, 0xc3 };
    x86_buf_append_bytes(&emod.text_section, code, sizeof(code));

    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, emod.function_count + 1, sizeof(X86_Function_Code));
    emod.functions[emod.function_count++] = (X86_Function_Code){
        .name = ny_str(fn_name),
        .offset = 0,
        .size = sizeof(code),
    };

    if (add_reloc && reloc_target) {
        X86_Relocation reloc = {
            .kind = X86_FIXUP_CALL_REL32,
            .code_offset = 1,
            .symbol_name = ny_str(reloc_target),
            .addend = 0,
        };
        x86_buf_append_reloc(&emod.text_section, reloc);
    }

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    TEST_ASSERT(ny_emit_coff_x86_64(obj_buf, &emod, &diags));
    ny_diagnostic_list_destroy(&diags);
    x86_encoded_mod_destroy(&emod);
}

static void emit_cli_dummy_elf(Ny_Object_Buffer *obj_buf, const char *fn_name, bool add_reloc, const char *reloc_target) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("dummy_elf"));

    const uint8_t code[] = { 0xe8, 0x00, 0x00, 0x00, 0x00, 0xc3 };
    x86_buf_append_bytes(&emod.text_section, code, sizeof(code));

    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, emod.function_count + 1, sizeof(X86_Function_Code));
    emod.functions[emod.function_count++] = (X86_Function_Code){
        .name = ny_str(fn_name),
        .offset = 0,
        .size = sizeof(code),
    };

    if (add_reloc && reloc_target) {
        X86_Relocation reloc = {
            .kind = X86_FIXUP_CALL_REL32,
            .code_offset = 1,
            .symbol_name = ny_str(reloc_target),
            .addend = 0,
        };
        x86_buf_append_reloc(&emod.text_section, reloc);
    }

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    TEST_ASSERT(ny_emit_elf64_x86_64(obj_buf, &emod, &diags));
    ny_diagnostic_list_destroy(&diags);
    x86_encoded_mod_destroy(&emod);
}

static void write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    TEST_ASSERT(f != nullptr);
    if (size > 0) {
        TEST_ASSERT_EQ(fwrite(data, 1, size, f), size);
    }
    fclose(f);
}

static bool file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return true;
    }
    return false;
}

static void build_cli_test_archive(Ny_Object_Buffer *ar_buf, const char **member_names, const Ny_Object_Buffer *member_objs, size_t count) {
    ny_obj_buf_init(ar_buf);
    ny_obj_buf_append_bytes(ar_buf, (const uint8_t *)"!<arch>\n", 8);

    for (size_t i = 0; i < count; i++) {
        char hdr[60];
        memset(hdr, ' ', sizeof(hdr));

        char name_slash[17];
        snprintf(name_slash, sizeof(name_slash), "%s/", member_names[i]);
        size_t nlen = strlen(name_slash);
        if (nlen > 16) nlen = 16;
        memcpy(hdr, name_slash, nlen);

        memcpy(hdr + 16, "0           ", 12);
        memcpy(hdr + 28, "0     ", 6);
        memcpy(hdr + 34, "0     ", 6);
        memcpy(hdr + 40, "644     ", 8);

        char size_str[11];
        snprintf(size_str, sizeof(size_str), "%-10zu", member_objs[i].count);
        memcpy(hdr + 48, size_str, 10);

        hdr[58] = '`';
        hdr[59] = '\n';

        ny_obj_buf_append_bytes(ar_buf, (const uint8_t *)hdr, 60);
        ny_obj_buf_append_bytes(ar_buf, member_objs[i].bytes, member_objs[i].count);

        if (member_objs[i].count & 1) {
            ny_obj_buf_append_byte(ar_buf, '\n');
        }
    }
}

void test_cli_link_basic_objects(void) {
    Ny_Object_Buffer obj1, obj2;
    ny_obj_buf_init(&obj1);
    ny_obj_buf_init(&obj2);

    emit_cli_dummy_coff(&obj1, "main", true, "sub_func");
    emit_cli_dummy_coff(&obj2, "sub_func", false, nullptr);

    write_file("bin/cli_main.obj", obj1.bytes, obj1.count);
    write_file("bin/cli_sub.obj", obj2.bytes, obj2.count);

    int ret = system("bin\\nybit.exe link bin/cli_main.obj bin/cli_sub.obj -o bin/cli_basic.exe");
    TEST_ASSERT_EQ(ret, 0);

    FILE *f = fopen("bin/cli_basic.exe", "rb");
    TEST_ASSERT(f != nullptr);
    fclose(f);

    remove("bin/cli_basic.exe");
    remove("bin/cli_sub.obj");
    remove("bin/cli_main.obj");
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_cli_link_with_archive_lazy_extraction(void) {
    Ny_Object_Buffer obj_main, obj_needed, obj_unused;
    ny_obj_buf_init(&obj_main);
    ny_obj_buf_init(&obj_needed);
    ny_obj_buf_init(&obj_unused);

    emit_cli_dummy_coff(&obj_main, "main", true, "arch_add");
    emit_cli_dummy_coff(&obj_needed, "arch_add", false, nullptr);
    emit_cli_dummy_coff(&obj_unused, "arch_unused", false, nullptr);

    Ny_Object_Buffer ar_buf;
    const char *names[] = { "add.obj", "unused.obj" };
    Ny_Object_Buffer objs[] = { obj_needed, obj_unused };
    build_cli_test_archive(&ar_buf, names, objs, 2);

    write_file("bin/cli_ar_main.obj", obj_main.bytes, obj_main.count);
    write_file("bin/cli_mymath.lib", ar_buf.bytes, ar_buf.count);

    int ret = system("bin\\nybit.exe link bin/cli_ar_main.obj bin/cli_mymath.lib -o bin/cli_ar.exe");
    TEST_ASSERT_EQ(ret, 0);

    FILE *f = fopen("bin/cli_ar.exe", "rb");
    TEST_ASSERT(f != nullptr);
    fclose(f);

    remove("bin/cli_ar.exe");
    remove("bin/cli_mymath.lib");
    remove("bin/cli_ar_main.obj");

    ny_obj_buf_destroy(&ar_buf);
    ny_obj_buf_destroy(&obj_unused);
    ny_obj_buf_destroy(&obj_needed);
    ny_obj_buf_destroy(&obj_main);
}

void test_cli_link_search_path_and_library(void) {
    mkdir_compat("bin/test_libdir");

    Ny_Object_Buffer obj_main, obj_needed;
    ny_obj_buf_init(&obj_main);
    ny_obj_buf_init(&obj_needed);

    emit_cli_dummy_coff(&obj_main, "main", true, "lib_fn");
    emit_cli_dummy_coff(&obj_needed, "lib_fn", false, nullptr);

    Ny_Object_Buffer ar_buf;
    const char *names[] = { "sub.obj" };
    Ny_Object_Buffer objs[] = { obj_needed };
    build_cli_test_archive(&ar_buf, names, objs, 1);

    write_file("bin/cli_search_main.obj", obj_main.bytes, obj_main.count);
    write_file("bin/test_libdir/mylib.lib", ar_buf.bytes, ar_buf.count);

    int ret = system("bin\\nybit.exe link bin/cli_search_main.obj -Lbin/test_libdir -lmylib -o bin/cli_search.exe");
    TEST_ASSERT_EQ(ret, 0);

    FILE *f = fopen("bin/cli_search.exe", "rb");
    TEST_ASSERT(f != nullptr);
    fclose(f);

    remove("bin/cli_search.exe");
    remove("bin/test_libdir/mylib.lib");
    remove("bin/cli_search_main.obj");
#if defined(_WIN32) || defined(_WIN64)
    _rmdir("bin/test_libdir");
#else
    rmdir("bin/test_libdir");
#endif

    ny_obj_buf_destroy(&ar_buf);
    ny_obj_buf_destroy(&obj_needed);
    ny_obj_buf_destroy(&obj_main);
}

void test_cli_link_options_entry_base_target(void) {
    Ny_Object_Buffer obj1, obj2;
    ny_obj_buf_init(&obj1);
    ny_obj_buf_init(&obj2);

    emit_cli_dummy_elf(&obj1, "custom_entry", true, "elf_sub");
    emit_cli_dummy_elf(&obj2, "elf_sub", false, nullptr);

    write_file("bin/cli_elf1.o", obj1.bytes, obj1.count);
    write_file("bin/cli_elf2.o", obj2.bytes, obj2.count);

    int ret = system("bin\\nybit.exe link --target=elf64 --entry=custom_entry --base=0x600000 bin/cli_elf1.o bin/cli_elf2.o -o bin/cli_elf_out");
    TEST_ASSERT_EQ(ret, 0);

    FILE *f = fopen("bin/cli_elf_out", "rb");
    TEST_ASSERT(f != nullptr);
    uint8_t magic[4];
    TEST_ASSERT_EQ(fread(magic, 1, 4, f), 4);
    fclose(f);

    TEST_ASSERT_EQ(magic[0], 0x7F);
    TEST_ASSERT_EQ(magic[1], 'E');
    TEST_ASSERT_EQ(magic[2], 'L');
    TEST_ASSERT_EQ(magic[3], 'F');

    remove("bin/cli_elf_out");
    remove("bin/cli_elf2.o");
    remove("bin/cli_elf1.o");
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_cli_link_error_handling_and_cleanup(void) {
    /* 1. Missing input file */
    int ret1 = system("bin\\nybit.exe link bin/non_existent_file.obj -o bin/cli_fail.exe 2>nul");
    TEST_ASSERT(ret1 != 0);
    TEST_ASSERT(!file_exists("bin/cli_fail.exe"));

    /* 2. Undefined symbol */
    Ny_Object_Buffer obj_undef;
    ny_obj_buf_init(&obj_undef);
    emit_cli_dummy_coff(&obj_undef, "main", true, "missing_symbol");
    write_file("bin/cli_undef.obj", obj_undef.bytes, obj_undef.count);

    int ret2 = system("bin\\nybit.exe link bin/cli_undef.obj -o bin/cli_fail.exe 2>nul");
    TEST_ASSERT(ret2 != 0);
    TEST_ASSERT(!file_exists("bin/cli_fail.exe"));
    remove("bin/cli_undef.obj");
    ny_obj_buf_destroy(&obj_undef);

    /* 3. Duplicate symbol */
    Ny_Object_Buffer obj_d1, obj_d2;
    ny_obj_buf_init(&obj_d1);
    ny_obj_buf_init(&obj_d2);
    emit_cli_dummy_coff(&obj_d1, "duplicate_fn", false, nullptr);
    emit_cli_dummy_coff(&obj_d2, "duplicate_fn", false, nullptr);
    write_file("bin/cli_d1.obj", obj_d1.bytes, obj_d1.count);
    write_file("bin/cli_d2.obj", obj_d2.bytes, obj_d2.count);

    int ret3 = system("bin\\nybit.exe link bin/cli_d1.obj bin/cli_d2.obj -o bin/cli_fail.exe 2>nul");
    TEST_ASSERT(ret3 != 0);
    TEST_ASSERT(!file_exists("bin/cli_fail.exe"));
    remove("bin/cli_d2.obj");
    remove("bin/cli_d1.obj");
    ny_obj_buf_destroy(&obj_d2);
    ny_obj_buf_destroy(&obj_d1);

    /* 4. Unsupported target */
    int ret4 = system("bin\\nybit.exe link --target=mips bin/cli_undef.obj -o bin/cli_fail.exe 2>nul");
    TEST_ASSERT(ret4 != 0);

    /* 5. Invalid option */
    int ret5 = system("bin\\nybit.exe link --bogus-opt 2>nul");
    TEST_ASSERT(ret5 != 0);
}

void test_cli_link_determinism(void) {
    Ny_Object_Buffer obj1, obj2;
    ny_obj_buf_init(&obj1);
    ny_obj_buf_init(&obj2);

    emit_cli_dummy_coff(&obj1, "main", true, "sub_det");
    emit_cli_dummy_coff(&obj2, "sub_det", false, nullptr);

    write_file("bin/cli_det1.obj", obj1.bytes, obj1.count);
    write_file("bin/cli_det2.obj", obj2.bytes, obj2.count);

    int ret1 = system("bin\\nybit.exe link bin/cli_det1.obj bin/cli_det2.obj -o bin/cli_det_out1.exe");
    TEST_ASSERT_EQ(ret1, 0);

    int ret2 = system("bin\\nybit.exe link bin/cli_det1.obj bin/cli_det2.obj -o bin/cli_det_out2.exe");
    TEST_ASSERT_EQ(ret2, 0);

    FILE *f1 = fopen("bin/cli_det_out1.exe", "rb");
    FILE *f2 = fopen("bin/cli_det_out2.exe", "rb");
    TEST_ASSERT(f1 != nullptr && f2 != nullptr);

    fseek(f1, 0, SEEK_END);
    fseek(f2, 0, SEEK_END);
    long s1 = ftell(f1);
    long s2 = ftell(f2);
    TEST_ASSERT_EQ(s1, s2);

    fseek(f1, 0, SEEK_SET);
    fseek(f2, 0, SEEK_SET);
    uint8_t b1[512], b2[512];
    while (s1 > 0) {
        size_t n = s1 > 512 ? 512 : (size_t)s1;
        TEST_ASSERT_EQ(fread(b1, 1, n, f1), n);
        TEST_ASSERT_EQ(fread(b2, 1, n, f2), n);
        TEST_ASSERT_EQ(memcmp(b1, b2, n), 0);
        s1 -= n;
    }

    fclose(f2);
    fclose(f1);

    remove("bin/cli_det_out2.exe");
    remove("bin/cli_det_out1.exe");
    remove("bin/cli_det2.obj");
    remove("bin/cli_det1.obj");
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_cli_link_e2e_execution(void) {
#if defined(_WIN32) || defined(_WIN64)
    X86_Encoded_Module emod_sub;
    x86_encoded_mod_init(&emod_sub, ny_str("emod_sub"));
    const uint8_t code_sub[] = { 0x8d, 0x04, 0x11, 0xc3 }; /* lea eax, [rcx + rdx]; ret */
    x86_buf_append_bytes(&emod_sub.text_section, code_sub, sizeof(code_sub));
    ny_buf_grow((void **)&emod_sub.functions, &emod_sub.function_capacity, 1, sizeof(X86_Function_Code));
    emod_sub.functions[emod_sub.function_count++] = (X86_Function_Code){
        .name = ny_str("cli_fn_add"),
        .offset = 0,
        .size = sizeof(code_sub),
    };
    Ny_Object_Buffer obj_sub;
    ny_obj_buf_init(&obj_sub);
    Ny_Diagnostic_List diags_sub;
    ny_diagnostic_list_init(&diags_sub);
    TEST_ASSERT(ny_emit_coff_x86_64(&obj_sub, &emod_sub, &diags_sub));
    ny_diagnostic_list_destroy(&diags_sub);
    x86_encoded_mod_destroy(&emod_sub);

    Ny_Object_Buffer obj_unused;
    ny_obj_buf_init(&obj_unused);
    emit_cli_dummy_coff(&obj_unused, "cli_fn_unused", false, nullptr);

    mkdir_compat("bin/test_e2e_lib");
    Ny_Object_Buffer ar_buf;
    const char *names[] = { "add.obj", "unused.obj" };
    Ny_Object_Buffer objs[] = { obj_sub, obj_unused };
    build_cli_test_archive(&ar_buf, names, objs, 2);
    write_file("bin/test_e2e_lib/math.lib", ar_buf.bytes, ar_buf.count);

    X86_Encoded_Module emod_main;
    x86_encoded_mod_init(&emod_main, ny_str("emod_main"));
    const uint8_t code_main[] = {
        0x48, 0x83, 0xec, 0x28,
        0xb9, 0x14, 0x00, 0x00, 0x00,
        0xba, 0x16, 0x00, 0x00, 0x00,
        0xe8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x83, 0xc4, 0x28,
        0xc3
    };
    x86_buf_append_bytes(&emod_main.text_section, code_main, sizeof(code_main));
    ny_buf_grow((void **)&emod_main.functions, &emod_main.function_capacity, 1, sizeof(X86_Function_Code));
    emod_main.functions[emod_main.function_count++] = (X86_Function_Code){
        .name = ny_str("main"),
        .offset = 0,
        .size = sizeof(code_main),
    };
    X86_Relocation reloc = {
        .kind = X86_FIXUP_CALL_REL32,
        .code_offset = 15,
        .symbol_name = ny_str("cli_fn_add"),
        .addend = 0,
    };
    x86_buf_append_reloc(&emod_main.text_section, reloc);

    Ny_Object_Buffer obj_main;
    ny_obj_buf_init(&obj_main);
    Ny_Diagnostic_List diags_main;
    ny_diagnostic_list_init(&diags_main);
    TEST_ASSERT(ny_emit_coff_x86_64(&obj_main, &emod_main, &diags_main));
    ny_diagnostic_list_destroy(&diags_main);
    x86_encoded_mod_destroy(&emod_main);
    write_file("bin/cli_e2e_main.obj", obj_main.bytes, obj_main.count);

    int link_ret = system("bin\\nybit.exe link bin/cli_e2e_main.obj -Lbin/test_e2e_lib -lmath -o bin/cli_e2e_app.exe");
    TEST_ASSERT_EQ(link_ret, 0);

    int app_ret = system("bin\\cli_e2e_app.exe");
    TEST_ASSERT_EQ(app_ret, 42);

    remove("bin/cli_e2e_app.exe");
    remove("bin/cli_e2e_main.obj");
    remove("bin/test_e2e_lib/math.lib");
#if defined(_WIN32) || defined(_WIN64)
    _rmdir("bin/test_e2e_lib");
#else
    rmdir("bin/test_e2e_lib");
#endif

    ny_obj_buf_destroy(&obj_main);
    ny_obj_buf_destroy(&ar_buf);
    ny_obj_buf_destroy(&obj_unused);
    ny_obj_buf_destroy(&obj_sub);
#endif
}

void test_cli_link_shared_options(void) {
    Ny_Object_Buffer obj1;
    ny_obj_buf_init(&obj1);
    emit_cli_dummy_elf(&obj1, "so_func", false, nullptr);
    write_file("bin/cli_so_input.o", obj1.bytes, obj1.count);

    /* 1. Successful shared object emission via CLI */
    int ret = system("bin\\nybit.exe link --target=elf64 --shared bin/cli_so_input.o --soname=libfoo.so.1 --needed=libc.so.6 -o bin/libcli_foo.so");
    TEST_ASSERT_EQ(ret, 0);

    FILE *f = fopen("bin/libcli_foo.so", "rb");
    TEST_ASSERT(f != nullptr);

    uint8_t ident[16];
    TEST_ASSERT_EQ(fread(ident, 1, 16, f), 16);
    TEST_ASSERT_EQ(ident[0], 0x7F);
    TEST_ASSERT_EQ(ident[1], 'E');
    TEST_ASSERT_EQ(ident[2], 'L');
    TEST_ASSERT_EQ(ident[3], 'F');

    uint16_t e_type = 0;
    TEST_ASSERT_EQ(fread(&e_type, 2, 1, f), 1);
    TEST_ASSERT_EQ(e_type, 3); /* ET_DYN */

    fclose(f);
    remove("bin/libcli_foo.so");

    /* 2. Rejection of --shared on PE target */
    int ret_pe = system("bin\\nybit.exe link --target=pe-x86-64 --shared bin/cli_so_input.o -o bin/foo.dll 2>NUL");
    TEST_ASSERT(ret_pe != 0);

    remove("bin/cli_so_input.o");
    ny_obj_buf_destroy(&obj1);
}
