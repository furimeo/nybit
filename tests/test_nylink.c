// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nylink/nylink.h"
#include "nybit/object.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include <string.h>
#include <stdlib.h>

static void emit_dummy_elf_with_addend(Ny_Object_Buffer *obj_buf, const char *fn_name, bool add_reloc, const char *reloc_target, int64_t addend) {
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
            .addend = addend,
        };
        x86_buf_append_reloc(&emod.text_section, reloc);
    }

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    TEST_ASSERT(ny_emit_elf64_x86_64(obj_buf, &emod, &diags));

    ny_diagnostic_list_destroy(&diags);
    x86_encoded_mod_destroy(&emod);
}

static void emit_dummy_elf(Ny_Object_Buffer *obj_buf, const char *fn_name, bool add_reloc, const char *reloc_target) {
    emit_dummy_elf_with_addend(obj_buf, fn_name, add_reloc, reloc_target, 0);
}

static void emit_dummy_coff(Ny_Object_Buffer *obj_buf, const char *fn_name, bool add_reloc, const char *reloc_target) {
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

void test_nylink_context_lifecycle(void) {
    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(ctx != nullptr);
    TEST_ASSERT_EQ(nylink_get_section_count(ctx), 0);
    TEST_ASSERT_EQ(nylink_get_symbol_count(ctx), 0);
    TEST_ASSERT_EQ(nylink_get_relocation_count(ctx), 0);
    TEST_ASSERT_EQ(nylink_get_diagnostic_count(ctx), 0);
    TEST_ASSERT(!nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
}

void test_nylink_single_elf_loading(void) {
    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);
    emit_dummy_elf(&obj_buf, "foo_func", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "foo.o", obj_buf.bytes, obj_buf.count));
    TEST_ASSERT(!nylink_has_errors(ctx));

    TEST_ASSERT(nylink_get_section_count(ctx) >= 1);
    const Nylink_Section *text_sec = nullptr;
    for (size_t i = 0; i < nylink_get_section_count(ctx); i++) {
        const Nylink_Section *s = nylink_get_section(ctx, i);
        if (s && s->kind == NYLINK_SEC_TEXT) {
            text_sec = s;
            break;
        }
    }
    TEST_ASSERT(text_sec != nullptr);
    TEST_ASSERT_STR_EQ(text_sec->name, ".text");
    TEST_ASSERT_EQ(text_sec->size, 6);

    const Nylink_Symbol *sym = nylink_find_symbol(ctx, "foo_func");
    TEST_ASSERT(sym != nullptr);
    TEST_ASSERT_STR_EQ(sym->name, "foo_func");
    TEST_ASSERT(sym->is_defined);
    TEST_ASSERT_EQ(sym->binding, NYLINK_SYM_GLOBAL);

    TEST_ASSERT(nylink_resolve_symbols(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj_buf);
}

void test_nylink_single_coff_loading(void) {
    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);
    emit_dummy_coff(&obj_buf, "bar_func", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "bar.obj", obj_buf.bytes, obj_buf.count));
    TEST_ASSERT(!nylink_has_errors(ctx));

    TEST_ASSERT(nylink_get_section_count(ctx) >= 1);
    const Nylink_Section *text_sec = nullptr;
    for (size_t i = 0; i < nylink_get_section_count(ctx); i++) {
        const Nylink_Section *s = nylink_get_section(ctx, i);
        if (s && s->kind == NYLINK_SEC_TEXT) {
            text_sec = s;
            break;
        }
    }
    TEST_ASSERT(text_sec != nullptr);
    TEST_ASSERT_EQ(text_sec->size, 6);

    const Nylink_Symbol *sym = nylink_find_symbol(ctx, "bar_func");
    TEST_ASSERT(sym != nullptr);
    TEST_ASSERT_STR_EQ(sym->name, "bar_func");
    TEST_ASSERT(sym->is_defined);
    TEST_ASSERT_EQ(sym->binding, NYLINK_SYM_GLOBAL);

    TEST_ASSERT(nylink_resolve_symbols(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj_buf);
}

void test_nylink_multi_object_relocations(void) {
    Ny_Object_Buffer obj1_buf;
    ny_obj_buf_init(&obj1_buf);
    emit_dummy_elf(&obj1_buf, "caller_fn", true, "callee_fn");

    Ny_Object_Buffer obj2_buf;
    ny_obj_buf_init(&obj2_buf);
    emit_dummy_elf(&obj2_buf, "callee_fn", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "caller.o", obj1_buf.bytes, obj1_buf.count));
    TEST_ASSERT(nylink_add_object(ctx, "callee.o", obj2_buf.bytes, obj2_buf.count));
    TEST_ASSERT(!nylink_has_errors(ctx));

    TEST_ASSERT_EQ(nylink_get_relocation_count(ctx), 1);
    const Nylink_Relocation *reloc = nylink_get_relocation(ctx, 0);
    TEST_ASSERT(reloc != nullptr);
    TEST_ASSERT_EQ(reloc->offset, 1);
    TEST_ASSERT(reloc->type == NYLINK_RELOC_X86_64_PC32 || reloc->type == NYLINK_RELOC_X86_64_PLT32);

    TEST_ASSERT(nylink_resolve_symbols(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    const Nylink_Symbol *caller_sym = nylink_find_symbol(ctx, "caller_fn");
    const Nylink_Symbol *callee_sym = nylink_find_symbol(ctx, "callee_fn");
    TEST_ASSERT(caller_sym != nullptr && caller_sym->is_defined);
    TEST_ASSERT(callee_sym != nullptr && callee_sym->is_defined);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2_buf);
    ny_obj_buf_destroy(&obj1_buf);
}

void test_nylink_duplicate_symbol_error(void) {
    Ny_Object_Buffer obj1_buf;
    ny_obj_buf_init(&obj1_buf);
    emit_dummy_elf(&obj1_buf, "duplicate_fn", false, nullptr);

    Ny_Object_Buffer obj2_buf;
    ny_obj_buf_init(&obj2_buf);
    emit_dummy_elf(&obj2_buf, "duplicate_fn", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "first.o", obj1_buf.bytes, obj1_buf.count));
    TEST_ASSERT(nylink_add_object(ctx, "second.o", obj2_buf.bytes, obj2_buf.count));
    TEST_ASSERT(!nylink_has_errors(ctx));

    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(!resolved);
    TEST_ASSERT(nylink_has_errors(ctx));
    TEST_ASSERT(nylink_get_diagnostic_count(ctx) > 0);

    const Nylink_Diagnostic *diag = nylink_get_diagnostic(ctx, 0);
    TEST_ASSERT(diag != nullptr);
    TEST_ASSERT(diag->message != nullptr);
    TEST_ASSERT(strstr(diag->message, "duplicate symbol definition") != nullptr);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2_buf);
    ny_obj_buf_destroy(&obj1_buf);
}

void test_nylink_undefined_symbol_error(void) {
    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);
    emit_dummy_elf(&obj_buf, "main_caller", true, "missing_external_symbol");

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "main.o", obj_buf.bytes, obj_buf.count));
    TEST_ASSERT(!nylink_has_errors(ctx));

    bool resolved = nylink_resolve_symbols(ctx);
    TEST_ASSERT(!resolved);
    TEST_ASSERT(nylink_has_errors(ctx));
    TEST_ASSERT(nylink_get_diagnostic_count(ctx) > 0);

    const Nylink_Diagnostic *diag = nylink_get_diagnostic(ctx, 0);
    TEST_ASSERT(diag != nullptr);
    TEST_ASSERT(strstr(diag->message, "undefined symbol: missing_external_symbol") != nullptr);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj_buf);
}

void test_nylink_malformed_objects(void) {
    Nylink_Context *ctx = nylink_context_create();

    const uint8_t tiny_data[] = { 0x01, 0x02 };
    TEST_ASSERT(!nylink_add_object(ctx, "too_small.o", tiny_data, sizeof(tiny_data)));
    TEST_ASSERT(nylink_has_errors(ctx));

    const uint8_t bogus_magic[] = { 'B', 'A', 'D', '!', 0x00, 0x00, 0x00, 0x00 };
    TEST_ASSERT(!nylink_add_object(ctx, "bogus.o", bogus_magic, sizeof(bogus_magic)));

    const uint8_t truncated_elf[] = { 0x7F, 'E', 'L', 'F', 0x02, 0x01, 0x01, 0x00, 0x00, 0x00 };
    TEST_ASSERT(!nylink_add_object(ctx, "truncated.o", truncated_elf, sizeof(truncated_elf)));

    TEST_ASSERT(nylink_get_diagnostic_count(ctx) >= 3);

    nylink_context_destroy(ctx);
}

void test_nylink_section_layout_and_symbol_vas(void) {
    Ny_Object_Buffer obj1;
    ny_obj_buf_init(&obj1);
    emit_dummy_elf(&obj1, "fn1", false, nullptr);

    Ny_Object_Buffer obj2;
    ny_obj_buf_init(&obj2);
    emit_dummy_elf(&obj2, "fn2", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "o1.o", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx, "o2.o", obj2.bytes, obj2.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .base_address = 0x400000,
        .entry_point = "fn1",
    };

    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    const Nylink_Symbol *s1 = nylink_find_symbol(ctx, "fn1");
    const Nylink_Symbol *s2 = nylink_find_symbol(ctx, "fn2");
    TEST_ASSERT(s1 != nullptr && s2 != nullptr);

    uint64_t va1 = nylink_symbol_get_final_va(ctx, s1->id);
    uint64_t va2 = nylink_symbol_get_final_va(ctx, s2->id);

    TEST_ASSERT(va1 >= 0x400000);
    TEST_ASSERT(va2 > va1);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_relocation_application(void) {
    Ny_Object_Buffer obj1;
    ny_obj_buf_init(&obj1);
    emit_dummy_elf(&obj1, "caller_main", true, "callee_target");

    Ny_Object_Buffer obj2;
    ny_obj_buf_init(&obj2);
    emit_dummy_elf(&obj2, "callee_target", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "caller.o", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx, "callee.o", obj2.bytes, obj2.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .base_address = 0x400000,
        .entry_point = "caller_main",
    };

    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_relocation_pc32_overflow(void) {
    Ny_Object_Buffer obj1;
    ny_obj_buf_init(&obj1);
    /* Set massive addend to force 32-bit PC-relative overflow */
    emit_dummy_elf_with_addend(&obj1, "far_caller", true, "far_target", 0x10000000000LL);

    Ny_Object_Buffer obj2;
    ny_obj_buf_init(&obj2);
    emit_dummy_elf(&obj2, "far_target", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "far1.o", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx, "far2.o", obj2.bytes, obj2.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .base_address = 0x400000,
        .entry_point = "far_caller",
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));

    TEST_ASSERT(!nylink_apply_relocations(ctx));
    TEST_ASSERT(nylink_has_errors(ctx));

    const Nylink_Diagnostic *diag = nylink_get_diagnostic(ctx, 0);
    TEST_ASSERT(diag != nullptr && diag->message != nullptr);
    TEST_ASSERT(strstr(diag->message, "relocation overflow") != nullptr);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_elf64_executable_emission(void) {
    Ny_Object_Buffer obj1;
    ny_obj_buf_init(&obj1);
    emit_dummy_elf(&obj1, "elf_main", true, "elf_sub");

    Ny_Object_Buffer obj2;
    ny_obj_buf_init(&obj2);
    emit_dummy_elf(&obj2, "elf_sub", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "em1.o", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx, "em2.o", obj2.bytes, obj2.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .base_address = 0x400000,
        .entry_point = "elf_main",
    };

    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *out_exe = "bin/test_nylink_elf.exe";
    TEST_ASSERT(nylink_write_executable(ctx, out_exe, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Verify written ELF binary */
    FILE *f = fopen(out_exe, "rb");
    TEST_ASSERT(f != nullptr);
    uint8_t magic[4];
    TEST_ASSERT_EQ(fread(magic, 1, 4, f), 4);
    TEST_ASSERT_EQ(magic[0], 0x7F);
    TEST_ASSERT_EQ(magic[1], 'E');
    TEST_ASSERT_EQ(magic[2], 'L');
    TEST_ASSERT_EQ(magic[3], 'F');
    fclose(f);
    remove(out_exe);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_pe_executable_emission(void) {
    Ny_Object_Buffer obj1;
    ny_obj_buf_init(&obj1);
    emit_dummy_coff(&obj1, "pe_main", true, "pe_sub");

    Ny_Object_Buffer obj2;
    ny_obj_buf_init(&obj2);
    emit_dummy_coff(&obj2, "pe_sub", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "pe1.obj", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx, "pe2.obj", obj2.bytes, obj2.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_PE,
        .base_address = 0x140000000ULL,
        .entry_point = "pe_main",
    };

    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *out_exe = "bin/test_nylink_pe.exe";
    TEST_ASSERT(nylink_write_executable(ctx, out_exe, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Verify written PE binary */
    FILE *f = fopen(out_exe, "rb");
    TEST_ASSERT(f != nullptr);
    uint8_t dos_sig[2];
    TEST_ASSERT_EQ(fread(dos_sig, 1, 2, f), 2);
    TEST_ASSERT_EQ(dos_sig[0], 'M');
    TEST_ASSERT_EQ(dos_sig[1], 'Z');

    fseek(f, 0x80, SEEK_SET);
    uint8_t pe_sig[4];
    TEST_ASSERT_EQ(fread(pe_sig, 1, 4, f), 4);
    TEST_ASSERT_EQ(pe_sig[0], 'P');
    TEST_ASSERT_EQ(pe_sig[1], 'E');
    TEST_ASSERT_EQ(pe_sig[2], 0);
    TEST_ASSERT_EQ(pe_sig[3], 0);
    fclose(f);
    remove(out_exe);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_e2e_multi_object_execution(void) {
#if defined(_WIN32)
    /* Test creating two COFF objects:
       Object 1 (sub):
         fn_add(a, b): returns a + b
       Object 2 (main):
         main(): calls fn_add(20, 22), returns 42
    */
    X86_Encoded_Module emod_sub;
    x86_encoded_mod_init(&emod_sub, ny_str("emod_sub"));

    /* sub function:
       mov eax, ecx
       add eax, edx
       ret
       bytes: 89 c8 01 d0 c3
    */
    const uint8_t code_sub[] = { 0x89, 0xc8, 0x01, 0xd0, 0xc3 };
    x86_buf_append_bytes(&emod_sub.text_section, code_sub, sizeof(code_sub));
    ny_buf_grow((void **)&emod_sub.functions, &emod_sub.function_capacity, 1, sizeof(X86_Function_Code));
    emod_sub.functions[emod_sub.function_count++] = (X86_Function_Code){
        .name = ny_str("fn_add"),
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

    /* main function:
       sub rsp, 40 (shadow space)
       mov ecx, 20
       mov edx, 22
       call fn_add (rel32 at offset 13)
       add rsp, 40
       ret
       bytes: 48 83 ec 28 b9 14 00 00 00 ba 16 00 00 00 e8 00 00 00 00 48 83 c4 28 c3
    */
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
        .symbol_name = ny_str("fn_add"),
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

    /* Link using nylink.lib */
    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "sub.obj", obj_sub.bytes, obj_sub.count));
    TEST_ASSERT(nylink_add_object(ctx, "main.obj", obj_main.bytes, obj_main.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_PE,
        .base_address = 0x140000000ULL,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *out_exe = "bin/test_nylink_run.exe";
    TEST_ASSERT(nylink_write_executable(ctx, out_exe, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    int ret = system("bin\\test_nylink_run.exe");
    TEST_ASSERT_EQ(ret, 42);

    remove(out_exe);
    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj_main);
    ny_obj_buf_destroy(&obj_sub);
#endif
}
