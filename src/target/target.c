// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/target.h"
#include "nybit/target_x86_64.h"
#include "nybit/object.h"
#include <string.h>

static void x86_target_destroy_func(void *target_fn) {
    if (!target_fn) return;
    X86_Function *fn = (X86_Function *)target_fn;
    x86_func_destroy(fn);
    ny_free(fn, sizeof(X86_Function));
}

static bool x86_target_emit_object_sysv(const Ny_Target *target, const void *encoded_mod, Ny_Object_Buffer *out_buf, Ny_Diagnostic_List *diags) {
    (void)target;
    return ny_emit_elf64_x86_64(out_buf, (const X86_Encoded_Module *)encoded_mod, diags);
}

static bool x86_target_emit_object_win64(const Ny_Target *target, const void *encoded_mod, Ny_Object_Buffer *out_buf, Ny_Diagnostic_List *diags) {
    (void)target;
    return ny_emit_coff_x86_64(out_buf, (const X86_Encoded_Module *)encoded_mod, diags);
}

const Ny_Target g_ny_target_x86_64_sysv = {
    .name = "x86_64-sysv",
    .arch = NY_ARCH_X86_64,
    .os = NY_OS_SYSV,
    .abi = NY_ABI_SYSV_AMD64,
    .info = {
        .ptr_size = 8,
        .ptr_align = 8,
        .stack_align = 16,
        .shadow_space = 0,
    },
    .lower_function = x86_lower_machine_func,
    .destroy_function = x86_target_destroy_func,
    .dump_function = x86_dump_func,
    .emit_object = x86_target_emit_object_sysv,
};

const Ny_Target g_ny_target_x86_64_win64 = {
    .name = "x86_64-windows",
    .arch = NY_ARCH_X86_64,
    .os = NY_OS_WINDOWS,
    .abi = NY_ABI_WINDOWS_X64,
    .info = {
        .ptr_size = 8,
        .ptr_align = 8,
        .stack_align = 16,
        .shadow_space = 32,
    },
    .lower_function = x86_lower_machine_func,
    .destroy_function = x86_target_destroy_func,
    .dump_function = x86_dump_func,
    .emit_object = x86_target_emit_object_win64,
};

const Ny_Target *ny_target_get_default(void) {
#if defined(_WIN32)
    return &g_ny_target_x86_64_win64;
#else
    return &g_ny_target_x86_64_sysv;
#endif
}

const Ny_Target *ny_target_find(const char *name) {
    if (!name) return NULL;
    if (strcmp(name, "x86_64") == 0 ||
        strcmp(name, "x86_64-sysv") == 0 ||
        strcmp(name, "x86_64-linux") == 0) {
        return &g_ny_target_x86_64_sysv;
    }
    if (strcmp(name, "x86_64-windows") == 0 ||
        strcmp(name, "x86_64-win64") == 0 ||
        strcmp(name, "x86_64-msvc") == 0) {
        return &g_ny_target_x86_64_win64;
    }
    return NULL;
}
