// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>

#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_EXECUTABLE_IMAGE 0x0002
#define IMAGE_FILE_LARGE_ADDRESS_AWARE 0x0020

#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b
#define IMAGE_SUBSYSTEM_WINDOWS_CUI 3

#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000

#pragma pack(push, 1)
typedef struct Pe_Dos_Header {
    uint16_t e_magic;    /* "MZ" (0x5A4D) */
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
    uint32_t e_lfanew;   /* Offset to PE signature */
} Pe_Dos_Header;

typedef struct Pe_File_Header {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
} Pe_File_Header;

typedef struct Pe_Data_Directory {
    uint32_t VirtualAddress;
    uint32_t Size;
} Pe_Data_Directory;

typedef struct Pe_Optional_Header64 {
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
    Pe_Data_Directory DataDirectory[16];
} Pe_Optional_Header64;

typedef struct Pe_Section_Header {
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
} Pe_Section_Header;
#pragma pack(pop)

bool nylink_write_pe_executable(Nylink_Context *ctx, const char *out_path, const Nylink_Config *cfg) {
    (void)cfg;
    if (!ctx || !out_path) return false;
    if (!ctx->is_laid_out || !ctx->relocations_applied) {
        nylink_diag_add(ctx, "emit error: layout and relocations must be performed before write", nullptr, nullptr);
        return false;
    }

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        nylink_diag_add(ctx, "emit error: cannot open output file for writing", out_path, nullptr);
        return false;
    }

    uint16_t num_sections = 0;
    for (size_t i = 0; i < 4; i++) {
        if (ctx->out_sections[i].mem_size > 0) {
            num_sections++;
        }
    }

    /* DOS header */
    Pe_Dos_Header dos_hdr;
    memset(&dos_hdr, 0, sizeof(dos_hdr));
    dos_hdr.e_magic = 0x5A4D; /* 'MZ' */
    dos_hdr.e_cblp = 0x0090;
    dos_hdr.e_cp = 0x0003;
    dos_hdr.e_cparhdr = 0x0004;
    dos_hdr.e_maxalloc = 0xFFFF;
    dos_hdr.e_sp = 0x00B8;
    dos_hdr.e_lfarlc = 0x0040;
    dos_hdr.e_lfanew = 0x80; /* PE header at offset 0x80 (128) */

    /* PE signature */
    uint32_t pe_sig = 0x00004550; /* 'PE\0\0' */

    /* COFF File Header: TimeDateStamp = 0 for 100% deterministic binary builds */
    Pe_File_Header file_hdr;
    memset(&file_hdr, 0, sizeof(file_hdr));
    file_hdr.Machine = IMAGE_FILE_MACHINE_AMD64;
    file_hdr.NumberOfSections = num_sections;
    file_hdr.TimeDateStamp = 0;
    file_hdr.SizeOfOptionalHeader = sizeof(Pe_Optional_Header64);
    file_hdr.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_LARGE_ADDRESS_AWARE;

    const Nylink_Output_Section *sec_text = &ctx->out_sections[0];
    const Nylink_Output_Section *sec_rodata = &ctx->out_sections[1];
    const Nylink_Output_Section *sec_data = &ctx->out_sections[2];
    const Nylink_Output_Section *sec_bss = &ctx->out_sections[3];

    uint32_t entry_rva = (uint32_t)(ctx->entry_point_va - ctx->image_base);
    uint32_t code_base_rva = (uint32_t)(sec_text->va - ctx->image_base);

    /* Compute SizeOfImage: highest section RVA + aligned VirtualSize */
    uint32_t size_of_image = 0x1000;
    for (size_t i = 0; i < 4; i++) {
        if (ctx->out_sections[i].mem_size > 0) {
            uint32_t sec_rva = (uint32_t)(ctx->out_sections[i].va - ctx->image_base);
            uint32_t aligned_mem = (uint32_t)((ctx->out_sections[i].mem_size + 0xFFF) & ~0xFFFULL);
            if (sec_rva + aligned_mem > size_of_image) {
                size_of_image = sec_rva + aligned_mem;
            }
        }
    }

    /* Optional Header 64 */
    Pe_Optional_Header64 opt_hdr;
    memset(&opt_hdr, 0, sizeof(opt_hdr));
    opt_hdr.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    opt_hdr.MajorLinkerVersion = 1;
    opt_hdr.MinorLinkerVersion = 0;
    opt_hdr.SizeOfCode = (uint32_t)sec_text->file_size;
    opt_hdr.SizeOfInitializedData = (uint32_t)(sec_rodata->file_size + sec_data->file_size);
    opt_hdr.SizeOfUninitializedData = (uint32_t)sec_bss->mem_size;
    opt_hdr.AddressOfEntryPoint = entry_rva;
    opt_hdr.BaseOfCode = code_base_rva;
    opt_hdr.ImageBase = ctx->image_base;
    opt_hdr.SectionAlignment = 0x1000;
    opt_hdr.FileAlignment = 0x200;
    opt_hdr.MajorOperatingSystemVersion = 6;
    opt_hdr.MinorOperatingSystemVersion = 0;
    opt_hdr.MajorSubsystemVersion = 6;
    opt_hdr.MinorSubsystemVersion = 0;
    opt_hdr.SizeOfImage = size_of_image;
    opt_hdr.SizeOfHeaders = 0x400;
    opt_hdr.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    opt_hdr.SizeOfStackReserve = 0x100000;
    opt_hdr.SizeOfStackCommit = 0x1000;
    opt_hdr.SizeOfHeapReserve = 0x100000;
    opt_hdr.SizeOfHeapCommit = 0x1000;
    opt_hdr.NumberOfRvaAndSizes = 16;

    /* Section Headers */
    Pe_Section_Header shdrs[4];
    memset(shdrs, 0, sizeof(shdrs));
    size_t active_idx = 0;

    for (size_t i = 0; i < 4; i++) {
        const Nylink_Output_Section *s = &ctx->out_sections[i];
        if (s->mem_size == 0) continue;

        Pe_Section_Header *sh = &shdrs[active_idx++];
        if (s->kind == NYLINK_SEC_TEXT) {
            memcpy(sh->Name, ".text\0\0\0", 8);
            sh->Characteristics = IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ;
        } else if (s->kind == NYLINK_SEC_RODATA) {
            memcpy(sh->Name, ".rdata\0\0", 8);
            sh->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
        } else if (s->kind == NYLINK_SEC_DATA) {
            memcpy(sh->Name, ".data\0\0\0", 8);
            sh->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
        } else if (s->kind == NYLINK_SEC_BSS) {
            memcpy(sh->Name, ".bss\0\0\0\0", 8);
            sh->Characteristics = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;
        }

        sh->VirtualSize = (uint32_t)s->mem_size;
        sh->VirtualAddress = (uint32_t)(s->va - ctx->image_base);
        sh->SizeOfRawData = (uint32_t)((s->file_size + 0x1FF) & ~0x1FFULL);
        sh->PointerToRawData = (s->kind == NYLINK_SEC_BSS) ? 0 : (uint32_t)s->file_offset;
    }

    /* Write DOS header */
    fwrite(&dos_hdr, sizeof(dos_hdr), 1, f);

    /* Pad to e_lfanew (0x80) */
    long cur_pos = ftell(f);
    if (0x80 > cur_pos) {
        uint8_t stub[128] = {0};
        fwrite(stub, 1, 0x80 - cur_pos, f);
    }

    /* Write PE signature, COFF header, Optional header, Section headers */
    fwrite(&pe_sig, 4, 1, f);
    fwrite(&file_hdr, sizeof(file_hdr), 1, f);
    fwrite(&opt_hdr, sizeof(opt_hdr), 1, f);
    fwrite(shdrs, sizeof(Pe_Section_Header), num_sections, f);

    /* Pad headers to SizeOfHeaders (0x400) */
    cur_pos = ftell(f);
    if (0x400 > cur_pos) {
        size_t pad = (size_t)(0x400 - cur_pos);
        uint8_t zeros[1024] = {0};
        fwrite(zeros, 1, pad, f);
    }

    /* Write section data */
    for (size_t i = 0; i < 4; i++) {
        const Nylink_Output_Section *s = &ctx->out_sections[i];
        if (s->kind == NYLINK_SEC_BSS || s->file_size == 0 || !s->data) continue;

        cur_pos = ftell(f);
        if (s->file_offset > (uint64_t)cur_pos) {
            size_t pad = (size_t)(s->file_offset - (uint64_t)cur_pos);
            uint8_t zeros[512] = {0};
            while (pad > 0) {
                size_t chunk = pad > sizeof(zeros) ? sizeof(zeros) : pad;
                fwrite(zeros, 1, chunk, f);
                pad -= chunk;
            }
        }

        fwrite(s->data, 1, s->file_size, f);

        /* Pad raw data to file alignment (0x200) */
        uint32_t aligned_raw = (uint32_t)((s->file_size + 0x1FF) & ~0x1FFULL);
        if (aligned_raw > s->file_size) {
            size_t raw_pad = (size_t)(aligned_raw - s->file_size);
            uint8_t zeros[512] = {0};
            fwrite(zeros, 1, raw_pad, f);
        }
    }

    fclose(f);
    return true;
}
