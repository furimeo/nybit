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

    Elf64_Test_Phdr phdrs[8];
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


