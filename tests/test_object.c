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

void test_object_e2e_link_executable(void) {
    const char *src =
        "@function nybit_compute() -> i32;\n"
        ".entry;\n"
        "    %a = const 20;\n"
        "    %b = const 22;\n"
        "    %res = add %a, %b;\n"
        "    @return %res;\n"
        ";;\n";

    Ny_Context ctx;
    ny_context_init(&ctx, "test_e2e_obj");

    Ny_Parser parser;
    ny_parser_init(&parser, &ctx.module, src, strlen(src), &ctx.arena);
    TEST_ASSERT(ny_parse_module(&parser));

    Ny_Diagnostic_List val_diags;
    ny_diagnostic_list_init(&val_diags);
    TEST_ASSERT(ny_validate_module(&ctx.module, &val_diags));
    ny_diagnostic_list_destroy(&val_diags);

    ny_opt_run_module_pipeline(&ctx.module, NY_OPT_O1);

    const Ny_Target *target = ny_target_get_default();
    Ny_Machine_Module mmod;
    Ny_Diagnostic_List mir_diags;
    ny_diagnostic_list_init(&mir_diags);
    TEST_ASSERT(ny_ir_lower_to_mir(&ctx.module, &mmod, &mir_diags));
    ny_diagnostic_list_destroy(&mir_diags);

    for (size_t f = 0; f < mmod.function_count; f++) {
        Ny_Diagnostic_List ra_diags;
        ny_diagnostic_list_init(&ra_diags);
        TEST_ASSERT(ny_regalloc_run(&mmod.functions[f], target->abi, nullptr, &ra_diags));
        TEST_ASSERT(ny_mfunc_validate_allocated(&mmod.functions[f], &ra_diags));
        ny_diagnostic_list_destroy(&ra_diags);
    }

    X86_Module xmod;
    Ny_Diagnostic_List x86_diags;
    ny_diagnostic_list_init(&x86_diags);
    TEST_ASSERT(x86_lower_machine_mod(target, &mmod, &xmod, &x86_diags));
    ny_diagnostic_list_destroy(&x86_diags);

    X86_Encoded_Module emod;
    Ny_Diagnostic_List enc_diags;
    ny_diagnostic_list_init(&enc_diags);
    TEST_ASSERT(x86_encode_module(&emod, &xmod, &enc_diags));
    ny_diagnostic_list_destroy(&enc_diags);

    Ny_Object_Buffer obj_buf;
    ny_obj_buf_init(&obj_buf);
    Ny_Diagnostic_List obj_diags;
    ny_diagnostic_list_init(&obj_diags);
    TEST_ASSERT(ny_emit_object_module(&obj_buf, target, &emod, &obj_diags));
    ny_diagnostic_list_destroy(&obj_diags);

#if defined(_WIN32)
    FILE *fobj = fopen("bin/test_e2e_part.obj", "wb");
    TEST_ASSERT(fobj != nullptr);
    fwrite(obj_buf.bytes, 1, obj_buf.count, fobj);
    fclose(fobj);

    FILE *fc = fopen("bin/test_e2e_driver.c", "w");
    TEST_ASSERT(fc != nullptr);
    fputs("extern int nybit_compute(void);\nint main(void) { return nybit_compute() == 42 ? 0 : 1; }\n", fc);
    fclose(fc);

    int link_res = system("gcc bin/test_e2e_driver.c bin/test_e2e_part.obj -o bin/test_e2e_run.exe");
    TEST_ASSERT_EQ(link_res, 0);

    int run_res = system("bin\\test_e2e_run.exe");
    TEST_ASSERT_EQ(run_res, 0);

    remove("bin/test_e2e_driver.c");
    remove("bin/test_e2e_part.obj");
    remove("bin/test_e2e_run.exe");
#endif

    ny_obj_buf_destroy(&obj_buf);
    x86_encoded_mod_destroy(&emod);
    x86_mod_destroy(&xmod);
    ny_mmod_destroy(&mmod);
    ny_context_destroy(&ctx);
}
