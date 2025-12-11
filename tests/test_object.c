// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include "nybit/object.h"
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include "nybit/regalloc.h"
#include "nybit/opt.h"
#include "nybit/parser.h"
#include <string.h>
#include <stdlib.h>

#if defined(__x86_64__) || defined(_M_X64)
#define NY_X86_HOST 1
#else
#define NY_X86_HOST 0
#endif

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'
#define ET_REL 1
#define EM_X86_64 62

#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_REL_AMD64_REL32 4

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
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Test_Sym;

typedef struct Elf64_Test_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Test_Rela;

typedef struct Coff_Test_File_Header {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} Coff_Test_File_Header;

typedef struct Coff_Test_Section_Header {
    uint8_t Name[8];
    uint32_t VirtualSize;
    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
} Coff_Test_Section_Header;

typedef struct Coff_Test_Relocation {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
} Coff_Test_Relocation;

typedef struct Coff_Test_Symbol {
    union {
        uint8_t ShortName[8];
        struct {
            uint32_t Zeros;
            uint32_t Offset;
        } LongName;
    } N;
    uint32_t Value;
    int16_t SectionNumber;
    uint16_t Type;
    uint8_t StorageClass;
    uint8_t NumberOfAuxSymbols;
} Coff_Test_Symbol;
#pragma pack(pop)

void test_object_buffer_growth_and_alignment(void) {
    Ny_Object_Buffer buf;
    ny_obj_buf_init(&buf);

    TEST_ASSERT_EQ(buf.count, 0);
    TEST_ASSERT_EQ(buf.capacity, 0);

    ny_obj_buf_append_byte(&buf, 0x48);
    TEST_ASSERT_EQ(buf.count, 1);
    TEST_ASSERT(buf.capacity >= 1);
    TEST_ASSERT_EQ(buf.bytes[0], 0x48);

    const uint8_t data[] = { 0x89, 0xd8, 0xc3 };
    ny_obj_buf_append_bytes(&buf, data, sizeof(data));
    TEST_ASSERT_EQ(buf.count, 4);
    TEST_ASSERT_EQ(buf.bytes[3], 0xc3);

    ny_obj_buf_align_to(&buf, 16);
    TEST_ASSERT_EQ(buf.count, 16);
    for (size_t i = 4; i < 16; i++) {
        TEST_ASSERT_EQ(buf.bytes[i], 0);
    }

    ny_obj_buf_destroy(&buf);
    TEST_ASSERT_EQ(buf.bytes, nullptr);
    TEST_ASSERT_EQ(buf.count, 0);
}

void test_object_elf64_basic_structure(void) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("test_elf"));

    const uint8_t code[] = { 0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3 };
    x86_buf_append_bytes(&emod.text_section, code, sizeof(code));

    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, emod.function_count + 1, sizeof(X86_Function_Code));
    emod.functions[emod.function_count++] = (X86_Function_Code){
        .name = ny_str("main"),
        .offset = 0,
        .size = sizeof(code),
    };

    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    TEST_ASSERT(ny_emit_elf64_x86_64(&obj_buf, &emod, &diags));
    TEST_ASSERT(obj_buf.count > sizeof(Elf64_Test_Ehdr));

    const Elf64_Test_Ehdr *ehdr = (const Elf64_Test_Ehdr *)obj_buf.bytes;
    TEST_ASSERT_EQ(ehdr->e_ident[0], ELF_MAGIC_0);
    TEST_ASSERT_EQ(ehdr->e_ident[1], ELF_MAGIC_1);
    TEST_ASSERT_EQ(ehdr->e_ident[2], ELF_MAGIC_2);
    TEST_ASSERT_EQ(ehdr->e_ident[3], ELF_MAGIC_3);
    TEST_ASSERT_EQ(ehdr->e_type, ET_REL);
    TEST_ASSERT_EQ(ehdr->e_machine, EM_X86_64);
    TEST_ASSERT_EQ(ehdr->e_shnum, 5);

    const Elf64_Test_Shdr *shdrs = (const Elf64_Test_Shdr *)(obj_buf.bytes + ehdr->e_shoff);
    const Elf64_Test_Shdr *sh_text = &shdrs[1];
    TEST_ASSERT_EQ(sh_text->sh_size, sizeof(code));

    const uint8_t *text_code = obj_buf.bytes + sh_text->sh_offset;
    for (size_t i = 0; i < sizeof(code); i++) {
        TEST_ASSERT_EQ(text_code[i], code[i]);
    }

    const Elf64_Test_Shdr *sh_symtab = &shdrs[2];
    const Elf64_Test_Shdr *sh_strtab = &shdrs[3];
    const char *strtab = (const char *)(obj_buf.bytes + sh_strtab->sh_offset);
    const Elf64_Test_Sym *syms = (const Elf64_Test_Sym *)(obj_buf.bytes + sh_symtab->sh_offset);

    TEST_ASSERT_EQ(syms[0].st_name, 0);
    TEST_ASSERT_EQ(syms[1].st_shndx, 1);
    TEST_ASSERT_STR_EQ(strtab + syms[2].st_name, "main");
    TEST_ASSERT_EQ(syms[2].st_value, 0);
    TEST_ASSERT_EQ(syms[2].st_size, sizeof(code));

    ny_diagnostic_list_destroy(&diags);
    ny_obj_buf_destroy(&obj_buf);
    x86_encoded_mod_destroy(&emod);
}

void test_object_elf64_relocations(void) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("test_elf_reloc"));

    const uint8_t code[] = { 0xe8, 0x00, 0x00, 0x00, 0x00, 0xc3 };
    x86_buf_append_bytes(&emod.text_section, code, sizeof(code));

    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, emod.function_count + 1, sizeof(X86_Function_Code));
    emod.functions[emod.function_count++] = (X86_Function_Code){
        .name = ny_str("caller"),
        .offset = 0,
        .size = sizeof(code),
    };

    X86_Relocation reloc = {
        .kind = X86_FIXUP_CALL_REL32,
        .code_offset = 1,
        .symbol_name = ny_str("extern_fn"),
        .addend = 0,
    };
    x86_buf_append_reloc(&emod.text_section, reloc);

    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    TEST_ASSERT(ny_emit_elf64_x86_64(&obj_buf, &emod, &diags));

    const Elf64_Test_Ehdr *ehdr = (const Elf64_Test_Ehdr *)obj_buf.bytes;
    TEST_ASSERT_EQ(ehdr->e_shnum, 6);

    const Elf64_Test_Shdr *shdrs = (const Elf64_Test_Shdr *)(obj_buf.bytes + ehdr->e_shoff);
    const Elf64_Test_Shdr *sh_rela = &shdrs[2];
    TEST_ASSERT_EQ(sh_rela->sh_size, sizeof(Elf64_Test_Rela));

    const Elf64_Test_Rela *rela = (const Elf64_Test_Rela *)(obj_buf.bytes + sh_rela->sh_offset);
    TEST_ASSERT_EQ(rela->r_offset, 1);
    TEST_ASSERT_EQ((uint32_t)(rela->r_info & 0xffffffffL), 4);
    TEST_ASSERT_EQ(rela->r_addend, -4);

    uint32_t sym_idx = (uint32_t)(rela->r_info >> 32);
    const Elf64_Test_Shdr *sh_symtab = &shdrs[3];
    const Elf64_Test_Shdr *sh_strtab = &shdrs[4];
    const char *strtab = (const char *)(obj_buf.bytes + sh_strtab->sh_offset);
    const Elf64_Test_Sym *syms = (const Elf64_Test_Sym *)(obj_buf.bytes + sh_symtab->sh_offset);
    TEST_ASSERT_STR_EQ(strtab + syms[sym_idx].st_name, "extern_fn");

    ny_diagnostic_list_destroy(&diags);
    ny_obj_buf_destroy(&obj_buf);
    x86_encoded_mod_destroy(&emod);
}

void test_object_coff_basic_structure(void) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("test_coff"));

    const uint8_t code[] = { 0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3 };
    x86_buf_append_bytes(&emod.text_section, code, sizeof(code));

    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, emod.function_count + 1, sizeof(X86_Function_Code));
    emod.functions[emod.function_count++] = (X86_Function_Code){
        .name = ny_str("main"),
        .offset = 0,
        .size = sizeof(code),
    };

    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    TEST_ASSERT(ny_emit_coff_x86_64(&obj_buf, &emod, &diags));

    const Coff_Test_File_Header *fhdr = (const Coff_Test_File_Header *)obj_buf.bytes;
    TEST_ASSERT_EQ(fhdr->Machine, IMAGE_FILE_MACHINE_AMD64);
    TEST_ASSERT_EQ(fhdr->NumberOfSections, 1);
    TEST_ASSERT_EQ(fhdr->NumberOfSymbols, 1);

    const Coff_Test_Section_Header *shdr = (const Coff_Test_Section_Header *)(obj_buf.bytes + sizeof(Coff_Test_File_Header));
    TEST_ASSERT_EQ(shdr->SizeOfRawData, sizeof(code));
    TEST_ASSERT_EQ(shdr->NumberOfRelocations, 0);

    const uint8_t *text_bytes = obj_buf.bytes + shdr->PointerToRawData;
    for (size_t i = 0; i < sizeof(code); i++) {
        TEST_ASSERT_EQ(text_bytes[i], code[i]);
    }

    const Coff_Test_Symbol *sym = (const Coff_Test_Symbol *)(obj_buf.bytes + fhdr->PointerToSymbolTable);
    char name_buf[9] = {0};
    memcpy(name_buf, sym->N.ShortName, 4);
    TEST_ASSERT_STR_EQ(name_buf, "main");
    TEST_ASSERT_EQ(sym->Value, 0);
    TEST_ASSERT_EQ(sym->SectionNumber, 1);

    ny_diagnostic_list_destroy(&diags);
    ny_obj_buf_destroy(&obj_buf);
    x86_encoded_mod_destroy(&emod);
}

void test_object_coff_relocations_and_long_names(void) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("test_coff_reloc"));

    const uint8_t code[] = { 0xe8, 0x00, 0x00, 0x00, 0x00, 0xc3 };
    x86_buf_append_bytes(&emod.text_section, code, sizeof(code));

    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, emod.function_count + 1, sizeof(X86_Function_Code));
    emod.functions[emod.function_count++] = (X86_Function_Code){
        .name = ny_str("very_long_function_name_exceeding_8_chars"),
        .offset = 0,
        .size = sizeof(code),
    };

    X86_Relocation reloc = {
        .kind = X86_FIXUP_CALL_REL32,
        .code_offset = 1,
        .symbol_name = ny_str("external_target_function"),
        .addend = 0,
    };
    x86_buf_append_reloc(&emod.text_section, reloc);

    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    TEST_ASSERT(ny_emit_coff_x86_64(&obj_buf, &emod, &diags));

    const Coff_Test_File_Header *fhdr = (const Coff_Test_File_Header *)obj_buf.bytes;
    TEST_ASSERT_EQ(fhdr->NumberOfSymbols, 2);

    const Coff_Test_Section_Header *shdr = (const Coff_Test_Section_Header *)(obj_buf.bytes + sizeof(Coff_Test_File_Header));
    TEST_ASSERT_EQ(shdr->NumberOfRelocations, 1);

    const Coff_Test_Relocation *creloc = (const Coff_Test_Relocation *)(obj_buf.bytes + shdr->PointerToRelocations);
    TEST_ASSERT_EQ(creloc->VirtualAddress, 1);
    TEST_ASSERT_EQ(creloc->Type, IMAGE_REL_AMD64_REL32);
    TEST_ASSERT_EQ(creloc->SymbolTableIndex, 1);

    const Coff_Test_Symbol *syms = (const Coff_Test_Symbol *)(obj_buf.bytes + fhdr->PointerToSymbolTable);
    const char *strtab = (const char *)(syms + fhdr->NumberOfSymbols);

    TEST_ASSERT_EQ(syms[0].N.LongName.Zeros, 0);
    const char *fn_name = strtab + syms[0].N.LongName.Offset;
    TEST_ASSERT_STR_EQ(fn_name, "very_long_function_name_exceeding_8_chars");

    TEST_ASSERT_EQ(syms[1].N.LongName.Zeros, 0);
    const char *ext_name = strtab + syms[1].N.LongName.Offset;
    TEST_ASSERT_STR_EQ(ext_name, "external_target_function");

    ny_diagnostic_list_destroy(&diags);
    ny_obj_buf_destroy(&obj_buf);
    x86_encoded_mod_destroy(&emod);
}

void test_object_multi_function_and_relocations(void) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("multi_test"));

    /* Function 1: f1 at offset 0, size 6 */
    const uint8_t code1[] = { 0xb8, 0x01, 0x00, 0x00, 0x00, 0xc3 };
    x86_buf_append_bytes(&emod.text_section, code1, sizeof(code1));

    /* Function 2: f2 at offset 6, size 10, calls extern_a and extern_b */
    const uint8_t code2[] = { 0xe8, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x00, 0x00, 0x00, 0x00 };
    x86_buf_append_bytes(&emod.text_section, code2, sizeof(code2));

    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, 2, sizeof(X86_Function_Code));
    emod.functions[0] = (X86_Function_Code){ .name = ny_str("func_one"), .offset = 0, .size = sizeof(code1) };
    emod.functions[1] = (X86_Function_Code){ .name = ny_str("func_two"), .offset = sizeof(code1), .size = sizeof(code2) };
    emod.function_count = 2;

    X86_Relocation r1 = { .kind = X86_FIXUP_CALL_REL32, .code_offset = 7, .symbol_name = ny_str("extern_alpha"), .addend = 0 };
    X86_Relocation r2 = { .kind = X86_FIXUP_CALL_REL32, .code_offset = 12, .symbol_name = ny_str("extern_beta"), .addend = 0 };
    x86_buf_append_reloc(&emod.text_section, r1);
    x86_buf_append_reloc(&emod.text_section, r2);

    /* Test ELF multi-function & reloc */
    Ny_Object_Buffer elf_buf;
    ny_obj_buf_init(&elf_buf);
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    TEST_ASSERT(ny_emit_elf64_x86_64(&elf_buf, &emod, &diags));

    const Elf64_Test_Ehdr *ehdr = (const Elf64_Test_Ehdr *)elf_buf.bytes;
    const Elf64_Test_Shdr *shdrs = (const Elf64_Test_Shdr *)(elf_buf.bytes + ehdr->e_shoff);
    const Elf64_Test_Shdr *sh_rela = &shdrs[2];
    TEST_ASSERT_EQ(sh_rela->sh_size, 2 * sizeof(Elf64_Test_Rela));

    const Elf64_Test_Rela *rela = (const Elf64_Test_Rela *)(elf_buf.bytes + sh_rela->sh_offset);
    TEST_ASSERT_EQ(rela[0].r_offset, 7);
    TEST_ASSERT_EQ(rela[1].r_offset, 12);
    ny_obj_buf_destroy(&elf_buf);

    /* Test COFF multi-function & reloc */
    Ny_Object_Buffer coff_buf;
    ny_obj_buf_init(&coff_buf);
    TEST_ASSERT(ny_emit_coff_x86_64(&coff_buf, &emod, &diags));

    const Coff_Test_File_Header *fhdr = (const Coff_Test_File_Header *)coff_buf.bytes;
    TEST_ASSERT_EQ(fhdr->NumberOfSymbols, 4);
    const Coff_Test_Section_Header *shdr = (const Coff_Test_Section_Header *)(coff_buf.bytes + sizeof(Coff_Test_File_Header));
    TEST_ASSERT_EQ(shdr->NumberOfRelocations, 2);

    const Coff_Test_Relocation *crelocs = (const Coff_Test_Relocation *)(coff_buf.bytes + shdr->PointerToRelocations);
    TEST_ASSERT_EQ(crelocs[0].VirtualAddress, 7);
    TEST_ASSERT_EQ(crelocs[1].VirtualAddress, 12);

    ny_obj_buf_destroy(&coff_buf);
    ny_diagnostic_list_destroy(&diags);
    x86_encoded_mod_destroy(&emod);
}

void test_object_bounds_and_error_validation(void) {
    X86_Encoded_Module emod;
    x86_encoded_mod_init(&emod, ny_str("invalid_mod"));

    const uint8_t code[] = { 0xc3 };
    x86_buf_append_bytes(&emod.text_section, code, sizeof(code));

    /* Function out of bounds */
    ny_buf_grow((void **)&emod.functions, &emod.function_capacity, 1, sizeof(X86_Function_Code));
    emod.functions[0] = (X86_Function_Code){ .name = ny_str("bad_fn"), .offset = 10, .size = 5 };
    emod.function_count = 1;

    Ny_Object_Buffer buf;
    ny_obj_buf_init(&buf);
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);

    TEST_ASSERT(!ny_emit_elf64_x86_64(&buf, &emod, &diags));
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    ny_diagnostic_list_init(&diags);
    TEST_ASSERT(!ny_emit_coff_x86_64(&buf, &emod, &diags));
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    /* Fix function bounds, add invalid relocation offset */
    emod.functions[0].offset = 0;
    emod.functions[0].size = 1;

    X86_Relocation bad_reloc = {
        .kind = X86_FIXUP_CALL_REL32,
        .code_offset = 100,
        .symbol_name = ny_str("ext"),
        .addend = 0,
    };
    x86_buf_append_reloc(&emod.text_section, bad_reloc);

    ny_diagnostic_list_init(&diags);
    TEST_ASSERT(!ny_emit_elf64_x86_64(&buf, &emod, &diags));
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    ny_diagnostic_list_init(&diags);
    TEST_ASSERT(!ny_emit_coff_x86_64(&buf, &emod, &diags));
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    ny_obj_buf_destroy(&buf);
    x86_encoded_mod_destroy(&emod);
}

void test_object_readelf_and_objdump_inspection(void) {
    if (!NY_X86_HOST) { printf("test_object_readelf_and_objdump_inspection: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    const char *src =
        "@function multi_a(%x: i32) -> i32;\n"
        ".entry;\n"
        "    %one = const 1;\n"
        "    %res = add %x, %one;\n"
        "    @return %res;\n"
        ";;\n"
        "@function multi_b(%y: i32) -> i32;\n"
        ".entry;\n"
        "    %two = const 2;\n"
        "    %res = mul %y, %two;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "toolchain_check");

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, src, strlen(src), &ctx.arena);
    TEST_ASSERT(ny_parse_module(&parser));

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    TEST_ASSERT(ny_validate_module(&ctx.module, &val_diags));
    ny_diagnostic_list_destroy(&val_diags);

    ny_opt_run_module_pipeline(&ctx.module, NY_OPT_O1);

    const Ny_Target *tgt_sysv = ny_target_find("x86_64-sysv");
    const Ny_Target *tgt_win = ny_target_find("x86_64-windows");

    /* SysV ELF */
    Ny_Machine_Module mmod_elf;
    Ny_Diagnostic_List mdiags;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(ny_ir_lower_to_mir(&ctx.module, &mmod_elf, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    for (size_t f = 0; f < mmod_elf.function_count; f++) {
        Ny_Diagnostic_List rdiags;
        ny_diagnostic_list_init(&rdiags);
        TEST_ASSERT(ny_regalloc_run(&mmod_elf.functions[f], tgt_sysv->abi, nullptr, &rdiags));
        ny_diagnostic_list_destroy(&rdiags);
    }
    X86_Module xmod_elf;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(x86_lower_machine_mod(tgt_sysv, &mmod_elf, &xmod_elf, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);
    X86_Encoded_Module emod_elf;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(x86_encode_module(&emod_elf, &xmod_elf, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    Ny_Object_Buffer elf_buf;
    ny_obj_buf_init(&elf_buf);
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(ny_emit_object_module(&elf_buf, tgt_sysv, &emod_elf, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    FILE *felf = fopen("bin/test_check.o", "wb");
    TEST_ASSERT(felf != nullptr);
    fwrite(elf_buf.bytes, 1, elf_buf.count, felf);
    fclose(felf);

    /* Inspect ELF using readelf and objdump */
    int elf_read_res = system("readelf -h bin/test_check.o > nul 2>&1");
    TEST_ASSERT_EQ(elf_read_res, 0);
    int elf_dump_res = system("objdump -dr bin/test_check.o > nul 2>&1");
    TEST_ASSERT_EQ(elf_dump_res, 0);
    remove("bin/test_check.o");

    ny_obj_buf_destroy(&elf_buf);
    x86_encoded_mod_destroy(&emod_elf);
    x86_mod_destroy(&xmod_elf);
    ny_mmod_destroy(&mmod_elf);

    /* Windows COFF */
    Ny_Machine_Module mmod_coff;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(ny_ir_lower_to_mir(&ctx.module, &mmod_coff, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    for (size_t f = 0; f < mmod_coff.function_count; f++) {
        Ny_Diagnostic_List rdiags;
        ny_diagnostic_list_init(&rdiags);
        TEST_ASSERT(ny_regalloc_run(&mmod_coff.functions[f], tgt_win->abi, nullptr, &rdiags));
        ny_diagnostic_list_destroy(&rdiags);
    }
    X86_Module xmod_coff;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(x86_lower_machine_mod(tgt_win, &mmod_coff, &xmod_coff, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);
    X86_Encoded_Module emod_coff;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(x86_encode_module(&emod_coff, &xmod_coff, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    Ny_Object_Buffer coff_buf;
    ny_obj_buf_init(&coff_buf);
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(ny_emit_object_module(&coff_buf, tgt_win, &emod_coff, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    FILE *fcoff = fopen("bin/test_check.obj", "wb");
    TEST_ASSERT(fcoff != nullptr);
    fwrite(coff_buf.bytes, 1, coff_buf.count, fcoff);
    fclose(fcoff);

    /* Inspect COFF using objdump */
    int coff_dump_res = system("objdump -dr bin/test_check.obj > nul 2>&1");
    TEST_ASSERT_EQ(coff_dump_res, 0);
    remove("bin/test_check.obj");

    ny_obj_buf_destroy(&coff_buf);
    x86_encoded_mod_destroy(&emod_coff);
    x86_mod_destroy(&xmod_coff);
    ny_mmod_destroy(&mmod_coff);

    ny_context_destroy(&ctx);
}

static bool compile_source_to_obj(const char *src, const char *out_obj_path, const Ny_Target *target) {
    Ny_Context ctx;
    ny_context_init(&ctx, "test_e2e");

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, src, strlen(src), &ctx.arena);
    if (!ny_parse_module(&parser)) {
        if (parser.diag_count > 0) fprintf(stderr, "parse error: %s (line %d, col %d)\n", parser.diagnostics[0].message, parser.diagnostics[0].line, parser.diagnostics[0].col);
        ny_parser_destroy(&parser);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_parser_destroy(&parser);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    if (!ny_validate_module(&ctx.module, &val_diags)) {
        if (val_diags.count > 0) fprintf(stderr, "val error: %s\n", val_diags.items[0].message);
        ny_diagnostic_list_destroy(&val_diags);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&val_diags);

    ny_opt_run_module_pipeline(&ctx.module, NY_OPT_O1);

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List mir_diags;
    ny_diagnostic_list_init(&mir_diags);
    if (!ny_ir_lower_to_mir(&ctx.module, &mmod, &mir_diags)) {
        ny_diagnostic_list_destroy(&mir_diags);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&mir_diags);

    for (size_t f = 0; f < mmod.function_count; f++) {
        Ny_Diagnostic_List ra_diags;
        ny_diagnostic_list_init(&ra_diags);
        if (!ny_regalloc_run(&mmod.functions[f], target->abi, nullptr, &ra_diags) ||
            !ny_mfunc_validate_allocated(&mmod.functions[f], &ra_diags)) {
            ny_diagnostic_list_destroy(&ra_diags);
            ny_mmod_destroy(&mmod);
            ny_context_destroy(&ctx);
            return false;
        }
        ny_diagnostic_list_destroy(&ra_diags);
    }

    X86_Module xmod;
    Ny_Diagnostic_List x86_diags;
    ny_diagnostic_list_init(&x86_diags);
    if (!x86_lower_machine_mod(target, &mmod, &xmod, &x86_diags)) {
        ny_diagnostic_list_destroy(&x86_diags);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&x86_diags);

    X86_Encoded_Module emod;
    Ny_Diagnostic_List enc_diags;
    ny_diagnostic_list_init(&enc_diags);
    if (!x86_encode_module(&emod, &xmod, &enc_diags)) {
        ny_diagnostic_list_destroy(&enc_diags);
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&enc_diags);

    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);
    Ny_Diagnostic_List obj_diags;
    ny_diagnostic_list_init(&obj_diags);
    if (!ny_emit_object_module(&obj_buf, target, &emod, &obj_diags)) {
        ny_diagnostic_list_destroy(&obj_diags);
        ny_obj_buf_destroy(&obj_buf);
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    ny_diagnostic_list_destroy(&obj_diags);

    FILE *fobj = fopen(out_obj_path, "wb");
    if (!fobj) {
        ny_obj_buf_destroy(&obj_buf);
        x86_encoded_mod_destroy(&emod);
        x86_mod_destroy(&xmod);
        ny_mmod_destroy(&mmod);
        ny_context_destroy(&ctx);
        return false;
    }
    fwrite(obj_buf.bytes, 1, obj_buf.count, fobj);
    fclose(fobj);

    ny_obj_buf_destroy(&obj_buf);
    x86_encoded_mod_destroy(&emod);
    x86_mod_destroy(&xmod);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
    return true;
}

void test_object_e2e_link_executable(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_link_executable: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    const char *src =
        "@function nybit_compute() -> i32;\n"
        ".entry;\n"
        "    %a = const 20;\n"
        "    %b = const 22;\n"
        "    %res = add %a, %b;\n"
        "    @return %res;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *obj_path = "bin/test_e2e_part.obj";
    TEST_ASSERT(compile_source_to_obj(src, obj_path, target));

#if defined(_WIN32)
    FILE *fc = fopen("bin/test_e2e_driver.c", "w");
    TEST_ASSERT(fc != nullptr);
    fputs("extern int nybit_compute(void);\nint main(void) { return nybit_compute() == 42 ? 0 : 1; }\n", fc);
    fclose(fc);

    const char *objs[2] = { "bin/test_e2e_driver.c", obj_path };
    Ny_Diagnostic_List link_diags;
    ny_diagnostic_list_init(&link_diags);
    bool link_ok = ny_link_executable_with_extra(objs, 2, "bin/test_e2e_run.exe", target, &link_diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&link_diags);

    int run_res = system("bin\\test_e2e_run.exe");
    TEST_ASSERT_EQ(run_res, 0);

    remove("bin/test_e2e_driver.c");
    remove(obj_path);
    remove("bin/test_e2e_run.exe");
#endif
}

void test_object_e2e_internal_calls(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_internal_calls: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    const char *src =
        "@function helper_mul(%x: i32, %y: i32) -> i32;\n"
        ".entry;\n"
        "    %res = mul %x, %y;\n"
        "    @return %res;\n"
        ";;\n\n"
        "@function is_positive(%n: i32) -> i32;\n"
        ".entry;\n"
        "    %zero = const 0;\n"
        "    %cond = cmp.gt.s %n, %zero;\n"
        "    @branch_if %cond, .positive, .negative;\n"
        ".positive;\n"
        "    %one = const 1;\n"
        "    @return %one;\n"
        ".negative;\n"
        "    %zero_ret = const 0;\n"
        "    @return %zero_ret;\n"
        ";;\n\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %c6 = const 6;\n"
        "    %c7 = const 7;\n"
        "    %prod = call @helper_mul, %c6, %c7;\n"
        "    %pos = call @is_positive, %prod;\n"
        "    %zero = const 0;\n"
        "    %cond = cmp.eq %pos, %zero;\n"
        "    @branch_if %cond, .fail, .ok;\n"
        ".ok;\n"
        "    @return %prod;\n"
        ".fail;\n"
        "    %err = const 1;\n"
        "    @return %err;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *obj_path = "bin/test_e2e_internal.obj";
    const char *exe_path = "bin/test_e2e_internal.exe";

    TEST_ASSERT(compile_source_to_obj(src, obj_path, target));

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable(obj_path, exe_path, target, &diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&diags);

    int exit_code = ny_test_system("./bin/test_e2e_internal.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove(obj_path);
    remove(exe_path);
}

void test_object_e2e_external_calls(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_external_calls: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    FILE *fc = fopen("bin/test_e2e_host.c", "w");
    TEST_ASSERT(fc != nullptr);
    fputs("int host_mult_add(int a, int b, int c) { return (a * b) + c; }\n", fc);
    fclose(fc);

    int c_build = system("gcc -c bin/test_e2e_host.c -o bin/test_e2e_host.o");
    TEST_ASSERT_EQ(c_build, 0);

    const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %c5 = const 5;\n"
        "    %c8 = const 8;\n"
        "    %c2 = const 2;\n"
        "    %res = call @host_mult_add, %c5, %c8, %c2;\n"
        "    @return %res;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *ny_obj = "bin/test_e2e_ext_main.obj";
    const char *exe_path = "bin/test_e2e_ext.exe";

    TEST_ASSERT(compile_source_to_obj(src, ny_obj, target));

    const char *objs[2] = { ny_obj, "bin/test_e2e_host.o" };
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable_with_extra(objs, 2, exe_path, target, &diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&diags);

    int exit_code = ny_test_system("./bin/test_e2e_ext.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove("bin/test_e2e_host.c");
    remove("bin/test_e2e_host.o");
    remove(ny_obj);
    remove(exe_path);
}

void test_object_e2e_linker_diagnostics(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_linker_diagnostics: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    const char *src =
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %res = call @undefined_external_symbol_12345;\n"
        "    @return %res;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *obj_path = "bin/test_e2e_undef.obj";
    const char *exe_path = "bin/test_e2e_undef.exe";

    TEST_ASSERT(compile_source_to_obj(src, obj_path, target));

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable(obj_path, exe_path, target, &diags);
    TEST_ASSERT(!link_ok);
    TEST_ASSERT(diags.count > 0);
    ny_diagnostic_list_destroy(&diags);

    remove(obj_path);
}

void test_object_globals_rodata_data_bss(void) {
    const char *src =
        "@global @readonly @k_const: i32 = 42;\n"
        "@global @k_val: i32 = 100;\n"
        "@global @k_zero: i32;\n"
        "\n"
        "@function get_const() -> i32;\n"
        ".entry;\n"
        "    %ptr = global_addr @k_const;\n"
        "    %val = load %ptr;\n"
        "    @return %val;\n"
        ";;\n"
        "\n"
        "@function read_write_globals() -> i32;\n"
        ".entry;\n"
        "    %p_val = global_addr @k_val;\n"
        "    %v = load %p_val;\n"
        "    %p_zero = global_addr @k_zero;\n"
        "    store %p_zero, %v;\n"
        "    %res = load %p_zero;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_globals_emit");

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, src, strlen(src), &ctx.arena);
    TEST_ASSERT(ny_parse_module(&parser));
    ny_parser_destroy(&parser);

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    TEST_ASSERT(ny_validate_module(&ctx.module, &val_diags));
    ny_diagnostic_list_destroy(&val_diags);

    const Ny_Target *tgt_sysv = ny_target_find("x86_64-sysv");
    const Ny_Target *tgt_win = ny_target_find("x86_64-windows");

    /* SysV ELF */
    Ny_Machine_Module mmod_elf;
    Ny_Diagnostic_List mdiags;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(ny_ir_lower_to_mir(&ctx.module, &mmod_elf, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    for (size_t f = 0; f < mmod_elf.function_count; f++) {
        Ny_Diagnostic_List rdiags;
        ny_diagnostic_list_init(&rdiags);
        TEST_ASSERT(ny_regalloc_run(&mmod_elf.functions[f], tgt_sysv->abi, nullptr, &rdiags));
        ny_diagnostic_list_destroy(&rdiags);
    }
    X86_Module xmod_elf;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(x86_lower_machine_mod(tgt_sysv, &mmod_elf, &xmod_elf, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);
    X86_Encoded_Module emod_elf;
    ny_diagnostic_list_init(&mdiags);
    bool enc_ok = x86_encode_module(&emod_elf, &xmod_elf, &mdiags);
    if (!enc_ok) {
        for (size_t d = 0; d < mdiags.count; d++) {
            fprintf(stderr, "x86_encode error: %s\n", mdiags.items[d].message);
        }
    }
    TEST_ASSERT(enc_ok);
    ny_diagnostic_list_destroy(&mdiags);

    TEST_ASSERT_EQ(emod_elf.rodata_section.count, 4);
    TEST_ASSERT_EQ(emod_elf.data_section.count, 4);
    TEST_ASSERT_EQ(emod_elf.bss_size, 4);

    Ny_Object_Buffer elf_buf;
    ny_obj_buf_init(&elf_buf);
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(ny_emit_object_module(&elf_buf, tgt_sysv, &emod_elf, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    /* Verify ELF section header count: null, .text, .rodata, .data, .bss, .rela.text, .symtab, .strtab, .shstrtab = 9 */
    const Elf64_Test_Ehdr *ehdr = (const Elf64_Test_Ehdr *)elf_buf.bytes;
    TEST_ASSERT_EQ(ehdr->e_shnum, 9);

    ny_obj_buf_destroy(&elf_buf);
    x86_encoded_mod_destroy(&emod_elf);
    x86_mod_destroy(&xmod_elf);
    ny_mmod_destroy(&mmod_elf);

    /* Windows COFF */
    Ny_Machine_Module mmod_coff;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(ny_ir_lower_to_mir(&ctx.module, &mmod_coff, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    for (size_t f = 0; f < mmod_coff.function_count; f++) {
        Ny_Diagnostic_List rdiags;
        ny_diagnostic_list_init(&rdiags);
        TEST_ASSERT(ny_regalloc_run(&mmod_coff.functions[f], tgt_win->abi, nullptr, &rdiags));
        ny_diagnostic_list_destroy(&rdiags);
    }
    X86_Module xmod_coff;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(x86_lower_machine_mod(tgt_win, &mmod_coff, &xmod_coff, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);
    X86_Encoded_Module emod_coff;
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(x86_encode_module(&emod_coff, &xmod_coff, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    Ny_Object_Buffer coff_buf;
    ny_obj_buf_init(&coff_buf);
    ny_diagnostic_list_init(&mdiags);
    TEST_ASSERT(ny_emit_object_module(&coff_buf, tgt_win, &emod_coff, &mdiags));
    ny_diagnostic_list_destroy(&mdiags);

    const Coff_Test_File_Header *fhdr = (const Coff_Test_File_Header *)coff_buf.bytes;
    TEST_ASSERT_EQ(fhdr->NumberOfSections, 4); /* .text, .rdata, .data, .bss */
    TEST_ASSERT_EQ(fhdr->NumberOfSymbols, 5);  /* 2 functions + 3 globals */

    ny_obj_buf_destroy(&coff_buf);
    x86_encoded_mod_destroy(&emod_coff);
    x86_mod_destroy(&xmod_coff);
    ny_mmod_destroy(&mmod_coff);

    ny_context_destroy(&ctx);
}

void test_object_e2e_globals_execution(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_globals_execution: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    const char *src =
        "@global @readonly @k_base: i32 = 20;\n"
        "@global @g_accum: i32 = 12;\n"
        "@global @g_temp: i32;\n"
        "\n"
        "@function compute_globals() -> i32;\n"
        ".entry;\n"
        "    %p_base = global_addr @k_base;\n"
        "    %base = load %p_base;\n"
        "    %p_accum = global_addr @g_accum;\n"
        "    %accum = load %p_accum;\n"
        "    %sum = add %base, %accum;\n"
        "    %c10 = const 10;\n"
        "    %total = add %sum, %c10;\n"
        "    %p_temp = global_addr @g_temp;\n"
        "    store %p_temp, %total;\n"
        "    %res = load %p_temp;\n"
        "    @return %res;\n"
        ";;\n"
        "\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %res = call @compute_globals;\n"
        "    @return %res;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *obj_path = "bin/test_e2e_globals.obj";
    const char *exe_path = "bin/test_e2e_globals.exe";

    TEST_ASSERT(compile_source_to_obj(src, obj_path, target));

    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable(obj_path, exe_path, target, &diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&diags);

    int exit_code = ny_test_system("./bin/test_e2e_globals.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove(obj_path);
    remove(exe_path);
}

void test_object_e2e_abi_stack_arguments(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_abi_stack_arguments: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    FILE *fc = fopen("bin/test_e2e_abi_host.c", "w");
    TEST_ASSERT(fc != nullptr);
    fputs(
        "int host_sum8(int a, int b, int c, int d, int e, int f, int g, int h) {\n"
        "    return a + b + c + d + e + f + g + h;\n"
        "}\n",
        fc
    );
    fclose(fc);

    int c_build = system("gcc -c bin/test_e2e_abi_host.c -o bin/test_e2e_abi_host.o");
    TEST_ASSERT_EQ(c_build, 0);

    const char *src =
        "@function host_sum8(%a: i32, %b: i32, %c: i32, %d: i32, %e: i32, %f: i32, %g: i32, %h: i32) -> i32;\n"
        "\n"
        "@function nybit_callee8(%a: i32, %b: i32, %c: i32, %d: i32, %e: i32, %f: i32, %g: i32, %h: i32) -> i32;\n"
        ".entry;\n"
        "    %s1 = add %a, %b;\n"
        "    %s2 = add %s1, %c;\n"
        "    %s3 = add %s2, %d;\n"
        "    %s4 = add %s3, %e;\n"
        "    %s5 = add %s4, %f;\n"
        "    %s6 = add %s5, %g;\n"
        "    %s7 = add %s6, %h;\n"
        "    @return %s7;\n"
        ";;\n"
        "\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %c1 = const 1;\n"
        "    %c2 = const 2;\n"
        "    %c3 = const 3;\n"
        "    %c4 = const 4;\n"
        "    %c5 = const 5;\n"
        "    %c6 = const 6;\n"
        "    %c7 = const 7;\n"
        "    %c8 = const 8;\n"
        "    %hsum = call @host_sum8, %c1, %c2, %c3, %c4, %c5, %c6, %c7, %c8;\n"
        "    %nsum = call @nybit_callee8, %c1, %c2, %c3, %c4, %c5, %c6, %c7, %c8;\n"
        "    %diff = sub %hsum, %nsum;\n"
        "    %c42 = const 42;\n"
        "    %res = add %c42, %diff;\n"
        "    @return %res;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *ny_obj = "bin/test_e2e_abi_stack.obj";
    const char *exe_path = "bin/test_e2e_abi_stack.exe";

    TEST_ASSERT(compile_source_to_obj(src, ny_obj, target));

    const char *objs[2] = { ny_obj, "bin/test_e2e_abi_host.o" };
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable_with_extra(objs, 2, exe_path, target, &diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&diags);

    int exit_code = ny_test_system("./bin/test_e2e_abi_stack.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove("bin/test_e2e_abi_host.c");
    remove("bin/test_e2e_abi_host.o");
    remove(ny_obj);
    remove(exe_path);
}

void test_object_e2e_abi_scalar_widths(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_abi_scalar_widths: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    FILE *fc = fopen("bin/test_e2e_widths_host.c", "w");
    TEST_ASSERT(fc != nullptr);
    fputs(
        "#include <stdint.h>\n"
        "int8_t host_i8(int8_t x) { return (int8_t)(x + 5); }\n"
        "int16_t host_i16(int16_t x) { return (int16_t)(x + 10); }\n"
        "int64_t host_i64(int64_t x) { return x + 100; }\n",
        fc
    );
    fclose(fc);

    int c_build = system("gcc -c bin/test_e2e_widths_host.c -o bin/test_e2e_widths_host.o");
    TEST_ASSERT_EQ(c_build, 0);

    const char *src =
        "@function host_i8(%x: i8) -> i8;\n"
        "@function host_i16(%x: i16) -> i16;\n"
        "@function host_i64(%x: i64) -> i64;\n"
        "\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %v8 = const 5;\n"
        "    %r8 = call @host_i8, %v8;\n"
        "    %v16 = const 10;\n"
        "    %r16 = call @host_i16, %v16;\n"
        "    %v64 = const 100;\n"
        "    %r64 = call @host_i64, %v64;\n"
        "    %s1 = add %r8, %r16;\n"
        "    %c12 = const 12;\n"
        "    %s2 = add %s1, %c12;\n"
        "    @return %s2;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *ny_obj = "bin/test_e2e_widths.obj";
    const char *exe_path = "bin/test_e2e_widths.exe";

    TEST_ASSERT(compile_source_to_obj(src, ny_obj, target));

    const char *objs[2] = { ny_obj, "bin/test_e2e_widths_host.o" };
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable_with_extra(objs, 2, exe_path, target, &diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&diags);

    int exit_code = ny_test_system("./bin/test_e2e_widths.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove("bin/test_e2e_widths_host.c");
    remove("bin/test_e2e_widths_host.o");
    remove(ny_obj);
    remove(exe_path);
}

void test_object_e2e_abi_fp_and_mixed(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_abi_fp_and_mixed: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    FILE *fc = fopen("bin/test_e2e_fp_host.c", "w");
    TEST_ASSERT(fc != nullptr);
    fputs(
        "#include <stdint.h>\n"
        "#include <math.h>\n"
        "float host_get_f32(void) { return 7.0f; }\n"
        "double host_get_f64(void) { return 20.0; }\n"
        "float host_add_f32(float a, float b) { return a + b; }\n"
        "double host_add_f64(double a, double b) { return a + b; }\n"
        "int32_t host_mixed(int32_t a, double b, int32_t c, float d) {\n"
        "    return a + (int32_t)b + c + (int32_t)d;\n"
        "}\n",
        fc
    );
    fclose(fc);

    int c_build = system("gcc -c bin/test_e2e_fp_host.c -o bin/test_e2e_fp_host.o");
    TEST_ASSERT_EQ(c_build, 0);

    const char *src =
        "@function host_get_f32() -> f32;\n"
        "@function host_get_f64() -> f64;\n"
        "@function host_add_f32(%a: f32, %b: f32) -> f32;\n"
        "@function host_add_f64(%a: f64, %b: f64) -> f64;\n"
        "@function host_mixed(%a: i32, %b: f64, %c: i32, %d: f32) -> i32;\n"
        "\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %v1 = const 10;\n"
        "    %v2 = call @host_get_f64;\n"
        "    %v3 = const 5;\n"
        "    %v4 = call @host_get_f32;\n"
        "    %res = call @host_mixed, %v1, %v2, %v3, %v4;\n"
        "    @return %res;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *ny_obj = "bin/test_e2e_fp.obj";
    const char *exe_path = "bin/test_e2e_fp.exe";

    TEST_ASSERT(compile_source_to_obj(src, ny_obj, target));

    const char *objs[2] = { ny_obj, "bin/test_e2e_fp_host.o" };
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable_with_extra(objs, 2, exe_path, target, &diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&diags);

    int exit_code = ny_test_system("./bin/test_e2e_fp.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove("bin/test_e2e_fp_host.c");
    remove("bin/test_e2e_fp_host.o");
    remove(ny_obj);
    remove(exe_path);
}

void test_target_unsupported_types(void) {
    const char *src_i128 =
        "@function test_i128(%x: i128) -> i128;\n"
        ".entry;\n"
        "    @return %x;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_unsupported");

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, src_i128, strlen(src_i128), &ctx.arena);
    TEST_ASSERT(ny_parse_module(&parser));
    ny_parser_destroy(&parser);

    Ny_Machine_Module mmod;
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool mir_ok = ny_ir_lower_to_mir(&ctx.module, &mmod, &diags);
    TEST_ASSERT(!mir_ok);
    TEST_ASSERT(diags.count > 0);
    TEST_ASSERT(strstr(diags.items[0].message, "does not support type 'i128'") != nullptr);

    ny_diagnostic_list_destroy(&diags);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
}

void test_object_e2e_aggregate_values(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_aggregate_values: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    FILE *fc = fopen("bin/test_e2e_agg_host.c", "w");
    TEST_ASSERT(fc != nullptr);
    fputs(
        "#include <stdint.h>\n"
        "typedef struct { int32_t x; int32_t y; } Point;\n"
        "typedef struct { int64_t a; int64_t b; int64_t c; } BigData;\n"
        "\n"
        "int32_t host_sum_point(Point *p) {\n"
        "    return p->x + p->y;\n"
        "}\n"
        "void host_fill_point(Point *p, int32_t x, int32_t y) {\n"
        "    p->x = x;\n"
        "    p->y = y;\n"
        "}\n"
        "int64_t host_sum_big(BigData *b) {\n"
        "    return b->a + b->b + b->c;\n"
        "}\n"
        "int32_t host_point_val(Point p) {\n"
        "    return p.x * 2 + p.y;\n"
        "}\n",
        fc
    );
    fclose(fc);

    int c_build = system("gcc -c bin/test_e2e_agg_host.c -o bin/test_e2e_agg_host.o");
    TEST_ASSERT_EQ(c_build, 0);

    const char *src =
        "@type Point = struct { x: i32, y: i32 };\n"
        "@type BigData = struct { a: i64, b: i64, c: i64 };\n"
        "\n"
        "@global @readonly @k_point: Point = 15;\n" /* k_point.x initialized with 15 */
        "\n"
        "@function host_sum_point(%p: ptr) -> i32;\n"
        "@function host_fill_point(%p: ptr, %x: i32, %y: i32) -> void;\n"
        "@function host_sum_big(%b: ptr) -> i64;\n"
        "@function host_point_val(%p: i64) -> i32;\n"
        "\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    %pt = stack_slot 8, 4;\n"
        "    %c20 = const 20;\n"
        "    %c7 = const 7;\n"
        "    call @host_fill_point, %pt, %c20, %c7;\n"
        "    %s1 = call @host_sum_point, %pt;\n" /* 27 */
        "\n"
        "    %big = stack_slot 24, 8;\n"
        "    %off_a = addr_offset %big, 0;\n"
        "    %c5 = const 5;\n"
        "    store %off_a, %c5;\n"
        "    %off_b = addr_offset %big, 8;\n"
        "    %c8 = const 8;\n"
        "    store %off_b, %c8;\n"
        "    %off_c = addr_offset %big, 16;\n"
        "    %c2 = const 2;\n"
        "    store %off_c, %c2;\n"
        "    %s2_64 = call @host_sum_big, %big;\n" /* 5 + 8 + 2 = 15 */
        "    %s2 = truncate %s2_64;\n"
        "\n"
        "    %p_k = global_addr @k_point;\n"
        "    %k_val = load %p_k;\n" /* 15 */
        "    %p_pt_val = load %pt;\n" /* Point loaded into 64-bit scalar: (7 << 32) | 20 */
        "    %pv_res = call @host_point_val, %p_pt_val;\n" /* 20 * 2 + 7 = 47 */
        "\n"
        "    %tmp1 = add %s1, %s2;\n" /* 27 + 15 = 42 */
        "    %tmp2 = add %tmp1, %k_val;\n" /* 42 + 15 = 57 */
        "    %tmp3 = add %tmp2, %pv_res;\n" /* 57 + 47 = 104 */
        "    %c62 = const 62;\n"
        "    %res = sub %tmp3, %c62;\n" /* 104 - 62 = 42 */
        "    @return %res;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *ny_obj = "bin/test_e2e_agg.obj";
    const char *exe_path = "bin/test_e2e_agg.exe";

    TEST_ASSERT(compile_source_to_obj(src, ny_obj, target));

    const char *objs[2] = { ny_obj, "bin/test_e2e_agg_host.o" };
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable_with_extra(objs, 2, exe_path, target, &diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&diags);

    int exit_code = ny_test_system("./bin/test_e2e_agg.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove("bin/test_e2e_agg_host.c");
    remove("bin/test_e2e_agg_host.o");
    remove(ny_obj);
    remove(exe_path);
}

void test_object_e2e_aggregate_abi(void) {
    if (!NY_X86_HOST) { printf("test_object_e2e_aggregate_abi: skipped (x86_64 host required)\n"); TEST_SKIP(0); }
    FILE *fc = fopen("bin/test_e2e_agg_abi_host.c", "w");
    TEST_ASSERT(fc != nullptr);
    fputs(
        "#include <stdint.h>\n"
        "#include <string.h>\n"
        "typedef struct { int32_t a; int32_t b; } Pair32;\n"
        "typedef struct { void *ptr; int32_t count; } Slice;\n"
        "typedef struct { int64_t w; int64_t x; int64_t y; int64_t z; } BigQuad;\n"
        "\n"
        "/* IR passes Pair32 as i64 (a in low 32, b in high 32) and returns i64 same way */\n"
        "int64_t host_add_pair(int64_t p_raw, int32_t delta) {\n"
        "    Pair32 p;\n"
        "    memcpy(&p, &p_raw, sizeof(p));\n"
        "    p.a += delta;\n"
        "    p.b += delta * 2;\n"
        "    int64_t result;\n"
        "    memcpy(&result, &p, sizeof(result));\n"
        "    return result;\n"
        "}\n"
        "\n"
        "/* IR passes Slice as ptr (pointer to Slice on stack) */\n"
        "int32_t host_sum_slice(Slice *s) {\n"
        "    int32_t *arr = (int32_t *)s->ptr;\n"
        "    int32_t sum = 0;\n"
        "    for (int32_t i = 0; i < s->count; i++) sum += arr[i];\n"
        "    return sum;\n"
        "}\n"
        "\n"
        "/* IR passes Float2 as i64 (x in low 32, y in high 32) */\n"
        "int32_t host_sum_float2(int64_t f_raw) {\n"
        "    float x, y;\n"
        "    memcpy(&x, &f_raw, sizeof(x));\n"
        "    memcpy(&y, (char *)&f_raw + 4, sizeof(y));\n"
        "    return (int32_t)(x + y);\n"
        "}\n"
        "\n"
        "/* IR passes sret ptr in 1st arg, b_in ptr in 2nd arg, factor in 3rd */\n"
        "void host_compute_big(BigQuad *sret, BigQuad *b, int64_t factor) {\n"
        "    sret->w = b->w * factor;\n"
        "    sret->x = b->x * factor;\n"
        "    sret->y = b->y * factor;\n"
        "    sret->z = b->z * factor;\n"
        "}\n"
        "int64_t host_sum_quad(BigQuad *b) {\n"
        "    return b->w + b->x + b->y + b->z;\n"
        "}\n",
        fc
    );
    fclose(fc);

    int c_build = system("gcc -c bin/test_e2e_agg_abi_host.c -o bin/test_e2e_agg_abi_host.o");
    TEST_ASSERT_EQ(c_build, 0);

    const char *src =
        "@type Pair32 = struct { a: i32, b: i32 };\n"
        "@type Slice = struct { ptr: ptr, count: i32 };\n"
        "@type Float2 = struct { x: f32, y: f32 };\n"
        "@type BigQuad = struct { w: i64, x: i64, y: i64, z: i64 };\n"
        "\n"
        "@function host_add_pair(%p: i64, %delta: i32) -> i64;\n"
        "@function host_sum_slice(%s_ptr: ptr) -> i32;\n"
        "@function host_sum_float2(%f: i64) -> i32;\n"
        "@function host_compute_big(%sret: ptr, %b_in: ptr, %factor: i64) -> void;\n"
        "@function host_sum_quad(%b: ptr) -> i64;\n"
        "\n"
        "@function ny_helper_nested(%p: ptr) -> i32;\n"
        ".entry;\n"
        "    %v = load %p;\n"
        "    %delta = const 5;\n"
        "    %ret64 = call @host_add_pair, %v, %delta;\n"
        "    store %p, %ret64;\n"
        "    %c0 = const 0;\n"
        "    @return %c0;\n"
        ";;\n"
        "\n"
        "@function main() -> i32;\n"
        ".entry;\n"
        "    /* Test 1: Pair32 */\n"
        "    %pair_slot = stack_slot 8, 4;\n"
        "    %p_a = addr_offset %pair_slot, 0;\n"
        "    %c10 = const 10;\n"
        "    store %p_a, %c10;\n"
        "    %p_b = addr_offset %pair_slot, 4;\n"
        "    %c20 = const 20;\n"
        "    store %p_b, %c20;\n"
        "\n"
        "    /* Nested call passing pointer to Pair32, which internally calls host_add_pair */\n"
        "    call @ny_helper_nested, %pair_slot;\n"
        "    /* now pair_slot.a = 15, pair_slot.b = 30 */\n"
        "    %a_new = load %p_a;\n"
        "    %b_new = load %p_b;\n"
        "    %p_sum = add %a_new, %b_new;\n" /* 15 + 30 = 45 */
        "\n"
        "    /* Test 2: Slice with small integer array */\n"
        "    %arr_slot = stack_slot 12, 4;\n" /* 3 x i32: [3, 4, 5] */
        "    %arr0 = addr_offset %arr_slot, 0;\n"
        "    %c3 = const 3;\n"
        "    store %arr0, %c3;\n"
        "    %arr1 = addr_offset %arr_slot, 4;\n"
        "    %c4 = const 4;\n"
        "    store %arr1, %c4;\n"
        "    %arr2 = addr_offset %arr_slot, 8;\n"
        "    %c5 = const 5;\n"
        "    store %arr2, %c5;\n"
        "    %slice_slot = stack_slot 16, 8;\n"
        "    %sl_ptr = addr_offset %slice_slot, 0;\n"
        "    store %sl_ptr, %arr_slot;\n"
        "    %sl_cnt = addr_offset %slice_slot, 8;\n"
        "    %cnt = const 3;\n"
        "    store %sl_cnt, %cnt;\n"
        "    %slice_sum = call @host_sum_slice, %slice_slot;\n" /* 3 + 4 + 5 = 12 */
        "\n"
        "    /* Test 3: Float2 */\n"
        "    %f_slot = stack_slot 8, 4;\n"
        "    %f_x = addr_offset %f_slot, 0;\n"
        "    %f_y = addr_offset %f_slot, 4;\n"
        "    %cf1 = const 0x40400000;\n" /* 3.0f in IEEE-754 */
        "    store %f_x, %cf1;\n"
        "    %cf2 = const 0x40800000;\n" /* 4.0f in IEEE-754 */
        "    store %f_y, %cf2;\n"
        "    %f_val = load %f_slot;\n"
        "    %f_int = call @host_sum_float2, %f_val;\n" /* 3.0 + 4.0 = 7 */
        "\n"
        "    /* Test 4: BigQuad memory pass & sret return */\n"
        "    %quad_in = stack_slot 32, 8;\n"
        "    %qw = addr_offset %quad_in, 0;\n"
        "    %c1 = const 1;\n"
        "    store %qw, %c1;\n"
        "    %qx = addr_offset %quad_in, 8;\n"
        "    %c2 = const 2;\n"
        "    store %qx, %c2;\n"
        "    %qy = addr_offset %quad_in, 16;\n"
        "    store %qy, %c3;\n"
        "    %qz = addr_offset %quad_in, 24;\n"
        "    store %qz, %c4;\n"
        "\n"
        "    %quad_out = stack_slot 32, 8;\n"
        "    %fac = const 2;\n"
        "    call @host_compute_big, %quad_out, %quad_in, %fac;\n"
        "    /* quad_out = { 2, 4, 6, 8 }, sum = 20 */\n"
        "    %quad_sum_64 = call @host_sum_quad, %quad_out;\n"
        "    %quad_sum = truncate %quad_sum_64;\n" /* 20 */
        "\n"
        "    /* Compute combined result to equal 42: */\n"
        "    /* p_sum = 45 */\n"
        "    /* slice_sum = 12 */\n"
        "    /* f_int = 7 */\n"
        "    /* quad_sum = 20 */\n"
        "    /* total = 45 + 12 + 7 + 20 = 84 */\n"
        "    %t1 = add %p_sum, %slice_sum;\n" /* 57 */
        "    %t2 = add %t1, %f_int;\n" /* 64 */
        "    %t3 = add %t2, %quad_sum;\n" /* 84 */
        "    %c42 = const 42;\n"
        "    %final_res = sub %t3, %c42;\n" /* 84 - 42 = 42 */
        "    @return %final_res;\n"
        ";;\n";

    const Ny_Target *target = ny_target_get_default();
    const char *ny_obj = "bin/test_e2e_agg_abi.obj";
    const char *exe_path = "bin/test_e2e_agg_abi.exe";

    TEST_ASSERT(compile_source_to_obj(src, ny_obj, target));

    const char *objs[2] = { ny_obj, "bin/test_e2e_agg_abi_host.o" };
    Ny_Diagnostic_List diags;
    ny_diagnostic_list_init(&diags);
    bool link_ok = ny_link_executable_with_extra(objs, 2, exe_path, target, &diags);
    TEST_ASSERT(link_ok);
    ny_diagnostic_list_destroy(&diags);

    int exit_code = ny_test_system("./bin/test_e2e_agg_abi.exe");
    TEST_ASSERT_EQ(exit_code, 42);

    remove("bin/test_e2e_agg_abi_host.c");
    remove("bin/test_e2e_agg_abi_host.o");
    remove(ny_obj);
    remove(exe_path);
}


