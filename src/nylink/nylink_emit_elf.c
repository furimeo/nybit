// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ET_EXEC 2
#define EM_X86_64 62

#define PT_LOAD 1
#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_NOBITS 8
#define SHT_STRTAB 3

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

#pragma pack(push, 1)
typedef struct Elf64_Ehdr {
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
} Elf64_Ehdr;

typedef struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

typedef struct Elf64_Shdr {
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
} Elf64_Shdr;
#pragma pack(pop)

bool nylink_write_elf_executable(Nylink_Context *ctx, const char *out_path, const Nylink_Config *cfg) {
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

    Elf64_Phdr phdrs[3];
    memset(phdrs, 0, sizeof(phdrs));
    uint16_t phnum = 0;

    const Nylink_Output_Section *sec_text = &ctx->out_sections[0];
    const Nylink_Output_Section *sec_rodata = &ctx->out_sections[1];
    const Nylink_Output_Section *sec_data = &ctx->out_sections[2];
    const Nylink_Output_Section *sec_bss = &ctx->out_sections[3];

    /* Code segment (includes headers and .text) */
    phdrs[phnum].p_type = PT_LOAD;
    phdrs[phnum].p_flags = PF_R | PF_X;
    phdrs[phnum].p_offset = 0;
    phdrs[phnum].p_vaddr = ctx->image_base;
    phdrs[phnum].p_paddr = ctx->image_base;
    uint64_t text_end_off = (sec_text->file_offset > 0) ? (sec_text->file_offset + sec_text->file_size) : 0x1000;
    phdrs[phnum].p_filesz = text_end_off;
    phdrs[phnum].p_memsz = text_end_off;
    phdrs[phnum].p_align = 0x1000;
    phnum++;

    /* Read-only data segment (only emitted if populated) */
    if (sec_rodata->mem_size > 0) {
        phdrs[phnum].p_type = PT_LOAD;
        phdrs[phnum].p_flags = PF_R;
        phdrs[phnum].p_offset = sec_rodata->file_offset;
        phdrs[phnum].p_vaddr = sec_rodata->va;
        phdrs[phnum].p_paddr = sec_rodata->va;
        phdrs[phnum].p_filesz = sec_rodata->file_size;
        phdrs[phnum].p_memsz = sec_rodata->mem_size;
        phdrs[phnum].p_align = 0x1000;
        phnum++;
    }

    /* Writable data / bss segment (only emitted if populated) */
    if (sec_data->mem_size > 0 || sec_bss->mem_size > 0) {
        uint64_t start_va = (sec_data->mem_size > 0) ? sec_data->va : sec_bss->va;
        uint64_t file_off = (sec_data->mem_size > 0) ? sec_data->file_offset : 0;
        uint64_t file_sz = sec_data->file_size;
        uint64_t mem_sz = sec_data->mem_size + sec_bss->mem_size;

        phdrs[phnum].p_type = PT_LOAD;
        phdrs[phnum].p_flags = PF_R | PF_W;
        phdrs[phnum].p_offset = file_off;
        phdrs[phnum].p_vaddr = start_va;
        phdrs[phnum].p_paddr = start_va;
        phdrs[phnum].p_filesz = file_sz;
        phdrs[phnum].p_memsz = mem_sz;
        phdrs[phnum].p_align = 0x1000;
        phnum++;
    }

    /* Build shstrtab */
    char shstrtab[256];
    memset(shstrtab, 0, sizeof(shstrtab));
    size_t shstrtab_len = 1;

    Elf64_Shdr shdrs[6];
    memset(shdrs, 0, sizeof(shdrs));
    uint16_t shnum = 1; /* index 0 is always SHT_NULL */

    /* .text */
    uint32_t name_text = (uint32_t)shstrtab_len;
    strcpy(shstrtab + shstrtab_len, ".text");
    shstrtab_len += strlen(".text") + 1;

    shdrs[shnum].sh_name = name_text;
    shdrs[shnum].sh_type = SHT_PROGBITS;
    shdrs[shnum].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[shnum].sh_addr = sec_text->va;
    shdrs[shnum].sh_offset = sec_text->file_offset;
    shdrs[shnum].sh_size = sec_text->file_size;
    shdrs[shnum].sh_addralign = sec_text->align;
    shnum++;

    /* .rodata */
    if (sec_rodata->mem_size > 0) {
        uint32_t name_rodata = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".rodata");
        shstrtab_len += strlen(".rodata") + 1;

        shdrs[shnum].sh_name = name_rodata;
        shdrs[shnum].sh_type = SHT_PROGBITS;
        shdrs[shnum].sh_flags = SHF_ALLOC;
        shdrs[shnum].sh_addr = sec_rodata->va;
        shdrs[shnum].sh_offset = sec_rodata->file_offset;
        shdrs[shnum].sh_size = sec_rodata->file_size;
        shdrs[shnum].sh_addralign = sec_rodata->align;
        shnum++;
    }

    /* .data */
    if (sec_data->mem_size > 0) {
        uint32_t name_data = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".data");
        shstrtab_len += strlen(".data") + 1;

        shdrs[shnum].sh_name = name_data;
        shdrs[shnum].sh_type = SHT_PROGBITS;
        shdrs[shnum].sh_flags = SHF_ALLOC | SHF_WRITE;
        shdrs[shnum].sh_addr = sec_data->va;
        shdrs[shnum].sh_offset = sec_data->file_offset;
        shdrs[shnum].sh_size = sec_data->file_size;
        shdrs[shnum].sh_addralign = sec_data->align;
        shnum++;
    }

    /* .bss */
    if (sec_bss->mem_size > 0) {
        uint32_t name_bss = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".bss");
        shstrtab_len += strlen(".bss") + 1;

        shdrs[shnum].sh_name = name_bss;
        shdrs[shnum].sh_type = SHT_NOBITS;
        shdrs[shnum].sh_flags = SHF_ALLOC | SHF_WRITE;
        shdrs[shnum].sh_addr = sec_bss->va;
        shdrs[shnum].sh_offset = sec_bss->file_offset;
        shdrs[shnum].sh_size = sec_bss->mem_size;
        shdrs[shnum].sh_addralign = sec_bss->align;
        shnum++;
    }

    /* .shstrtab */
    uint32_t name_shstrtab = (uint32_t)shstrtab_len;
    strcpy(shstrtab + shstrtab_len, ".shstrtab");
    shstrtab_len += strlen(".shstrtab") + 1;

    uint16_t shstrtab_idx = shnum;
    uint64_t shstrtab_offset = ctx->total_file_size;
    shdrs[shnum].sh_name = name_shstrtab;
    shdrs[shnum].sh_type = SHT_STRTAB;
    shdrs[shnum].sh_flags = 0;
    shdrs[shnum].sh_addr = 0;
    shdrs[shnum].sh_offset = shstrtab_offset;
    shdrs[shnum].sh_size = shstrtab_len;
    shdrs[shnum].sh_addralign = 1;
    shnum++;

    uint64_t shoff = (shstrtab_offset + shstrtab_len + 7) & ~7ULL;

    Elf64_Ehdr ehdr;
    memset(&ehdr, 0, sizeof(ehdr));
    ehdr.e_ident[0] = ELF_MAGIC_0;
    ehdr.e_ident[1] = ELF_MAGIC_1;
    ehdr.e_ident[2] = ELF_MAGIC_2;
    ehdr.e_ident[3] = ELF_MAGIC_3;
    ehdr.e_ident[4] = ELFCLASS64;
    ehdr.e_ident[5] = ELFDATA2LSB;
    ehdr.e_ident[6] = EV_CURRENT;
    ehdr.e_type = ET_EXEC;
    ehdr.e_machine = EM_X86_64;
    ehdr.e_version = EV_CURRENT;
    ehdr.e_entry = ctx->entry_point_va;
    ehdr.e_phoff = sizeof(Elf64_Ehdr);
    ehdr.e_shoff = shoff;
    ehdr.e_ehsize = sizeof(Elf64_Ehdr);
    ehdr.e_phentsize = sizeof(Elf64_Phdr);
    ehdr.e_phnum = phnum;
    ehdr.e_shentsize = sizeof(Elf64_Shdr);
    ehdr.e_shnum = shnum;
    ehdr.e_shstrndx = shstrtab_idx;

    /* Write ELF Header and Program Headers */
    fwrite(&ehdr, sizeof(ehdr), 1, f);
    fwrite(phdrs, sizeof(Elf64_Phdr), phnum, f);

    /* Pad to .text file offset */
    long cur_pos = ftell(f);
    if (sec_text->file_offset > (uint64_t)cur_pos) {
        size_t pad = (size_t)(sec_text->file_offset - (uint64_t)cur_pos);
        uint8_t zeros[4096];
        memset(zeros, 0, sizeof(zeros));
        while (pad > 0) {
            size_t chunk = pad > sizeof(zeros) ? sizeof(zeros) : pad;
            fwrite(zeros, 1, chunk, f);
            pad -= chunk;
        }
    }

    /* Write .text */
    if (sec_text->file_size > 0 && sec_text->data) {
        fwrite(sec_text->data, 1, sec_text->file_size, f);
    }

    /* Write .rodata */
    if (sec_rodata->file_size > 0 && sec_rodata->data) {
        cur_pos = ftell(f);
        if (sec_rodata->file_offset > (uint64_t)cur_pos) {
            size_t pad = (size_t)(sec_rodata->file_offset - (uint64_t)cur_pos);
            uint8_t zeros[512] = {0};
            while (pad > 0) {
                size_t chunk = pad > sizeof(zeros) ? sizeof(zeros) : pad;
                fwrite(zeros, 1, chunk, f);
                pad -= chunk;
            }
        }
        fwrite(sec_rodata->data, 1, sec_rodata->file_size, f);
    }

    /* Write .data */
    if (sec_data->file_size > 0 && sec_data->data) {
        cur_pos = ftell(f);
        if (sec_data->file_offset > (uint64_t)cur_pos) {
            size_t pad = (size_t)(sec_data->file_offset - (uint64_t)cur_pos);
            uint8_t zeros[512] = {0};
            while (pad > 0) {
                size_t chunk = pad > sizeof(zeros) ? sizeof(zeros) : pad;
                fwrite(zeros, 1, chunk, f);
                pad -= chunk;
            }
        }
        fwrite(sec_data->data, 1, sec_data->file_size, f);
    }

    /* Pad to shstrtab */
    cur_pos = ftell(f);
    if (shstrtab_offset > (uint64_t)cur_pos) {
        size_t pad = (size_t)(shstrtab_offset - (uint64_t)cur_pos);
        uint8_t zeros[512] = {0};
        while (pad > 0) {
            size_t chunk = pad > sizeof(zeros) ? sizeof(zeros) : pad;
            fwrite(zeros, 1, chunk, f);
            pad -= chunk;
        }
    }
    fwrite(shstrtab, 1, shstrtab_len, f);

    /* Pad to shoff */
    cur_pos = ftell(f);
    if (shoff > (uint64_t)cur_pos) {
        size_t pad = (size_t)(shoff - (uint64_t)cur_pos);
        uint8_t zeros[64] = {0};
        fwrite(zeros, 1, pad, f);
    }

    /* Write Section Headers */
    fwrite(shdrs, sizeof(Elf64_Shdr), shnum, f);

    fclose(f);
    return true;
}
