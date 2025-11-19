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
#define ET_DYN 3
#define EM_X86_64 62

#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_GNU_STACK 0x6474e551
#define PT_GNU_RELRO 0x6474e552

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_HASH 5
#define SHT_DYNAMIC 6
#define SHT_NOBITS 8
#define SHT_DYNSYM 11

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_PLTGOT 3
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_RELAENT 9
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_SONAME 14
#define DT_PLTREL 20
#define DT_JMPREL 23

#define STB_GLOBAL 1
#define STB_WEAK 2

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2

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

typedef struct Elf64_Sym {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct Elf64_Dyn {
    int64_t d_tag;
    uint64_t d_val;
} Elf64_Dyn;
#pragma pack(pop)

static void pad_file_to(FILE *f, uint64_t target_off) {
    long cur = ftell(f);
    if (target_off > (uint64_t)cur) {
        size_t pad = (size_t)(target_off - (uint64_t)cur);
        uint8_t zeros[512] = {0};
        while (pad > 0) {
            size_t chunk = pad > sizeof(zeros) ? sizeof(zeros) : pad;
            fwrite(zeros, 1, chunk, f);
            pad -= chunk;
        }
    } else if (target_off < (uint64_t)cur) {
        fseek(f, (long)target_off, SEEK_SET);
    }
}

static uint32_t dynstr_offset_of(const Nylink_Context *ctx, const char *name) {
    if (!name || !ctx->dynstr_data) return 0;
    size_t len = strlen(name);
    for (size_t p = 1; p + len < ctx->dynstr_size; p++) {
        if (ctx->dynstr_data[p - 1] == '\0' &&
            memcmp(ctx->dynstr_data + p, name, len) == 0 &&
            ctx->dynstr_data[p + len] == '\0') {
            return (uint32_t)p;
        }
    }
    return 0;
}

bool nylink_write_elf_executable(Nylink_Context *ctx, const char *out_path, const Nylink_Config *cfg) {
    (void)cfg;
    if (!ctx || !out_path) return false;
    if (!ctx->is_laid_out || !ctx->relocations_applied) {
        nylink_diag_add(ctx, "emit error: layout and relocations must be performed before write", nullptr, nullptr);
        return false;
    }

    bool dyn = ctx->uses_dynamic;

    FILE *f = fopen(out_path, "wb");
    if (!f) {
        nylink_diag_add(ctx, "emit error: cannot open output file for writing", out_path, nullptr);
        return false;
    }

    const Nylink_Output_Section *sec_text = &ctx->out_sections[0];
    const Nylink_Output_Section *sec_rodata = &ctx->out_sections[1];
    const Nylink_Output_Section *sec_data = &ctx->out_sections[2];
    const Nylink_Output_Section *sec_bss = &ctx->out_sections[3];

    Elf64_Phdr phdrs[16];
    memset(phdrs, 0, sizeof(phdrs));
    uint16_t phnum = 0;

    if (ctx->interp_file_size > 0) {
        phdrs[phnum].p_type = PT_INTERP;
        phdrs[phnum].p_flags = PF_R;
        phdrs[phnum].p_offset = ctx->interp_file_offset;
        phdrs[phnum].p_vaddr = ctx->interp_va;
        phdrs[phnum].p_paddr = ctx->interp_va;
        phdrs[phnum].p_filesz = ctx->interp_file_size;
        phdrs[phnum].p_memsz = ctx->interp_file_size;
        phdrs[phnum].p_align = 1;
        phnum++;
    }

    uint64_t rx_end = 0x1000;
    if (sec_text->file_size > 0) {
        rx_end = sec_text->file_offset + sec_text->file_size;
    }
    if (dyn) {
        uint64_t ends[] = {
            ctx->interp_file_size > 0 ? ctx->interp_file_offset + ctx->interp_file_size : 0,
            ctx->hash_file_size > 0 ? ctx->hash_file_offset + ctx->hash_file_size : 0,
            ctx->dynsym_file_size > 0 ? ctx->dynsym_file_offset + ctx->dynsym_file_size : 0,
            ctx->dynstr_file_size > 0 ? ctx->dynstr_file_offset + ctx->dynstr_file_size : 0,
            ctx->rela_dyn_file_size > 0 ? ctx->rela_dyn_file_offset + ctx->rela_dyn_file_size : 0,
            ctx->rela_plt_file_size > 0 ? ctx->rela_plt_file_offset + ctx->rela_plt_file_size : 0,
            ctx->plt_file_size > 0 ? ctx->plt_file_offset + ctx->plt_file_size : 0,
        };
        for (size_t e = 0; e < sizeof(ends) / sizeof(ends[0]); e++) {
            if (ends[e] > rx_end) rx_end = ends[e];
        }
    }

    phdrs[phnum].p_type = PT_LOAD;
    phdrs[phnum].p_flags = PF_R | PF_X;
    phdrs[phnum].p_offset = 0;
    phdrs[phnum].p_vaddr = ctx->image_base;
    phdrs[phnum].p_paddr = ctx->image_base;
    phdrs[phnum].p_filesz = rx_end;
    phdrs[phnum].p_memsz = rx_end;
    phdrs[phnum].p_align = 0x1000;
    phnum++;

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

    uint64_t rw_start_va = 0;
    uint64_t rw_file_off = 0;
    uint64_t rw_file_end = 0;
    bool has_rw = false;

    if (sec_data->mem_size > 0) {
        has_rw = true;
        rw_start_va = sec_data->va;
        rw_file_off = sec_data->file_offset;
        rw_file_end = sec_data->file_offset + sec_data->file_size;
    } else if (dyn) {
        has_rw = true;
        rw_start_va = ctx->got_va;
        rw_file_off = ctx->got_file_offset;
        rw_file_end = ctx->got_file_offset + ctx->got_file_size;
    }

    if (dyn) {
        uint64_t dyn_end_off = ctx->dynamic_file_offset + ctx->dynamic_file_size;
        if (dyn_end_off > rw_file_end) rw_file_end = dyn_end_off;
        if (ctx->got_plt_file_size > 0) {
            uint64_t gotplt_end_off = ctx->got_plt_file_offset + ctx->got_plt_file_size;
            if (gotplt_end_off > rw_file_end) rw_file_end = gotplt_end_off;
        }
    }

    uint64_t rw_mem_end_va = 0;
    if (has_rw) {
        rw_mem_end_va = ctx->image_base ? ctx->image_base + rw_file_end : rw_file_end;
        if (sec_bss->mem_size > 0 && sec_bss->va + sec_bss->mem_size > rw_mem_end_va) {
            rw_mem_end_va = sec_bss->va + sec_bss->mem_size;
        }
    } else if (sec_bss->mem_size > 0) {
        has_rw = true;
        rw_start_va = sec_bss->va;
        rw_file_off = 0;
        rw_file_end = 0;
        rw_mem_end_va = sec_bss->va + sec_bss->mem_size;
    }

    if (has_rw) {
        phdrs[phnum].p_type = PT_LOAD;
        phdrs[phnum].p_flags = PF_R | PF_W;
        phdrs[phnum].p_offset = rw_file_off;
        phdrs[phnum].p_vaddr = rw_start_va;
        phdrs[phnum].p_paddr = rw_start_va;
        phdrs[phnum].p_filesz = rw_file_end - rw_file_off;
        phdrs[phnum].p_memsz = rw_mem_end_va - rw_start_va;
        phdrs[phnum].p_align = 0x1000;
        phnum++;
    }

    if (dyn) {
        phdrs[phnum].p_type = PT_DYNAMIC;
        phdrs[phnum].p_flags = PF_R | PF_W;
        phdrs[phnum].p_offset = ctx->dynamic_file_offset;
        phdrs[phnum].p_vaddr = ctx->dynamic_va;
        phdrs[phnum].p_paddr = ctx->dynamic_va;
        phdrs[phnum].p_filesz = ctx->dynamic_file_size;
        phdrs[phnum].p_memsz = ctx->dynamic_file_size;
        phdrs[phnum].p_align = 8;
        phnum++;

        uint64_t relro_start_va = ctx->got_file_size > 0 ? ctx->got_va : ctx->dynamic_va;
        uint64_t relro_file_off = ctx->got_file_size > 0 ? ctx->got_file_offset : ctx->dynamic_file_offset;
        uint64_t relro_sz = (ctx->dynamic_va + ctx->dynamic_file_size) - relro_start_va;
        phdrs[phnum].p_type = PT_GNU_RELRO;
        phdrs[phnum].p_flags = PF_R;
        phdrs[phnum].p_offset = relro_file_off;
        phdrs[phnum].p_vaddr = relro_start_va;
        phdrs[phnum].p_paddr = relro_start_va;
        phdrs[phnum].p_filesz = relro_sz;
        phdrs[phnum].p_memsz = relro_sz;
        phdrs[phnum].p_align = 1;
        phnum++;

        phdrs[phnum].p_type = PT_GNU_STACK;
        phdrs[phnum].p_flags = PF_R | PF_W;
        phdrs[phnum].p_offset = 0;
        phdrs[phnum].p_vaddr = 0;
        phdrs[phnum].p_paddr = 0;
        phdrs[phnum].p_filesz = 0;
        phdrs[phnum].p_memsz = 0;
        phdrs[phnum].p_align = 0x10;
        phnum++;
    }

    char shstrtab[1024];
    memset(shstrtab, 0, sizeof(shstrtab));
    size_t shstrtab_len = 1;

    Elf64_Shdr shdrs[24];
    memset(shdrs, 0, sizeof(shdrs));
    uint16_t shnum = 1;

    uint16_t dynsym_shndx = 0;
    uint16_t dynstr_shndx = 0;
    uint16_t hash_shndx = 0;

    if (dyn) {
        if (ctx->interp_file_size > 0) {
            uint32_t n = (uint32_t)shstrtab_len;
            strcpy(shstrtab + shstrtab_len, ".interp");
            shstrtab_len += strlen(".interp") + 1;
            shdrs[shnum].sh_name = n;
            shdrs[shnum].sh_type = SHT_PROGBITS;
            shdrs[shnum].sh_flags = SHF_ALLOC;
            shdrs[shnum].sh_addr = ctx->interp_va;
            shdrs[shnum].sh_offset = ctx->interp_file_offset;
            shdrs[shnum].sh_size = ctx->interp_file_size;
            shdrs[shnum].sh_addralign = 1;
            shnum++;
        }

        if (ctx->hash_file_size > 0) {
            hash_shndx = shnum;
            uint32_t n = (uint32_t)shstrtab_len;
            strcpy(shstrtab + shstrtab_len, ".hash");
            shstrtab_len += strlen(".hash") + 1;
            shdrs[shnum].sh_name = n;
            shdrs[shnum].sh_type = SHT_HASH;
            shdrs[shnum].sh_flags = SHF_ALLOC;
            shdrs[shnum].sh_addr = ctx->hash_va;
            shdrs[shnum].sh_offset = ctx->hash_file_offset;
            shdrs[shnum].sh_size = ctx->hash_file_size;
            shdrs[shnum].sh_entsize = 4;
            shdrs[shnum].sh_addralign = 8;
            shnum++;
        }

        dynsym_shndx = shnum;
        uint32_t n = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".dynsym");
        shstrtab_len += strlen(".dynsym") + 1;
        shdrs[shnum].sh_name = n;
        shdrs[shnum].sh_type = SHT_DYNSYM;
        shdrs[shnum].sh_flags = SHF_ALLOC;
        shdrs[shnum].sh_addr = ctx->dynsym_va;
        shdrs[shnum].sh_offset = ctx->dynsym_file_offset;
        shdrs[shnum].sh_size = ctx->dynsym_file_size;
        shdrs[shnum].sh_entsize = sizeof(Elf64_Sym);
        shdrs[shnum].sh_addralign = 8;
        shnum++;

        dynstr_shndx = shnum;
        n = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".dynstr");
        shstrtab_len += strlen(".dynstr") + 1;
        shdrs[shnum].sh_name = n;
        shdrs[shnum].sh_type = SHT_STRTAB;
        shdrs[shnum].sh_flags = SHF_ALLOC;
        shdrs[shnum].sh_addr = ctx->dynstr_va;
        shdrs[shnum].sh_offset = ctx->dynstr_file_offset;
        shdrs[shnum].sh_size = ctx->dynstr_file_size;
        shdrs[shnum].sh_addralign = 1;
        shnum++;

        shdrs[dynsym_shndx].sh_link = dynstr_shndx;
        shdrs[dynsym_shndx].sh_info = 1;
        if (hash_shndx != 0) {
            shdrs[hash_shndx].sh_link = dynsym_shndx;
        }

        if (ctx->rela_dyn_file_size > 0) {
            uint32_t rn = (uint32_t)shstrtab_len;
            strcpy(shstrtab + shstrtab_len, ".rela.dyn");
            shstrtab_len += strlen(".rela.dyn") + 1;
            shdrs[shnum].sh_name = rn;
            shdrs[shnum].sh_type = SHT_RELA;
            shdrs[shnum].sh_flags = SHF_ALLOC;
            shdrs[shnum].sh_addr = ctx->rela_dyn_va;
            shdrs[shnum].sh_offset = ctx->rela_dyn_file_offset;
            shdrs[shnum].sh_size = ctx->rela_dyn_file_size;
            shdrs[shnum].sh_entsize = 24;
            shdrs[shnum].sh_link = dynsym_shndx;
            shdrs[shnum].sh_addralign = 8;
            shnum++;
        }

        if (ctx->rela_plt_file_size > 0) {
            uint32_t rn = (uint32_t)shstrtab_len;
            strcpy(shstrtab + shstrtab_len, ".rela.plt");
            shstrtab_len += strlen(".rela.plt") + 1;
            shdrs[shnum].sh_name = rn;
            shdrs[shnum].sh_type = SHT_RELA;
            shdrs[shnum].sh_flags = SHF_ALLOC;
            shdrs[shnum].sh_addr = ctx->rela_plt_va;
            shdrs[shnum].sh_offset = ctx->rela_plt_file_offset;
            shdrs[shnum].sh_size = ctx->rela_plt_file_size;
            shdrs[shnum].sh_entsize = 24;
            shdrs[shnum].sh_link = dynsym_shndx;
            shdrs[shnum].sh_addralign = 8;
            shnum++;
        }

        if (ctx->plt_file_size > 0) {
            uint32_t pn = (uint32_t)shstrtab_len;
            strcpy(shstrtab + shstrtab_len, ".plt");
            shstrtab_len += strlen(".plt") + 1;
            shdrs[shnum].sh_name = pn;
            shdrs[shnum].sh_type = SHT_PROGBITS;
            shdrs[shnum].sh_flags = SHF_ALLOC | SHF_EXECINSTR;
            shdrs[shnum].sh_addr = ctx->plt_va;
            shdrs[shnum].sh_offset = ctx->plt_file_offset;
            shdrs[shnum].sh_size = ctx->plt_file_size;
            shdrs[shnum].sh_entsize = 16;
            shdrs[shnum].sh_addralign = 16;
            shnum++;
        }
    }

    uint16_t text_shndx = shnum;
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

    uint16_t rodata_shndx = 0;
    if (sec_rodata->mem_size > 0) {
        rodata_shndx = shnum;
        uint32_t n = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".rodata");
        shstrtab_len += strlen(".rodata") + 1;
        shdrs[shnum].sh_name = n;
        shdrs[shnum].sh_type = SHT_PROGBITS;
        shdrs[shnum].sh_flags = SHF_ALLOC;
        shdrs[shnum].sh_addr = sec_rodata->va;
        shdrs[shnum].sh_offset = sec_rodata->file_offset;
        shdrs[shnum].sh_size = sec_rodata->file_size;
        shdrs[shnum].sh_addralign = sec_rodata->align;
        shnum++;
    }

    uint16_t data_shndx = 0;
    if (sec_data->mem_size > 0) {
        data_shndx = shnum;
        uint32_t n = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".data");
        shstrtab_len += strlen(".data") + 1;
        shdrs[shnum].sh_name = n;
        shdrs[shnum].sh_type = SHT_PROGBITS;
        shdrs[shnum].sh_flags = SHF_ALLOC | SHF_WRITE;
        shdrs[shnum].sh_addr = sec_data->va;
        shdrs[shnum].sh_offset = sec_data->file_offset;
        shdrs[shnum].sh_size = sec_data->file_size;
        shdrs[shnum].sh_addralign = sec_data->align;
        shnum++;
    }

    uint16_t bss_shndx = 0;
    if (sec_bss->mem_size > 0) {
        bss_shndx = shnum;
        uint32_t n = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".bss");
        shstrtab_len += strlen(".bss") + 1;
        shdrs[shnum].sh_name = n;
        shdrs[shnum].sh_type = SHT_NOBITS;
        shdrs[shnum].sh_flags = SHF_ALLOC | SHF_WRITE;
        shdrs[shnum].sh_addr = sec_bss->va;
        shdrs[shnum].sh_offset = sec_bss->file_offset;
        shdrs[shnum].sh_size = sec_bss->mem_size;
        shdrs[shnum].sh_addralign = sec_bss->align;
        shnum++;
    }

    if (dyn) {
        if (ctx->got_file_size > 0) {
            uint32_t n = (uint32_t)shstrtab_len;
            strcpy(shstrtab + shstrtab_len, ".got");
            shstrtab_len += strlen(".got") + 1;
            shdrs[shnum].sh_name = n;
            shdrs[shnum].sh_type = SHT_PROGBITS;
            shdrs[shnum].sh_flags = SHF_ALLOC | SHF_WRITE;
            shdrs[shnum].sh_addr = ctx->got_va;
            shdrs[shnum].sh_offset = ctx->got_file_offset;
            shdrs[shnum].sh_size = ctx->got_file_size;
            shdrs[shnum].sh_entsize = 8;
            shdrs[shnum].sh_addralign = 8;
            shnum++;
        }

        if (ctx->got_plt_file_size > 0) {
            uint32_t n = (uint32_t)shstrtab_len;
            strcpy(shstrtab + shstrtab_len, ".got.plt");
            shstrtab_len += strlen(".got.plt") + 1;
            shdrs[shnum].sh_name = n;
            shdrs[shnum].sh_type = SHT_PROGBITS;
            shdrs[shnum].sh_flags = SHF_ALLOC | SHF_WRITE;
            shdrs[shnum].sh_addr = ctx->got_plt_va;
            shdrs[shnum].sh_offset = ctx->got_plt_file_offset;
            shdrs[shnum].sh_size = ctx->got_plt_file_size;
            shdrs[shnum].sh_entsize = 8;
            shdrs[shnum].sh_addralign = 8;
            shnum++;
        }

        uint32_t n = (uint32_t)shstrtab_len;
        strcpy(shstrtab + shstrtab_len, ".dynamic");
        shstrtab_len += strlen(".dynamic") + 1;
        shdrs[shnum].sh_name = n;
        shdrs[shnum].sh_type = SHT_DYNAMIC;
        shdrs[shnum].sh_flags = SHF_ALLOC | SHF_WRITE;
        shdrs[shnum].sh_addr = ctx->dynamic_va;
        shdrs[shnum].sh_offset = ctx->dynamic_file_offset;
        shdrs[shnum].sh_size = ctx->dynamic_file_size;
        shdrs[shnum].sh_entsize = sizeof(Elf64_Dyn);
        shdrs[shnum].sh_link = dynstr_shndx;
        shdrs[shnum].sh_addralign = 8;
        shnum++;
    }

    uint32_t name_shstrtab = (uint32_t)shstrtab_len;
    strcpy(shstrtab + shstrtab_len, ".shstrtab");
    shstrtab_len += strlen(".shstrtab") + 1;

    uint16_t shstrtab_idx = shnum;
    uint64_t shstrtab_offset = ctx->total_file_size;
    shdrs[shnum].sh_name = name_shstrtab;
    shdrs[shnum].sh_type = SHT_STRTAB;
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
    ehdr.e_type = (ctx->output_mode == NYLINK_OUTPUT_PIE || ctx->output_mode == NYLINK_OUTPUT_SHARED) ? ET_DYN : ET_EXEC;
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

    fwrite(&ehdr, sizeof(ehdr), 1, f);
    fwrite(phdrs, sizeof(Elf64_Phdr), phnum, f);

    if (dyn) {
        if (ctx->interp_file_size > 0 && ctx->dynamic_linker) {
            pad_file_to(f, ctx->interp_file_offset);
            fwrite(ctx->dynamic_linker, 1, ctx->interp_file_size, f);
        }

        if (ctx->hash_file_size > 0 && ctx->hash_data) {
            pad_file_to(f, ctx->hash_file_offset);
            fwrite(ctx->hash_data, 1, ctx->hash_file_size, f);
        }

        Elf64_Sym *dynsym_entries = (Elf64_Sym *)ny_alloc_zero(ctx->dynsym_count * sizeof(Elf64_Sym));
        for (size_t d = 1; d < ctx->dynsym_count; d++) {
            uint32_t s_id = ctx->dynsym_sym_ids[d];
            const Nylink_Symbol *sym = &ctx->symbols[s_id];

            dynsym_entries[d].st_name = dynstr_offset_of(ctx, sym->name);

            uint8_t bind = (sym->binding == NYLINK_SYM_WEAK) ? STB_WEAK : STB_GLOBAL;
            uint8_t type = STT_NOTYPE;
            if (sym->is_function) type = STT_FUNC;
            else if (sym->is_object) type = STT_OBJECT;
            dynsym_entries[d].st_info = (uint8_t)((bind << 4) | (type & 0xF));
            dynsym_entries[d].st_other = sym->visibility;

            if (sym->is_defined && !sym->is_dynamic && sym->sec_id < ctx->section_count) {
                dynsym_entries[d].st_value = ctx->resolved_symbols[s_id].final_va;
                dynsym_entries[d].st_size = sym->size;
                uint32_t out_idx = ctx->sec_layouts[sym->sec_id].out_sec_idx;
                if (out_idx == 0) dynsym_entries[d].st_shndx = text_shndx;
                else if (out_idx == 1) dynsym_entries[d].st_shndx = rodata_shndx;
                else if (out_idx == 2) dynsym_entries[d].st_shndx = data_shndx;
                else if (out_idx == 3) dynsym_entries[d].st_shndx = bss_shndx;
            } else {
                dynsym_entries[d].st_shndx = 0;
                dynsym_entries[d].st_value = 0;
                dynsym_entries[d].st_size = 0;
            }
        }

        pad_file_to(f, ctx->dynsym_file_offset);
        fwrite(dynsym_entries, sizeof(Elf64_Sym), ctx->dynsym_count, f);
        ny_free(dynsym_entries, ctx->dynsym_count * sizeof(Elf64_Sym));

        pad_file_to(f, ctx->dynstr_file_offset);
        fwrite(ctx->dynstr_data, 1, ctx->dynstr_size, f);

        if (ctx->rela_dyn_file_size > 0 && ctx->rela_dyn_data) {
            pad_file_to(f, ctx->rela_dyn_file_offset);
            fwrite(ctx->rela_dyn_data, 1, ctx->rela_dyn_file_size, f);
        }

        if (ctx->rela_plt_file_size > 0 && ctx->rela_plt_data) {
            pad_file_to(f, ctx->rela_plt_file_offset);
            fwrite(ctx->rela_plt_data, 1, ctx->rela_plt_file_size, f);
        }

        if (ctx->plt_file_size > 0 && ctx->plt_data) {
            pad_file_to(f, ctx->plt_file_offset);
            fwrite(ctx->plt_data, 1, ctx->plt_file_size, f);
        }
    }

    if (sec_text->file_size > 0 && sec_text->data) {
        pad_file_to(f, sec_text->file_offset);
        fwrite(sec_text->data, 1, sec_text->file_size, f);
    }

    if (sec_rodata->file_size > 0 && sec_rodata->data) {
        pad_file_to(f, sec_rodata->file_offset);
        fwrite(sec_rodata->data, 1, sec_rodata->file_size, f);
    }

    if (sec_data->file_size > 0 && sec_data->data) {
        pad_file_to(f, sec_data->file_offset);
        fwrite(sec_data->data, 1, sec_data->file_size, f);
    }

    if (dyn) {
        if (ctx->got_file_size > 0 && ctx->got_data) {
            pad_file_to(f, ctx->got_file_offset);
            fwrite(ctx->got_data, 1, ctx->got_file_size, f);
        }

        size_t dyn_cap = ctx->dynamic_file_size / sizeof(Elf64_Dyn);
        Elf64_Dyn *dyn_entries = (Elf64_Dyn *)ny_alloc_zero(dyn_cap * sizeof(Elf64_Dyn));
        size_t cur_dyn = 0;

        if (ctx->soname) {
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_SONAME, .d_val = dynstr_offset_of(ctx, ctx->soname) };
        }

        for (size_t i = 0; i < ctx->needed_lib_count; i++) {
            if (ctx->needed_libs[i]) {
                dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_NEEDED, .d_val = dynstr_offset_of(ctx, ctx->needed_libs[i]) };
            }
        }

        if (ctx->hash_file_size > 0) {
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_HASH, .d_val = ctx->hash_va };
        }
        dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_STRTAB, .d_val = ctx->dynstr_va };
        dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_STRSZ, .d_val = ctx->dynstr_size };
        dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_SYMTAB, .d_val = ctx->dynsym_va };
        dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_SYMENT, .d_val = sizeof(Elf64_Sym) };
        if (ctx->rela_dyn_file_size > 0) {
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_RELA, .d_val = ctx->rela_dyn_va };
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_RELASZ, .d_val = ctx->rela_dyn_file_size };
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_RELAENT, .d_val = 24 };
        }
        if (ctx->rela_plt_file_size > 0) {
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_PLTGOT, .d_val = ctx->got_plt_va };
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_PLTRELSZ, .d_val = ctx->rela_plt_file_size };
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_PLTREL, .d_val = 7 };
            dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_JMPREL, .d_val = ctx->rela_plt_va };
        }
        dyn_entries[cur_dyn++] = (Elf64_Dyn){ .d_tag = DT_NULL, .d_val = 0 };

        pad_file_to(f, ctx->dynamic_file_offset);
        fwrite(dyn_entries, sizeof(Elf64_Dyn), cur_dyn, f);
        ny_free(dyn_entries, dyn_cap * sizeof(Elf64_Dyn));

        if (ctx->got_plt_file_size > 0 && ctx->got_plt_data) {
            pad_file_to(f, ctx->got_plt_file_offset);
            fwrite(ctx->got_plt_data, 1, ctx->got_plt_file_size, f);
        }
    }

    pad_file_to(f, shstrtab_offset);
    fwrite(shstrtab, 1, shstrtab_len, f);

    pad_file_to(f, shoff);
    fwrite(shdrs, sizeof(Elf64_Shdr), shnum, f);

    fclose(f);
    return true;
}
