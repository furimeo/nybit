// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nylink/nylink.h"
#include "nybit/object.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include <string.h>

static void emit_dummy_elf(Ny_Object_Buffer *obj_buf, const char *fn_name, bool add_reloc, const char *reloc_target) {
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
