// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nylink_internal.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#pragma pack(push, 1)
typedef struct Ar_Header {
    char ar_name[16];
    char ar_date[12];
    char ar_uid[6];
    char ar_gid[6];
    char ar_mode[8];
    char ar_size[10];
    char ar_fmag[2];
} Ar_Header;
#pragma pack(pop)

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

static uint32_t read_le32(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16)  |
           ((uint32_t)p[3] << 24);
}

static bool parse_decimal(const char *str, size_t max_len, size_t *out_val) {
    size_t val = 0;
    size_t i = 0;
    while (i < max_len && (str[i] == ' ' || str[i] == '\t')) {
        i++;
    }
    if (i == max_len || str[i] < '0' || str[i] > '9') {
        return false;
    }
    while (i < max_len && str[i] >= '0' && str[i] <= '9') {
        size_t digit = (size_t)(str[i] - '0');
        if (val > (SIZE_MAX - digit) / 10) {
            return false;
        }
        val = val * 10 + digit;
        i++;
    }
    *out_val = val;
    return true;
}

static void add_archive_symbol(Nylink_Archive *arch, const char *name, size_t member_offset) {
    if (!name || name[0] == '\0') return;

    for (size_t i = 0; i < arch->symbol_count; i++) {
        if (strcmp(arch->symbols[i].name, name) == 0 && arch->symbols[i].member_offset == member_offset) {
            return;
        }
    }

    ny_buf_grow((void **)&arch->symbols, &arch->symbol_capacity, arch->symbol_count, sizeof(Nylink_Archive_Symbol));
    Nylink_Archive_Symbol *sym = &arch->symbols[arch->symbol_count++];

    size_t len = strlen(name);
    sym->name = (char *)ny_alloc(len + 1);
    memcpy(sym->name, name, len + 1);
    sym->member_offset = member_offset;
}

static bool parse_first_linker_member(Nylink_Archive *arch, const uint8_t *data, size_t size) {
    if (size < 4) return false;
    uint32_t num_syms = read_be32(data);
    if (num_syms > (size - 4) / 4) return false;

    size_t offsets_size = (size_t)num_syms * 4;
    size_t strings_offset = 4 + offsets_size;
    if (strings_offset > size) return false;

    const uint8_t *offsets_ptr = data + 4;
    const char *str_tab = (const char *)(data + strings_offset);
    size_t str_tab_len = size - strings_offset;

    size_t str_pos = 0;
    for (uint32_t i = 0; i < num_syms; i++) {
        if (str_pos >= str_tab_len) break;
        uint32_t member_off = read_be32(offsets_ptr + i * 4);
        const char *sym_name = str_tab + str_pos;
        size_t len = strlen(sym_name);
        if (str_pos + len >= str_tab_len) break;

        add_archive_symbol(arch, sym_name, (size_t)member_off);
        str_pos += len + 1;
    }
    return true;
}

static bool parse_second_linker_member(Nylink_Archive *arch, const uint8_t *data, size_t size) {
    if (size < 4) return false;
    uint32_t num_members = read_le32(data);
    if (num_members > (size - 4) / 4) return false;

    size_t members_offset = 4;
    size_t sym_count_offset = members_offset + (size_t)num_members * 4;
    if (sym_count_offset + 4 > size) return false;

    uint32_t num_syms = read_le32(data + sym_count_offset);
    size_t indices_offset = sym_count_offset + 4;
    size_t indices_size = (size_t)num_syms * 2;
    if (indices_offset + indices_size > size) return false;

    size_t strings_offset = indices_offset + indices_size;
    const uint8_t *members_ptr = data + members_offset;
    const uint8_t *indices_ptr = data + indices_offset;
    const char *str_tab = (const char *)(data + strings_offset);
    size_t str_tab_len = size - strings_offset;

    size_t str_pos = 0;
    for (uint32_t i = 0; i < num_syms; i++) {
        if (str_pos >= str_tab_len) break;
        uint16_t member_idx_1based = (uint16_t)(indices_ptr[i * 2] | (indices_ptr[i * 2 + 1] << 8));
        if (member_idx_1based == 0 || member_idx_1based > num_members) {
            break;
        }
        uint32_t member_off = read_le32(members_ptr + (member_idx_1based - 1) * 4);
        const char *sym_name = str_tab + str_pos;
        size_t len = strlen(sym_name);
        if (str_pos + len >= str_tab_len) break;

        add_archive_symbol(arch, sym_name, (size_t)member_off);
        str_pos += len + 1;
    }
    return true;
}

static void index_symbols_from_elf(Nylink_Archive *arch, const uint8_t *data, size_t size, size_t member_offset) {
    if (size < 64) return;
    if (data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') return;
    if (data[4] != 2 || data[5] != 1) return;

    uint16_t e_type = (uint16_t)(data[16] | (data[17] << 8));
    uint16_t e_machine = (uint16_t)(data[18] | (data[19] << 8));
    if (e_type != 1 || (e_machine != 62 && e_machine != 183)) return;

    uint64_t shoff = 0;
    memcpy(&shoff, data + 40, sizeof(uint64_t));
    uint16_t shentsize = (uint16_t)(data[58] | (data[59] << 8));
    uint16_t shnum = (uint16_t)(data[60] | (data[61] << 8));

    if (shentsize < 64 || shnum == 0) return;
    if (shoff > size || (size_t)shnum > (size - shoff) / shentsize) return;

    for (uint16_t i = 0; i < shnum; i++) {
        const uint8_t *sh = data + shoff + (size_t)i * shentsize;
        uint32_t sh_type = *(const uint32_t *)(sh + 4);
        if (sh_type == 2) { /* SHT_SYMTAB */
            uint64_t sym_offset = *(const uint64_t *)(sh + 24);
            uint64_t sym_size = *(const uint64_t *)(sh + 32);
            uint32_t sh_link = *(const uint32_t *)(sh + 40);
            uint64_t sym_entsize = *(const uint64_t *)(sh + 56);

            if (sym_entsize < 24 || sh_link >= shnum) continue;
            if (sym_offset > size || sym_size > size - sym_offset) continue;

            const uint8_t *str_sh = data + shoff + (size_t)sh_link * shentsize;
            uint64_t str_offset = *(const uint64_t *)(str_sh + 24);
            uint64_t str_size = *(const uint64_t *)(str_sh + 32);
            if (str_offset > size || str_size > size - str_offset) continue;

            const char *strtab = (const char *)(data + str_offset);
            size_t num_syms = (size_t)(sym_size / sym_entsize);

            for (size_t s = 1; s < num_syms; s++) {
                const uint8_t *sym_entry = data + sym_offset + s * sym_entsize;
                uint32_t st_name = *(const uint32_t *)sym_entry;
                uint8_t st_info = sym_entry[4];
                uint16_t st_shndx = *(const uint16_t *)(sym_entry + 6);

                uint8_t binding = st_info >> 4;
                if ((binding == 1 || binding == 2) && st_shndx != 0) {
                    if (st_name < str_size) {
                        const char *name = strtab + st_name;
                        if (name[0] != '\0') {
                            add_archive_symbol(arch, name, member_offset);
                        }
                    }
                }
            }
        }
    }
}

static void index_symbols_from_coff(Nylink_Archive *arch, const uint8_t *data, size_t size, size_t member_offset) {
    if (size < 20) return;
    uint16_t machine = (uint16_t)(data[0] | (data[1] << 8));
    if (machine != 0x8664) return;

    uint32_t sym_table_offset = *(const uint32_t *)(data + 8);
    uint32_t sym_count = *(const uint32_t *)(data + 12);

    if (sym_table_offset > size) return;
    if ((uint64_t)sym_count * 18 > size - sym_table_offset) return;

    size_t strtab_offset = sym_table_offset + (size_t)sym_count * 18;
    const char *strtab = nullptr;
    size_t strtab_size = 0;
    if (strtab_offset + 4 <= size) {
        uint32_t total_str_size = *(const uint32_t *)(data + strtab_offset);
        if (strtab_offset + total_str_size <= size && total_str_size >= 4) {
            strtab = (const char *)(data + strtab_offset);
            strtab_size = total_str_size;
        }
    }

    for (uint32_t i = 0; i < sym_count; i++) {
        const uint8_t *sym = data + sym_table_offset + (size_t)i * 18;
        int16_t sec_num = *(const int16_t *)(sym + 12);
        uint8_t storage_class = sym[16];
        uint8_t num_aux = sym[17];

        if (sec_num > 0 && (storage_class == 2 || storage_class == 105)) {
            char name_buf[9];
            const char *name = nullptr;
            if (sym[0] == 0 && sym[1] == 0 && sym[2] == 0 && sym[3] == 0) {
                uint32_t offset = *(const uint32_t *)(sym + 4);
                if (strtab && offset < strtab_size) {
                    name = strtab + offset;
                }
            } else {
                memcpy(name_buf, sym, 8);
                name_buf[8] = '\0';
                name = name_buf;
            }

            if (name && name[0] != '\0') {
                add_archive_symbol(arch, name, member_offset);
            }
        }
        i += num_aux;
    }
}

bool nylink_read_archive(Nylink_Context *ctx, uint32_t arch_idx) {
    if (!ctx || arch_idx >= ctx->archive_count) return false;
    Nylink_Archive *arch = &ctx->archives[arch_idx];

    if (!arch->data || arch->size < 8) {
        nylink_diag_add(ctx, "corrupt archive: insufficient size", arch->name, nullptr);
        return false;
    }

    if (memcmp(arch->data, "!<arch>\n", 8) != 0) {
        nylink_diag_add(ctx, "invalid archive magic", arch->name, nullptr);
        return false;
    }

    size_t offset = 8;
    bool seen_first_linker = false;

    while (offset + sizeof(Ar_Header) <= arch->size) {
        const Ar_Header *hdr = (const Ar_Header *)(arch->data + offset);

        if (hdr->ar_fmag[0] != '`' || hdr->ar_fmag[1] != '\n') {
            nylink_diag_add(ctx, "corrupt archive member header trailer", arch->name, nullptr);
            return false;
        }

        size_t member_size = 0;
        if (!parse_decimal(hdr->ar_size, sizeof(hdr->ar_size), &member_size)) {
            nylink_diag_add(ctx, "malformed archive member size field", arch->name, nullptr);
            return false;
        }

        size_t data_offset = offset + sizeof(Ar_Header);
        if (member_size > arch->size || data_offset > arch->size - member_size) {
            nylink_diag_add(ctx, "archive member size exceeds archive file bounds", arch->name, nullptr);
            return false;
        }

        char raw_name[17];
        memcpy(raw_name, hdr->ar_name, 16);
        raw_name[16] = '\0';

        size_t raw_len = 16;
        while (raw_len > 0 && raw_name[raw_len - 1] == ' ') {
            raw_name[--raw_len] = '\0';
        }

        bool is_special = false;
        if (strcmp(raw_name, "/") == 0) {
            is_special = true;
            if (!seen_first_linker) {
                seen_first_linker = true;
                parse_first_linker_member(arch, arch->data + data_offset, member_size);
            } else {
                parse_second_linker_member(arch, arch->data + data_offset, member_size);
            }
        } else if (strcmp(raw_name, "//") == 0) {
            is_special = true;
            arch->long_names = (const char *)(arch->data + data_offset);
            arch->long_names_size = member_size;
        } else if (strcmp(raw_name, "__.SYMDEF") == 0 || strcmp(raw_name, "__.SYMDEF SORTED") == 0) {
            is_special = true;
        }

        if (!is_special) {
            char resolved_name[256];
            resolved_name[0] = '\0';

            if (raw_name[0] == '/' && raw_name[1] >= '0' && raw_name[1] <= '9') {
                size_t long_offset = 0;
                if (parse_decimal(raw_name + 1, strlen(raw_name + 1), &long_offset) &&
                    arch->long_names && long_offset < arch->long_names_size) {
                    const char *ln = arch->long_names + long_offset;
                    size_t i = 0;
                    while (long_offset + i < arch->long_names_size && ln[i] != '/' && ln[i] != '\0' && ln[i] != '\n' && i < sizeof(resolved_name) - 1) {
                        resolved_name[i] = ln[i];
                        i++;
                    }
                    resolved_name[i] = '\0';
                }
            }

            if (resolved_name[0] == '\0') {
                size_t l = strlen(raw_name);
                if (l > 0 && raw_name[l - 1] == '/') {
                    raw_name[l - 1] = '\0';
                }
                snprintf(resolved_name, sizeof(resolved_name), "%s", raw_name);
            }

            ny_buf_grow((void **)&arch->members, &arch->member_capacity, arch->member_count, sizeof(Nylink_Archive_Member));
            Nylink_Archive_Member *m = &arch->members[arch->member_count++];
            size_t nlen = strlen(resolved_name);
            m->name = (char *)ny_alloc(nlen + 1);
            memcpy(m->name, resolved_name, nlen + 1);
            m->header_offset = offset;
            m->data_offset = data_offset;
            m->size = member_size;
            m->is_extracted = false;

            if (member_size >= 4) {
                const uint8_t *mb = arch->data + data_offset;
                uint16_t sig1 = (uint16_t)(mb[0] | (mb[1] << 8));
                uint16_t sig2 = (uint16_t)(mb[2] | (mb[3] << 8));
                if (sig1 == 0 && sig2 == 0xFFFF) {
                    /* DLL import library short-format member (IMPORT_OBJECT_HEADER) */
                    if (member_size >= 20) {
                        const char *sym_name = (const char *)(mb + 20);
                        size_t sym_len = strlen(sym_name);
                        if (20 + sym_len + 1 < member_size) {
                            const char *dll_name = sym_name + sym_len + 1;
                            if (dll_name[0] != '\0') {
                                /* Record DLL name if not present */
                                uint32_t dll_idx = UINT32_MAX;
                                for (size_t d = 0; d < ctx->pe_dll_count; d++) {
                                    if (strcmp(ctx->pe_dll_names[d], dll_name) == 0) {
                                        dll_idx = (uint32_t)d;
                                        break;
                                    }
                                }
                                if (dll_idx == UINT32_MAX) {
                                    ny_buf_grow((void **)&ctx->pe_dll_names, &ctx->pe_dll_capacity, ctx->pe_dll_count, sizeof(char *));
                                    dll_idx = (uint32_t)ctx->pe_dll_count++;
                                    size_t dlen = strlen(dll_name);
                                    char *dcopy = (char *)ny_alloc(dlen + 1);
                                    memcpy(dcopy, dll_name, dlen + 1);
                                    ctx->pe_dll_names[dll_idx] = dcopy;
                                }

                                /* Record imported symbol */
                                bool sym_exists = false;
                                for (size_t k = 0; k < ctx->pe_imp_count; k++) {
                                    if (strcmp(ctx->pe_imp_sym_names[k], sym_name) == 0) {
                                        sym_exists = true;
                                        break;
                                    }
                                }
                                if (!sym_exists) {
                                    size_t old_imp_cap = ctx->pe_imp_capacity;
                                    if (ctx->pe_imp_count >= old_imp_cap) {
                                        size_t new_cap = old_imp_cap == 0 ? 8 : old_imp_cap * 2;
                                        while (new_cap <= ctx->pe_imp_count) new_cap *= 2;
                                        ctx->pe_imp_sym_names   = (char **)ny_realloc(ctx->pe_imp_sym_names, old_imp_cap * sizeof(char *), new_cap * sizeof(char *));
                                        ctx->pe_imp_dll_indices = (uint32_t *)ny_realloc(ctx->pe_imp_dll_indices, old_imp_cap * sizeof(uint32_t), new_cap * sizeof(uint32_t));
                                        ctx->pe_imp_sym_ids     = (uint32_t *)ny_realloc(ctx->pe_imp_sym_ids, old_imp_cap * sizeof(uint32_t), new_cap * sizeof(uint32_t));
                                        ctx->pe_imp_iat_rvas    = (uint32_t *)ny_realloc(ctx->pe_imp_iat_rvas, old_imp_cap * sizeof(uint32_t), new_cap * sizeof(uint32_t));
                                        ctx->pe_imp_thunk_rvas  = (uint32_t *)ny_realloc(ctx->pe_imp_thunk_rvas, old_imp_cap * sizeof(uint32_t), new_cap * sizeof(uint32_t));
                                        ctx->pe_imp_capacity = new_cap;
                                    }

                                    size_t imp_idx = ctx->pe_imp_count++;
                                    char *scopy = (char *)ny_alloc(sym_len + 1);
                                    memcpy(scopy, sym_name, sym_len + 1);
                                    ctx->pe_imp_sym_names[imp_idx] = scopy;
                                    ctx->pe_imp_dll_indices[imp_idx] = dll_idx;
                                    ctx->pe_imp_sym_ids[imp_idx] = UINT32_MAX;
                                    ctx->pe_imp_iat_rvas[imp_idx] = 0;
                                    ctx->pe_imp_thunk_rvas[imp_idx] = 0;

                                    /* Also add __imp_<sym_name> and <sym_name> to archive symbols so they can satisfy references */
                                    add_archive_symbol(arch, sym_name, offset);
                                    char imp_name_buf[512];
                                    snprintf(imp_name_buf, sizeof(imp_name_buf), "__imp_%s", sym_name);
                                    add_archive_symbol(arch, imp_name_buf, offset);
                                }
                            }
                        }
                    }
                } else if (mb[0] == 0x7F && mb[1] == 'E' && mb[2] == 'L' && mb[3] == 'F') {
                    if (!seen_first_linker) {
                        index_symbols_from_elf(arch, mb, member_size, offset);
                    }
                } else if (sig1 == 0x8664) {
                    if (!seen_first_linker) {
                        index_symbols_from_coff(arch, mb, member_size, offset);
                    }
                }
            }
        }

        size_t padded_size = (member_size + 1) & ~((size_t)1);
        if (data_offset > SIZE_MAX - padded_size) {
            nylink_diag_add(ctx, "archive member size causes integer overflow", arch->name, nullptr);
            return false;
        }
        offset = data_offset + padded_size;
    }

    return true;
}

bool nylink_extract_needed_archive_members(Nylink_Context *ctx) {
    if (!ctx || ctx->archive_count == 0) return true;

    bool progress = true;
    while (progress) {
        progress = false;

        for (size_t s = 0; s < ctx->symbol_count; s++) {
            Nylink_Symbol *sym = &ctx->symbols[s];
            if (sym->binding == NYLINK_SYM_LOCAL) continue;
            if (sym->is_defined) continue;
            if (!sym->name || sym->name[0] == '\0') continue;

            bool resolved_by_other = false;
            for (size_t o = 0; o < ctx->symbol_count; o++) {
                if (o != s && ctx->symbols[o].is_defined &&
                    ctx->symbols[o].binding != NYLINK_SYM_LOCAL &&
                    ctx->symbols[o].name && strcmp(ctx->symbols[o].name, sym->name) == 0) {
                    resolved_by_other = true;
                    break;
                }
            }
            if (resolved_by_other) continue;

            for (size_t a = 0; a < ctx->archive_count; a++) {
                Nylink_Archive *arch = &ctx->archives[a];
                for (size_t sym_idx = 0; sym_idx < arch->symbol_count; sym_idx++) {
                    if (strcmp(arch->symbols[sym_idx].name, sym->name) == 0) {
                        size_t member_off = arch->symbols[sym_idx].member_offset;

                        for (size_t m = 0; m < arch->member_count; m++) {
                            Nylink_Archive_Member *member = &arch->members[m];
                            if (member->header_offset == member_off && !member->is_extracted) {
                                char display_name[512];
                                snprintf(display_name, sizeof(display_name), "%s(%s)", arch->name, member->name);

                                const uint8_t *mdata = arch->data + member->data_offset;
                                size_t msize = member->size;

                                member->is_extracted = true;

                                if (msize >= 20 && mdata[0] == 0 && mdata[1] == 0 && mdata[2] == 0xFF && mdata[3] == 0xFF) {
                                    /* Short-format import header: define symbols in ctx->symbols */
                                    const char *imp_sym = (const char *)(mdata + 20);
                                    size_t imp_len = strlen(imp_sym);
                                    uint16_t type_flags = (uint16_t)(mdata[18] | (mdata[19] << 8));
                                    uint16_t type = type_flags & 0x3;
                                    bool is_fn = (type == 0); /* 0 = IMPORT_CODE */

                                    /* Define <sym> */
                                    ny_buf_grow((void **)&ctx->symbols, &ctx->symbol_capacity, ctx->symbol_count, sizeof(Nylink_Symbol));
                                    uint32_t s_id = (uint32_t)ctx->symbol_count++;
                                    Nylink_Symbol *ns = &ctx->symbols[s_id];
                                    memset(ns, 0, sizeof(Nylink_Symbol));
                                    ns->id = s_id;
                                    ns->name = (char *)ny_alloc(imp_len + 1);
                                    memcpy(ns->name, imp_sym, imp_len + 1);
                                    ns->binding = NYLINK_SYM_GLOBAL;
                                    ns->is_defined = true;
                                    ns->is_dynamic = true;
                                    ns->is_function = is_fn;
                                    ns->sec_id = UINT32_MAX;
                                    ns->obj_index = UINT32_MAX;

                                    /* Define __imp_<sym> */
                                    char imp_name[512];
                                    snprintf(imp_name, sizeof(imp_name), "__imp_%s", imp_sym);
                                    size_t imp_name_len = strlen(imp_name);

                                    ny_buf_grow((void **)&ctx->symbols, &ctx->symbol_capacity, ctx->symbol_count, sizeof(Nylink_Symbol));
                                    uint32_t is_id = (uint32_t)ctx->symbol_count++;
                                    Nylink_Symbol *nis = &ctx->symbols[is_id];
                                    memset(nis, 0, sizeof(Nylink_Symbol));
                                    nis->id = is_id;
                                    nis->name = (char *)ny_alloc(imp_name_len + 1);
                                    memcpy(nis->name, imp_name, imp_name_len + 1);
                                    nis->binding = NYLINK_SYM_GLOBAL;
                                    nis->is_defined = true;
                                    nis->is_dynamic = true;
                                    nis->is_function = false;
                                    nis->sec_id = UINT32_MAX;
                                    nis->obj_index = UINT32_MAX;
                                } else {
                                    if (!nylink_add_object(ctx, display_name, mdata, msize)) {
                                        return false;
                                    }
                                }

                                progress = true;
                                break;
                            }
                        }
                    }
                    if (progress) break;
                }
                if (progress) break;
            }
        }
    }

    return true;
}

