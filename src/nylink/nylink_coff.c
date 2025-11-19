// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>

#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000

#define IMAGE_SYM_CLASS_EXTERNAL 2
#define IMAGE_SYM_CLASS_STATIC 3
#define IMAGE_SYM_CLASS_WEAK_EXTERNAL 105

#define IMAGE_REL_AMD64_ADDR64 1
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

bool nylink_read_coff(Nylink_Context *ctx, uint32_t obj_idx) {
    Nylink_Object *obj = &ctx->objects[obj_idx];
    const uint8_t *data = obj->data;
    size_t size = obj->size;

    if (size < sizeof(Coff_File_Header)) {
        nylink_diag_add(ctx, "malformed object: size smaller than COFF header", obj->name, nullptr);
        return false;
    }

    const Coff_File_Header *fhdr = (const Coff_File_Header *)data;
    if (fhdr->Machine != IMAGE_FILE_MACHINE_AMD64) {
        nylink_diag_add(ctx, "unsupported object format: COFF machine is not AMD64", obj->name, nullptr);
        return false;
    }

    uint16_t num_sections = fhdr->NumberOfSections;
    size_t shdr_offset = sizeof(Coff_File_Header) + fhdr->SizeOfOptionalHeader;
    if (shdr_offset > size || (num_sections * sizeof(Coff_Section_Header)) > size ||
        (shdr_offset + num_sections * sizeof(Coff_Section_Header)) > size) {
        nylink_diag_add(ctx, "malformed object: COFF section header table out of bounds", obj->name, nullptr);
        return false;
    }

    const Coff_Section_Header *shdrs = (const Coff_Section_Header *)(data + shdr_offset);

    /* Locate Symbol Table and String Table */
    const char *strtab = nullptr;
    size_t strtab_size = 0;
    uint32_t symtab_offset = fhdr->PointerToSymbolTable;
    uint32_t num_symbols = fhdr->NumberOfSymbols;

    if (symtab_offset > 0 && num_symbols > 0) {
        size_t symtab_bytes = (size_t)num_symbols * sizeof(Coff_Symbol);
        if (symtab_offset > size || symtab_bytes > size || (symtab_offset + symtab_bytes) > size) {
            nylink_diag_add(ctx, "malformed object: COFF symbol table out of bounds", obj->name, nullptr);
            return false;
        }

        size_t strtab_offset = symtab_offset + symtab_bytes;
        if (strtab_offset + 4 <= size) {
            uint32_t str_size = *(const uint32_t *)(data + strtab_offset);
            if (str_size >= 4 && (strtab_offset + str_size) <= size) {
                strtab = (const char *)(data + strtab_offset);
                strtab_size = str_size;
            }
        }
    }

    /* Map 1-based COFF section number to Nylink section ID */
    uint32_t *coff_to_nylink_sec = (uint32_t *)ny_alloc_zero((num_sections + 1) * sizeof(uint32_t));
    for (size_t s = 0; s <= num_sections; s++) {
        coff_to_nylink_sec[s] = UINT32_MAX;
    }

    obj->first_sec_idx = (uint32_t)ctx->section_count;
    uint32_t sec_count_added = 0;

    for (size_t i = 0; i < num_sections; i++) {
        const Coff_Section_Header *sh = &shdrs[i];
        int16_t sec_num = (int16_t)(i + 1);

        char name_buf[128];
        memset(name_buf, 0, sizeof(name_buf));

        if (sh->Name[0] == '/') {
            uint32_t str_off = 0;
            if (sscanf((const char *)&sh->Name[1], "%u", &str_off) == 1 && strtab && str_off < strtab_size) {
                strncpy(name_buf, strtab + str_off, sizeof(name_buf) - 1);
            } else {
                memcpy(name_buf, sh->Name, 8);
            }
        } else {
            memcpy(name_buf, sh->Name, 8);
        }

        Nylink_Sec_Kind kind = NYLINK_SEC_UNKNOWN;
        if (strcmp(name_buf, ".text") == 0 || strncmp(name_buf, ".text$", 6) == 0) {
            kind = NYLINK_SEC_TEXT;
        } else if (strcmp(name_buf, ".rdata") == 0 || strncmp(name_buf, ".rdata$", 7) == 0) {
            kind = NYLINK_SEC_RODATA;
        } else if (strcmp(name_buf, ".data") == 0 || strncmp(name_buf, ".data$", 6) == 0) {
            kind = NYLINK_SEC_DATA;
        } else if (strcmp(name_buf, ".bss") == 0 || strncmp(name_buf, ".bss$", 5) == 0) {
            kind = NYLINK_SEC_BSS;
        }

        if (kind != NYLINK_SEC_BSS && sh->SizeOfRawData > 0) {
            if (sh->PointerToRawData > size || sh->SizeOfRawData > size ||
                (sh->PointerToRawData + sh->SizeOfRawData) > size) {
                nylink_diag_add(ctx, "malformed object: COFF section raw data out of bounds", obj->name, name_buf);
                ny_free(coff_to_nylink_sec, (num_sections + 1) * sizeof(uint32_t));
                return false;
            }
        }

        ny_buf_grow((void **)&ctx->sections, &ctx->section_capacity, ctx->section_count, sizeof(Nylink_Section));
        uint32_t sec_id = (uint32_t)ctx->section_count++;
        sec_count_added++;
        coff_to_nylink_sec[sec_num] = sec_id;

        Nylink_Section *nsec = &ctx->sections[sec_id];
        memset(nsec, 0, sizeof(Nylink_Section));
        nsec->id = sec_id;
        nsec->obj_index = obj_idx;
        nsec->kind = kind;
        size_t nlen = strlen(name_buf);
        nsec->name = (char *)ny_alloc(nlen + 1);
        memcpy(nsec->name, name_buf, nlen + 1);
        nsec->align = 16;
        nsec->flags = sh->Characteristics;
        nsec->size = sh->SizeOfRawData;
        nsec->data = (kind == NYLINK_SEC_BSS || sh->PointerToRawData == 0) ? nullptr : (data + sh->PointerToRawData);
    }
    obj->sec_count = sec_count_added;

    /* Parse Symbol Table */
    uint32_t *coff_to_nylink_sym = nullptr;
    if (num_symbols > 0) {
        coff_to_nylink_sym = (uint32_t *)ny_alloc_zero(num_symbols * sizeof(uint32_t));
        for (size_t s = 0; s < num_symbols; s++) {
            coff_to_nylink_sym[s] = UINT32_MAX;
        }

        const Coff_Symbol *coff_syms = (const Coff_Symbol *)(data + symtab_offset);
        obj->first_sym_idx = (uint32_t)ctx->symbol_count;
        uint32_t syms_added = 0;

        for (uint32_t i = 0; i < num_symbols; i++) {
            const Coff_Symbol *csym = &coff_syms[i];
            char sname_buf[256];
            memset(sname_buf, 0, sizeof(sname_buf));

            if (csym->N.LongName.Zeros == 0) {
                uint32_t off = csym->N.LongName.Offset;
                if (strtab && off < strtab_size) {
                    strncpy(sname_buf, strtab + off, sizeof(sname_buf) - 1);
                }
            } else {
                memcpy(sname_buf, csym->N.ShortName, 8);
            }

            Nylink_Sym_Binding nbind = NYLINK_SYM_LOCAL;
            if (csym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL) {
                nbind = NYLINK_SYM_GLOBAL;
            } else if (csym->StorageClass == IMAGE_SYM_CLASS_WEAK_EXTERNAL) {
                nbind = NYLINK_SYM_WEAK;
            }

            bool is_def = (csym->SectionNumber > 0 && csym->SectionNumber <= num_sections);
            uint32_t sec_id = UINT32_MAX;
            if (is_def) {
                sec_id = coff_to_nylink_sec[csym->SectionNumber];
            }

            if (sname_buf[0] != '\0' && (csym->StorageClass == IMAGE_SYM_CLASS_EXTERNAL ||
                                         csym->StorageClass == IMAGE_SYM_CLASS_WEAK_EXTERNAL ||
                                         csym->StorageClass == IMAGE_SYM_CLASS_STATIC)) {
                ny_buf_grow((void **)&ctx->symbols, &ctx->symbol_capacity, ctx->symbol_count, sizeof(Nylink_Symbol));
                uint32_t sym_id = (uint32_t)ctx->symbol_count++;
                syms_added++;
                coff_to_nylink_sym[i] = sym_id;

                Nylink_Symbol *nsym = &ctx->symbols[sym_id];
                memset(nsym, 0, sizeof(Nylink_Symbol));
                nsym->id = sym_id;
                size_t nlen = strlen(sname_buf);
                nsym->name = (char *)ny_alloc(nlen + 1);
                memcpy(nsym->name, sname_buf, nlen + 1);
                nsym->binding = nbind;
                nsym->is_defined = is_def;
                nsym->sec_id = sec_id;
                nsym->value = csym->Value;
                nsym->size = 0;
                nsym->obj_index = obj_idx;
            }

            i += csym->NumberOfAuxSymbols;
        }
        obj->sym_count = syms_added;
    }

    /* Parse Relocations */
    for (size_t i = 0; i < num_sections; i++) {
        const Coff_Section_Header *sh = &shdrs[i];
        int16_t sec_num = (int16_t)(i + 1);
        uint32_t target_nylink_sec = coff_to_nylink_sec[sec_num];
        if (target_nylink_sec == UINT32_MAX || sh->NumberOfRelocations == 0) {
            continue;
        }

        size_t reloc_bytes = (size_t)sh->NumberOfRelocations * sizeof(Coff_Relocation);
        if (sh->PointerToRelocations > size || reloc_bytes > size ||
            (sh->PointerToRelocations + reloc_bytes) > size) {
            nylink_diag_add(ctx, "malformed object: COFF relocations out of bounds", obj->name, nullptr);
            ny_free(coff_to_nylink_sec, (num_sections + 1) * sizeof(uint32_t));
            if (coff_to_nylink_sym) ny_free(coff_to_nylink_sym, num_symbols * sizeof(uint32_t));
            return false;
        }

        const Coff_Relocation *crelocs = (const Coff_Relocation *)(data + sh->PointerToRelocations);
        const Nylink_Section *tsec = &ctx->sections[target_nylink_sec];

        for (size_t r = 0; r < sh->NumberOfRelocations; r++) {
            const Coff_Relocation *crel = &crelocs[r];
            if (crel->VirtualAddress >= tsec->size) {
                nylink_diag_add(ctx, "invalid relocation: offset out of target section bounds", obj->name, tsec->name);
                ny_free(coff_to_nylink_sec, (num_sections + 1) * sizeof(uint32_t));
                if (coff_to_nylink_sym) ny_free(coff_to_nylink_sym, num_symbols * sizeof(uint32_t));
                return false;
            }

            Nylink_Reloc_Type rtype = NYLINK_RELOC_NONE;
            if (crel->Type == IMAGE_REL_AMD64_ADDR64) rtype = NYLINK_RELOC_X86_64_64;
            else if (crel->Type == IMAGE_REL_AMD64_REL32) rtype = NYLINK_RELOC_X86_64_PC32;

            uint32_t nylink_sym_id = UINT32_MAX;
            if (coff_to_nylink_sym && crel->SymbolTableIndex < num_symbols) {
                nylink_sym_id = coff_to_nylink_sym[crel->SymbolTableIndex];
            }

            ny_buf_grow((void **)&ctx->relocations, &ctx->relocation_capacity, ctx->relocation_count, sizeof(Nylink_Relocation));
            Nylink_Relocation *nrel = &ctx->relocations[ctx->relocation_count++];
            nrel->sec_id = target_nylink_sec;
            nrel->offset = crel->VirtualAddress;
            nrel->type = rtype;
            nrel->sym_id = nylink_sym_id;
            nrel->addend = -4; /* In COFF x86-64, PC-relative relocations have implicit -4 addend */
        }
    }

    ny_free(coff_to_nylink_sec, (num_sections + 1) * sizeof(uint32_t));
    if (coff_to_nylink_sym) ny_free(coff_to_nylink_sym, num_symbols * sizeof(uint32_t));
    return true;
}
