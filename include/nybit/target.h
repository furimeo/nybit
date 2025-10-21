// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYBIT_TARGET_H
#define NYBIT_TARGET_H

#include "nybit/support.h"
#include "nybit/ir.h"
#include "nybit/machine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Ny_Target_Arch {
    NY_ARCH_X86_64 = 0,
    NY_ARCH_AARCH64,
    NY_ARCH_RISCV64,
} Ny_Target_Arch;

typedef enum Ny_Target_OS {
    NY_OS_SYSV = 0,
    NY_OS_WINDOWS,
    NY_OS_DARWIN,
} Ny_Target_OS;

typedef enum Ny_Target_ABI {
    NY_ABI_SYSV_AMD64 = 0,
    NY_ABI_WINDOWS_X64,
    NY_ABI_AAPCS64,
} Ny_Target_ABI;

typedef struct Ny_Target_Info {
    uint8_t ptr_size;
    uint8_t ptr_align;
    uint8_t stack_align;
    uint8_t shadow_space;
} Ny_Target_Info;

typedef struct Ny_Target Ny_Target;
typedef struct Ny_Object_Buffer Ny_Object_Buffer;

struct Ny_Target {
    const char *name;
    Ny_Target_Arch arch;
    Ny_Target_OS os;
    Ny_Target_ABI abi;
    Ny_Target_Info info;

    bool (*lower_function)(const Ny_Target *target, const Ny_Machine_Function *mfn, void **out_target_fn, Ny_Diagnostic_List *diags);
    void (*destroy_function)(void *target_fn);
    char *(*dump_function)(const void *target_fn, Ny_Arena *scratch);
    bool (*emit_object)(const Ny_Target *target, const void *encoded_mod, Ny_Object_Buffer *out_buf, Ny_Diagnostic_List *diags);
};

const Ny_Target *ny_target_get_default(void);
const Ny_Target *ny_target_find(const char *name);

extern const Ny_Target g_ny_target_x86_64_sysv;
extern const Ny_Target g_ny_target_x86_64_win64;

#ifdef __cplusplus
}
#endif

#endif
