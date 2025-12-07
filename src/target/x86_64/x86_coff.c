#include "nybit/object.h"
#include "x86_unwind.h"
#include <string.h>

#define IMAGE_FILE_MACHINE_AMD64 0x8664
#define IMAGE_SCN_CNT_CODE 0x00000020
#define IMAGE_SCN_CNT_INITIALIZED_DATA 0x00000040
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080
#define IMAGE_SCN_ALIGN_16BYTES 0x00500000
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
#define IMAGE_SCN_MEM_READ 0x40000000
#define IMAGE_SCN_MEM_WRITE 0x80000000

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
    if (!out_buf || !emod) {
        if (diags) ny_diagnostic_list_append(diags, "coff emission error: invalid null argument");
        return false;
    }

    if (emod->text_section.count > UINT32_MAX) {
        if (diags) ny_diagnostic_list_append(diags, "coff emission error: .text section size exceeds 4GB limit");
        return false;
    }

    if (emod->text_section.reloc_count > UINT16_MAX) {
        if (diags) ny_diagnostic_list_append(diags, "coff emission error: relocation count exceeds 16-bit field limit");
        return false;
    }

    for (size_t i = 0; i < emod->function_count; i++) {
        const X86_Function_Code *fn = &emod->functions[i];
        if (fn->offset > emod->text_section.count || fn->offset + fn->size > emod->text_section.count) {
            if (diags) ny_diagnostic_list_append(diags, "coff emission error: function bounds outside .text section");
            return false;
        }
    }

    for (size_t r = 0; r < emod->text_section.reloc_count; r++) {
        const X86_Relocation *reloc = &emod->text_section.relocs[r];
        if (reloc->code_offset + 4 > emod->text_section.count) {
            if (diags) ny_diagnostic_list_append(diags, "coff emission error: relocation code offset outside .text bounds");
            return false;
        }
        if (reloc->kind != X86_FIXUP_CALL_REL32 && reloc->kind != X86_FIXUP_GLOBAL_REL32) {
            if (diags) ny_diagnostic_list_append(diags, "coff emission error: unsupported relocation kind");
            return false;
        }
    }

    Ny_Object_Buffer symtab_buf;
    ny_obj_buf_init(&symtab_buf);

    Ny_Object_Buffer strtab_buf;
    ny_obj_buf_init(&strtab_buf);
    uint32_t dummy_size = 4;
    ny_obj_buf_append_bytes(&strtab_buf, &dummy_size, 4);

    Ny_Object_Buffer reloc_buf;
    ny_obj_buf_init(&reloc_buf);

    int16_t text_sec_num = 1;
    bool has_rdata = emod->rodata_section.count > 0;
    bool has_data = emod->data_section.count > 0;
    bool has_bss = emod->bss_size > 0;
    bool has_unwind = emod->emit_unwind;
    bool has_debug = emod->debug_info;

    Ny_Object_Buffer pdata_buf;
    ny_obj_buf_init(&pdata_buf);
    Ny_Object_Buffer xdata_buf;
    ny_obj_buf_init(&xdata_buf);
    if (has_unwind) {
        x86_build_coff_pdata_xdata(&pdata_buf, &xdata_buf, emod->source_mod, emod, 0);
    }

    Ny_Object_Buffer debug_s_buf;
    ny_obj_buf_init(&debug_s_buf);
    if (has_debug) {
        x86_build_codeview_debug_s(&debug_s_buf, emod->source_mod, emod);
    }

    int16_t next_sec_num = 2;
    int16_t rdata_sec_num = has_rdata ? next_sec_num++ : 0;
    int16_t data_sec_num = has_data ? next_sec_num++ : 0;
    int16_t bss_sec_num = has_bss ? next_sec_num++ : 0;
    if (has_unwind) next_sec_num += 2;
    if (has_debug) next_sec_num += 1;
    uint16_t total_sections = (uint16_t)(next_sec_num - 1);

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

    for (size_t g = 0; g < emod->global_count; g++) {
        const X86_Encoded_Global *eg = &emod->globals[g];
        Coff_Symbol sym = {0};
        coff_encode_name(&sym, &strtab_buf, eg->name);
        sym.Value = (uint32_t)eg->offset;
        if (eg->kind == NY_GLOBAL_CONST) sym.SectionNumber = rdata_sec_num;
        else if (eg->kind == NY_GLOBAL_DATA) sym.SectionNumber = data_sec_num;
        else if (eg->kind == NY_GLOBAL_BSS) sym.SectionNumber = bss_sec_num;
        sym.Type = 0;
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
                size_t slen = 0;
                while (slen < 8 && syms[s].N.ShortName[slen] != '\0') slen++;
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
        .NumberOfSections = total_sections,
        .TimeDateStamp = 0,
        .PointerToSymbolTable = 0,
        .NumberOfSymbols = (uint32_t)(symtab_buf.count / sizeof(Coff_Symbol)),
        .SizeOfOptionalHeader = 0,
        .Characteristics = 0,
    };
    ny_obj_buf_append_bytes(out_buf, &fhdr, sizeof(fhdr));

    size_t shdr_table_offset = out_buf->count;
    Coff_Section_Header *shdrs = (Coff_Section_Header *)ny_alloc_zero(sizeof(Coff_Section_Header) * total_sections);

    shdrs[0] = (Coff_Section_Header){
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

    uint16_t sec_idx = 1;
    if (has_rdata) {
        shdrs[sec_idx++] = (Coff_Section_Header){
            .Name = {'.', 'r', 'd', 'a', 't', 'a', 0, 0},
            .VirtualSize = 0,
            .VirtualAddress = 0,
            .SizeOfRawData = (uint32_t)emod->rodata_section.count,
            .PointerToRawData = 0,
            .PointerToRelocations = 0,
            .PointerToLinenumbers = 0,
            .NumberOfRelocations = 0,
            .NumberOfLinenumbers = 0,
            .Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES,
        };
    }

    if (has_data) {
        shdrs[sec_idx++] = (Coff_Section_Header){
            .Name = {'.', 'd', 'a', 't', 'a', 0, 0, 0},
            .VirtualSize = 0,
            .VirtualAddress = 0,
            .SizeOfRawData = (uint32_t)emod->data_section.count,
            .PointerToRawData = 0,
            .PointerToRelocations = 0,
            .PointerToLinenumbers = 0,
            .NumberOfRelocations = 0,
            .NumberOfLinenumbers = 0,
            .Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES,
        };
    }

    if (has_bss) {
        shdrs[sec_idx++] = (Coff_Section_Header){
            .Name = {'.', 'b', 's', 's', 0, 0, 0, 0},
            .VirtualSize = (uint32_t)emod->bss_size,
            .VirtualAddress = 0,
            .SizeOfRawData = 0,
            .PointerToRawData = 0,
            .PointerToRelocations = 0,
            .PointerToLinenumbers = 0,
            .NumberOfRelocations = 0,
            .NumberOfLinenumbers = 0,
            .Characteristics = IMAGE_SCN_CNT_UNINITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES,
        };
    }

    if (has_unwind) {
        shdrs[sec_idx++] = (Coff_Section_Header){
            .Name = {'.', 'p', 'd', 'a', 't', 'a', 0, 0},
            .VirtualSize = 0,
            .VirtualAddress = 0,
            .SizeOfRawData = (uint32_t)pdata_buf.count,
            .PointerToRawData = 0,
            .PointerToRelocations = 0,
            .PointerToLinenumbers = 0,
            .NumberOfRelocations = 0,
            .NumberOfLinenumbers = 0,
            .Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES,
        };
        shdrs[sec_idx++] = (Coff_Section_Header){
            .Name = {'.', 'x', 'd', 'a', 't', 'a', 0, 0},
            .VirtualSize = 0,
            .VirtualAddress = 0,
            .SizeOfRawData = (uint32_t)xdata_buf.count,
            .PointerToRawData = 0,
            .PointerToRelocations = 0,
            .PointerToLinenumbers = 0,
            .NumberOfRelocations = 0,
            .NumberOfLinenumbers = 0,
            .Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_ALIGN_16BYTES,
        };
    }

    if (has_debug) {
        shdrs[sec_idx++] = (Coff_Section_Header){
            .Name = {'.', 'd', 'e', 'b', 'u', 'g', '$', 'S'},
            .VirtualSize = 0,
            .VirtualAddress = 0,
            .SizeOfRawData = (uint32_t)debug_s_buf.count,
            .PointerToRawData = 0,
            .PointerToRelocations = 0,
            .PointerToLinenumbers = 0,
            .NumberOfRelocations = 0,
            .NumberOfLinenumbers = 0,
            .Characteristics = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_ALIGN_16BYTES,
        };
    }

    ny_obj_buf_append_bytes(out_buf, shdrs, sizeof(Coff_Section_Header) * total_sections);

    /* Emit .text raw data */
    ny_obj_buf_align_to(out_buf, 16);
    uint32_t text_raw_ptr = (uint32_t)out_buf->count;
    if (emod->text_section.count > 0) {
        ny_obj_buf_append_bytes(out_buf, emod->text_section.bytes, emod->text_section.count);
    }

    /* Emit .rdata raw data */
    uint32_t rdata_raw_ptr = 0;
    if (has_rdata) {
        ny_obj_buf_align_to(out_buf, 16);
        rdata_raw_ptr = (uint32_t)out_buf->count;
        ny_obj_buf_append_bytes(out_buf, emod->rodata_section.bytes, emod->rodata_section.count);
    }

    /* Emit .data raw data */
    uint32_t data_raw_ptr = 0;
    if (has_data) {
        ny_obj_buf_align_to(out_buf, 16);
        data_raw_ptr = (uint32_t)out_buf->count;
        ny_obj_buf_append_bytes(out_buf, emod->data_section.bytes, emod->data_section.count);
    }

    /* Emit .pdata and .xdata raw data */
    uint32_t pdata_raw_ptr = 0;
    uint32_t xdata_raw_ptr = 0;
    if (has_unwind) {
        ny_obj_buf_align_to(out_buf, 16);
        pdata_raw_ptr = (uint32_t)out_buf->count;
        ny_obj_buf_append_bytes(out_buf, pdata_buf.bytes, pdata_buf.count);

        ny_obj_buf_align_to(out_buf, 16);
        xdata_raw_ptr = (uint32_t)out_buf->count;
        ny_obj_buf_append_bytes(out_buf, xdata_buf.bytes, xdata_buf.count);
    }

    /* Emit .debug$S raw data */
    uint32_t debug_s_raw_ptr = 0;
    if (has_debug) {
        ny_obj_buf_align_to(out_buf, 16);
        debug_s_raw_ptr = (uint32_t)out_buf->count;
        ny_obj_buf_append_bytes(out_buf, debug_s_buf.bytes, debug_s_buf.count);
    }

    /* Emit .text relocations */
    uint32_t reloc_ptr = 0;
    if (reloc_count > 0) {
        ny_obj_buf_align_to(out_buf, 4);
        reloc_ptr = (uint32_t)out_buf->count;
        ny_obj_buf_append_bytes(out_buf, reloc_buf.bytes, reloc_buf.count);
    }

    /* Emit Symbol Table */
    ny_obj_buf_align_to(out_buf, 4);
    uint32_t symtab_ptr = (uint32_t)out_buf->count;
    ny_obj_buf_append_bytes(out_buf, symtab_buf.bytes, symtab_buf.count);

    /* Emit String Table */
    if (final_strtab_size > 4) {
        ny_obj_buf_append_bytes(out_buf, strtab_buf.bytes, strtab_buf.count);
    } else {
        uint32_t empty_str_sz = 4;
        ny_obj_buf_append_bytes(out_buf, &empty_str_sz, 4);
    }

    /* Patch File Header */
    Coff_File_Header *patch_fhdr = (Coff_File_Header *)out_buf->bytes;
    patch_fhdr->PointerToSymbolTable = symtab_ptr;

    /* Patch Section Headers */
    Coff_Section_Header *patch_shdrs = (Coff_Section_Header *)(out_buf->bytes + shdr_table_offset);
    patch_shdrs[0].PointerToRawData = text_raw_ptr;
    patch_shdrs[0].PointerToRelocations = reloc_ptr;

    uint16_t patch_idx = 1;
    if (has_rdata) {
        patch_shdrs[patch_idx++].PointerToRawData = rdata_raw_ptr;
    }
    if (has_data) {
        patch_shdrs[patch_idx++].PointerToRawData = data_raw_ptr;
    }
    if (has_bss) {
        patch_shdrs[patch_idx++].PointerToRawData = 0;
    }
    if (has_unwind) {
        patch_shdrs[patch_idx++].PointerToRawData = pdata_raw_ptr;
        patch_shdrs[patch_idx++].PointerToRawData = xdata_raw_ptr;
    }
    if (has_debug) {
        patch_shdrs[patch_idx++].PointerToRawData = debug_s_raw_ptr;
    }

    ny_free(shdrs, sizeof(Coff_Section_Header) * total_sections);
    ny_obj_buf_destroy(&symtab_buf);
    ny_obj_buf_destroy(&strtab_buf);
    ny_obj_buf_destroy(&reloc_buf);
    ny_obj_buf_destroy(&pdata_buf);
    ny_obj_buf_destroy(&xdata_buf);
    ny_obj_buf_destroy(&debug_s_buf);

    return true;
}
