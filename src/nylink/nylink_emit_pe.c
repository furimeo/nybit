// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>

#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_FILE_EXECUTABLE_IMAGE 0x0002
#define IMAGE_FILE_LARGE_ADDRESS_AWARE 0x0020
#define IMAGE_FILE_DLL 0x2000

#define IMAGE_NT_OPTIONAL_HDR64_MAGIC 0x20b
#define IMAGE_SUBSYSTEM_WINDOWS_CUI 3

#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define IMAGE_SCN_MEM_DISCARDABLE 0x02000000
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

typedef struct Ar_Header {
    char ar_name[16];
    char ar_date[12];
    char ar_uid[6];
    char ar_gid[6];
    char ar_mode[8];
    char ar_size[10];
    char ar_fmag[2];
} Ar_Header;

typedef struct Import_Object_Header {
    uint16_t Sig1;           /* IMAGE_FILE_MACHINE_UNKNOWN (0) */
    uint16_t Sig2;           /* IMPORT_OBJECT_HDR_SIG2 (0xFFFF) */
    uint16_t Version;        /* 0 */
    uint16_t Machine;        /* IMAGE_FILE_MACHINE_AMD64 (0x8664) */
    uint32_t TimeDateStamp;  /* 0 */
    uint32_t SizeOfData;     /* Size of data following header */
    uint16_t OrdinalHint;    /* 0 */
    uint16_t TypeFlags;      /* Type: 2 bits, NameType: 3 bits */
} Import_Object_Header;
#pragma pack(pop)

static int uint32_cmp(const void *a, const void *b) {
    uint32_t ua = *(const uint32_t *)a;
    uint32_t ub = *(const uint32_t *)b;
    if (ua < ub) return -1;
    if (ua > ub) return 1;
    return 0;
}

static uint8_t *pe_build_base_relocs(Nylink_Context *ctx, size_t *out_size) {
    if (ctx->pe_base_reloc_count == 0) {
        *out_size = 0;
        return nullptr;
    }

    qsort(ctx->pe_base_relocs, ctx->pe_base_reloc_count, sizeof(uint32_t), uint32_cmp);

    /* Deduplicate */
    size_t unique_count = 0;
    for (size_t i = 0; i < ctx->pe_base_reloc_count; i++) {
        if (i == 0 || ctx->pe_base_relocs[i] != ctx->pe_base_relocs[i - 1]) {
            ctx->pe_base_relocs[unique_count++] = ctx->pe_base_relocs[i];
        }
    }
    ctx->pe_base_reloc_count = unique_count;

    /* Build blocks per 4KB page */
    size_t cap = 256;
    size_t size = 0;
    uint8_t *buf = (uint8_t *)ny_alloc_zero(cap);

    size_t i = 0;
    while (i < unique_count) {
        uint32_t page_rva = ctx->pe_base_relocs[i] & ~0xFFFU;
        size_t block_start = i;
        while (i < unique_count && (ctx->pe_base_relocs[i] & ~0xFFFU) == page_rva) {
            i++;
        }
        size_t entries_in_block = i - block_start;
        size_t entries_to_write = entries_in_block;
        if (entries_to_write % 2 != 0) {
            entries_to_write++; /* Pad to 4-byte boundary */
        }

        uint32_t block_size = (uint32_t)(8 + entries_to_write * 2);
        while (size + block_size > cap) {
            size_t ncap = cap * 2;
            buf = (uint8_t *)ny_realloc(buf, cap, ncap);
            memset(buf + cap, 0, ncap - cap);
            cap = ncap;
        }

        *(uint32_t *)(buf + size) = page_rva;
        *(uint32_t *)(buf + size + 4) = block_size;
        size += 8;

        for (size_t k = 0; k < entries_in_block; k++) {
            uint32_t rva = ctx->pe_base_relocs[block_start + k];
            uint16_t offset_in_page = (uint16_t)(rva & 0xFFFU);
            uint16_t entry = (uint16_t)((10 << 12) | offset_in_page); /* IMAGE_REL_BASED_DIR64 = 10 */
            *(uint16_t *)(buf + size) = entry;
            size += 2;
        }
        if (entries_to_write > entries_in_block) {
            *(uint16_t *)(buf + size) = 0; /* IMAGE_REL_BASED_ABSOLUTE padding */
            size += 2;
        }
    }

    *out_size = size;
    if (size < cap) {
        uint8_t *tight = (uint8_t *)ny_alloc_zero(size);
        memcpy(tight, buf, size);
        ny_free(buf, cap);
        return tight;
    }
    return buf;
}

bool nylink_write_pe_executable(Nylink_Context *ctx, const char *out_path, const Nylink_Config *cfg) {
    (void)cfg;
    if (!ctx || !out_path) return false;
    if (!ctx->is_laid_out || !ctx->relocations_applied) {
        nylink_diag_add(ctx, "emit error: layout and relocations must be performed before write", nullptr, nullptr);
        return false;
    }

    /* Build .reloc section if relocations exist */
    size_t reloc_sec_size = 0;
    uint8_t *reloc_sec_data = pe_build_base_relocs(ctx, &reloc_sec_size);

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        if (reloc_sec_data) ny_free(reloc_sec_data, reloc_sec_size);
        nylink_diag_add(ctx, "emit error: cannot open output file for writing", out_path, nullptr);
        return false;
    }

    uint16_t num_sections = 0;
    for (size_t i = 0; i < 4; i++) {
        if (ctx->out_sections[i].mem_size > 0 || ctx->out_sections[i].file_size > 0) {
            num_sections++;
        }
    }
    if (reloc_sec_size > 0) {
        num_sections++;
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
    if (ctx->output_mode == NYLINK_OUTPUT_DLL) {
        file_hdr.Characteristics |= IMAGE_FILE_DLL;
    }

    const Nylink_Output_Section *sec_text = &ctx->out_sections[0];
    const Nylink_Output_Section *sec_rodata = &ctx->out_sections[1];
    const Nylink_Output_Section *sec_data = &ctx->out_sections[2];
    const Nylink_Output_Section *sec_bss = &ctx->out_sections[3];

    uint32_t entry_rva = (ctx->entry_point_va != 0) ? (uint32_t)(ctx->entry_point_va - ctx->image_base) : 0;
    uint32_t code_base_rva = (sec_text->va != 0) ? (uint32_t)(sec_text->va - ctx->image_base) : 0x1000;

    /* Compute SizeOfImage: highest section RVA + aligned VirtualSize */
    uint32_t size_of_image = 0x1000;
    for (size_t i = 0; i < 4; i++) {
        if (ctx->out_sections[i].mem_size > 0 || ctx->out_sections[i].file_size > 0) {
            uint32_t sec_rva = (uint32_t)(ctx->out_sections[i].va - ctx->image_base);
            size_t eff_mem = ctx->out_sections[i].mem_size > ctx->out_sections[i].file_size ? ctx->out_sections[i].mem_size : ctx->out_sections[i].file_size;
            uint32_t aligned_mem = (uint32_t)((eff_mem + 0xFFF) & ~0xFFFULL);
            if (sec_rva + aligned_mem > size_of_image) {
                size_of_image = sec_rva + aligned_mem;
            }
        }
    }

    uint32_t reloc_rva = size_of_image;
    uint32_t reloc_file_offset = (uint32_t)ctx->total_file_size;
    if (reloc_sec_size > 0) {
        uint32_t aligned_reloc_mem = (uint32_t)((reloc_sec_size + 0xFFF) & ~0xFFFULL);
        size_of_image += aligned_reloc_mem;
        ctx->pe_reloc_va = ctx->image_base + reloc_rva;
        ctx->pe_reloc_size = (uint32_t)reloc_sec_size;
    }

    /* Optional Header 64 */
    Pe_Optional_Header64 opt_hdr;
    memset(&opt_hdr, 0, sizeof(opt_hdr));
    opt_hdr.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    opt_hdr.MajorLinkerVersion = 1;
    opt_hdr.MinorLinkerVersion = 0;
    opt_hdr.SizeOfCode = (uint32_t)sec_text->file_size;
    opt_hdr.SizeOfInitializedData = (uint32_t)(sec_rodata->file_size + sec_data->file_size + (reloc_sec_size > 0 ? reloc_sec_size : 0));
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
    opt_hdr.DllCharacteristics = 0x8160; /* HIGH_ENTROPY_VA | DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE */
    if (reloc_sec_size == 0) {
        /* No base relocations: image is permanently fixed at its preferred
           ImageBase. Clear DYNAMIC_BASE so the loader does not attempt
           relocation without a .reloc table. DLLs use a different default
           ImageBase (0x180000000) from EXEs (0x140000000) to avoid conflict. */
        opt_hdr.DllCharacteristics &= ~0x0040;
    }
    opt_hdr.SizeOfStackReserve = 0x100000;
    opt_hdr.SizeOfStackCommit = 0x1000;
    opt_hdr.SizeOfHeapReserve = 0x100000;
    opt_hdr.SizeOfHeapCommit = 0x1000;
    opt_hdr.NumberOfRvaAndSizes = 16;

    /* Data Directories */
    if (ctx->pe_export_va != 0 && ctx->pe_export_size > 0) {
        opt_hdr.DataDirectory[0].VirtualAddress = (uint32_t)(ctx->pe_export_va - ctx->image_base);
        opt_hdr.DataDirectory[0].Size = ctx->pe_export_size;
    }
    if (ctx->pe_import_va != 0 && ctx->pe_import_size > 0) {
        opt_hdr.DataDirectory[1].VirtualAddress = (uint32_t)(ctx->pe_import_va - ctx->image_base);
        opt_hdr.DataDirectory[1].Size = ctx->pe_import_size;
    }
    if (reloc_sec_size > 0) {
        opt_hdr.DataDirectory[5].VirtualAddress = reloc_rva;
        opt_hdr.DataDirectory[5].Size = (uint32_t)reloc_sec_size;
    }
    if (ctx->pe_iat_va != 0 && ctx->pe_iat_size > 0) {
        opt_hdr.DataDirectory[12].VirtualAddress = (uint32_t)(ctx->pe_iat_va - ctx->image_base);
        opt_hdr.DataDirectory[12].Size = ctx->pe_iat_size;
    }

    /* Section Headers */
    Pe_Section_Header shdrs[5];
    memset(shdrs, 0, sizeof(shdrs));
    size_t active_idx = 0;

    for (size_t i = 0; i < 4; i++) {
        const Nylink_Output_Section *s = &ctx->out_sections[i];
        if (s->mem_size == 0 && s->file_size == 0) continue;

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

        size_t eff_mem = s->mem_size > s->file_size ? s->mem_size : s->file_size;
        sh->VirtualSize = (uint32_t)eff_mem;
        sh->VirtualAddress = (uint32_t)(s->va - ctx->image_base);
        sh->SizeOfRawData = (uint32_t)((s->file_size + 0x1FF) & ~0x1FFULL);
        sh->PointerToRawData = (s->kind == NYLINK_SEC_BSS) ? 0 : (uint32_t)s->file_offset;
    }

    if (reloc_sec_size > 0) {
        Pe_Section_Header *sh = &shdrs[active_idx++];
        memcpy(sh->Name, ".reloc\0\0", 8);
        sh->Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_DISCARDABLE | IMAGE_SCN_MEM_READ;
        sh->VirtualSize = (uint32_t)reloc_sec_size;
        sh->VirtualAddress = reloc_rva;
        sh->SizeOfRawData = (uint32_t)((reloc_sec_size + 0x1FF) & ~0x1FFULL);
        sh->PointerToRawData = reloc_file_offset;
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

    /* Write .reloc section if present */
    if (reloc_sec_size > 0 && reloc_sec_data) {
        cur_pos = ftell(f);
        if ((uint64_t)reloc_file_offset > (uint64_t)cur_pos) {
            size_t pad = (size_t)((uint64_t)reloc_file_offset - (uint64_t)cur_pos);
            uint8_t zeros[512] = {0};
            while (pad > 0) {
                size_t chunk = pad > sizeof(zeros) ? sizeof(zeros) : pad;
                fwrite(zeros, 1, chunk, f);
                pad -= chunk;
            }
        }

        fwrite(reloc_sec_data, 1, reloc_sec_size, f);

        uint32_t aligned_reloc_raw = (uint32_t)((reloc_sec_size + 0x1FF) & ~0x1FFULL);
        if (aligned_reloc_raw > reloc_sec_size) {
            size_t raw_pad = (size_t)(aligned_reloc_raw - reloc_sec_size);
            uint8_t zeros[512] = {0};
            fwrite(zeros, 1, raw_pad, f);
        }
    }

    if (reloc_sec_data) {
        ny_free(reloc_sec_data, reloc_sec_size);
    }

    fclose(f);
    return true;
}

static void format_ar_hdr(Ar_Header *hdr, const char *name, size_t size) {
    memset(hdr, ' ', sizeof(Ar_Header));
    size_t nlen = strlen(name);
    if (nlen > 16) nlen = 16;
    memcpy(hdr->ar_name, name, nlen);
    memcpy(hdr->ar_date, "0           ", 12);
    memcpy(hdr->ar_uid, "0     ", 6);
    memcpy(hdr->ar_gid, "0     ", 6);
    memcpy(hdr->ar_mode, "0       ", 8);

    char size_buf[16];
    snprintf(size_buf, sizeof(size_buf), "%-10zu", size);
    memcpy(hdr->ar_size, size_buf, 10);
    hdr->ar_fmag[0] = '`';
    hdr->ar_fmag[1] = '\n';
}

bool nylink_write_pe_implib(Nylink_Context *ctx, const char *out_lib_path, const char *dll_name) {
    if (!ctx || !out_lib_path || !dll_name) return false;

    FILE *f = fopen(out_lib_path, "wb");
    if (!f) {
        nylink_diag_add(ctx, "implib error: cannot open import library file for writing", out_lib_path, nullptr);
        return false;
    }

    fwrite("!<arch>\n", 1, 8, f);

    /* Collect symbols to export into implib:
       For each symbol 'foo', we write two symbols in the 1st linker member:
       'foo' and '__imp_foo', both pointing to member i */
    size_t num_exports = ctx->pe_export_count;

    /* Build 1st Linker Member */
    /* Symbol count in index = num_exports * 2 */
    uint32_t num_idx_syms = (uint32_t)(num_exports * 2);

    /* Calculate strings size */
    size_t str_tab_size = 0;
    for (size_t e = 0; e < num_exports; e++) {
        const char *sname = ctx->pe_exports[e];
        str_tab_size += strlen(sname) + 1;           /* foo\0 */
        str_tab_size += strlen("__imp_") + strlen(sname) + 1; /* __imp_foo\0 */
    }

    size_t first_member_data_size = 4 + (size_t)num_idx_syms * 4 + str_tab_size;

    /* We need to pre-compute member offsets:
       Offset 0: !<arch>\n (8 bytes)
       Offset of 1st member: 8
       1st member total: 60 + first_member_data_size + (pad if odd)
     */
    size_t cur_file_offset = 8 + 60 + first_member_data_size;
    if (first_member_data_size % 2 != 0) cur_file_offset++;

    /* Allocate member offsets array */
    uint32_t *member_offsets = (uint32_t *)ny_alloc_zero(num_exports * sizeof(uint32_t));
    for (size_t e = 0; e < num_exports; e++) {
        member_offsets[e] = (uint32_t)cur_file_offset;
        const char *sname = ctx->pe_exports[e];
        size_t mdata_size = sizeof(Import_Object_Header) + strlen(sname) + 1 + strlen(dll_name) + 1;
        cur_file_offset += 60 + mdata_size;
        if (mdata_size % 2 != 0) cur_file_offset++;
    }

    /* Write 1st linker member header */
    Ar_Header first_hdr;
    format_ar_hdr(&first_hdr, "/", first_member_data_size);
    fwrite(&first_hdr, sizeof(first_hdr), 1, f);

    /* Write num_syms (big-endian) */
    uint8_t num_syms_be[4];
    num_syms_be[0] = (uint8_t)((num_idx_syms >> 24) & 0xFF);
    num_syms_be[1] = (uint8_t)((num_idx_syms >> 16) & 0xFF);
    num_syms_be[2] = (uint8_t)((num_idx_syms >> 8) & 0xFF);
    num_syms_be[3] = (uint8_t)(num_idx_syms & 0xFF);
    fwrite(num_syms_be, 1, 4, f);

    /* Write offsets (big-endian) */
    for (size_t e = 0; e < num_exports; e++) {
        uint32_t off = member_offsets[e];
        uint8_t off_be[4];
        off_be[0] = (uint8_t)((off >> 24) & 0xFF);
        off_be[1] = (uint8_t)((off >> 16) & 0xFF);
        off_be[2] = (uint8_t)((off >> 8) & 0xFF);
        off_be[3] = (uint8_t)(off & 0xFF);
        fwrite(off_be, 1, 4, f); /* for foo */
        fwrite(off_be, 1, 4, f); /* for __imp_foo */
    }

    /* Write strings */
    for (size_t e = 0; e < num_exports; e++) {
        const char *sname = ctx->pe_exports[e];
        fwrite(sname, 1, strlen(sname) + 1, f);
        fwrite("__imp_", 1, 6, f);
        fwrite(sname, 1, strlen(sname) + 1, f);
    }

    if (first_member_data_size % 2 != 0) {
        fputc('\n', f);
    }

    ny_free(member_offsets, num_exports * sizeof(uint32_t));

    /* Write short-format import member for each export */
    for (size_t e = 0; e < num_exports; e++) {
        const char *sname = ctx->pe_exports[e];
        const Nylink_Symbol *sym = nylink_find_symbol(ctx, sname);
        bool is_func = sym ? sym->is_function : true;

        size_t sname_len = strlen(sname);
        size_t dname_len = strlen(dll_name);
        size_t data_size = sizeof(Import_Object_Header) + sname_len + 1 + dname_len + 1;

        Ar_Header mhdr;
        format_ar_hdr(&mhdr, dll_name, data_size);
        fwrite(&mhdr, sizeof(mhdr), 1, f);

        Import_Object_Header ioh;
        memset(&ioh, 0, sizeof(ioh));
        ioh.Sig1 = 0;
        ioh.Sig2 = 0xFFFF;
        ioh.Version = 0;
        ioh.Machine = IMAGE_FILE_MACHINE_AMD64;
        ioh.TimeDateStamp = 0;
        ioh.SizeOfData = (uint32_t)(sname_len + 1 + dname_len + 1);
        ioh.OrdinalHint = (uint16_t)(e + 1);
        /* Type: 0 = IMPORT_CODE, 1 = IMPORT_DATA. NameType: 0 = IMPORT_NAME */
        uint16_t itype = is_func ? 0 : 1;
        ioh.TypeFlags = itype;

        fwrite(&ioh, sizeof(ioh), 1, f);
        fwrite(sname, 1, sname_len + 1, f);
        fwrite(dll_name, 1, dname_len + 1, f);

        if (data_size % 2 != 0) {
            fputc('\n', f);
        }
    }

    fclose(f);
    return true;
}
