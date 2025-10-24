// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/object.h"
#include <string.h>

#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_ALIGN_16BYTES 0x00500000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000

#define IMAGE_SYM_CLASS_EXTERNAL 2
#define IMAGE_SYM_DTYPE_FUNCTION 0x20

#define IMAGE_REL_AMD64_REL32 4

#pragma pack(push, 1)
typedef struct Coff_File_Header {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} Coff_File_Header;

typedef struct Coff_Section_Header {
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
} Coff_Section_Header;

typedef struct Coff_Relocation {
    uint32_t VirtualAddress;
    uint32_t SymbolTableIndex;
    uint16_t Type;
} Coff_Relocation;

typedef struct Coff_Symbol {
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
} Coff_Symbol;
#pragma pack(pop)

static void coff_encode_name(Coff_Symbol *sym, Ny_Object_Buffer *strtab, Ny_String name) {
    if (name.len <= 8) {
        memset(sym->N.ShortName, 0, 8);
        memcpy(sym->N.ShortName, name.data, name.len);
    } else {
        uint32_t str_off = (uint32_t)strtab->count;
        ny_obj_buf_append_bytes(strtab, name.data, name.len);
        ny_obj_buf_append_byte(strtab, 0);
        sym->N.LongName.Zeros = 0;
        sym->N.LongName.Offset = str_off;
    }
}

bool ny_emit_coff_x86_64(Ny_Object_Buffer *out_buf, const X86_Encoded_Module *emod, Ny_Diagnostic_List *diags) {
    (void)diags;

    Ny_Object_Buffer symtab_buf;
    ny_obj_buf_init(&symtab_buf);

    Ny_Object_Buffer strtab_buf;
    ny_obj_buf_init(&strtab_buf);
    uint32_t dummy_size = 4;
    ny_obj_buf_append_bytes(&strtab_buf, &dummy_size, 4);

    Ny_Object_Buffer reloc_buf;
    ny_obj_buf_init(&reloc_buf);

    int16_t text_sec_num = 1;
    for (size_t i = 0; i < emod->function_count; i++) {
        const X86_Function_Code *fn = &emod->functions[i];
        Coff_Symbol sym = {0};
        coff_encode_name(&sym, &strtab_buf, fn->name);
        sym.Value = (uint32_t)fn->offset;
        sym.SectionNumber = text_sec_num;
        sym.Type = IMAGE_SYM_DTYPE_FUNCTION;
        sym.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
        sym.NumberOfAuxSymbols = 0;
        ny_obj_buf_append_bytes(&symtab_buf, &sym, sizeof(sym));
    }

    size_t reloc_count = emod->text_section.reloc_count;
    for (size_t r = 0; r < reloc_count; r++) {
        const X86_Relocation *reloc = &emod->text_section.relocs[r];
        uint32_t sym_idx = 0xFFFFFFFF;
        size_t total_syms = symtab_buf.count / sizeof(Coff_Symbol);
        const Coff_Symbol *syms = (const Coff_Symbol *)symtab_buf.bytes;

        for (size_t s = 0; s < total_syms; s++) {
            if (syms[s].N.LongName.Zeros == 0) {
                const char *sname = (const char *)(strtab_buf.bytes + syms[s].N.LongName.Offset);
                if (ny_str_eq(reloc->symbol_name, (Ny_String){.data = sname, .len = strlen(sname)})) {
                    sym_idx = (uint32_t)s;
                    break;
                }
            } else {
                size_t slen = strnlen((const char *)syms[s].N.ShortName, 8);
                if (ny_str_eq(reloc->symbol_name, (Ny_String){.data = (const char *)syms[s].N.ShortName, .len = slen})) {
                    sym_idx = (uint32_t)s;
                    break;
                }
            }
        }

        if (sym_idx == 0xFFFFFFFF) {
            Coff_Symbol sym = {0};
            coff_encode_name(&sym, &strtab_buf, reloc->symbol_name);
            sym.Value = 0;
            sym.SectionNumber = 0;
            sym.Type = 0;
            sym.StorageClass = IMAGE_SYM_CLASS_EXTERNAL;
            sym.NumberOfAuxSymbols = 0;
            sym_idx = (uint32_t)(symtab_buf.count / sizeof(Coff_Symbol));
            ny_obj_buf_append_bytes(&symtab_buf, &sym, sizeof(sym));
        }

        Coff_Relocation creloc = {
            .VirtualAddress = (uint32_t)reloc->code_offset,
            .SymbolTableIndex = sym_idx,
            .Type = IMAGE_REL_AMD64_REL32,
        };
        ny_obj_buf_append_bytes(&reloc_buf, &creloc, sizeof(creloc));
    }

    uint32_t final_strtab_size = (uint32_t)strtab_buf.count;
    memcpy(strtab_buf.bytes, &final_strtab_size, 4);

    Coff_File_Header fhdr = {
        .Machine = IMAGE_FILE_MACHINE_AMD64,
        .NumberOfSections = 1,
        .TimeDateStamp = 0,
        .PointerToSymbolTable = 0,
        .NumberOfSymbols = (uint32_t)(symtab_buf.count / sizeof(Coff_Symbol)),
        .SizeOfOptionalHeader = 0,
        .Characteristics = 0,
    };
    ny_obj_buf_append_bytes(out_buf, &fhdr, sizeof(fhdr));

    Coff_Section_Header shdr = {
        .Name = {'.', 't', 'e', 'x', 't', 0, 0, 0},
        .VirtualSize = 0,
        .VirtualAddress = 0,
        .SizeOfRawData = (uint32_t)emod->text_section.count,
        .PointerToRawData = 0,
        .PointerToRelocations = 0,
        .PointerToLinenumbers = 0,
        .NumberOfRelocations = (uint16_t)reloc_count,
        .NumberOfLinenumbers = 0,
        .Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES,
    };
    size_t shdr_file_offset = out_buf->count;
    ny_obj_buf_append_bytes(out_buf, &shdr, sizeof(shdr));

    ny_obj_buf_align_to(out_buf, 16);
    uint32_t raw_data_ptr = (uint32_t)out_buf->count;
    if (emod->text_section.count > 0) {
        ny_obj_buf_append_bytes(out_buf, emod->text_section.bytes, emod->text_section.count);
    }

    uint32_t reloc_ptr = 0;
    if (reloc_count > 0) {
        ny_obj_buf_align_to(out_buf, 4);
        reloc_ptr = (uint32_t)out_buf->count;
        ny_obj_buf_append_bytes(out_buf, reloc_buf.bytes, reloc_buf.count);
    }

    ny_obj_buf_align_to(out_buf, 4);
    uint32_t symtab_ptr = (uint32_t)out_buf->count;
    ny_obj_buf_append_bytes(out_buf, symtab_buf.bytes, symtab_buf.count);

    if (final_strtab_size > 4) {
        ny_obj_buf_append_bytes(out_buf, strtab_buf.bytes, strtab_buf.count);
    } else {
        uint32_t empty_str_sz = 4;
        ny_obj_buf_append_bytes(out_buf, &empty_str_sz, 4);
    }

    Coff_File_Header *patch_fhdr = (Coff_File_Header *)out_buf->bytes;
    patch_fhdr->PointerToSymbolTable = symtab_ptr;

    Coff_Section_Header *patch_shdr = (Coff_Section_Header *)(out_buf->bytes + shdr_file_offset);
    patch_shdr->PointerToRawData = raw_data_ptr;
    patch_shdr->PointerToRelocations = reloc_ptr;

    ny_obj_buf_destroy(&symtab_buf);
    ny_obj_buf_destroy(&strtab_buf);
    ny_obj_buf_destroy(&reloc_buf);

    return true;
}
