// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/object.h"
#include <string.h>

#define ELF_MAGIC_0 0x7F
#define ELF_MAGIC_1 'E'
#define ELF_MAGIC_2 'L'
#define ELF_MAGIC_3 'F'

#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EV_CURRENT 1
#define ELFOSABI_SYSV 0

#define ET_REL 1
#define EM_X86_64 62

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4

#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHF_INFO_LINK 0x40

#define STB_LOCAL 0
#define STB_GLOBAL 1

#define STT_NOTYPE 0
#define STT_FUNC 2
#define STT_SECTION 3

#define ELF64_ST_BIND(val) (((unsigned char)(val)) >> 4)
#define ELF64_ST_TYPE(val) ((val) & 0xf)
#define ELF64_ST_INFO(bind, type) (((bind) << 4) + ((type) & 0xf))

#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_PLT32 4

#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffffL)
#define ELF64_R_INFO(sym, type) ((((uint64_t)(sym)) << 32) + (type))

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

static uint32_t elf_add_string(Ny_Object_Buffer *strtab, Ny_String name) {
    if (name.len == 0) {
        return 0;
    }
    uint32_t offset = (uint32_t)strtab->count;
    ny_obj_buf_append_bytes(strtab, name.data, name.len);
    ny_obj_buf_append_byte(strtab, 0);
    return offset;
}

static uint32_t elf_add_cstr(Ny_Object_Buffer *strtab, const char *str) {
    if (!str || !*str) {
        return 0;
    }
    size_t len = strlen(str);
    return elf_add_string(strtab, (Ny_String){.data = str, .len = len});
}

bool ny_emit_elf64_x86_64(Ny_Object_Buffer *out_buf, const X86_Encoded_Module *emod, Ny_Diagnostic_List *diags) {
    (void)diags;

    Ny_Object_Buffer symtab_buf;
    ny_obj_buf_init(&symtab_buf);

    Ny_Object_Buffer strtab_buf;
    ny_obj_buf_init(&strtab_buf);
    ny_obj_buf_append_byte(&strtab_buf, 0);

    Ny_Object_Buffer shstrtab_buf;
    ny_obj_buf_init(&shstrtab_buf);
    ny_obj_buf_append_byte(&shstrtab_buf, 0);

    Ny_Object_Buffer rela_buf;
    ny_obj_buf_init(&rela_buf);

    Elf64_Sym null_sym = {0};
    ny_obj_buf_append_bytes(&symtab_buf, &null_sym, sizeof(null_sym));

    uint16_t text_shndx = 1;
    Elf64_Sym sec_sym = {
        .st_name = 0,
        .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
        .st_other = 0,
        .st_shndx = text_shndx,
        .st_value = 0,
        .st_size = 0,
    };
    ny_obj_buf_append_bytes(&symtab_buf, &sec_sym, sizeof(sec_sym));
    uint32_t first_global_idx = 2;

    for (size_t i = 0; i < emod->function_count; i++) {
        const X86_Function_Code *fn = &emod->functions[i];
        uint32_t name_off = elf_add_string(&strtab_buf, fn->name);
        Elf64_Sym sym = {
            .st_name = name_off,
            .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_FUNC),
            .st_other = 0,
            .st_shndx = text_shndx,
            .st_value = fn->offset,
            .st_size = fn->size,
        };
        ny_obj_buf_append_bytes(&symtab_buf, &sym, sizeof(sym));
    }

    size_t reloc_count = emod->text_section.reloc_count;
    for (size_t r = 0; r < reloc_count; r++) {
        const X86_Relocation *reloc = &emod->text_section.relocs[r];
        uint32_t sym_idx = 0;
        size_t total_syms = symtab_buf.count / sizeof(Elf64_Sym);
        const Elf64_Sym *syms = (const Elf64_Sym *)symtab_buf.bytes;

        for (size_t s = 2; s < total_syms; s++) {
            const char *sname = (const char *)(strtab_buf.bytes + syms[s].st_name);
            if (ny_str_eq(reloc->symbol_name, (Ny_String){.data = sname, .len = strlen(sname)})) {
                sym_idx = (uint32_t)s;
                break;
            }
        }

        if (sym_idx == 0) {
            uint32_t name_off = elf_add_string(&strtab_buf, reloc->symbol_name);
            Elf64_Sym sym = {
                .st_name = name_off,
                .st_info = ELF64_ST_INFO(STB_GLOBAL, STT_NOTYPE),
                .st_other = 0,
                .st_shndx = 0,
                .st_value = 0,
                .st_size = 0,
            };
            sym_idx = (uint32_t)(symtab_buf.count / sizeof(Elf64_Sym));
            ny_obj_buf_append_bytes(&symtab_buf, &sym, sizeof(sym));
        }

        uint32_t r_type = (reloc->kind == X86_FIXUP_CALL_REL32) ? R_X86_64_PLT32 : R_X86_64_PC32;
        Elf64_Rela rela = {
            .r_offset = reloc->code_offset,
            .r_info = ELF64_R_INFO(sym_idx, r_type),
            .r_addend = -4 + reloc->addend,
        };
        ny_obj_buf_append_bytes(&rela_buf, &rela, sizeof(rela));
    }

    bool has_rela = (reloc_count > 0);
    uint32_t str_text = elf_add_cstr(&shstrtab_buf, ".text");
    uint32_t str_rela_text = has_rela ? elf_add_cstr(&shstrtab_buf, ".rela.text") : 0;
    uint32_t str_symtab = elf_add_cstr(&shstrtab_buf, ".symtab");
    uint32_t str_strtab = elf_add_cstr(&shstrtab_buf, ".strtab");
    uint32_t str_shstrtab = elf_add_cstr(&shstrtab_buf, ".shstrtab");

    uint16_t rela_shndx = has_rela ? 2 : 0;
    uint16_t symtab_shndx = has_rela ? 3 : 2;
    uint16_t strtab_shndx = has_rela ? 4 : 3;
    uint16_t shstrtab_shndx = has_rela ? 5 : 4;
    uint16_t total_sections = has_rela ? 6 : 5;

    Elf64_Ehdr ehdr = {
        .e_ident = {
            ELF_MAGIC_0, ELF_MAGIC_1, ELF_MAGIC_2, ELF_MAGIC_3,
            ELFCLASS64, ELFDATA2LSB, EV_CURRENT, ELFOSABI_SYSV,
            0, 0, 0, 0, 0, 0, 0, 0
        },
        .e_type = ET_REL,
        .e_machine = EM_X86_64,
        .e_version = EV_CURRENT,
        .e_entry = 0,
        .e_phoff = 0,
        .e_shoff = 0,
        .e_flags = 0,
        .e_ehsize = sizeof(Elf64_Ehdr),
        .e_phentsize = 0,
        .e_phnum = 0,
        .e_shentsize = sizeof(Elf64_Shdr),
        .e_shnum = total_sections,
        .e_shstrndx = shstrtab_shndx,
    };

    ny_obj_buf_append_bytes(out_buf, &ehdr, sizeof(ehdr));

    ny_obj_buf_align_to(out_buf, 16);
    uint64_t text_offset = out_buf->count;
    uint64_t text_size = emod->text_section.count;
    if (text_size > 0) {
        ny_obj_buf_append_bytes(out_buf, emod->text_section.bytes, text_size);
    }

    uint64_t rela_offset = 0;
    uint64_t rela_size = 0;
    if (has_rela) {
        ny_obj_buf_align_to(out_buf, 8);
        rela_offset = out_buf->count;
        rela_size = rela_buf.count;
        ny_obj_buf_append_bytes(out_buf, rela_buf.bytes, rela_size);
    }

    ny_obj_buf_align_to(out_buf, 8);
    uint64_t symtab_offset = out_buf->count;
    uint64_t symtab_size = symtab_buf.count;
    ny_obj_buf_append_bytes(out_buf, symtab_buf.bytes, symtab_size);

    ny_obj_buf_align_to(out_buf, 1);
    uint64_t strtab_offset = out_buf->count;
    uint64_t strtab_size = strtab_buf.count;
    ny_obj_buf_append_bytes(out_buf, strtab_buf.bytes, strtab_size);

    ny_obj_buf_align_to(out_buf, 1);
    uint64_t shstrtab_offset = out_buf->count;
    uint64_t shstrtab_size = shstrtab_buf.count;
    ny_obj_buf_append_bytes(out_buf, shstrtab_buf.bytes, shstrtab_size);

    ny_obj_buf_align_to(out_buf, 8);
    uint64_t shoff = out_buf->count;
    ((Elf64_Ehdr *)out_buf->bytes)->e_shoff = shoff;

    Elf64_Shdr shdrs[6];
    memset(shdrs, 0, sizeof(shdrs));

    shdrs[0] = (Elf64_Shdr){0};

    shdrs[text_shndx] = (Elf64_Shdr){
        .sh_name = str_text,
        .sh_type = SHT_PROGBITS,
        .sh_flags = SHF_ALLOC | SHF_EXECINSTR,
        .sh_addr = 0,
        .sh_offset = text_offset,
        .sh_size = text_size,
        .sh_link = 0,
        .sh_info = 0,
        .sh_addralign = 16,
        .sh_entsize = 0,
    };

    if (has_rela) {
        shdrs[rela_shndx] = (Elf64_Shdr){
            .sh_name = str_rela_text,
            .sh_type = SHT_RELA,
            .sh_flags = SHF_INFO_LINK,
            .sh_addr = 0,
            .sh_offset = rela_offset,
            .sh_size = rela_size,
            .sh_link = symtab_shndx,
            .sh_info = text_shndx,
            .sh_addralign = 8,
            .sh_entsize = sizeof(Elf64_Rela),
        };
    }

    shdrs[symtab_shndx] = (Elf64_Shdr){
        .sh_name = str_symtab,
        .sh_type = SHT_SYMTAB,
        .sh_flags = 0,
        .sh_addr = 0,
        .sh_offset = symtab_offset,
        .sh_size = symtab_size,
        .sh_link = strtab_shndx,
        .sh_info = first_global_idx,
        .sh_addralign = 8,
        .sh_entsize = sizeof(Elf64_Sym),
    };

    shdrs[strtab_shndx] = (Elf64_Shdr){
        .sh_name = str_strtab,
        .sh_type = SHT_STRTAB,
        .sh_flags = 0,
        .sh_addr = 0,
        .sh_offset = strtab_offset,
        .sh_size = strtab_size,
        .sh_link = 0,
        .sh_info = 0,
        .sh_addralign = 1,
        .sh_entsize = 0,
    };

    shdrs[shstrtab_shndx] = (Elf64_Shdr){
        .sh_name = str_shstrtab,
        .sh_type = SHT_STRTAB,
        .sh_flags = 0,
        .sh_addr = 0,
        .sh_offset = shstrtab_offset,
        .sh_size = shstrtab_size,
        .sh_link = 0,
        .sh_info = 0,
        .sh_addralign = 1,
        .sh_entsize = 0,
    };

    ny_obj_buf_append_bytes(out_buf, shdrs, sizeof(Elf64_Shdr) * total_sections);

    ny_obj_buf_destroy(&symtab_buf);
    ny_obj_buf_destroy(&strtab_buf);
    ny_obj_buf_destroy(&shstrtab_buf);
    ny_obj_buf_destroy(&rela_buf);

    return true;
}
