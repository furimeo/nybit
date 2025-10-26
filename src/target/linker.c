// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "nybit/object.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

static const char *find_compiler(void) {
    const char *cc = getenv("CC");
    if (cc && cc[0] != '\0') return cc;

#if defined(_WIN32)
    static const char *local_gcc = "tools\\mingw\\bin\\gcc.exe";
    FILE *f = fopen(local_gcc, "rb");
    if (f) {
        fclose(f);
        return local_gcc;
    }
    if (system("gcc --version > nul 2>&1") == 0) {
        return "gcc";
    }
#endif
    return "gcc";
}

bool ny_link_executable_with_extra(const char *const *obj_paths, size_t obj_count, const char *out_exe_path, const Ny_Target *target, Ny_Diagnostic_List *diags) {
    (void)target;
    if (!obj_paths || obj_count == 0 || !out_exe_path) {
        if (diags) ny_diagnostic_list_append(diags, "linker error: invalid null arguments to ny_link_executable");
        return false;
    }

    for (size_t i = 0; i < obj_count; i++) {
        FILE *fobj = fopen(obj_paths[i], "rb");
        if (!fobj) {
            if (diags) {
                char msg[256];
                snprintf(msg, sizeof(msg), "linker error: cannot open input object file '%s'", obj_paths[i]);
                ny_diagnostic_list_append(diags, msg);
            }
            return false;
        }
        fclose(fobj);
    }

    const char *cc = find_compiler();
    char cmd[2048];
    size_t pos = 0;

    int written = snprintf(cmd + pos, sizeof(cmd) - pos, "%s", cc);
    if (written > 0) pos += (size_t)written;

    for (size_t i = 0; i < obj_count; i++) {
        written = snprintf(cmd + pos, sizeof(cmd) - pos, " %s", obj_paths[i]);
        if (written > 0) pos += (size_t)written;
    }

#if defined(_WIN32)
    written = snprintf(cmd + pos, sizeof(cmd) - pos, " -o %s > nul 2>&1", out_exe_path);
#else
    written = snprintf(cmd + pos, sizeof(cmd) - pos, " -o %s > /dev/null 2>&1", out_exe_path);
#endif
    if (written > 0) pos += (size_t)written;

    int ret = system(cmd);
    if (ret != 0) {
        if (diags) ny_diagnostic_list_append(diags, "linker error: platform linker invocation failed");
        return false;
    }

    return true;
}

bool ny_link_executable(const char *obj_path, const char *out_exe_path, const Ny_Target *target, Ny_Diagnostic_List *diags) {
    const char *objs[1] = { obj_path };
    return ny_link_executable_with_extra(objs, 1, out_exe_path, target, diags);
}
