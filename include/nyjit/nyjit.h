// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#ifndef NYJIT_H
#define NYJIT_H

#include <nygen/nygen.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Nyjit_Module Nyjit_Module;

typedef struct Nyjit_Symbol {
    const char *name;
    const void *addr;
} Nyjit_Symbol;

typedef const void *(*Nyjit_Resolver)(const char *name, void *user_data);

typedef struct Nyjit_Config {
    const Nyjit_Symbol *symbols;
    size_t symbol_count;
    Nyjit_Resolver resolver;
    void *resolver_user_data;
} Nyjit_Config;

void nyjit_config_init(Nyjit_Config *config);
Nyjit_Module *nyjit_create(const Nyjit_Config *config);
bool nyjit_link(Nyjit_Module *jit, const Nygen_Encoded_Module *module,
                Nygen_Diagnostic **out_diags, size_t *out_diag_count);
void nyjit_diagnostics_destroy(Nygen_Diagnostic *diags, size_t diag_count);
void *nyjit_lookup(Nyjit_Module *jit, const char *symbol_name);
void nyjit_destroy(Nyjit_Module *jit);

#ifdef __cplusplus
}
#endif

#endif /* NYJIT_H */
