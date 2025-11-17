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
#define ET_REL 1
#define EM_X86_64 62

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_NOBITS 8

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2

#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4

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
    unsigned char st_info;
    unsigned char st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Elf64_Sym;

typedef struct Elf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Elf64_Rela;
#pragma pack(pop)

bool nylink_read_elf64(Nylink_Context *ctx, uint32_t obj_idx) {
    Nylink_Object *obj = &ctx->objects[obj_idx];
    const uint8_t *data = obj->data;
    size_t size = obj->size;

    if (size < sizeof(Elf64_Ehdr)) {
        nylink_diag_add(ctx, "malformed object: size smaller than ELF header", obj->name, nullptr);
        return false;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)data;
    if (ehdr->e_ident[0] != ELF_MAGIC_0 || ehdr->e_ident[1] != ELF_MAGIC_1 ||
        ehdr->e_ident[2] != ELF_MAGIC_2 || ehdr->e_ident[3] != ELF_MAGIC_3) {
        nylink_diag_add(ctx, "malformed object: invalid ELF magic", obj->name, nullptr);
        return false;
    }

    if (ehdr->e_ident[4] != ELFCLASS64 || ehdr->e_ident[5] != ELFDATA2LSB) {
        nylink_diag_add(ctx, "unsupported object format: not 64-bit little endian ELF", obj->name, nullptr);
        return false;
    }

    if (ehdr->e_type != ET_REL) {
        nylink_diag_add(ctx, "unsupported object format: not a relocatable object (ET_REL)", obj->name, nullptr);
        return false;
    }

    if (ehdr->e_machine != EM_X86_64) {
        nylink_diag_add(ctx, "unsupported object format: machine is not x86_64", obj->name, nullptr);
        return false;
    }

    if (ehdr->e_shentsize != sizeof(Elf64_Shdr)) {
        nylink_diag_add(ctx, "malformed object: unexpected section header entry size", obj->name, nullptr);
        return false;
    }

    uint64_t shoff = ehdr->e_shoff;
    uint64_t shnum = ehdr->e_shnum;
    if (shnum > 0xFFFF || (shnum * sizeof(Elf64_Shdr)) > size || shoff > size || (shoff + shnum * sizeof(Elf64_Shdr)) > size) {
        nylink_diag_add(ctx, "malformed object: section header table out of bounds", obj->name, nullptr);
        return false;
    }

    if (ehdr->e_shstrndx >= shnum) {
        nylink_diag_add(ctx, "malformed object: invalid section header string table index", obj->name, nullptr);
        return false;
    }

    const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(data + shoff);
    const Elf64_Shdr *shstr_shdr = &shdrs[ehdr->e_shstrndx];
    if (shstr_shdr->sh_offset > size || shstr_shdr->sh_size > size || (shstr_shdr->sh_offset + shstr_shdr->sh_size) > size) {
        nylink_diag_add(ctx, "malformed object: shstrtab out of bounds", obj->name, nullptr);
        return false;
    }
    const char *shstrtab = (const char *)(data + shstr_shdr->sh_offset);
    size_t shstrtab_size = (size_t)shstr_shdr->sh_size;

    /* Map ELF section index to allocated Nylink section ID */
    uint32_t *elf_to_nylink_sec = (uint32_t *)ny_alloc_zero(shnum * sizeof(uint32_t));
    for (size_t s = 0; s < shnum; s++) {
        elf_to_nylink_sec[s] = UINT32_MAX;
    }

    obj->first_sec_idx = (uint32_t)ctx->section_count;
    uint32_t sec_count_added = 0;

    for (size_t s = 1; s < shnum; s++) {
        const Elf64_Shdr *sh = &shdrs[s];
        if (sh->sh_name >= shstrtab_size) {
            nylink_diag_add(ctx, "malformed object: section name offset out of bounds", obj->name, nullptr);
            ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
            return false;
        }
        const char *sname = shstrtab + sh->sh_name;

        if (sh->sh_type == SHT_PROGBITS || sh->sh_type == SHT_NOBITS) {
            Nylink_Sec_Kind kind = NYLINK_SEC_UNKNOWN;
            if (strcmp(sname, ".text") == 0 || strncmp(sname, ".text.", 6) == 0) {
                kind = NYLINK_SEC_TEXT;
            } else if (strcmp(sname, ".rodata") == 0 || strncmp(sname, ".rodata.", 8) == 0) {
                kind = NYLINK_SEC_RODATA;
            } else if (strcmp(sname, ".data") == 0 || strncmp(sname, ".data.", 6) == 0) {
                kind = NYLINK_SEC_DATA;
            } else if (strcmp(sname, ".bss") == 0 || strncmp(sname, ".bss.", 5) == 0) {
                kind = NYLINK_SEC_BSS;
            }

            if (sh->sh_type == SHT_PROGBITS) {
                if (sh->sh_offset > size || sh->sh_size > size || (sh->sh_offset + sh->sh_size) > size) {
                    nylink_diag_add(ctx, "malformed object: section data out of bounds", obj->name, sname);
                    ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
                    return false;
                }
            }

            ny_buf_grow((void **)&ctx->sections, &ctx->section_capacity, ctx->section_count, sizeof(Nylink_Section));
            uint32_t sec_id = (uint32_t)ctx->section_count++;
            sec_count_added++;
            elf_to_nylink_sec[s] = sec_id;

            Nylink_Section *nsec = &ctx->sections[sec_id];
            nsec->id = sec_id;
            nsec->obj_index = obj_idx;
            nsec->kind = kind;
            size_t nlen = strlen(sname);
            nsec->name = (char *)ny_alloc(nlen + 1);
            memcpy(nsec->name, sname, nlen + 1);
            nsec->align = (uint32_t)(sh->sh_addralign > 0 ? sh->sh_addralign : 1);
            nsec->flags = (uint32_t)sh->sh_flags;
            nsec->size = (size_t)sh->sh_size;
            nsec->data = (sh->sh_type == SHT_NOBITS) ? nullptr : (data + sh->sh_offset);
        }
    }
    obj->sec_count = sec_count_added;

    /* Parse Symbol Table */
    const Elf64_Shdr *symtab_shdr = nullptr;
    const Elf64_Shdr *strtab_shdr = nullptr;
    for (size_t s = 1; s < shnum; s++) {
        if (shdrs[s].sh_type == SHT_SYMTAB) {
            symtab_shdr = &shdrs[s];
            if (symtab_shdr->sh_link < shnum && shdrs[symtab_shdr->sh_link].sh_type == SHT_STRTAB) {
                strtab_shdr = &shdrs[symtab_shdr->sh_link];
            }
            break;
        }
    }

    uint32_t *elf_to_nylink_sym = nullptr;
    size_t elf_sym_count = 0;

    if (symtab_shdr && strtab_shdr) {
        if (symtab_shdr->sh_offset > size || symtab_shdr->sh_size > size || (symtab_shdr->sh_offset + symtab_shdr->sh_size) > size ||
            strtab_shdr->sh_offset > size || strtab_shdr->sh_size > size || (strtab_shdr->sh_offset + strtab_shdr->sh_size) > size) {
            nylink_diag_add(ctx, "malformed object: symtab or strtab out of bounds", obj->name, nullptr);
            ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
            return false;
        }

        if (symtab_shdr->sh_entsize != sizeof(Elf64_Sym)) {
            nylink_diag_add(ctx, "malformed object: invalid symtab entry size", obj->name, nullptr);
            ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
            return false;
        }

        elf_sym_count = (size_t)(symtab_shdr->sh_size / sizeof(Elf64_Sym));
        const Elf64_Sym *elf_syms = (const Elf64_Sym *)(data + symtab_shdr->sh_offset);
        const char *strtab = (const char *)(data + strtab_shdr->sh_offset);
        size_t strtab_size = (size_t)strtab_shdr->sh_size;

        elf_to_nylink_sym = (uint32_t *)ny_alloc_zero(elf_sym_count * sizeof(uint32_t));
        for (size_t i = 0; i < elf_sym_count; i++) {
            elf_to_nylink_sym[i] = UINT32_MAX;
        }

        obj->first_sym_idx = (uint32_t)ctx->symbol_count;
        uint32_t syms_added = 0;

        for (size_t i = 1; i < elf_sym_count; i++) {
            const Elf64_Sym *esym = &elf_syms[i];
            unsigned char bind = (esym->st_info >> 4);
            unsigned char type = (esym->st_info & 0xF);

            if (type == STT_SECTION) {
                continue;
            }

            if (esym->st_name >= strtab_size) {
                nylink_diag_add(ctx, "malformed object: symbol name offset out of bounds", obj->name, nullptr);
                ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
                ny_free(elf_to_nylink_sym, elf_sym_count * sizeof(uint32_t));
                return false;
            }
            const char *sym_name = strtab + esym->st_name;
            if (!sym_name || !*sym_name) continue;

            Nylink_Sym_Binding nbind = NYLINK_SYM_LOCAL;
            if (bind == STB_GLOBAL) nbind = NYLINK_SYM_GLOBAL;
            else if (bind == STB_WEAK) nbind = NYLINK_SYM_WEAK;

            bool is_def = (esym->st_shndx != 0 && esym->st_shndx < shnum);
            uint32_t sec_id = UINT32_MAX;
            if (is_def) {
                sec_id = elf_to_nylink_sec[esym->st_shndx];
            }

            ny_buf_grow((void **)&ctx->symbols, &ctx->symbol_capacity, ctx->symbol_count, sizeof(Nylink_Symbol));
            uint32_t sym_id = (uint32_t)ctx->symbol_count++;
            syms_added++;
            elf_to_nylink_sym[i] = sym_id;

            Nylink_Symbol *nsym = &ctx->symbols[sym_id];
            nsym->id = sym_id;
            size_t nlen = strlen(sym_name);
            nsym->name = (char *)ny_alloc(nlen + 1);
            memcpy(nsym->name, sym_name, nlen + 1);
            nsym->binding = nbind;
            nsym->is_defined = is_def;
            nsym->sec_id = sec_id;
            nsym->value = esym->st_value;
            nsym->size = (size_t)esym->st_size;
            nsym->obj_index = obj_idx;
            nsym->visibility = esym->st_other & 0x03;
        }
        obj->sym_count = syms_added;
    }

    /* Parse Relocations */
    for (size_t s = 1; s < shnum; s++) {
        const Elf64_Shdr *sh = &shdrs[s];
        if (sh->sh_type == SHT_RELA) {
            uint32_t target_sec_ndx = sh->sh_info;
            if (target_sec_ndx >= shnum) {
                nylink_diag_add(ctx, "malformed object: invalid relocation target section index", obj->name, nullptr);
                ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
                if (elf_to_nylink_sym) ny_free(elf_to_nylink_sym, elf_sym_count * sizeof(uint32_t));
                return false;
            }
            uint32_t target_nylink_sec = elf_to_nylink_sec[target_sec_ndx];
            if (target_nylink_sec == UINT32_MAX) {
                continue;
            }

            if (sh->sh_offset > size || sh->sh_size > size || (sh->sh_offset + sh->sh_size) > size) {
                nylink_diag_add(ctx, "malformed object: rela section out of bounds", obj->name, nullptr);
                ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
                if (elf_to_nylink_sym) ny_free(elf_to_nylink_sym, elf_sym_count * sizeof(uint32_t));
                return false;
            }

            if (sh->sh_entsize != sizeof(Elf64_Rela)) {
                nylink_diag_add(ctx, "malformed object: invalid rela entry size", obj->name, nullptr);
                ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
                if (elf_to_nylink_sym) ny_free(elf_to_nylink_sym, elf_sym_count * sizeof(uint32_t));
                return false;
            }

            size_t rela_count = (size_t)(sh->sh_size / sizeof(Elf64_Rela));
            const Elf64_Rela *relas = (const Elf64_Rela *)(data + sh->sh_offset);
            const Nylink_Section *tsec = &ctx->sections[target_nylink_sec];

            for (size_t r = 0; r < rela_count; r++) {
                const Elf64_Rela *rela = &relas[r];
                uint32_t sym_idx = (uint32_t)(rela->r_info >> 32);
                uint32_t type = (uint32_t)(rela->r_info & 0xFFFFFFFF);

                if (rela->r_offset >= tsec->size) {
                    nylink_diag_add(ctx, "invalid relocation: offset out of target section bounds", obj->name, tsec->name);
                    ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
                    if (elf_to_nylink_sym) ny_free(elf_to_nylink_sym, elf_sym_count * sizeof(uint32_t));
                    return false;
                }

                Nylink_Reloc_Type rtype = NYLINK_RELOC_NONE;
                if (type == R_X86_64_64) rtype = NYLINK_RELOC_X86_64_64;
                else if (type == R_X86_64_PC32) rtype = NYLINK_RELOC_X86_64_PC32;
                else if (type == R_X86_64_PLT32) rtype = NYLINK_RELOC_X86_64_PLT32;

                uint32_t nylink_sym_id = UINT32_MAX;
                if (elf_to_nylink_sym && sym_idx < elf_sym_count) {
                    nylink_sym_id = elf_to_nylink_sym[sym_idx];
                }

                ny_buf_grow((void **)&ctx->relocations, &ctx->relocation_capacity, ctx->relocation_count, sizeof(Nylink_Relocation));
                Nylink_Relocation *nreloc = &ctx->relocations[ctx->relocation_count++];
                nreloc->sec_id = target_nylink_sec;
                nreloc->offset = rela->r_offset;
                nreloc->type = rtype;
                nreloc->sym_id = nylink_sym_id;
                nreloc->addend = rela->r_addend;
            }
        }
    }

    ny_free(elf_to_nylink_sec, shnum * sizeof(uint32_t));
    if (elf_to_nylink_sym) ny_free(elf_to_nylink_sym, elf_sym_count * sizeof(uint32_t));
    return true;
}
