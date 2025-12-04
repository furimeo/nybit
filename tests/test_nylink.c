// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nylink/nylink.h"
#include "nybit/object.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include <string.h>
#include <stdlib.h>

#pragma pack(push, 1)
typedef struct Elf64_Test_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Test_Ehdr;

typedef struct Elf64_Test_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Test_Phdr;

typedef struct Elf64_Test_Shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} Elf64_Test_Shdr;

typedef struct Elf64_Test_Sym {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Test_Sym;

typedef struct Elf64_Test_Dyn {
    int64_t d_tag;
    uint64_t d_val;
} Elf64_Test_Dyn;

typedef struct Elf64_Test_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} Elf64_Test_Rela;

typedef struct Pe_Test_Dos_Header {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;
} Pe_Test_Dos_Header;

typedef struct Pe_Test_File_Header {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} Pe_Test_File_Header;

typedef struct Pe_Test_Data_Directory {
    uint32_t VirtualAddress;
    uint32_t Size;
} Pe_Test_Data_Directory;

typedef struct Pe_Test_Optional_Header64 {
    uint16_t Magic;
    uint8_t  MajorLinkerVersion;
    uint8_t  MinorLinkerVersion;
    uint32_t SizeOfCode;
    uint32_t SizeOfInitializedData;
    uint32_t SizeOfUninitializedData;
    uint32_t AddressOfEntryPoint;
    uint32_t BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment;
    uint32_t FileAlignment;
    uint16_t MajorOperatingSystemVersion;
    uint16_t MinorOperatingSystemVersion;
    uint16_t MajorImageVersion;
    uint16_t MinorImageVersion;
    uint16_t MajorSubsystemVersion;
    uint16_t MinorSubsystemVersion;
    uint32_t Win32VersionValue;
    uint32_t SizeOfImage;
    uint32_t SizeOfHeaders;
    uint32_t CheckSum;
    uint16_t Subsystem;
    uint16_t DllCharacteristics;
    uint64_t SizeOfStackReserve;
    uint64_t SizeOfStackCommit;
    uint64_t SizeOfHeapReserve;
    uint64_t SizeOfHeapCommit;
    uint32_t LoaderFlags;
    uint32_t NumberOfRvaAndSizes;
    Pe_Test_Data_Directory DataDirectory[16];
} Pe_Test_Optional_Header64;

typedef struct Pe_Test_Section_Header {
    uint8_t  Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} Pe_Test_Section_Header;

typedef struct Pe_Test_Relocation {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
} Pe_Test_Relocation;
#pragma pack(pop)

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

static void emit_dummy_elf_data_ref(Ny_Object_Buffer *obj_buf, const char *fn_name, const char *data_target) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("dummy_elf_data_ref"));

    const uint8_t code[] = { 0x8b, 0x05, 0x00, 0x00, 0x00, 0x00, 0xc3 }; /* mov eax, [rip+disp32]; ret */
    x86_buf_append_bytes(&emod.text_section, code, sizeof(code));

    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, emod.function_count + 1, sizeof(X86_Function_Code));
    emod.functions[emod.function_count++] = (X86_Function_Code){
        .name = ny_str(fn_name),
        .offset = 0,
        .size = sizeof(code),
    };

    X86_Relocation reloc = {
        .kind = X86_FIXUP_GLOBAL_REL32,
        .code_offset = 2,
        .symbol_name = ny_str(data_target),
        .addend = 0,
    };
    x86_buf_append_reloc(&emod.text_section, reloc);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    TEST_ASSERT(ny_emit_elf64_x86_64(obj_buf, &emod, &diags));

    ny_diagnostic_list_destroy(&diags);
    x86_encoded_mod_destroy(&emod);
}

static void emit_dummy_elf_gotpcrel(Ny_Object_Buffer *obj_buf, const char *fn_name, const char *data_target) {
    emit_dummy_elf_data_ref(obj_buf, fn_name, data_target);

    Elf64_Test_Ehdr *ehdr = (Elf64_Test_Ehdr *)obj_buf->bytes;
    Elf64_Test_Shdr *shdrs = (Elf64_Test_Shdr *)(obj_buf->bytes + ehdr->e_shoff);
    const char *shstrtab = (const char *)(obj_buf->bytes + shdrs[ehdr->e_shstrndx].sh_offset);

    for (uint16_t s = 1; s < ehdr->e_shnum; s++) {
        if (strcmp(shstrtab + shdrs[s].sh_name, ".rela.text") == 0) {
            Elf64_Test_Rela *relas = (Elf64_Test_Rela *)(obj_buf->bytes + shdrs[s].sh_offset);
            size_t nrelas = shdrs[s].sh_size / sizeof(Elf64_Test_Rela);
            for (size_t r = 0; r < nrelas; r++) {
                uint32_t sym_idx = (uint32_t)(relas[r].r_info >> 32);
                relas[r].r_info = ((uint64_t)sym_idx << 32) | 9; /* R_X86_64_GOTPCREL */
            }
            break;
        }
    }
}

static void emit_dummy_elf(Ny_Object_Buffer *obj_buf, const char *fn_name, bool add_reloc, const char *reloc_target) {
    emit_dummy_elf_with_addend(obj_buf, fn_name, add_reloc, reloc_target, 0);
}

static void emit_dummy_elf_data(Ny_Object_Buffer *obj_buf, const char *var_name) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("dummy_elf_data"));

    uint32_t val = 123;
    x86_buf_append_bytes(&emod.data_section, (const uint8_t *)&val, sizeof(val));

    ny_buf_grow((void **)&emod.globals, &emod.global_capacity, emod.global_count + 1, sizeof(X86_Encoded_Global));
    emod.globals[emod.global_count++] = (X86_Encoded_Global){
        .name = ny_str(var_name),
        .kind = NY_GLOBAL_DATA,
        .offset = 0,
        .size = sizeof(val),
        .align = 4,
    };

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

    /* Parse and strictly validate ELF64 header and program headers */
    FILE *f = fopen(out_exe, "rb");
    TEST_ASSERT(f != nullptr);

    Elf64_Test_Ehdr ehdr;
    TEST_ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    TEST_ASSERT_EQ(ehdr.e_ident[0], 0x7F);
    TEST_ASSERT_EQ(ehdr.e_ident[1], 'E');
    TEST_ASSERT_EQ(ehdr.e_ident[2], 'L');
    TEST_ASSERT_EQ(ehdr.e_ident[3], 'F');
    TEST_ASSERT_EQ(ehdr.e_type, 2); /* ET_EXEC */
    TEST_ASSERT_EQ(ehdr.e_machine, 62); /* EM_X86_64 */
    TEST_ASSERT_EQ(ehdr.e_entry, nylink_symbol_get_final_va(ctx, nylink_find_symbol(ctx, "elf_main")->id));
    TEST_ASSERT(ehdr.e_phnum >= 1);

    Elf64_Test_Phdr phdrs[4];
    fseek(f, (long)ehdr.e_phoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(phdrs, sizeof(Elf64_Test_Phdr), ehdr.e_phnum, f), ehdr.e_phnum);
    TEST_ASSERT_EQ(phdrs[0].p_type, 1); /* PT_LOAD */
    TEST_ASSERT_EQ(phdrs[0].p_flags, 5); /* PF_R | PF_X */
    TEST_ASSERT_EQ(phdrs[0].p_vaddr, 0x400000);

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

    /* Parse and strictly validate PE32+ header and section headers */
    FILE *f = fopen(out_exe, "rb");
    TEST_ASSERT(f != nullptr);

    Pe_Test_Dos_Header dos_hdr;
    TEST_ASSERT_EQ(fread(&dos_hdr, sizeof(dos_hdr), 1, f), 1);
    TEST_ASSERT_EQ(dos_hdr.e_magic, 0x5A4D); /* MZ */
    TEST_ASSERT_EQ(dos_hdr.e_lfanew, 0x80);

    fseek(f, (long)dos_hdr.e_lfanew, SEEK_SET);
    uint32_t pe_sig = 0;
    TEST_ASSERT_EQ(fread(&pe_sig, 4, 1, f), 1);
    TEST_ASSERT_EQ(pe_sig, 0x00004550); /* PE\0\0 */

    Pe_Test_File_Header fhdr;
    TEST_ASSERT_EQ(fread(&fhdr, sizeof(fhdr), 1, f), 1);
    TEST_ASSERT_EQ(fhdr.Machine, 0x8664); /* AMD64 */
    TEST_ASSERT_EQ(fhdr.TimeDateStamp, 0); /* Deterministic zero timestamp */
    TEST_ASSERT_EQ(fhdr.NumberOfSymbols, 0); /* Executable image has no symbol table */
    TEST_ASSERT_EQ(fhdr.PointerToSymbolTable, 0);

    Pe_Test_Optional_Header64 opt;
    TEST_ASSERT_EQ(fread(&opt, sizeof(opt), 1, f), 1);
    TEST_ASSERT_EQ(opt.Magic, 0x20B); /* PE32+ */
    TEST_ASSERT_EQ(opt.ImageBase, 0x140000000ULL);
    TEST_ASSERT_EQ(opt.SectionAlignment, 0x1000);
    TEST_ASSERT_EQ(opt.FileAlignment, 0x200);
    TEST_ASSERT_EQ(opt.Subsystem, 3); /* Windows CUI */
    TEST_ASSERT(opt.AddressOfEntryPoint >= 0x1000);

    Pe_Test_Section_Header shdr;
    TEST_ASSERT_EQ(fread(&shdr, sizeof(shdr), 1, f), 1);
    TEST_ASSERT_STR_EQ((const char *)shdr.Name, ".text");
    TEST_ASSERT(shdr.Characteristics & 0x20000000); /* MEM_EXECUTE */
    TEST_ASSERT_EQ(shdr.NumberOfRelocations, 0);

    fclose(f);
    remove(out_exe);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_deterministic_emission(void) {
    Ny_Object_Buffer obj1;
    ny_obj_buf_init(&obj1);
    emit_dummy_coff(&obj1, "det_main", true, "det_sub");

    Ny_Object_Buffer obj2;
    ny_obj_buf_init(&obj2);
    emit_dummy_coff(&obj2, "det_sub", false, nullptr);

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_PE,
        .base_address = 0x140000000ULL,
        .entry_point = "det_main",
    };

    /* Build 1 */
    Nylink_Context *ctx1 = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx1, "o1.obj", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx1, "o2.obj", obj2.bytes, obj2.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx1));
    TEST_ASSERT(nylink_layout(ctx1, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx1));
    const char *out1 = "bin/test_det1.exe";
    TEST_ASSERT(nylink_write_executable(ctx1, out1, &cfg));

    /* Build 2 */
    Nylink_Context *ctx2 = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx2, "o1.obj", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx2, "o2.obj", obj2.bytes, obj2.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx2));
    TEST_ASSERT(nylink_layout(ctx2, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx2));
    const char *out2 = "bin/test_det2.exe";
    TEST_ASSERT(nylink_write_executable(ctx2, out2, &cfg));

    /* Compare byte-for-byte */
    FILE *f1 = fopen(out1, "rb");
    FILE *f2 = fopen(out2, "rb");
    TEST_ASSERT(f1 != nullptr && f2 != nullptr);

    fseek(f1, 0, SEEK_END);
    fseek(f2, 0, SEEK_END);
    long sz1 = ftell(f1);
    long sz2 = ftell(f2);
    TEST_ASSERT_EQ(sz1, sz2);

    fseek(f1, 0, SEEK_SET);
    fseek(f2, 0, SEEK_SET);
    uint8_t b1[512], b2[512];
    while (sz1 > 0) {
        size_t n = sz1 > 512 ? 512 : (size_t)sz1;
        TEST_ASSERT_EQ(fread(b1, 1, n, f1), n);
        TEST_ASSERT_EQ(fread(b2, 1, n, f2), n);
        TEST_ASSERT_EQ(memcmp(b1, b2, n), 0);
        sz1 -= n;
    }

    fclose(f2);
    fclose(f1);
    remove(out2);
    remove(out1);

    nylink_context_destroy(ctx2);
    nylink_context_destroy(ctx1);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_negative_validation_cases(void) {
    /* Test 1: Entry point outside .text */
    Ny_Object_Buffer obj;
    ny_obj_buf_init(&obj);
    emit_dummy_elf(&obj, "valid_func", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "obj.o", obj.bytes, obj.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg_invalid_entry = {
        .target_format = NYLINK_TARGET_ELF64,
        .base_address = 0x400000,
        .entry_point = "nonexistent_entry",
    };
    TEST_ASSERT(!nylink_layout(ctx, &cfg_invalid_entry));
    TEST_ASSERT(nylink_has_errors(ctx));
    TEST_ASSERT(nylink_get_diagnostic_count(ctx) > 0);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj);
}

void test_nylink_e2e_multi_object_execution(void) {
#if defined(_WIN32)
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
       call fn_add (rel32 at offset 15, instruction ends at 19)
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

    /* Verify patched call displacement:
       fn_add is at offset 0 of sub.obj. In .text layout, sub is laid out first, main is laid out second.
       fn_add VA = 0x140001000
       main VA = 0x140001000 + align_up(5, 16) = 0x140001010
       call site at main + 15 = 0x14000101f
       displacement = target (0x140001000) - (call_site (0x14000101f) + 4)
                    = 0x140001000 - 0x140001023 = -0x23 (-35) = 0xffffffdd
    */
    const char *out_exe = "bin/test_nylink_run.exe";
    TEST_ASSERT(nylink_write_executable(ctx, out_exe, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Verify patched call displacement in emitted binary:
       In PE, file alignment is 0x200, headers take 0x400.
       .text section raw data is at offset 0x400.
       fn_add (sub.obj) is at 0x400.
       main (main.obj) is aligned to 16, so offset is 0x410.
       call site relative operand is at 0x410 + 15 = 0x41f.
    */
    FILE *fexe = fopen(out_exe, "rb");
    TEST_ASSERT(fexe != nullptr);
    fseek(fexe, 0x41F, SEEK_SET);
    uint32_t patched_disp = 0;
    TEST_ASSERT_EQ(fread(&patched_disp, 4, 1, fexe), 1);
    fclose(fexe);
    TEST_ASSERT_EQ(patched_disp, 0xffffffdd);

    int ret = system("bin\\test_nylink_run.exe");
    TEST_ASSERT_EQ(ret, 42);

    remove(out_exe);
    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj_main);
    ny_obj_buf_destroy(&obj_sub);
#endif
}

static void build_test_archive(Ny_Object_Buffer *ar_buf, const char **member_names, const Ny_Object_Buffer *member_objs, size_t count) {
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

        /* Timestamp */
        memcpy(hdr + 16, "0           ", 12);
        /* UID, GID, Mode */
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

void test_nylink_archive_single_member_extraction(void) {
    Ny_Object_Buffer obj_m1, obj_m2, obj_caller;
    ny_obj_buf_init(&obj_m1);
    ny_obj_buf_init(&obj_m2);
    ny_obj_buf_init(&obj_caller);

    emit_dummy_coff(&obj_m1, "needed_func", false, nullptr);
    emit_dummy_coff(&obj_m2, "unused_func", false, nullptr);
    emit_dummy_coff(&obj_caller, "main", true, "needed_func");

    Ny_Object_Buffer ar_buf;
    const char *names[] = { "m1.obj", "m2.obj" };
    Ny_Object_Buffer objs[] = { obj_m1, obj_m2 };
    build_test_archive(&ar_buf, names, objs, 2);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "caller.obj", obj_caller.bytes, obj_caller.count));
    TEST_ASSERT(nylink_add_archive(ctx, "mylib.lib", ar_buf.bytes, ar_buf.count));

    TEST_ASSERT(nylink_resolve_symbols(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Only caller.obj and m1.obj must be loaded; m2.obj was unused and must NOT be loaded */
    TEST_ASSERT_EQ(nylink_get_object_count(ctx), 2);
    TEST_ASSERT_STR_EQ(nylink_get_object_name(ctx, 0), "caller.obj");
    TEST_ASSERT_STR_EQ(nylink_get_object_name(ctx, 1), "mylib.lib(m1.obj)");

    const Nylink_Symbol *sym_needed = nylink_find_symbol(ctx, "needed_func");
    TEST_ASSERT(sym_needed != nullptr && sym_needed->is_defined);

    const Nylink_Symbol *sym_unused = nylink_find_symbol(ctx, "unused_func");
    TEST_ASSERT(sym_unused == nullptr);

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_PE,
        .base_address = 0x140000000ULL,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&ar_buf);
    ny_obj_buf_destroy(&obj_caller);
    ny_obj_buf_destroy(&obj_m2);
    ny_obj_buf_destroy(&obj_m1);
}

void test_nylink_archive_unused_members(void) {
    Ny_Object_Buffer obj_m1, obj_m2, obj_standalone;
    ny_obj_buf_init(&obj_m1);
    ny_obj_buf_init(&obj_m2);
    ny_obj_buf_init(&obj_standalone);

    emit_dummy_elf(&obj_m1, "arch_fn1", false, nullptr);
    emit_dummy_elf(&obj_m2, "arch_fn2", false, nullptr);
    emit_dummy_elf(&obj_standalone, "main", false, nullptr);

    Ny_Object_Buffer ar_buf;
    const char *names[] = { "m1.o", "m2.o" };
    Ny_Object_Buffer objs[] = { obj_m1, obj_m2 };
    build_test_archive(&ar_buf, names, objs, 2);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "standalone.o", obj_standalone.bytes, obj_standalone.count));
    TEST_ASSERT(nylink_add_archive(ctx, "libunused.a", ar_buf.bytes, ar_buf.count));

    TEST_ASSERT(nylink_resolve_symbols(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Zero members extracted from archive */
    TEST_ASSERT_EQ(nylink_get_object_count(ctx), 1);
    TEST_ASSERT_STR_EQ(nylink_get_object_name(ctx, 0), "standalone.o");

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&ar_buf);
    ny_obj_buf_destroy(&obj_standalone);
    ny_obj_buf_destroy(&obj_m2);
    ny_obj_buf_destroy(&obj_m1);
}

void test_nylink_archive_chained_dependencies(void) {
    Ny_Object_Buffer obj_main, obj_a, obj_b;
    ny_obj_buf_init(&obj_main);
    ny_obj_buf_init(&obj_a);
    ny_obj_buf_init(&obj_b);

    /* main calls fn_a; fn_a calls fn_b; fn_b is leaf */
    emit_dummy_elf(&obj_main, "main", true, "fn_a");
    emit_dummy_elf(&obj_a, "fn_a", true, "fn_b");
    emit_dummy_elf(&obj_b, "fn_b", false, nullptr);

    Ny_Object_Buffer ar_buf;
    const char *names[] = { "a.o", "b.o" };
    Ny_Object_Buffer objs[] = { obj_a, obj_b };
    build_test_archive(&ar_buf, names, objs, 2);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "main.o", obj_main.bytes, obj_main.count));
    TEST_ASSERT(nylink_add_archive(ctx, "libchain.a", ar_buf.bytes, ar_buf.count));

    TEST_ASSERT(nylink_resolve_symbols(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Both a.o and b.o must be extracted in cascading fixpoint */
    TEST_ASSERT_EQ(nylink_get_object_count(ctx), 3);
    TEST_ASSERT_STR_EQ(nylink_get_object_name(ctx, 0), "main.o");
    TEST_ASSERT_STR_EQ(nylink_get_object_name(ctx, 1), "libchain.a(a.o)");
    TEST_ASSERT_STR_EQ(nylink_get_object_name(ctx, 2), "libchain.a(b.o)");

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .base_address = 0x400000,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&ar_buf);
    ny_obj_buf_destroy(&obj_b);
    ny_obj_buf_destroy(&obj_a);
    ny_obj_buf_destroy(&obj_main);
}

void test_nylink_archive_cyclic_dependencies(void) {
    Ny_Object_Buffer obj_main, obj_a, obj_b;
    ny_obj_buf_init(&obj_main);
    ny_obj_buf_init(&obj_a);
    ny_obj_buf_init(&obj_b);

    /* main calls fn_a; fn_a calls fn_b; fn_b calls fn_a_helper */
    emit_dummy_elf(&obj_main, "main", true, "fn_a");

    /* obj_a defines fn_a and fn_a_helper, calls fn_b */
    X86_Encoded_Module emod_a;
    x86_encoded_mod_init(&emod_a, ny_str("dummy_a"));
    const uint8_t code_a[] = {
        0xe8, 0x00, 0x00, 0x00, 0x00, 0xc3, /* fn_a: call fn_b; ret */
        0x90, 0xc3                          /* fn_a_helper: nop; ret */
    };
    x86_buf_append_bytes(&emod_a.text_section, code_a, sizeof(code_a));
    ny_buf_grow((void **)&emod_a.functions, &emod_a.function_capacity, 2, sizeof(X86_Function_Code));
    emod_a.functions[emod_a.function_count++] = (X86_Function_Code){
        .name = ny_str("fn_a"),
        .offset = 0,
        .size = 6,
    };
    emod_a.functions[emod_a.function_count++] = (X86_Function_Code){
        .name = ny_str("fn_a_helper"),
        .offset = 6,
        .size = 2,
    };
    X86_Relocation reloc_a = {
        .kind = X86_FIXUP_CALL_REL32,
        .code_offset = 1,
        .symbol_name = ny_str("fn_b"),
        .addend = 0,
    };
    x86_buf_append_reloc(&emod_a.text_section, reloc_a);
    Ny_Diagnostic_List diags_a;
    ny_diagnostic_list_init(&diags_a);
    TEST_ASSERT(ny_emit_elf64_x86_64(&obj_a, &emod_a, &diags_a));
    ny_diagnostic_list_destroy(&diags_a);
    x86_encoded_mod_destroy(&emod_a);

    /* obj_b defines fn_b, calls fn_a_helper */
    emit_dummy_elf(&obj_b, "fn_b", true, "fn_a_helper");

    Ny_Object_Buffer ar_buf;
    const char *names[] = { "a.o", "b.o" };
    Ny_Object_Buffer objs[] = { obj_a, obj_b };
    build_test_archive(&ar_buf, names, objs, 2);

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "main.o", obj_main.bytes, obj_main.count));
    TEST_ASSERT(nylink_add_archive(ctx, "libcyclic.a", ar_buf.bytes, ar_buf.count));

    TEST_ASSERT(nylink_resolve_symbols(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));
    TEST_ASSERT_EQ(nylink_get_object_count(ctx), 3);

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .base_address = 0x400000,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));
    TEST_ASSERT(!nylink_has_errors(ctx));

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&ar_buf);
    ny_obj_buf_destroy(&obj_b);
    ny_obj_buf_destroy(&obj_a);
    ny_obj_buf_destroy(&obj_main);
}

void test_nylink_archive_malformed_and_bounds(void) {
    /* 1. Too small */
    {
        Nylink_Context *ctx = nylink_context_create();
        const uint8_t tiny[4] = { '!', '<', 'a', 'r' };
        TEST_ASSERT(!nylink_add_archive(ctx, "tiny.a", tiny, sizeof(tiny)));
        TEST_ASSERT(nylink_has_errors(ctx));
        nylink_context_destroy(ctx);
    }

    /* 2. Bad magic */
    {
        Nylink_Context *ctx = nylink_context_create();
        const uint8_t bad_magic[8] = { 'N', 'O', 'T', 'A', 'R', 'C', 'H', '!' };
        TEST_ASSERT(!nylink_add_archive(ctx, "bad.a", bad_magic, sizeof(bad_magic)));
        TEST_ASSERT(nylink_has_errors(ctx));
        nylink_context_destroy(ctx);
    }

    /* 3. Member header trailer corrupt */
    {
        Nylink_Context *ctx = nylink_context_create();
        uint8_t corrupt[68];
        memcpy(corrupt, "!<arch>\n", 8);
        memset(corrupt + 8, ' ', 60);
        memcpy(corrupt + 8 + 48, "0         ", 10);
        corrupt[8 + 58] = 'X'; /* Invalid trailer */
        corrupt[8 + 59] = 'Y';
        TEST_ASSERT(!nylink_add_archive(ctx, "corrupt.a", corrupt, sizeof(corrupt)));
        TEST_ASSERT(nylink_has_errors(ctx));
        nylink_context_destroy(ctx);
    }

    /* 4. Member size out of bounds */
    {
        Nylink_Context *ctx = nylink_context_create();
        uint8_t out_of_bounds[68];
        memcpy(out_of_bounds, "!<arch>\n", 8);
        memset(out_of_bounds + 8, ' ', 60);
        memcpy(out_of_bounds + 8 + 48, "999999    ", 10);
        out_of_bounds[8 + 58] = '`';
        out_of_bounds[8 + 59] = '\n';
        TEST_ASSERT(!nylink_add_archive(ctx, "oob.a", out_of_bounds, sizeof(out_of_bounds)));
        TEST_ASSERT(nylink_has_errors(ctx));
        nylink_context_destroy(ctx);
    }
}

void test_nylink_archive_e2e_execution(void) {
#if defined(_WIN32) || defined(_WIN64)
    /* sub function in archive:
       returns ecx + edx
       bytes: 8d 04 11 c3 (lea eax, [rcx + rdx]; ret)
    */
    X86_Encoded_Module emod_sub;
    x86_encoded_mod_init(&emod_sub, ny_str("emod_sub"));
    const uint8_t code_sub[] = { 0x8d, 0x04, 0x11, 0xc3 };
    x86_buf_append_bytes(&emod_sub.text_section, code_sub, sizeof(code_sub));
    ny_buf_grow((void **)&emod_sub.functions, &emod_sub.function_capacity, 1, sizeof(X86_Function_Code));
    emod_sub.functions[emod_sub.function_count++] = (X86_Function_Code){
        .name = ny_str("fn_arch_add"),
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

    /* unused function in archive */
    Ny_Object_Buffer obj_unused;
    ny_obj_buf_init(&obj_unused);
    emit_dummy_coff(&obj_unused, "fn_unused_in_lib", false, nullptr);

    /* Package into archive mymath.lib */
    Ny_Object_Buffer ar_buf;
    const char *names[] = { "add.obj", "unused.obj" };
    Ny_Object_Buffer objs[] = { obj_sub, obj_unused };
    build_test_archive(&ar_buf, names, objs, 2);

    /* main function:
       sub rsp, 40
       mov ecx, 20
       mov edx, 22
       call fn_arch_add
       add rsp, 40
       ret
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
        .symbol_name = ny_str("fn_arch_add"),
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
    TEST_ASSERT(nylink_add_object(ctx, "main.obj", obj_main.bytes, obj_main.count));
    TEST_ASSERT(nylink_add_archive(ctx, "mymath.lib", ar_buf.bytes, ar_buf.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    TEST_ASSERT_EQ(nylink_get_object_count(ctx), 2);
    TEST_ASSERT_STR_EQ(nylink_get_object_name(ctx, 0), "main.obj");
    TEST_ASSERT_STR_EQ(nylink_get_object_name(ctx, 1), "mymath.lib(add.obj)");

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_PE,
        .base_address = 0x140000000ULL,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *out_exe = "bin/test_nylink_ar_run.exe";
    TEST_ASSERT(nylink_write_executable(ctx, out_exe, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    int ret = system("bin\\test_nylink_ar_run.exe");
    TEST_ASSERT_EQ(ret, 42);

    remove(out_exe);
    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj_main);
    ny_obj_buf_destroy(&ar_buf);
    ny_obj_buf_destroy(&obj_unused);
    ny_obj_buf_destroy(&obj_sub);
#endif
}

void test_nylink_elf64_shared_emission(void) {
    Ny_Object_Buffer obj1, obj2;
    ny_obj_buf_init(&obj1);
    ny_obj_buf_init(&obj2);

    emit_dummy_elf(&obj1, "so_func_exported", true, "so_external_ref");
    emit_dummy_elf(&obj2, "so_internal_helper", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    nylink_context_set_shared(ctx, true);
    TEST_ASSERT(nylink_add_object(ctx, "so1.o", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx, "so2.o", obj2.bytes, obj2.count));

    /* In shared mode, undefined external references should not fail resolution */
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    const char *needed[] = { "libc.so.6", "libm.so.6" };
    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .output_mode = NYLINK_OUTPUT_SHARED,
        .base_address = 0x0ULL,
        .soname = "libtest.so.1",
        .needed_libs = needed,
        .needed_lib_count = 2,
    };

    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *out_so = "bin/test_output.so";
    TEST_ASSERT(nylink_write_executable(ctx, out_so, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Validate emitted shared library file */
    FILE *f = fopen(out_so, "rb");
    TEST_ASSERT(f != nullptr);

    Elf64_Test_Ehdr ehdr;
    TEST_ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    TEST_ASSERT_EQ(ehdr.e_ident[0], 0x7F);
    TEST_ASSERT_EQ(ehdr.e_ident[1], 'E');
    TEST_ASSERT_EQ(ehdr.e_ident[2], 'L');
    TEST_ASSERT_EQ(ehdr.e_ident[3], 'F');
    TEST_ASSERT_EQ(ehdr.e_type, 3); /* ET_DYN */
    TEST_ASSERT_EQ(ehdr.e_machine, 62); /* EM_X86_64 */

    Elf64_Test_Phdr phdrs[16];
    fseek(f, (long)ehdr.e_phoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(phdrs, sizeof(Elf64_Test_Phdr), ehdr.e_phnum, f), ehdr.e_phnum);

    bool has_pt_dynamic = false;
    bool has_pt_relro = false;
    bool has_pt_stack = false;
    for (uint16_t p = 0; p < ehdr.e_phnum; p++) {
        if (phdrs[p].p_type == 2) has_pt_dynamic = true;
        if (phdrs[p].p_type == 0x6474e552) has_pt_relro = true;
        if (phdrs[p].p_type == 0x6474e551) has_pt_stack = true;
    }
    TEST_ASSERT(has_pt_dynamic);
    TEST_ASSERT(has_pt_relro);
    TEST_ASSERT(has_pt_stack);

    /* Read section headers */
    Elf64_Test_Shdr *shdrs = (Elf64_Test_Shdr *)ny_alloc_zero(ehdr.e_shnum * sizeof(Elf64_Test_Shdr));
    fseek(f, (long)ehdr.e_shoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(shdrs, sizeof(Elf64_Test_Shdr), ehdr.e_shnum, f), ehdr.e_shnum);

    /* Read shstrtab to check section names */
    TEST_ASSERT(ehdr.e_shstrndx < ehdr.e_shnum);
    char *shstrtab = (char *)ny_alloc_zero(shdrs[ehdr.e_shstrndx].sh_size);
    fseek(f, (long)shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(shstrtab, 1, shdrs[ehdr.e_shstrndx].sh_size, f), shdrs[ehdr.e_shstrndx].sh_size);

    bool has_dynsym_sec = false;
    bool has_dynstr_sec = false;
    bool has_dynamic_sec = false;
    bool has_got_sec = false;
    uint16_t dynamic_sec_idx = 0;

    for (uint16_t s = 1; s < ehdr.e_shnum; s++) {
        const char *sname = shstrtab + shdrs[s].sh_name;
        if (strcmp(sname, ".dynsym") == 0) has_dynsym_sec = true;
        if (strcmp(sname, ".dynstr") == 0) has_dynstr_sec = true;
        if (strcmp(sname, ".got") == 0) has_got_sec = true;
        if (strcmp(sname, ".dynamic") == 0) {
            has_dynamic_sec = true;
            dynamic_sec_idx = s;
        }
    }
    TEST_ASSERT(has_dynsym_sec);
    TEST_ASSERT(has_dynstr_sec);
    TEST_ASSERT(has_got_sec);
    TEST_ASSERT(has_dynamic_sec);

    /* Verify tags in .dynamic */
    size_t dyn_entries_count = shdrs[dynamic_sec_idx].sh_size / sizeof(Elf64_Test_Dyn);
    Elf64_Test_Dyn *dyn_entries = (Elf64_Test_Dyn *)ny_alloc_zero(shdrs[dynamic_sec_idx].sh_size);
    fseek(f, (long)shdrs[dynamic_sec_idx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(dyn_entries, sizeof(Elf64_Test_Dyn), dyn_entries_count, f), dyn_entries_count);

    bool has_dt_soname = false;
    size_t dt_needed_count = 0;
    bool has_dt_strtab = false;
    bool has_dt_symtab = false;
    bool has_dt_null = false;

    for (size_t d = 0; d < dyn_entries_count; d++) {
        if (dyn_entries[d].d_tag == 14) has_dt_soname = true; /* DT_SONAME */
        if (dyn_entries[d].d_tag == 1) dt_needed_count++;     /* DT_NEEDED */
        if (dyn_entries[d].d_tag == 5) has_dt_strtab = true;  /* DT_STRTAB */
        if (dyn_entries[d].d_tag == 6) has_dt_symtab = true;  /* DT_SYMTAB */
        if (dyn_entries[d].d_tag == 0) has_dt_null = true;    /* DT_NULL */
    }

    TEST_ASSERT(has_dt_soname);
    TEST_ASSERT_EQ(dt_needed_count, 2);
    TEST_ASSERT(has_dt_strtab);
    TEST_ASSERT(has_dt_symtab);
    TEST_ASSERT(has_dt_null);

    ny_free(dyn_entries, shdrs[dynamic_sec_idx].sh_size);
    ny_free(shstrtab, shdrs[ehdr.e_shstrndx].sh_size);
    ny_free(shdrs, ehdr.e_shnum * sizeof(Elf64_Test_Shdr));

    fclose(f);
    remove(out_so);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_pie_executable_emission(void) {
    Ny_Object_Buffer obj1, obj2;
    ny_obj_buf_init(&obj1);
    ny_obj_buf_init(&obj2);

    emit_dummy_elf(&obj1, "main", true, "so_target");
    emit_dummy_elf(&obj2, "so_target", false, nullptr);

    /* Write obj2 as a shared object input first */
    Nylink_Context *so_ctx = nylink_context_create();
    nylink_context_set_shared(so_ctx, true);
    TEST_ASSERT(nylink_add_object(so_ctx, "dummy_so.o", obj2.bytes, obj2.count));
    TEST_ASSERT(nylink_resolve_symbols(so_ctx));
    Nylink_Config so_cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .output_mode = NYLINK_OUTPUT_SHARED,
        .soname = "libdummy.so",
    };
    TEST_ASSERT(nylink_layout(so_ctx, &so_cfg));
    TEST_ASSERT(nylink_apply_relocations(so_ctx));
    const char *so_path = "bin/test_pie_dep.so";
    TEST_ASSERT(nylink_write_executable(so_ctx, so_path, &so_cfg));
    nylink_context_destroy(so_ctx);

    /* Read so_path back into buffer */
    FILE *f_so = fopen(so_path, "rb");
    TEST_ASSERT(f_so != nullptr);
    fseek(f_so, 0, SEEK_END);
    long so_sz = ftell(f_so);
    fseek(f_so, 0, SEEK_SET);
    uint8_t *so_bytes = (uint8_t *)ny_alloc((size_t)so_sz);
    TEST_ASSERT_EQ(fread(so_bytes, 1, (size_t)so_sz, f_so), (size_t)so_sz);
    fclose(f_so);

    /* Link PIE executable */
    Nylink_Context *ctx = nylink_context_create();
    nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_PIE);
    TEST_ASSERT(nylink_add_object(ctx, "main.o", obj1.bytes, obj1.count));
    TEST_ASSERT(nylink_add_object(ctx, "libdummy.so", so_bytes, (size_t)so_sz));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    const char *needed[] = { "libdummy.so" };
    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .output_mode = NYLINK_OUTPUT_PIE,
        .entry_point = "main",
        .rpath = "$ORIGIN",
        .needed_libs = needed,
        .needed_lib_count = 1,
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *pie_path = "bin/test_pie_out";
    TEST_ASSERT(nylink_write_executable(ctx, pie_path, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Parse emitted PIE */
    FILE *f = fopen(pie_path, "rb");
    TEST_ASSERT(f != nullptr);
    Elf64_Test_Ehdr ehdr;
    TEST_ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    TEST_ASSERT_EQ(ehdr.e_type, 3); /* ET_DYN */

    Elf64_Test_Phdr phdrs[16];
    fseek(f, (long)ehdr.e_phoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(phdrs, sizeof(Elf64_Test_Phdr), ehdr.e_phnum, f), ehdr.e_phnum);

    bool has_pt_interp = false;
    for (uint16_t p = 0; p < ehdr.e_phnum; p++) {
        if (phdrs[p].p_type == 3) has_pt_interp = true; /* PT_INTERP */
    }
    TEST_ASSERT(has_pt_interp);

    /* Validate section headers */
    Elf64_Test_Shdr *shdrs = (Elf64_Test_Shdr *)ny_alloc_zero(ehdr.e_shnum * sizeof(Elf64_Test_Shdr));
    fseek(f, (long)ehdr.e_shoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(shdrs, sizeof(Elf64_Test_Shdr), ehdr.e_shnum, f), ehdr.e_shnum);

    uint16_t dynamic_sec_idx = 0;
    char *shstrtab = (char *)ny_alloc_zero(shdrs[ehdr.e_shstrndx].sh_size);
    fseek(f, (long)shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(shstrtab, 1, shdrs[ehdr.e_shstrndx].sh_size, f), shdrs[ehdr.e_shstrndx].sh_size);

    for (uint16_t s = 1; s < ehdr.e_shnum; s++) {
        if (strcmp(shstrtab + shdrs[s].sh_name, ".dynamic") == 0) {
            dynamic_sec_idx = s;
            break;
        }
    }
    TEST_ASSERT(dynamic_sec_idx != 0);

    size_t dyn_entries_count = shdrs[dynamic_sec_idx].sh_size / sizeof(Elf64_Test_Dyn);
    Elf64_Test_Dyn *dyn_entries = (Elf64_Test_Dyn *)ny_alloc_zero(shdrs[dynamic_sec_idx].sh_size);
    fseek(f, (long)shdrs[dynamic_sec_idx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(dyn_entries, sizeof(Elf64_Test_Dyn), dyn_entries_count, f), dyn_entries_count);

    bool has_dt_runpath = false;
    bool has_dt_flags = false;
    bool has_dt_bind_now = false;
    for (size_t d = 0; d < dyn_entries_count; d++) {
        if (dyn_entries[d].d_tag == 29) has_dt_runpath = true; /* DT_RUNPATH */
        if (dyn_entries[d].d_tag == 30 && (dyn_entries[d].d_val & 0x8)) has_dt_flags = true; /* DT_FLAGS DF_BIND_NOW */
        if (dyn_entries[d].d_tag == 24 && dyn_entries[d].d_val == 1) has_dt_bind_now = true; /* DT_BIND_NOW */
    }
    TEST_ASSERT(has_dt_runpath);
    TEST_ASSERT(has_dt_flags);
    TEST_ASSERT(has_dt_bind_now);

    ny_free(dyn_entries, shdrs[dynamic_sec_idx].sh_size);
    ny_free(shstrtab, shdrs[ehdr.e_shstrndx].sh_size);
    ny_free(shdrs, ehdr.e_shnum * sizeof(Elf64_Test_Shdr));
    fclose(f);
    remove(pie_path);
    remove(so_path);
    ny_free(so_bytes, (size_t)so_sz);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj2);
    ny_obj_buf_destroy(&obj1);
}

void test_nylink_copy_reloc_rejection(void) {
    /* Build a dummy .so that exports an STT_OBJECT data symbol */
    Ny_Object_Buffer so_obj;
    ny_obj_buf_init(&so_obj);
    emit_dummy_elf_data(&so_obj, "exported_data");

    Nylink_Context *so_ctx = nylink_context_create();
    nylink_context_set_shared(so_ctx, true);
    TEST_ASSERT(nylink_add_object(so_ctx, "so.o", so_obj.bytes, so_obj.count));
    TEST_ASSERT(nylink_resolve_symbols(so_ctx));
    Nylink_Config so_cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .output_mode = NYLINK_OUTPUT_SHARED,
        .soname = "libdataref.so",
    };
    TEST_ASSERT(nylink_layout(so_ctx, &so_cfg));
    TEST_ASSERT(nylink_apply_relocations(so_ctx));
    const char *so_path = "bin/test_copy_so.so";
    TEST_ASSERT(nylink_write_executable(so_ctx, so_path, &so_cfg));
    nylink_context_destroy(so_ctx);

    FILE *f = fopen(so_path, "rb");
    TEST_ASSERT(f != nullptr);
    fseek(f, 0, SEEK_END);
    long so_sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *so_bytes = (uint8_t *)ny_alloc((size_t)so_sz);
    TEST_ASSERT_EQ(fread(so_bytes, 1, (size_t)so_sz, f), (size_t)so_sz);
    fclose(f);

    /* Build main object with PC32 relocation targeting an imported symbol marked as object */
    Ny_Object_Buffer main_obj;
    ny_obj_buf_init(&main_obj);
    emit_dummy_elf_data_ref(&main_obj, "main", "exported_data");

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "main.o", main_obj.bytes, main_obj.count));
    TEST_ASSERT(nylink_add_object(ctx, "libdataref.so", so_bytes, (size_t)so_sz));
    TEST_ASSERT(nylink_resolve_symbols(ctx));
    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .output_mode = NYLINK_OUTPUT_PIE,
        .entry_point = "main",
    };
    /* Layout should fail and diagnose copy reloc rejection */
    TEST_ASSERT(!nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_has_errors(ctx));

    bool found_diag = false;
    size_t dcount = nylink_get_diagnostic_count(ctx);
    for (size_t d = 0; d < dcount; d++) {
        const Nylink_Diagnostic *diag = nylink_get_diagnostic(ctx, d);
        if (diag->message && strstr(diag->message, "copy relocations are not supported")) {
            found_diag = true;
            break;
        }
    }
    TEST_ASSERT(found_diag);

    nylink_context_destroy(ctx);
    ny_free(so_bytes, (size_t)so_sz);
    remove(so_path);
    ny_obj_buf_destroy(&main_obj);
    ny_obj_buf_destroy(&so_obj);
}

void test_nylink_gotpcrel_relocation(void) {
    /* Build shared library exporting data symbol */
    Ny_Object_Buffer so_obj;
    ny_obj_buf_init(&so_obj);
    emit_dummy_elf_data(&so_obj, "ext_data_val");

    Nylink_Context *so_ctx = nylink_context_create();
    nylink_context_set_shared(so_ctx, true);
    TEST_ASSERT(nylink_add_object(so_ctx, "so.o", so_obj.bytes, so_obj.count));
    TEST_ASSERT(nylink_resolve_symbols(so_ctx));
    Nylink_Config so_cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .output_mode = NYLINK_OUTPUT_SHARED,
        .soname = "libgotdata.so",
    };
    TEST_ASSERT(nylink_layout(so_ctx, &so_cfg));
    TEST_ASSERT(nylink_apply_relocations(so_ctx));
    const char *so_path = "bin/test_gotdata.so";
    TEST_ASSERT(nylink_write_executable(so_ctx, so_path, &so_cfg));
    nylink_context_destroy(so_ctx);

    FILE *f_so = fopen(so_path, "rb");
    TEST_ASSERT(f_so != nullptr);
    fseek(f_so, 0, SEEK_END);
    long so_sz = ftell(f_so);
    fseek(f_so, 0, SEEK_SET);
    uint8_t *so_bytes = (uint8_t *)ny_alloc((size_t)so_sz);
    TEST_ASSERT_EQ(fread(so_bytes, 1, (size_t)so_sz, f_so), (size_t)so_sz);
    fclose(f_so);

    /* Build main object using GOTPCREL to reference ext_data_val */
    Ny_Object_Buffer main_obj;
    ny_obj_buf_init(&main_obj);
    emit_dummy_elf_gotpcrel(&main_obj, "main", "ext_data_val");

    Nylink_Context *ctx = nylink_context_create();
    nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_PIE);
    TEST_ASSERT(nylink_add_object(ctx, "main.o", main_obj.bytes, main_obj.count));
    TEST_ASSERT(nylink_add_object(ctx, "libgotdata.so", so_bytes, (size_t)so_sz));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_ELF64,
        .output_mode = NYLINK_OUTPUT_PIE,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *pie_path = "bin/test_gotpcrel_app";
    TEST_ASSERT(nylink_write_executable(ctx, pie_path, &cfg));
    TEST_ASSERT(!nylink_has_errors(ctx));

    /* Validate output ELF */
    FILE *f = fopen(pie_path, "rb");
    TEST_ASSERT(f != nullptr);

    Elf64_Test_Ehdr ehdr;
    TEST_ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    TEST_ASSERT_EQ(ehdr.e_type, 3); /* ET_DYN */

    Elf64_Test_Shdr *shdrs = (Elf64_Test_Shdr *)ny_alloc_zero(ehdr.e_shnum * sizeof(Elf64_Test_Shdr));
    fseek(f, (long)ehdr.e_shoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(shdrs, sizeof(Elf64_Test_Shdr), ehdr.e_shnum, f), ehdr.e_shnum);

    char *shstrtab = (char *)ny_alloc_zero(shdrs[ehdr.e_shstrndx].sh_size);
    fseek(f, (long)shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(shstrtab, 1, shdrs[ehdr.e_shstrndx].sh_size, f), shdrs[ehdr.e_shstrndx].sh_size);

    uint16_t got_idx = 0;
    uint16_t rela_dyn_idx = 0;
    for (uint16_t s = 1; s < ehdr.e_shnum; s++) {
        if (strcmp(shstrtab + shdrs[s].sh_name, ".got") == 0) got_idx = s;
        if (strcmp(shstrtab + shdrs[s].sh_name, ".rela.dyn") == 0) rela_dyn_idx = s;
    }

    TEST_ASSERT(got_idx != 0);
    TEST_ASSERT(rela_dyn_idx != 0);
    TEST_ASSERT(shdrs[got_idx].sh_size >= 8);

    /* Read .rela.dyn entries and find R_X86_64_GLOB_DAT targeting .got */
    size_t num_relas = shdrs[rela_dyn_idx].sh_size / sizeof(Elf64_Test_Rela);
    TEST_ASSERT(num_relas >= 1);
    Elf64_Test_Rela *relas = (Elf64_Test_Rela *)ny_alloc_zero(shdrs[rela_dyn_idx].sh_size);
    fseek(f, (long)shdrs[rela_dyn_idx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(relas, sizeof(Elf64_Test_Rela), num_relas, f), num_relas);

    bool found_glob_dat = false;
    for (size_t r = 0; r < num_relas; r++) {
        uint32_t r_type = (uint32_t)(relas[r].r_info & 0xFFFFFFFF);
        if (r_type == 6) { /* R_X86_64_GLOB_DAT */
            if (relas[r].r_offset >= shdrs[got_idx].sh_addr &&
                relas[r].r_offset < shdrs[got_idx].sh_addr + shdrs[got_idx].sh_size) {
                found_glob_dat = true;
                break;
            }
        }
    }
    TEST_ASSERT(found_glob_dat);

    ny_free(relas, shdrs[rela_dyn_idx].sh_size);
    ny_free(shstrtab, shdrs[ehdr.e_shstrndx].sh_size);
    ny_free(shdrs, ehdr.e_shnum * sizeof(Elf64_Test_Shdr));
    fclose(f);

    remove(pie_path);
    remove(so_path);
    ny_free(so_bytes, (size_t)so_sz);
    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&main_obj);
    ny_obj_buf_destroy(&so_obj);
}

void test_nylink_pe_shared_emission(void) {
    Ny_Object_Buffer obj;
    ny_obj_buf_init(&obj);
    emit_dummy_coff(&obj, "foo", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_DLL);
    nylink_add_export(ctx, "foo");

    TEST_ASSERT(nylink_add_object(ctx, "foo.obj", obj.bytes, obj.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    const char *exports[] = { "foo" };
    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_PE,
        .output_mode = NYLINK_OUTPUT_DLL,
        .base_address = 0x180000000ULL,
        .exports = exports,
        .export_count = 1,
    };

    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *dll_path = "bin/test_dll.dll";
    const char *lib_path = "bin/test_dll.lib";
    TEST_ASSERT(nylink_write_executable(ctx, dll_path, &cfg));
    TEST_ASSERT(nylink_write_pe_implib(ctx, lib_path, "test_dll.dll"));

    FILE *f_dll = fopen(dll_path, "rb");
    TEST_ASSERT(f_dll != nullptr);

    Pe_Test_Dos_Header dos_hdr;
    TEST_ASSERT_EQ(fread(&dos_hdr, sizeof(dos_hdr), 1, f_dll), 1);
    fseek(f_dll, (long)dos_hdr.e_lfanew, SEEK_SET);

    uint32_t pe_sig = 0;
    TEST_ASSERT_EQ(fread(&pe_sig, 4, 1, f_dll), 1);
    TEST_ASSERT_EQ(pe_sig, 0x00004550);

    Pe_Test_File_Header fhdr;
    TEST_ASSERT_EQ(fread(&fhdr, sizeof(fhdr), 1, f_dll), 1);
    TEST_ASSERT(fhdr.Characteristics & 0x2000); /* IMAGE_FILE_DLL */

    Pe_Test_Optional_Header64 opt;
    TEST_ASSERT_EQ(fread(&opt, sizeof(opt), 1, f_dll), 1);
    TEST_ASSERT(opt.DataDirectory[0].VirtualAddress != 0); /* Export Directory RVA */
    TEST_ASSERT(opt.DataDirectory[0].Size > 0);

    fclose(f_dll);
    remove(dll_path);

    /* Verify .lib is an archive and has 1st linker member */
    FILE *f_lib = fopen(lib_path, "rb");
    TEST_ASSERT(f_lib != nullptr);
    char magic[8];
    TEST_ASSERT_EQ(fread(magic, 1, 8, f_lib), 8);
    TEST_ASSERT(memcmp(magic, "!<arch>\n", 8) == 0);
    fclose(f_lib);
    remove(lib_path);

    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj);
}

void test_nylink_pe_import_resolution(void) {
    /* 1. Build a dummy DLL with export 'ext_func' */
    Ny_Object_Buffer obj_dll;
    ny_obj_buf_init(&obj_dll);
    emit_dummy_coff(&obj_dll, "ext_func", false, nullptr);

    Nylink_Context *dll_ctx = nylink_context_create();
    nylink_context_set_output_mode(dll_ctx, NYLINK_OUTPUT_DLL);
    nylink_add_export(dll_ctx, "ext_func");
    TEST_ASSERT(nylink_add_object(dll_ctx, "ext.obj", obj_dll.bytes, obj_dll.count));
    TEST_ASSERT(nylink_resolve_symbols(dll_ctx));

    const char *exports[] = { "ext_func" };
    Nylink_Config dll_cfg = {
        .target_format = NYLINK_TARGET_PE,
        .output_mode = NYLINK_OUTPUT_DLL,
        .exports = exports,
        .export_count = 1,
    };
    TEST_ASSERT(nylink_layout(dll_ctx, &dll_cfg));
    TEST_ASSERT(nylink_apply_relocations(dll_ctx));

    const char *dll_path = "bin/test_ext.dll";
    const char *lib_path = "bin/test_ext.lib";
    TEST_ASSERT(nylink_write_executable(dll_ctx, dll_path, &dll_cfg));
    TEST_ASSERT(nylink_write_pe_implib(dll_ctx, lib_path, "test_ext.dll"));
    nylink_context_destroy(dll_ctx);
    ny_obj_buf_destroy(&obj_dll);

    /* 2. Load .lib into byte buffer */
    FILE *fl = fopen(lib_path, "rb");
    TEST_ASSERT(fl != nullptr);
    fseek(fl, 0, SEEK_END);
    long lsz = ftell(fl);
    fseek(fl, 0, SEEK_SET);
    uint8_t *lib_bytes = (uint8_t *)ny_alloc((size_t)lsz);
    TEST_ASSERT_EQ(fread(lib_bytes, 1, (size_t)lsz, fl), (size_t)lsz);
    fclose(fl);

    /* 3. Build an exe referencing 'ext_func' via call */
    Ny_Object_Buffer obj_exe;
    ny_obj_buf_init(&obj_exe);
    emit_dummy_coff(&obj_exe, "main", true, "ext_func");

    Nylink_Context *exe_ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(exe_ctx, "main.obj", obj_exe.bytes, obj_exe.count));
    TEST_ASSERT(nylink_add_archive(exe_ctx, "test_ext.lib", lib_bytes, (size_t)lsz));

    TEST_ASSERT(nylink_resolve_symbols(exe_ctx));
    TEST_ASSERT(!nylink_has_errors(exe_ctx));

    Nylink_Config exe_cfg = {
        .target_format = NYLINK_TARGET_PE,
        .output_mode = NYLINK_OUTPUT_EXECUTABLE,
        .base_address = 0x140000000ULL,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(exe_ctx, &exe_cfg));
    TEST_ASSERT(nylink_apply_relocations(exe_ctx));

    const char *exe_path = "bin/test_pe_import_app.exe";
    TEST_ASSERT(nylink_write_executable(exe_ctx, exe_path, &exe_cfg));
    TEST_ASSERT(!nylink_has_errors(exe_ctx));

    /* 4. Validate exe headers: import directory and IAT directory */
    FILE *fe = fopen(exe_path, "rb");
    TEST_ASSERT(fe != nullptr);

    Pe_Test_Dos_Header dos_hdr;
    TEST_ASSERT_EQ(fread(&dos_hdr, sizeof(dos_hdr), 1, fe), 1);
    fseek(fe, (long)dos_hdr.e_lfanew + 4 + sizeof(Pe_Test_File_Header), SEEK_SET);

    Pe_Test_Optional_Header64 opt;
    TEST_ASSERT_EQ(fread(&opt, sizeof(opt), 1, fe), 1);
    TEST_ASSERT(opt.DataDirectory[1].VirtualAddress != 0); /* Import Directory */
    TEST_ASSERT(opt.DataDirectory[1].Size > 0);
    TEST_ASSERT(opt.DataDirectory[12].VirtualAddress != 0); /* IAT Directory */
    TEST_ASSERT(opt.DataDirectory[12].Size > 0);

    fclose(fe);
    remove(exe_path);
    remove(dll_path);
    remove(lib_path);
    ny_free(lib_bytes, (size_t)lsz);

    nylink_context_destroy(exe_ctx);
    ny_obj_buf_destroy(&obj_exe);
}

void test_nylink_pe_dll_structural_validation(void) {
    Ny_Object_Buffer obj;
    ny_obj_buf_init(&obj);
    emit_dummy_coff(&obj, "exp_fn_a", false, nullptr);

    Nylink_Context *ctx = nylink_context_create();
    nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_DLL);
    nylink_add_export(ctx, "exp_fn_a");
    TEST_ASSERT(nylink_add_object(ctx, "e.obj", obj.bytes, obj.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    const char *exports[] = { "exp_fn_a" };
    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_PE,
        .output_mode = NYLINK_OUTPUT_DLL,
        .base_address = 0x10000000ULL,
        .soname = "struct.dll",
        .exports = exports,
        .export_count = 1,
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *dll_path = "bin/test_pe_struct.dll";
    const char *lib_path = "bin/test_pe_struct.lib";
    TEST_ASSERT(nylink_write_executable(ctx, dll_path, &cfg));
    TEST_ASSERT(nylink_write_pe_implib(ctx, lib_path, "test_pe_struct.dll"));
    TEST_ASSERT(!nylink_has_errors(ctx));

    FILE *f = fopen(dll_path, "rb");
    TEST_ASSERT(f != nullptr);

    Pe_Test_Dos_Header dos;
    TEST_ASSERT_EQ(fread(&dos, sizeof(dos), 1, f), 1);
    TEST_ASSERT_EQ(dos.e_magic, 0x5A4D);
    TEST_ASSERT_EQ(dos.e_lfanew, 0x80);

    fseek(f, 0x80, SEEK_SET);
    uint32_t sig = 0;
    TEST_ASSERT_EQ(fread(&sig, 4, 1, f), 1);
    TEST_ASSERT_EQ(sig, 0x00004550);

    Pe_Test_File_Header fh;
    TEST_ASSERT_EQ(fread(&fh, sizeof(fh), 1, f), 1);
    TEST_ASSERT_EQ(fh.Machine, 0x8664);
    TEST_ASSERT(fh.Characteristics & 0x2000);

    Pe_Test_Optional_Header64 oh;
    TEST_ASSERT_EQ(fread(&oh, sizeof(oh), 1, f), 1);
    TEST_ASSERT_EQ(oh.Magic, 0x20B);
    TEST_ASSERT_EQ(oh.ImageBase, 0x10000000ULL);
    TEST_ASSERT_EQ(oh.SectionAlignment, 0x1000);
    TEST_ASSERT_EQ(oh.FileAlignment, 0x200);
    TEST_ASSERT(oh.SizeOfImage >= 0x2000);
    TEST_ASSERT_EQ(oh.SizeOfHeaders, 0x400);
    TEST_ASSERT(oh.DataDirectory[0].VirtualAddress != 0);
    TEST_ASSERT(oh.DataDirectory[0].Size > 0);

    uint16_t nsec = fh.NumberOfSections;
    Pe_Test_Section_Header *shdrs = (Pe_Test_Section_Header *)ny_alloc_zero(nsec * sizeof(Pe_Test_Section_Header));
    TEST_ASSERT_EQ(fread(shdrs, sizeof(Pe_Test_Section_Header), nsec, f), nsec);

    bool has_text = false;
    bool has_rdata = false;
    for (uint16_t i = 0; i < nsec; i++) {
        if (strncmp((const char *)shdrs[i].Name, ".text", 5) == 0) {
            has_text = true;
            TEST_ASSERT(shdrs[i].Characteristics & 0x20000000);
            TEST_ASSERT(shdrs[i].Characteristics & 0x40000000);
        }
        if (strncmp((const char *)shdrs[i].Name, ".rdata", 6) == 0) {
            has_rdata = true;
            TEST_ASSERT(shdrs[i].Characteristics & 0x40000000);
        }
    }
    TEST_ASSERT(has_text);
    TEST_ASSERT(has_rdata);

    uint32_t exp_rva = oh.DataDirectory[0].VirtualAddress;
    uint32_t exp_size = oh.DataDirectory[0].Size;
    TEST_ASSERT(exp_size >= 40);

    long exp_file_off = 0;
    for (uint16_t i = 0; i < nsec; i++) {
        if (exp_rva >= shdrs[i].VirtualAddress && exp_rva < shdrs[i].VirtualAddress + shdrs[i].VirtualSize) {
            exp_file_off = (long)(shdrs[i].PointerToRawData + (exp_rva - shdrs[i].VirtualAddress));
            break;
        }
    }
    TEST_ASSERT(exp_file_off > 0);

    fseek(f, exp_file_off, SEEK_SET);
    uint8_t *exp_dir = (uint8_t *)ny_alloc_zero(exp_size);
    TEST_ASSERT_EQ(fread(exp_dir, 1, exp_size, f), exp_size);

    uint32_t num_funcs = *(uint32_t *)(exp_dir + 20);
    uint32_t num_names = *(uint32_t *)(exp_dir + 24);
    uint32_t eat_rva = *(uint32_t *)(exp_dir + 28);
    uint32_t npt_rva = *(uint32_t *)(exp_dir + 32);
    uint32_t ot_rva = *(uint32_t *)(exp_dir + 36);
    uint32_t name_rva = *(uint32_t *)(exp_dir + 12);
    uint32_t ord_base = *(uint32_t *)(exp_dir + 16);

    TEST_ASSERT_EQ(num_funcs, 1);
    TEST_ASSERT_EQ(num_names, 1);
    TEST_ASSERT_EQ(ord_base, 1);

    uint32_t exp_name_off = name_rva - exp_rva;
    TEST_ASSERT(exp_name_off < exp_size);
    TEST_ASSERT_STR_EQ((const char *)(exp_dir + exp_name_off), "struct.dll");

    uint32_t npt_off = npt_rva - exp_rva;
    TEST_ASSERT(npt_off + 4 <= exp_size);
    uint32_t sym_name_rva = *(uint32_t *)(exp_dir + npt_off);
    uint32_t sym_name_off = sym_name_rva - exp_rva;
    TEST_ASSERT(sym_name_off < exp_size);
    TEST_ASSERT_STR_EQ((const char *)(exp_dir + sym_name_off), "exp_fn_a");

    uint32_t ot_off = ot_rva - exp_rva;
    TEST_ASSERT(ot_off + 2 <= exp_size);
    uint16_t ordinal = *(uint16_t *)(exp_dir + ot_off);
    TEST_ASSERT_EQ(ordinal, 0);

    uint32_t eat_off = eat_rva - exp_rva;
    TEST_ASSERT(eat_off + 4 <= exp_size);
    uint32_t func_rva = *(uint32_t *)(exp_dir + eat_off);
    TEST_ASSERT(func_rva >= 0x1000);
    TEST_ASSERT(func_rva < oh.SizeOfImage);

    ny_free(exp_dir, exp_size);

    fclose(f);
    remove(dll_path);
    remove(lib_path);
    ny_free(shdrs, nsec * sizeof(Pe_Test_Section_Header));
    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj);
}

void test_nylink_pe_import_structural_validation(void) {
    Ny_Object_Buffer obj_dll;
    ny_obj_buf_init(&obj_dll);
    emit_dummy_coff(&obj_dll, "imp_target", false, nullptr);

    Nylink_Context *dll_ctx = nylink_context_create();
    nylink_context_set_output_mode(dll_ctx, NYLINK_OUTPUT_DLL);
    nylink_add_export(dll_ctx, "imp_target");
    TEST_ASSERT(nylink_add_object(dll_ctx, "d.obj", obj_dll.bytes, obj_dll.count));
    TEST_ASSERT(nylink_resolve_symbols(dll_ctx));

    const char *exports[] = { "imp_target" };
    Nylink_Config dll_cfg = {
        .target_format = NYLINK_TARGET_PE,
        .output_mode = NYLINK_OUTPUT_DLL,
        .soname = "imp.dll",
        .exports = exports,
        .export_count = 1,
    };
    TEST_ASSERT(nylink_layout(dll_ctx, &dll_cfg));
    TEST_ASSERT(nylink_apply_relocations(dll_ctx));

    const char *dll_path = "bin/test_imp_str.dll";
    const char *lib_path = "bin/test_imp_str.lib";
    TEST_ASSERT(nylink_write_executable(dll_ctx, dll_path, &dll_cfg));
    TEST_ASSERT(nylink_write_pe_implib(dll_ctx, lib_path, "test_imp_str.dll"));
    nylink_context_destroy(dll_ctx);
    ny_obj_buf_destroy(&obj_dll);

    FILE *fl = fopen(lib_path, "rb");
    TEST_ASSERT(fl != nullptr);
    fseek(fl, 0, SEEK_END);
    long lsz = ftell(fl);
    fseek(fl, 0, SEEK_SET);
    uint8_t *lib_bytes = (uint8_t *)ny_alloc((size_t)lsz);
    TEST_ASSERT_EQ(fread(lib_bytes, 1, (size_t)lsz, fl), (size_t)lsz);
    fclose(fl);

    Ny_Object_Buffer obj_exe;
    ny_obj_buf_init(&obj_exe);
    emit_dummy_coff(&obj_exe, "main", true, "imp_target");

    Nylink_Context *exe_ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(exe_ctx, "m.obj", obj_exe.bytes, obj_exe.count));
    TEST_ASSERT(nylink_add_archive(exe_ctx, "i.lib", lib_bytes, (size_t)lsz));
    TEST_ASSERT(nylink_resolve_symbols(exe_ctx));

    Nylink_Config exe_cfg = {
        .target_format = NYLINK_TARGET_PE,
        .output_mode = NYLINK_OUTPUT_EXECUTABLE,
        .base_address = 0x140000000ULL,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(exe_ctx, &exe_cfg));
    TEST_ASSERT(nylink_apply_relocations(exe_ctx));

    const char *exe_path = "bin/test_imp_str.exe";
    TEST_ASSERT(nylink_write_executable(exe_ctx, exe_path, &exe_cfg));

    FILE *f = fopen(exe_path, "rb");
    TEST_ASSERT(f != nullptr);

    Pe_Test_Dos_Header dos;
    TEST_ASSERT_EQ(fread(&dos, sizeof(dos), 1, f), 1);
    fseek(f, (long)dos.e_lfanew + 4, SEEK_SET);

    Pe_Test_File_Header fh;
    TEST_ASSERT_EQ(fread(&fh, sizeof(fh), 1, f), 1);
    TEST_ASSERT(!(fh.Characteristics & 0x2000));

    Pe_Test_Optional_Header64 oh;
    TEST_ASSERT_EQ(fread(&oh, sizeof(oh), 1, f), 1);
    TEST_ASSERT(oh.DataDirectory[1].VirtualAddress != 0);
    TEST_ASSERT(oh.DataDirectory[1].Size > 0);
    TEST_ASSERT(oh.DataDirectory[12].VirtualAddress != 0);
    TEST_ASSERT(oh.DataDirectory[12].Size > 0);
    TEST_ASSERT(oh.AddressOfEntryPoint >= 0x1000);

    uint16_t nsec = fh.NumberOfSections;
    Pe_Test_Section_Header *shdrs = (Pe_Test_Section_Header *)ny_alloc_zero(nsec * sizeof(Pe_Test_Section_Header));
    TEST_ASSERT_EQ(fread(shdrs, sizeof(Pe_Test_Section_Header), nsec, f), nsec);

    uint32_t imp_rva = oh.DataDirectory[1].VirtualAddress;
    uint32_t iat_rva = oh.DataDirectory[12].VirtualAddress;
    uint32_t iat_size = oh.DataDirectory[12].Size;

    long imp_file_off = 0;
    long iat_file_off = 0;
    for (uint16_t i = 0; i < nsec; i++) {
        if (imp_rva >= shdrs[i].VirtualAddress && imp_rva < shdrs[i].VirtualAddress + shdrs[i].VirtualSize) {
            imp_file_off = (long)(shdrs[i].PointerToRawData + (imp_rva - shdrs[i].VirtualAddress));
        }
        if (iat_rva >= shdrs[i].VirtualAddress && iat_rva < shdrs[i].VirtualAddress + shdrs[i].VirtualSize) {
            iat_file_off = (long)(shdrs[i].PointerToRawData + (iat_rva - shdrs[i].VirtualAddress));
        }
    }
    TEST_ASSERT(imp_file_off > 0);
    TEST_ASSERT(iat_file_off > 0);

    fseek(f, imp_file_off, SEEK_SET);
    uint8_t idt[20];
    TEST_ASSERT_EQ(fread(idt, 1, 20, f), 20);
    uint32_t ilt_rva = *(uint32_t *)(idt + 0);
    uint32_t dll_name_rva = *(uint32_t *)(idt + 12);
    uint32_t first_thunk_rva = *(uint32_t *)(idt + 16);
    TEST_ASSERT(ilt_rva != 0);
    TEST_ASSERT(dll_name_rva != 0);
    TEST_ASSERT(first_thunk_rva != 0);
    TEST_ASSERT_EQ(first_thunk_rva, iat_rva);

    long dll_name_off = 0;
    for (uint16_t i = 0; i < nsec; i++) {
        if (dll_name_rva >= shdrs[i].VirtualAddress && dll_name_rva < shdrs[i].VirtualAddress + shdrs[i].VirtualSize) {
            dll_name_off = (long)(shdrs[i].PointerToRawData + (dll_name_rva - shdrs[i].VirtualAddress));
            break;
        }
    }
    TEST_ASSERT(dll_name_off > 0);
    fseek(f, dll_name_off, SEEK_SET);
    char dll_name_buf[256] = {0};
    size_t dn_len = 0;
    int c;
    while (dn_len < 255 && (c = fgetc(f)) != EOF && c != 0) {
        dll_name_buf[dn_len++] = (char)c;
    }
    dll_name_buf[dn_len] = '\0';
    TEST_ASSERT_STR_EQ(dll_name_buf, "test_imp_str.dll");

    fseek(f, iat_file_off, SEEK_SET);
    uint64_t iat_entry = 0;
    TEST_ASSERT_EQ(fread(&iat_entry, 8, 1, f), 1);
    TEST_ASSERT(iat_entry != 0);
    TEST_ASSERT(iat_size >= 16);

    ny_free(shdrs, nsec * sizeof(Pe_Test_Section_Header));
    fclose(f);
    remove(exe_path);
    remove(dll_path);
    remove(lib_path);
    ny_free(lib_bytes, (size_t)lsz);
    nylink_context_destroy(exe_ctx);
    ny_obj_buf_destroy(&obj_exe);
}

void test_nylink_pe_base_reloc_validation(void) {
    Ny_Object_Buffer obj_main, obj_data;
    ny_obj_buf_init(&obj_main);
    ny_obj_buf_init(&obj_data);

    X86_Encoded_Module emod_data;
    x86_encoded_mod_init(&emod_data, ny_str("reloc_data"));
    uint32_t val = 99;
    x86_buf_append_bytes(&emod_data.data_section, (const uint8_t *)&val, sizeof(val));
    ny_buf_grow((void **)&emod_data.globals, &emod_data.global_capacity, 1, sizeof(X86_Encoded_Global));
    emod_data.globals[emod_data.global_count++] = (X86_Encoded_Global){
        .name = ny_str("reloc_var"),
        .kind = NY_GLOBAL_DATA,
        .offset = 0,
        .size = sizeof(val),
        .align = 4,
    };
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    TEST_ASSERT(ny_emit_coff_x86_64(&obj_data, &emod_data, &diags));
    ny_diagnostic_list_destroy(&diags);
    x86_encoded_mod_destroy(&emod_data);

    X86_Encoded_Module emod_main;
    x86_encoded_mod_init(&emod_main, ny_str("reloc_main"));
    /* mov rax, <abs64 addr of reloc_var>; mov eax, [rax]; ret
       Uses 10-byte mov rax, imm64 with ADDR64 reloc at offset 2 */
    const uint8_t code[] = {
        0x48, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0,
        0x8b, 0x00,
        0xc3
    };
    x86_buf_append_bytes(&emod_main.text_section, code, sizeof(code));
    ny_buf_grow((void **)&emod_main.functions, &emod_main.function_capacity, 1, sizeof(X86_Function_Code));
    emod_main.functions[emod_main.function_count++] = (X86_Function_Code){
        .name = ny_str("main"),
        .offset = 0,
        .size = sizeof(code),
    };
    X86_Relocation reloc = {
        .kind = X86_FIXUP_GLOBAL_REL32,
        .code_offset = 2,
        .symbol_name = ny_str("reloc_var"),
        .addend = 0,
    };
    x86_buf_append_reloc(&emod_main.text_section, reloc);
    ny_diagnostic_list_init(&diags);
    TEST_ASSERT(ny_emit_coff_x86_64(&obj_main, &emod_main, &diags));
    ny_diagnostic_list_destroy(&diags);
    x86_encoded_mod_destroy(&emod_main);

    /* Patch the COFF relocation type from REL32 (4) to ADDR64 (1) and
       fix the instruction to 10-byte mov rax, imm64 (already correct) */
    {
        uint8_t *raw = obj_main.bytes;
        uint16_t nsec = *(uint16_t *)(raw + 2);
        uint16_t optsize = *(uint16_t *)(raw + 16);
        size_t shdr_off = 20 + optsize;
        for (uint16_t s = 0; s < nsec; s++) {
            uint8_t *sh = raw + shdr_off + (size_t)s * 40;
            uint16_t nreloc = *(uint16_t *)(sh + 32);
            uint32_t reloc_off = *(uint32_t *)(sh + 24);
            if (nreloc == 0 || reloc_off == 0) continue;
            for (uint16_t r = 0; r < nreloc; r++) {
                uint8_t *rel = raw + reloc_off + (size_t)r * 10;
                uint16_t type = *(uint16_t *)(rel + 8);
                if (type == 4) {
                    *(uint16_t *)(rel + 8) = 1; /* IMAGE_REL_AMD64_ADDR64 */
                }
            }
        }
    }

    Nylink_Context *ctx = nylink_context_create();
    TEST_ASSERT(nylink_add_object(ctx, "main.obj", obj_main.bytes, obj_main.count));
    TEST_ASSERT(nylink_add_object(ctx, "data.obj", obj_data.bytes, obj_data.count));
    TEST_ASSERT(nylink_resolve_symbols(ctx));

    Nylink_Config cfg = {
        .target_format = NYLINK_TARGET_PE,
        .output_mode = NYLINK_OUTPUT_EXECUTABLE,
        .base_address = 0x140000000ULL,
        .entry_point = "main",
    };
    TEST_ASSERT(nylink_layout(ctx, &cfg));
    TEST_ASSERT(nylink_apply_relocations(ctx));

    const char *exe_path = "bin/test_pe_reloc.exe";
    TEST_ASSERT(nylink_write_executable(ctx, exe_path, &cfg));

    FILE *f = fopen(exe_path, "rb");
    TEST_ASSERT(f != nullptr);
    Pe_Test_Dos_Header dos;
    TEST_ASSERT_EQ(fread(&dos, sizeof(dos), 1, f), 1);
    fseek(f, (long)dos.e_lfanew + 4, SEEK_SET);
    Pe_Test_File_Header fh;
    TEST_ASSERT_EQ(fread(&fh, sizeof(fh), 1, f), 1);
    Pe_Test_Optional_Header64 oh;
    TEST_ASSERT_EQ(fread(&oh, sizeof(oh), 1, f), 1);

    bool found_dir64 = false;
    if (oh.DataDirectory[5].VirtualAddress != 0 && oh.DataDirectory[5].Size > 0) {
        uint16_t nsec = fh.NumberOfSections;
        Pe_Test_Section_Header *shdrs = (Pe_Test_Section_Header *)ny_alloc_zero(nsec * sizeof(Pe_Test_Section_Header));
        TEST_ASSERT_EQ(fread(shdrs, sizeof(Pe_Test_Section_Header), nsec, f), nsec);

        uint32_t reloc_rva = oh.DataDirectory[5].VirtualAddress;
        uint32_t reloc_size = oh.DataDirectory[5].Size;
        long reloc_off = 0;
        for (uint16_t i = 0; i < nsec; i++) {
            if (reloc_rva >= shdrs[i].VirtualAddress && reloc_rva < shdrs[i].VirtualAddress + shdrs[i].VirtualSize) {
                reloc_off = (long)(shdrs[i].PointerToRawData + (reloc_rva - shdrs[i].VirtualAddress));
                break;
            }
        }
        TEST_ASSERT(reloc_off > 0);

        fseek(f, reloc_off, SEEK_SET);
        uint8_t *reloc_data = (uint8_t *)ny_alloc_zero(reloc_size);
        TEST_ASSERT_EQ(fread(reloc_data, 1, reloc_size, f), reloc_size);

        size_t pos = 0;
        while (pos + 8 <= reloc_size) {
            uint32_t block_size = *(uint32_t *)(reloc_data + pos + 4);
            if (block_size == 0 || block_size < 8) break;
            size_t entries = (block_size - 8) / 2;
            for (size_t e = 0; e < entries; e++) {
                uint16_t entry = *(uint16_t *)(reloc_data + pos + 8 + e * 2);
                uint16_t type = (entry >> 12) & 0xF;
                if (type == 10) {
                    found_dir64 = true;
                }
            }
            pos += block_size;
        }
        ny_free(reloc_data, reloc_size);
        ny_free(shdrs, nsec * sizeof(Pe_Test_Section_Header));
    }
    TEST_ASSERT(found_dir64);

    fclose(f);
    remove(exe_path);
    nylink_context_destroy(ctx);
    ny_obj_buf_destroy(&obj_data);
    ny_obj_buf_destroy(&obj_main);
}

void test_nylink_pe_negative_tests(void) {
    {
        Ny_Object_Buffer obj;
        ny_obj_buf_init(&obj);
        emit_dummy_coff(&obj, "dup_fn", false, nullptr);

        Nylink_Context *ctx = nylink_context_create();
        nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_DLL);
        nylink_add_export(ctx, "dup_fn");
        nylink_add_export(ctx, "dup_fn");
        TEST_ASSERT(nylink_add_object(ctx, "d.obj", obj.bytes, obj.count));
        TEST_ASSERT(nylink_resolve_symbols(ctx));

        TEST_ASSERT(nylink_add_export(ctx, "dup_fn"));

        nylink_context_destroy(ctx);
        ny_obj_buf_destroy(&obj);
    }

    {
        Nylink_Context *ctx = nylink_context_create();
        uint8_t bad_lib[68];
        memcpy(bad_lib, "!<arch>\n", 8);
        memset(bad_lib + 8, ' ', 60);
        memcpy(bad_lib + 8 + 48, "0         ", 10);
        bad_lib[8 + 58] = 'X';
        bad_lib[8 + 59] = 'Y';
        TEST_ASSERT(!nylink_add_archive(ctx, "bad.lib", bad_lib, sizeof(bad_lib)));
        TEST_ASSERT(nylink_has_errors(ctx));
        nylink_context_destroy(ctx);
    }

    {
        Ny_Object_Buffer obj;
        ny_obj_buf_init(&obj);
        emit_dummy_coff(&obj, "orphan", false, nullptr);

        Nylink_Context *ctx = nylink_context_create();
        nylink_context_set_output_mode(ctx, NYLINK_OUTPUT_DLL);
        nylink_add_export(ctx, "nonexistent_symbol");
        TEST_ASSERT(nylink_add_object(ctx, "o.obj", obj.bytes, obj.count));
        TEST_ASSERT(nylink_resolve_symbols(ctx));

        const char *exports[] = { "nonexistent_symbol" };
        Nylink_Config cfg = {
            .target_format = NYLINK_TARGET_PE,
            .output_mode = NYLINK_OUTPUT_DLL,
            .exports = exports,
            .export_count = 1,
        };
        TEST_ASSERT(nylink_layout(ctx, &cfg));
        TEST_ASSERT(nylink_apply_relocations(ctx));

        const char *dll_path = "bin/test_neg_orphan.dll";
        TEST_ASSERT(nylink_write_executable(ctx, dll_path, &cfg));

        FILE *f = fopen(dll_path, "rb");
        TEST_ASSERT(f != nullptr);
        Pe_Test_Dos_Header dos;
        TEST_ASSERT_EQ(fread(&dos, sizeof(dos), 1, f), 1);
        fseek(f, (long)dos.e_lfanew + 4 + sizeof(Pe_Test_File_Header), SEEK_SET);
        Pe_Test_Optional_Header64 oh;
        TEST_ASSERT_EQ(fread(&oh, sizeof(oh), 1, f), 1);
        TEST_ASSERT(oh.DataDirectory[0].VirtualAddress != 0);
        TEST_ASSERT(oh.DataDirectory[0].Size >= 40);

        fclose(f);
        remove(dll_path);
        nylink_context_destroy(ctx);
        ny_obj_buf_destroy(&obj);
    }
}

void test_nylink_hardening_rejections(void) {
    /* 1. Rejection of mixed ELF + COFF objects in link context */
    {
        Ny_Object_Buffer elf_obj;
        ny_obj_buf_init(&elf_obj);
        emit_dummy_elf(&elf_obj, "fn_elf", false, nullptr);

        Ny_Object_Buffer coff_obj;
        ny_obj_buf_init(&coff_obj);
        emit_dummy_coff(&coff_obj, "fn_coff", false, nullptr);

        Nylink_Context *ctx = nylink_context_create();
        TEST_ASSERT(nylink_add_object(ctx, "obj1.o", elf_obj.bytes, elf_obj.count));
        /* Adding COFF to ELF context must fail */
        TEST_ASSERT(!nylink_add_object(ctx, "obj2.obj", coff_obj.bytes, coff_obj.count));
        TEST_ASSERT(nylink_has_errors(ctx));

        nylink_context_destroy(ctx);
        ny_obj_buf_destroy(&elf_obj);
        ny_obj_buf_destroy(&coff_obj);
    }

    /* 2. Target format mismatch: PE target with ELF objects, and ELF target with COFF objects */
    {
        Ny_Object_Buffer elf_obj;
        ny_obj_buf_init(&elf_obj);
        emit_dummy_elf(&elf_obj, "main", false, nullptr);

        Nylink_Context *ctx = nylink_context_create();
        TEST_ASSERT(nylink_add_object(ctx, "elf_main.o", elf_obj.bytes, elf_obj.count));
        TEST_ASSERT(nylink_resolve_symbols(ctx));

        Nylink_Config cfg_pe = {
            .target_format = NYLINK_TARGET_PE,
            .output_mode = NYLINK_OUTPUT_EXECUTABLE,
            .entry_point = "main",
        };
        TEST_ASSERT(!nylink_layout(ctx, &cfg_pe));
        TEST_ASSERT(nylink_has_errors(ctx));

        nylink_context_destroy(ctx);
        ny_obj_buf_destroy(&elf_obj);
    }
    {
        Ny_Object_Buffer coff_obj;
        ny_obj_buf_init(&coff_obj);
        emit_dummy_coff(&coff_obj, "main", false, nullptr);

        Nylink_Context *ctx = nylink_context_create();
        TEST_ASSERT(nylink_add_object(ctx, "coff_main.obj", coff_obj.bytes, coff_obj.count));
        TEST_ASSERT(nylink_resolve_symbols(ctx));

        Nylink_Config cfg_elf = {
            .target_format = NYLINK_TARGET_ELF64,
            .output_mode = NYLINK_OUTPUT_EXECUTABLE,
            .entry_point = "main",
        };
        TEST_ASSERT(!nylink_layout(ctx, &cfg_elf));
        TEST_ASSERT(nylink_has_errors(ctx));

        nylink_context_destroy(ctx);
        ny_obj_buf_destroy(&coff_obj);
    }

    /* 3. ELF relocation symbol index out-of-bounds */
    {
        Ny_Object_Buffer obj;
        ny_obj_buf_init(&obj);
        emit_dummy_elf(&obj, "caller", true, "target_fn");

        Elf64_Test_Ehdr *ehdr = (Elf64_Test_Ehdr *)obj.bytes;
        Elf64_Test_Shdr *shdrs = (Elf64_Test_Shdr *)(obj.bytes + ehdr->e_shoff);
        const char *shstrtab = (const char *)(obj.bytes + shdrs[ehdr->e_shstrndx].sh_offset);
        for (uint16_t s = 1; s < ehdr->e_shnum; s++) {
            if (strcmp(shstrtab + shdrs[s].sh_name, ".rela.text") == 0) {
                Elf64_Test_Rela *relas = (Elf64_Test_Rela *)(obj.bytes + shdrs[s].sh_offset);
                relas[0].r_info = ((uint64_t)99999 << 32) | (relas[0].r_info & 0xFFFFFFFFULL);
                break;
            }
        }

        Nylink_Context *ctx = nylink_context_create();
        TEST_ASSERT(!nylink_add_object(ctx, "corrupted_elf.o", obj.bytes, obj.count));
        TEST_ASSERT(nylink_has_errors(ctx));

        nylink_context_destroy(ctx);
        ny_obj_buf_destroy(&obj);
    }

    /* 4. COFF relocation symbol index out-of-bounds */
    {
        Ny_Object_Buffer obj;
        ny_obj_buf_init(&obj);
        emit_dummy_coff(&obj, "caller", true, "target_fn");

        Pe_Test_File_Header *fhdr = (Pe_Test_File_Header *)obj.bytes;
        Pe_Test_Section_Header *shdrs = (Pe_Test_Section_Header *)(obj.bytes + sizeof(Pe_Test_File_Header) + fhdr->SizeOfOptionalHeader);
        for (uint16_t s = 0; s < fhdr->NumberOfSections; s++) {
            if (shdrs[s].NumberOfRelocations > 0 && shdrs[s].PointerToRelocations > 0) {
                Pe_Test_Relocation *reloc = (Pe_Test_Relocation *)(obj.bytes + shdrs[s].PointerToRelocations);
                reloc->SymbolTableIndex = 99999;
                break;
            }
        }

        Nylink_Context *ctx = nylink_context_create();
        TEST_ASSERT(!nylink_add_object(ctx, "corrupted_coff.obj", obj.bytes, obj.count));
        TEST_ASSERT(nylink_has_errors(ctx));

        nylink_context_destroy(ctx);
        ny_obj_buf_destroy(&obj);
    }
}
