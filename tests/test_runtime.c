// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 Le Hung Quang Minh (furimeo)
#include "test_framework.h"
#include <nylink/nylink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#include <process.h>
#include <windows.h>
#define PATH_SEP "\\"
#define WSL_EXE "wsl.exe"
#else
#include <unistd.h>
#include <sys/wait.h>
#define PATH_SEP "/"
#endif

typedef enum {
    RT_ENV_NONE = 0,
    RT_ENV_WSL,
    RT_ENV_NATIVE_LINUX,
} Rt_Env;

static Rt_Env g_rt_env = RT_ENV_NONE;
static char g_rt_interp[512] = {0};
static bool g_rt_checked = false;

#pragma pack(push, 1)
typedef struct Rt_Elf64_Ehdr {
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
} Rt_Elf64_Ehdr;

typedef struct Rt_Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Rt_Elf64_Phdr;

typedef struct Rt_Elf64_Shdr {
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
} Rt_Elf64_Shdr;

typedef struct Rt_Elf64_Sym {
    uint32_t st_name;
    uint8_t st_info;
    uint8_t st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
} Rt_Elf64_Sym;

typedef struct Rt_Elf64_Dyn {
    int64_t d_tag;
    uint64_t d_val;
} Rt_Elf64_Dyn;

typedef struct Rt_Elf64_Rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t r_addend;
} Rt_Elf64_Rela;
#pragma pack(pop)

static void rt_write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    TEST_ASSERT(f != nullptr);
    fputs(content, f);
    fclose(f);
}

static bool rt_file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) { fclose(f); return true; }
    return false;
}

static void rt_remove(const char *path) {
    remove(path);
}

static void rt_mkdir(const char *path) {
#if defined(_WIN32) || defined(_WIN64)
    _mkdir(path);
#else
    mkdir(path, 0755);
#endif
}

static void rt_rmdir(const char *path) {
#if defined(_WIN32) || defined(_WIN64)
    _rmdir(path);
#else
    rmdir(path);
#endif
}

static void rt_detect_env(void) {
    if (g_rt_checked) return;
    g_rt_checked = true;

#if defined(_WIN32) || defined(_WIN64)
    char existing_path[8192];
    DWORD path_len = GetEnvironmentVariableA("PATH", existing_path, sizeof(existing_path));
    if (path_len > 0 && strstr(existing_path, "tools\\mingw\\bin") == nullptr) {
        char full_mingw[1024];
        if (_fullpath(full_mingw, "tools\\mingw\\bin", sizeof(full_mingw)) != nullptr) {
            char new_path[10240];
            snprintf(new_path, sizeof(new_path), "%s;%s", full_mingw, existing_path);
            SetEnvironmentVariableA("PATH", new_path);
        }
    }

    FILE *pipe = popen("wsl.exe -d Debian /bin/bash -lc \"readelf -p .interp /bin/true 2>/dev/null | grep -o '/[^ ]*'\" 2>nul", "r");
    if (!pipe) {
        pipe = popen("wsl.exe /bin/bash -lc \"readelf -p .interp /bin/true 2>/dev/null | grep -o '/[^ ]*'\" 2>nul", "r");
    }
    if (pipe) {
        char buf[512];
        if (fgets(buf, sizeof(buf), pipe)) {
            size_t len = strlen(buf);
            while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r' || buf[len-1] == ' ')) {
                buf[--len] = '\0';
            }
            if (len > 0 && buf[0] == '/') {
                strncpy(g_rt_interp, buf, sizeof(g_rt_interp) - 1);
                g_rt_env = RT_ENV_WSL;
            }
        }
        pclose(pipe);
    }
#else
    FILE *f = fopen("/bin/true", "rb");
    if (!f) f = fopen("/bin/ls", "rb");
    if (f) {
        fclose(f);
        FILE *pipe = popen("readelf -p .interp /bin/true 2>/dev/null", "r");
        if (pipe) {
            char buf[512];
            while (fgets(buf, sizeof(buf), pipe)) {
                char *p = strstr(buf, "/");
                if (p) {
                    size_t l = strlen(p);
                    while (l > 0 && (p[l-1] == '\n' || p[l-1] == '\r' || p[l-1] == ' ')) p[--l] = '\0';
                    strncpy(g_rt_interp, p, sizeof(g_rt_interp) - 1);
                    g_rt_env = RT_ENV_NATIVE_LINUX;
                    break;
                }
            }
            pclose(pipe);
        }
    }
#endif
}

static const char *rt_wsl_path(const char *win_path) {
    static char wsl_buf[8][1024];
    static size_t wsl_buf_idx = 0;
    char *buf = wsl_buf[wsl_buf_idx++ % 8];
#if defined(_WIN32) || defined(_WIN64)
    char full[1024];
    if (_fullpath(full, win_path, sizeof(full)) != nullptr) {
        win_path = full;
    }
    if (win_path[1] == ':') {
        char drive = (char)tolower((unsigned char)win_path[0]);
        const char *rest = win_path + 2;
        snprintf(buf, 1024, "/mnt/%c%s", drive, rest);
        for (char *p = buf; *p; p++) {
            if (*p == '\\') *p = '/';
        }
        return buf;
    }
#endif
    snprintf(buf, 1024, "%s", win_path);
    return buf;
}

static int rt_run_script(const char *script_body, char *output, size_t output_sz) {
    if (output && output_sz > 0) output[0] = '\0';
    const char *tmp_script = "bin/_rt_tmp.sh";
    FILE *sf = fopen(tmp_script, "wb");
    if (!sf) return -1;
    fputs(script_body, sf);
    fclose(sf);

#if defined(_WIN32) || defined(_WIN64)
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "wsl.exe -d Debian /bin/bash %s 2>nul", rt_wsl_path(tmp_script));
#else
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "/bin/bash %s", tmp_script);
#endif
    int rc = -1;
    if (output && output_sz > 0) {
        FILE *pipe = popen(cmd, "r");
        if (pipe) {
            size_t off = 0;
            char buf[256];
            while (fgets(buf, sizeof(buf), pipe)) {
                size_t l = strlen(buf);
                if (off + l < output_sz) {
                    memcpy(output + off, buf, l);
                    off += l;
                    output[off] = '\0';
                }
            }
            rc = pclose(pipe);
        }
    } else {
        rc = system(cmd);
    }
    remove(tmp_script);
    return rc;
}

static const char *RT_FOO_C = "int foo(void) { return 42; }\n";

static const char *RT_DATA_C = "int value = 42;\n";

static const char *RT_START_FOO_C =
    "extern int foo(void);\n"
    "\n"
    "void _start(void) {\n"
    "    int ret = foo();\n"
    "    __asm__ volatile (\n"
    "        \"mov %0, %%edi\\n\"\n"
    "        \"mov $60, %%eax\\n\"\n"
    "        \"syscall\\n\"\n"
    "        :: \"r\"(ret)\n"
    "    );\n"
    "    __builtin_unreachable();\n"
    "}\n";

static const char *RT_START_DATA_C =
    "extern int value;\n"
    "\n"
    "void _start(void) {\n"
    "    int ret = value;\n"
    "    __asm__ volatile (\n"
    "        \"mov %0, %%edi\\n\"\n"
    "        \"mov $60, %%eax\\n\"\n"
    "        \"syscall\\n\"\n"
    "        :: \"r\"(ret)\n"
    "    );\n"
    "    __builtin_unreachable();\n"
    "}\n";

static const char *RT_START_BAR_C =
    "extern int bar(void);\n"
    "\n"
    "void _start(void) {\n"
    "    int ret = bar();\n"
    "    __asm__ volatile (\n"
    "        \"mov %0, %%edi\\n\"\n"
    "        \"mov $60, %%eax\\n\"\n"
    "        \"syscall\\n\"\n"
    "        :: \"r\"(ret)\n"
    "    );\n"
    "    __builtin_unreachable();\n"
    "}\n";

static const char *RT_START_EXIT42_C =
    "void _start(void) {\n"
    "    __asm__ volatile (\n"
    "        \"mov $42, %edi\\n\"\n"
    "        \"mov $60, %eax\\n\"\n"
    "        \"syscall\\n\"\n"
    "    );\n"
    "    __builtin_unreachable();\n"
    "}\n";

static const char *RT_DLOPEN_C =
    "#include <dlfcn.h>\n"
    "#include <stdio.h>\n"
    "int main(int argc, char **argv) {\n"
    "    void *h = dlopen(argv[1], RTLD_NOW);\n"
    "    if (!h) { fprintf(stderr, \"%s\\n\", dlerror()); return 1; }\n"
    "    int (*fn)(void) = (int(*)(void))dlsym(h, \"foo\");\n"
    "    if (!fn) { fprintf(stderr, \"%s\\n\", dlerror()); dlclose(h); return 2; }\n"
    "    int v = fn();\n"
    "    dlclose(h);\n"
    "    return v == 42 ? 0 : 3;\n"
    "}\n";

static bool rt_wsl_compile(const char *win_src, const char *win_obj, const char *flags) {
    char script[1024];
    snprintf(script, sizeof(script),
             "#!/bin/bash\n"
             "gcc %s -c '%s' -o '%s' 2>/dev/null\n",
             flags, rt_wsl_path(win_src), rt_wsl_path(win_obj));
    return rt_run_script(script, nullptr, 0) == 0;
}

static bool rt_setup_wsl_test_dir(const char *wsl_dir) {
    char script[1024];
    snprintf(script, sizeof(script),
             "#!/bin/bash\n"
             "mkdir -p '%s' 2>/dev/null\n",
             wsl_dir);
    return rt_run_script(script, nullptr, 0) == 0;
}

static void rt_cleanup_wsl_dir(const char *wsl_dir) {
    char script[1024];
    snprintf(script, sizeof(script),
             "#!/bin/bash\n"
             "rm -rf '%s' 2>/dev/null\n",
             wsl_dir);
    rt_run_script(script, nullptr, 0);
}

static bool rt_copy_to_wsl(const char *win_src, const char *wsl_dst) {
    char script[2048];
    snprintf(script, sizeof(script),
             "#!/bin/bash\n"
             "cp '%s' '%s' 2>/dev/null\n",
             rt_wsl_path(win_src), wsl_dst);
    return rt_run_script(script, nullptr, 0) == 0;
}

static bool rt_make_exec_in_wsl(const char *wsl_dir, const char *file) {
    char script[1024];
    snprintf(script, sizeof(script),
             "#!/bin/bash\n"
             "chmod +x '%s/%s' 2>/dev/null\n",
             wsl_dir, file);
    return rt_run_script(script, nullptr, 0) == 0;
}

static int rt_exec_in_wsl(const char *wsl_cwd, const char *binary, char *result, size_t result_sz) {
    char script[2048];
    snprintf(script, sizeof(script),
             "#!/bin/bash\n"
             "cd '%s' || exit 127\n"
             "./'%s' 2>_rt_err.txt\n"
             "EXIT_CODE=$?\n"
             "echo $EXIT_CODE > _rt_exit.txt\n"
             "cat _rt_err.txt\n"
             "exit $EXIT_CODE\n",
             wsl_cwd, binary);
    int rc = rt_run_script(script, result, result_sz);

    char read_script[512];
    char exit_buf[64] = {0};
    snprintf(read_script, sizeof(read_script),
             "#!/bin/bash\n"
             "cat '%s/_rt_exit.txt' 2>/dev/null\n"
             "rm -f '%s/_rt_exit.txt' '%s/_rt_err.txt' 2>/dev/null\n",
             wsl_cwd, wsl_cwd, wsl_cwd);
    rt_run_script(read_script, exit_buf, sizeof(exit_buf));
    int exit_code = -1;
    if (exit_buf[0] != '\0') {
        exit_code = atoi(exit_buf);
    } else {
        exit_code = rc;
    }
    return exit_code;
}


void test_linux_runtime_env_detection(void) {
    rt_detect_env();

    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_env_detection: skipped (no Linux/WSL environment)\n");
        g_tests_run++;
        return;
    }

    TEST_ASSERT(g_rt_interp[0] == '/');
    TEST_ASSERT(strlen(g_rt_interp) > 0);

    printf("test_linux_runtime_env_detection: ok (interp=%s, env=%s)\n",
           g_rt_interp,
           g_rt_env == RT_ENV_WSL ? "WSL" : "native-Linux");
}

void test_linux_runtime_shared_lib_structure(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_shared_lib_structure: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_so";
    const char *wsl_dir = "/tmp/nybit_rt_so";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_so/foo.c", RT_FOO_C);

    TEST_ASSERT(rt_wsl_compile("bin/rt_so/foo.c", "bin/rt_so/foo.o", "-fPIC -ffreestanding"));
    TEST_ASSERT(rt_file_exists("bin/rt_so/foo.o"));

    int link_rc = system("bin" PATH_SEP "nybit.exe link --target=elf64 --shared --soname=libfoo.so bin/rt_so/foo.o -o bin/rt_so/libfoo.so 2>nul");
#if !defined(_WIN32) && !defined(_WIN64)
    if (g_rt_env == RT_ENV_NATIVE_LINUX) {
        link_rc = system("./bin/nybit link --target=elf64 --shared --soname=libfoo.so bin/rt_so/foo.o -o bin/rt_so/libfoo.so");
    }
#endif
    TEST_ASSERT_EQ(link_rc, 0);
    TEST_ASSERT(rt_file_exists("bin/rt_so/libfoo.so"));

    FILE *f = fopen("bin/rt_so/libfoo.so", "rb");
    TEST_ASSERT(f != nullptr);

    Rt_Elf64_Ehdr ehdr;
    TEST_ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    TEST_ASSERT_EQ(ehdr.e_ident[0], 0x7F);
    TEST_ASSERT_EQ(ehdr.e_ident[1], 'E');
    TEST_ASSERT_EQ(ehdr.e_ident[2], 'L');
    TEST_ASSERT_EQ(ehdr.e_ident[3], 'F');
    TEST_ASSERT_EQ(ehdr.e_type, 3);
    TEST_ASSERT_EQ(ehdr.e_machine, 62);

    Rt_Elf64_Phdr phdrs[16];
    fseek(f, (long)ehdr.e_phoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(phdrs, sizeof(Rt_Elf64_Phdr), ehdr.e_phnum, f), ehdr.e_phnum);

    bool has_pt_dynamic = false;
    bool has_pt_relro = false;
    for (uint16_t p = 0; p < ehdr.e_phnum; p++) {
        if (phdrs[p].p_type == 2) has_pt_dynamic = true;
        if (phdrs[p].p_type == 0x6474e552) has_pt_relro = true;
    }
    TEST_ASSERT(has_pt_dynamic);
    TEST_ASSERT(has_pt_relro);

    Rt_Elf64_Shdr *shdrs = (Rt_Elf64_Shdr *)ny_alloc_zero(ehdr.e_shnum * sizeof(Rt_Elf64_Shdr));
    fseek(f, (long)ehdr.e_shoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(shdrs, sizeof(Rt_Elf64_Shdr), ehdr.e_shnum, f), ehdr.e_shnum);

    TEST_ASSERT(ehdr.e_shstrndx < ehdr.e_shnum);
    char *shstrtab = (char *)ny_alloc_zero(shdrs[ehdr.e_shstrndx].sh_size);
    fseek(f, (long)shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(shstrtab, 1, shdrs[ehdr.e_shstrndx].sh_size, f), shdrs[ehdr.e_shstrndx].sh_size);

    uint16_t dynsym_idx = 0, dynstr_idx = 0, dynamic_idx = 0;
    for (uint16_t s = 1; s < ehdr.e_shnum; s++) {
        const char *sname = shstrtab + shdrs[s].sh_name;
        if (strcmp(sname, ".dynsym") == 0) dynsym_idx = s;
        if (strcmp(sname, ".dynstr") == 0) dynstr_idx = s;
        if (strcmp(sname, ".dynamic") == 0) dynamic_idx = s;
    }
    TEST_ASSERT(dynsym_idx != 0);
    TEST_ASSERT(dynstr_idx != 0);
    TEST_ASSERT(dynamic_idx != 0);

    size_t nsym = shdrs[dynsym_idx].sh_size / sizeof(Rt_Elf64_Sym);
    Rt_Elf64_Sym *dynsym = (Rt_Elf64_Sym *)ny_alloc_zero(shdrs[dynsym_idx].sh_size);
    fseek(f, (long)shdrs[dynsym_idx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(dynsym, sizeof(Rt_Elf64_Sym), nsym, f), nsym);

    char *dynstr = (char *)ny_alloc_zero(shdrs[dynstr_idx].sh_size);
    fseek(f, (long)shdrs[dynstr_idx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(dynstr, 1, shdrs[dynstr_idx].sh_size, f), shdrs[dynstr_idx].sh_size);

    bool found_foo = false;
    bool foo_default_vis = false;
    for (size_t s = 1; s < nsym; s++) {
        const char *name = dynstr + dynsym[s].st_name;
        if (strcmp(name, "foo") == 0) {
            found_foo = true;
            TEST_ASSERT_EQ((dynsym[s].st_info >> 4) & 0xF, 1);
            TEST_ASSERT_EQ(dynsym[s].st_info & 0xF, 2);
            foo_default_vis = (dynsym[s].st_other == 0);
            break;
        }
    }
    TEST_ASSERT(found_foo);
    TEST_ASSERT(foo_default_vis);

    size_t ndyn = shdrs[dynamic_idx].sh_size / sizeof(Rt_Elf64_Dyn);
    Rt_Elf64_Dyn *dyn_entries = (Rt_Elf64_Dyn *)ny_alloc_zero(shdrs[dynamic_idx].sh_size);
    fseek(f, (long)shdrs[dynamic_idx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(dyn_entries, sizeof(Rt_Elf64_Dyn), ndyn, f), ndyn);

    bool has_soname = false;
    bool has_strtab = false;
    bool has_symtab = false;
    bool soname_correct = false;
    for (size_t d = 0; d < ndyn; d++) {
        if (dyn_entries[d].d_tag == 14) {
            has_soname = true;
            const char *soname_str = dynstr + dyn_entries[d].d_val;
            if (strcmp(soname_str, "libfoo.so") == 0) soname_correct = true;
        }
        if (dyn_entries[d].d_tag == 5) has_strtab = true;
        if (dyn_entries[d].d_tag == 6) has_symtab = true;
    }
    TEST_ASSERT(has_soname);
    TEST_ASSERT(soname_correct);
    TEST_ASSERT(has_strtab);
    TEST_ASSERT(has_symtab);

    ny_free(dyn_entries, shdrs[dynamic_idx].sh_size);
    ny_free(dynstr, shdrs[dynstr_idx].sh_size);
    ny_free(dynsym, shdrs[dynsym_idx].sh_size);
    ny_free(shstrtab, shdrs[ehdr.e_shstrndx].sh_size);
    ny_free(shdrs, ehdr.e_shnum * sizeof(Rt_Elf64_Shdr));
    fclose(f);

    rt_remove("bin/rt_so/libfoo.so");
    rt_remove("bin/rt_so/foo.o");
    rt_remove("bin/rt_so/foo.c");
    rt_rmdir("bin/rt_so");
    rt_cleanup_wsl_dir(wsl_dir);
}

void test_linux_runtime_pie_structure(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_pie_structure: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_pie";
    const char *wsl_dir = "/tmp/nybit_rt_pie";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_pie/foo.c", RT_FOO_C);
    rt_write_file("bin/rt_pie/start.c", RT_START_FOO_C);

    TEST_ASSERT(rt_wsl_compile("bin/rt_pie/foo.c", "bin/rt_pie/foo.o", "-fPIC -ffreestanding"));
    TEST_ASSERT(rt_wsl_compile("bin/rt_pie/start.c", "bin/rt_pie/main.o", "-fPIE -ffreestanding -nostdlib"));

    TEST_ASSERT(rt_file_exists("bin/rt_pie/foo.o"));
    TEST_ASSERT(rt_file_exists("bin/rt_pie/main.o"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --shared --soname=libfoo.so bin/rt_pie/foo.o -o bin/rt_pie/libfoo.so 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);

    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_pie -lfoo bin/rt_pie/main.o -o bin/rt_pie/app 2>nul",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --shared --soname=libfoo.so bin/rt_pie/foo.o -o bin/rt_pie/libfoo.so");
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_pie -lfoo bin/rt_pie/main.o -o bin/rt_pie/app",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    TEST_ASSERT(rt_file_exists("bin/rt_pie/app"));

    FILE *f = fopen("bin/rt_pie/app", "rb");
    TEST_ASSERT(f != nullptr);

    Rt_Elf64_Ehdr ehdr;
    TEST_ASSERT_EQ(fread(&ehdr, sizeof(ehdr), 1, f), 1);
    TEST_ASSERT_EQ(ehdr.e_type, 3);

    Rt_Elf64_Phdr phdrs[16];
    fseek(f, (long)ehdr.e_phoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(phdrs, sizeof(Rt_Elf64_Phdr), ehdr.e_phnum, f), ehdr.e_phnum);

    bool has_pt_interp = false;
    bool has_pt_phdr = false;
    bool has_pt_dynamic = false;
    for (uint16_t p = 0; p < ehdr.e_phnum; p++) {
        if (phdrs[p].p_type == 3) has_pt_interp = true;
        if (phdrs[p].p_type == 6) has_pt_phdr = true;
        if (phdrs[p].p_type == 2) has_pt_dynamic = true;
    }
    TEST_ASSERT(has_pt_interp);
    TEST_ASSERT(has_pt_phdr);
    TEST_ASSERT(has_pt_dynamic);

    Rt_Elf64_Shdr *shdrs = (Rt_Elf64_Shdr *)ny_alloc_zero(ehdr.e_shnum * sizeof(Rt_Elf64_Shdr));
    fseek(f, (long)ehdr.e_shoff, SEEK_SET);
    TEST_ASSERT_EQ(fread(shdrs, sizeof(Rt_Elf64_Shdr), ehdr.e_shnum, f), ehdr.e_shnum);

    char *shstrtab = (char *)ny_alloc_zero(shdrs[ehdr.e_shstrndx].sh_size);
    fseek(f, (long)shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(shstrtab, 1, shdrs[ehdr.e_shstrndx].sh_size, f), shdrs[ehdr.e_shstrndx].sh_size);

    uint16_t dynamic_idx = 0;
    uint16_t dynstr_idx = 0;
    for (uint16_t s = 1; s < ehdr.e_shnum; s++) {
        const char *sname = shstrtab + shdrs[s].sh_name;
        if (strcmp(sname, ".dynamic") == 0) dynamic_idx = s;
        if (strcmp(sname, ".dynstr") == 0) dynstr_idx = s;
    }
    TEST_ASSERT(dynamic_idx != 0);
    TEST_ASSERT(dynstr_idx != 0);

    size_t ndyn = shdrs[dynamic_idx].sh_size / sizeof(Rt_Elf64_Dyn);
    Rt_Elf64_Dyn *dyn_entries = (Rt_Elf64_Dyn *)ny_alloc_zero(shdrs[dynamic_idx].sh_size);
    fseek(f, (long)shdrs[dynamic_idx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(dyn_entries, sizeof(Rt_Elf64_Dyn), ndyn, f), ndyn);

    char *dynstr = (char *)ny_alloc_zero(shdrs[dynstr_idx].sh_size);
    fseek(f, (long)shdrs[dynstr_idx].sh_offset, SEEK_SET);
    TEST_ASSERT_EQ(fread(dynstr, 1, shdrs[dynstr_idx].sh_size, f), shdrs[dynstr_idx].sh_size);

    bool has_dt_needed = false;
    bool needed_correct = false;
    bool has_dt_runpath = false;
    bool runpath_correct = false;
    bool has_dt_flags_1 = false;
    bool has_dt_debug = false;
    for (size_t d = 0; d < ndyn; d++) {
        if (dyn_entries[d].d_tag == 1) {
            has_dt_needed = true;
            if (strcmp(dynstr + dyn_entries[d].d_val, "libfoo.so") == 0) needed_correct = true;
        }
        if (dyn_entries[d].d_tag == 29) {
            has_dt_runpath = true;
            if (strcmp(dynstr + dyn_entries[d].d_val, "$ORIGIN") == 0) runpath_correct = true;
        }
        if (dyn_entries[d].d_tag == (int64_t)0x6ffffffbLL) has_dt_flags_1 = true;
        if (dyn_entries[d].d_tag == 21) has_dt_debug = true;
    }
    TEST_ASSERT(has_dt_needed);
    TEST_ASSERT(needed_correct);
    TEST_ASSERT(has_dt_runpath);
    TEST_ASSERT(runpath_correct);
    TEST_ASSERT(has_dt_flags_1);
    TEST_ASSERT(has_dt_debug);

    ny_free(dynstr, shdrs[dynstr_idx].sh_size);
    ny_free(dyn_entries, shdrs[dynamic_idx].sh_size);
    ny_free(shstrtab, shdrs[ehdr.e_shstrndx].sh_size);
    ny_free(shdrs, ehdr.e_shnum * sizeof(Rt_Elf64_Shdr));
    fclose(f);

    rt_remove("bin/rt_pie/app");
    rt_remove("bin/rt_pie/libfoo.so");
    rt_remove("bin/rt_pie/foo.o");
    rt_remove("bin/rt_pie/main.o");
    rt_remove("bin/rt_pie/foo.c");
    rt_remove("bin/rt_pie/start.c");
    rt_rmdir("bin/rt_pie");
    rt_cleanup_wsl_dir(wsl_dir);
}

void test_linux_runtime_e2e_function_import(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_e2e_function_import: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_e2e";
    const char *wsl_dir = "/tmp/nybit_rt_e2e";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_e2e/foo.c", RT_FOO_C);
    rt_write_file("bin/rt_e2e/start.c", RT_START_FOO_C);

    TEST_ASSERT(rt_wsl_compile("bin/rt_e2e/foo.c", "bin/rt_e2e/foo.o", "-fPIC -ffreestanding"));
    TEST_ASSERT(rt_wsl_compile("bin/rt_e2e/start.c", "bin/rt_e2e/main.o", "-fPIE -ffreestanding -nostdlib"));

    TEST_ASSERT(rt_file_exists("bin/rt_e2e/foo.o"));
    TEST_ASSERT(rt_file_exists("bin/rt_e2e/main.o"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --shared --soname=libfoo.so bin/rt_e2e/foo.o -o bin/rt_e2e/libfoo.so 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);

    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_e2e -lfoo bin/rt_e2e/main.o -o bin/rt_e2e/app 2>nul",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --shared --soname=libfoo.so bin/rt_e2e/foo.o -o bin/rt_e2e/libfoo.so");
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_e2e -lfoo bin/rt_e2e/main.o -o bin/rt_e2e/app",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    TEST_ASSERT(rt_file_exists("bin/rt_e2e/app"));
    TEST_ASSERT(rt_file_exists("bin/rt_e2e/libfoo.so"));

    rt_copy_to_wsl("bin/rt_e2e/app", wsl_dir);
    rt_copy_to_wsl("bin/rt_e2e/libfoo.so", wsl_dir);
    rt_make_exec_in_wsl(wsl_dir, "app");
    rt_make_exec_in_wsl(wsl_dir, "libfoo.so");

    char result[512] = {0};
    int exit_code = rt_exec_in_wsl(wsl_dir, "app", result, sizeof(result));
    TEST_ASSERT_EQ(exit_code, 42);

    rt_remove("bin/rt_e2e/app");
    rt_remove("bin/rt_e2e/libfoo.so");
    rt_remove("bin/rt_e2e/foo.o");
    rt_remove("bin/rt_e2e/main.o");
    rt_remove("bin/rt_e2e/foo.c");
    rt_remove("bin/rt_e2e/start.c");
    rt_rmdir("bin/rt_e2e");
    rt_cleanup_wsl_dir(wsl_dir);
}

void test_linux_runtime_e2e_data_import(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_e2e_data_import: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_data";
    const char *wsl_dir = "/tmp/nybit_rt_data";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_data/data.c", RT_DATA_C);
    rt_write_file("bin/rt_data/start.c", RT_START_DATA_C);

    TEST_ASSERT(rt_wsl_compile("bin/rt_data/data.c", "bin/rt_data/data.o", "-fPIC -ffreestanding"));
    TEST_ASSERT(rt_wsl_compile("bin/rt_data/start.c", "bin/rt_data/main.o", "-fPIC -ffreestanding -nostdlib"));

    TEST_ASSERT(rt_file_exists("bin/rt_data/data.o"));
    TEST_ASSERT(rt_file_exists("bin/rt_data/main.o"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --shared --soname=libdata.so bin/rt_data/data.o -o bin/rt_data/libdata.so 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);

    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_data -ldata bin/rt_data/main.o -o bin/rt_data/app 2>nul",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --shared --soname=libdata.so bin/rt_data/data.o -o bin/rt_data/libdata.so");
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_data -ldata bin/rt_data/main.o -o bin/rt_data/app",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    TEST_ASSERT(rt_file_exists("bin/rt_data/app"));
    TEST_ASSERT(rt_file_exists("bin/rt_data/libdata.so"));

    rt_copy_to_wsl("bin/rt_data/app", wsl_dir);
    rt_copy_to_wsl("bin/rt_data/libdata.so", wsl_dir);
    rt_make_exec_in_wsl(wsl_dir, "app");
    rt_make_exec_in_wsl(wsl_dir, "libdata.so");

    char result[512] = {0};
    int exit_code = rt_exec_in_wsl(wsl_dir, "app", result, sizeof(result));
    TEST_ASSERT_EQ(exit_code, 42);

    rt_remove("bin/rt_data/app");
    rt_remove("bin/rt_data/libdata.so");
    rt_remove("bin/rt_data/data.o");
    rt_remove("bin/rt_data/main.o");
    rt_remove("bin/rt_data/data.c");
    rt_remove("bin/rt_data/start.c");
    rt_rmdir("bin/rt_data");
    rt_cleanup_wsl_dir(wsl_dir);
}

void test_linux_runtime_dlopen_supplemental(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_dlopen_supplemental: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_dl";
    const char *wsl_dir = "/tmp/nybit_rt_dl";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_dl/foo.c", RT_FOO_C);
    TEST_ASSERT(rt_wsl_compile("bin/rt_dl/foo.c", "bin/rt_dl/foo.o", "-fPIC -ffreestanding"));
    TEST_ASSERT(rt_file_exists("bin/rt_dl/foo.o"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --shared --soname=libfoo.so bin/rt_dl/foo.o -o bin/rt_dl/libfoo.so 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --shared --soname=libfoo.so bin/rt_dl/foo.o -o bin/rt_dl/libfoo.so");
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    TEST_ASSERT(rt_file_exists("bin/rt_dl/libfoo.so"));

    rt_copy_to_wsl("bin/rt_dl/libfoo.so", wsl_dir);

    char dl_script[2048];
    snprintf(dl_script, sizeof(dl_script),
             "#!/bin/bash\n"
             "cat > '%s/dltest.c' <<\"ENDC\"\n%s\nENDC\n"
             "gcc -o '%s/dltest' '%s/dltest.c' -ldl 2>/dev/null\n"
             "cd '%s'\n"
             "LD_LIBRARY_PATH=. ./'dltest' ./'libfoo.so' 2>/dev/null\n",
             wsl_dir, RT_DLOPEN_C, wsl_dir, wsl_dir, wsl_dir);
    int dl_exit = rt_run_script(dl_script, nullptr, 0);
    TEST_ASSERT_EQ(dl_exit, 0);

    rt_remove("bin/rt_dl/libfoo.so");
    rt_remove("bin/rt_dl/foo.o");
    rt_remove("bin/rt_dl/foo.c");
    rt_rmdir("bin/rt_dl");
    rt_cleanup_wsl_dir(wsl_dir);
}

void test_linux_runtime_negative_missing_so(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_negative_missing_so: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_neg";
    const char *wsl_dir = "/tmp/nybit_rt_neg";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_neg/start.c", RT_START_EXIT42_C);
    TEST_ASSERT(rt_wsl_compile("bin/rt_neg/start.c", "bin/rt_neg/main.o", "-fPIE -ffreestanding -nostdlib"));
    TEST_ASSERT(rt_file_exists("bin/rt_neg/main.o"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start --needed=libmissing.so bin/rt_neg/main.o -o bin/rt_neg/app 2>nul",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start --needed=libmissing.so bin/rt_neg/main.o -o bin/rt_neg/app",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    rt_copy_to_wsl("bin/rt_neg/app", wsl_dir);
    rt_make_exec_in_wsl(wsl_dir, "app");

    char result[512] = {0};
    int exit_code = rt_exec_in_wsl(wsl_dir, "app", result, sizeof(result));

    TEST_ASSERT(exit_code != 42);
    TEST_ASSERT(exit_code != 0);

    rt_remove("bin/rt_neg/app");
    rt_remove("bin/rt_neg/main.o");
    rt_remove("bin/rt_neg/start.c");
    rt_rmdir("bin/rt_neg");
    rt_cleanup_wsl_dir(wsl_dir);
}

void test_linux_runtime_negative_missing_symbol(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_negative_missing_symbol: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_nosym";
    rt_mkdir(win_dir);

    rt_write_file("bin/rt_nosym/foo.c", RT_FOO_C);
    rt_write_file("bin/rt_nosym/start.c", RT_START_BAR_C);

    TEST_ASSERT(rt_wsl_compile("bin/rt_nosym/foo.c", "bin/rt_nosym/foo.o", "-fPIC -ffreestanding"));
    TEST_ASSERT(rt_wsl_compile("bin/rt_nosym/start.c", "bin/rt_nosym/main.o", "-fPIE -ffreestanding -nostdlib"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --shared --soname=libfoo.so bin/rt_nosym/foo.o -o bin/rt_nosym/libfoo.so 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);

    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_nosym -lfoo bin/rt_nosym/main.o -o bin/rt_nosym/app 2>nul",
             g_rt_interp);
    int rc = system(link_cmd);
    TEST_ASSERT(rc != 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --shared --soname=libfoo.so bin/rt_nosym/foo.o -o bin/rt_nosym/libfoo.so 2>/dev/null");
    TEST_ASSERT_EQ(system(link_cmd), 0);

    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_nosym -lfoo bin/rt_nosym/main.o -o bin/rt_nosym/app 2>/dev/null",
             g_rt_interp);
    int rc = system(link_cmd);
    TEST_ASSERT(rc != 0);
#endif

    rt_remove("bin/rt_nosym/libfoo.so");
    rt_remove("bin/rt_nosym/foo.o");
    rt_remove("bin/rt_nosym/main.o");
    rt_remove("bin/rt_nosym/foo.c");
    rt_remove("bin/rt_nosym/start.c");
    rt_rmdir("bin/rt_nosym");
}

void test_linux_runtime_negative_bad_interp(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_negative_bad_interp: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_badinterp";
    const char *wsl_dir = "/tmp/nybit_rt_badinterp";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_badinterp/start.c", RT_START_EXIT42_C);
    TEST_ASSERT(rt_wsl_compile("bin/rt_badinterp/start.c", "bin/rt_badinterp/main.o", "-fPIE -ffreestanding -nostdlib"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=/nonexistent/ld-linux-x86-64.so.2 --entry=_start bin/rt_badinterp/main.o -o bin/rt_badinterp/app 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --pie --dynamic-linker=/nonexistent/ld-linux-x86-64.so.2 --entry=_start bin/rt_badinterp/main.o -o bin/rt_badinterp/app 2>/dev/null");
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    rt_copy_to_wsl("bin/rt_badinterp/app", wsl_dir);
    rt_make_exec_in_wsl(wsl_dir, "app");

    char result[512] = {0};
    int exit_code = rt_exec_in_wsl(wsl_dir, "app", result, sizeof(result));
    TEST_ASSERT(exit_code != 42);
    TEST_ASSERT(exit_code != 0);

    rt_remove("bin/rt_badinterp/app");
    rt_remove("bin/rt_badinterp/main.o");
    rt_remove("bin/rt_badinterp/start.c");
    rt_rmdir("bin/rt_badinterp");
    rt_cleanup_wsl_dir(wsl_dir);
}

void test_linux_runtime_pie_aslr(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_pie_aslr: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_aslr";
    const char *wsl_dir = "/tmp/nybit_rt_aslr";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_aslr/start.c", RT_START_EXIT42_C);
    TEST_ASSERT(rt_wsl_compile("bin/rt_aslr/start.c", "bin/rt_aslr/main.o", "-fPIE -ffreestanding -nostdlib"));
    TEST_ASSERT(rt_file_exists("bin/rt_aslr/main.o"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --entry=_start bin/rt_aslr/main.o -o bin/rt_aslr/app 2>nul",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --entry=_start bin/rt_aslr/main.o -o bin/rt_aslr/app",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    rt_copy_to_wsl("bin/rt_aslr/app", wsl_dir);
    rt_make_exec_in_wsl(wsl_dir, "app");

    int exit_codes[3] = {-1, -1, -1};
    for (int i = 0; i < 3; i++) {
        char result[256] = {0};
        exit_codes[i] = rt_exec_in_wsl(wsl_dir, "app", result, sizeof(result));
    }

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQ(exit_codes[i], 42);
    }

    rt_remove("bin/rt_aslr/app");
    rt_remove("bin/rt_aslr/main.o");
    rt_remove("bin/rt_aslr/start.c");
    rt_rmdir("bin/rt_aslr");
    rt_cleanup_wsl_dir(wsl_dir);
}

void test_linux_runtime_determinism(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_determinism: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_det";
    rt_mkdir(win_dir);

    rt_write_file("bin/rt_det/foo.c", RT_FOO_C);
    rt_write_file("bin/rt_det/start.c", RT_START_FOO_C);

    TEST_ASSERT(rt_wsl_compile("bin/rt_det/foo.c", "bin/rt_det/foo.o", "-fPIC -ffreestanding"));
    TEST_ASSERT(rt_wsl_compile("bin/rt_det/start.c", "bin/rt_det/main.o", "-fPIE -ffreestanding -nostdlib"));
    TEST_ASSERT(rt_file_exists("bin/rt_det/foo.o"));
    TEST_ASSERT(rt_file_exists("bin/rt_det/main.o"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --shared --soname=libfoo.so bin/rt_det/foo.o -o bin/rt_det/libfoo_a.so 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --shared --soname=libfoo.so bin/rt_det/foo.o -o bin/rt_det/libfoo_b.so 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);

    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start bin/rt_det/libfoo_a.so bin/rt_det/main.o -o bin/rt_det/app_a 2>nul",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start bin/rt_det/libfoo_b.so bin/rt_det/main.o -o bin/rt_det/app_b 2>nul",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd), "./bin/nybit link --target=elf64 --shared --soname=libfoo.so bin/rt_det/foo.o -o bin/rt_det/libfoo_a.so");
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd), "./bin/nybit link --target=elf64 --shared --soname=libfoo.so bin/rt_det/foo.o -o bin/rt_det/libfoo_b.so");
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd), "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start bin/rt_det/libfoo_a.so bin/rt_det/main.o -o bin/rt_det/app_a", g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd), "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start bin/rt_det/libfoo_b.so bin/rt_det/main.o -o bin/rt_det/app_b", g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    FILE *f1 = fopen("bin/rt_det/libfoo_a.so", "rb");
    FILE *f2 = fopen("bin/rt_det/libfoo_b.so", "rb");
    TEST_ASSERT(f1 != nullptr && f2 != nullptr);

    fseek(f1, 0, SEEK_END);
    fseek(f2, 0, SEEK_END);
    long s1 = ftell(f1);
    long s2 = ftell(f2);
    TEST_ASSERT_EQ(s1, s2);

    fseek(f1, 0, SEEK_SET);
    fseek(f2, 0, SEEK_SET);
    uint8_t b1[512], b2[512];
    bool so_match = true;
    while (s1 > 0) {
        size_t n = s1 > 512 ? 512 : (size_t)s1;
        TEST_ASSERT_EQ(fread(b1, 1, n, f1), n);
        TEST_ASSERT_EQ(fread(b2, 1, n, f2), n);
        if (memcmp(b1, b2, n) != 0) { so_match = false; break; }
        s1 -= n;
    }
    TEST_ASSERT(so_match);
    fclose(f2);
    fclose(f1);

    f1 = fopen("bin/rt_det/app_a", "rb");
    f2 = fopen("bin/rt_det/app_b", "rb");
    TEST_ASSERT(f1 != nullptr && f2 != nullptr);

    fseek(f1, 0, SEEK_END);
    fseek(f2, 0, SEEK_END);
    s1 = ftell(f1);
    s2 = ftell(f2);
    TEST_ASSERT_EQ(s1, s2);

    fseek(f1, 0, SEEK_SET);
    fseek(f2, 0, SEEK_SET);
    bool app_match = true;
    while (s1 > 0) {
        size_t n = s1 > 512 ? 512 : (size_t)s1;
        TEST_ASSERT_EQ(fread(b1, 1, n, f1), n);
        TEST_ASSERT_EQ(fread(b2, 1, n, f2), n);
        if (memcmp(b1, b2, n) != 0) { app_match = false; break; }
        s1 -= n;
    }
    TEST_ASSERT(app_match);
    fclose(f2);
    fclose(f1);

    rt_remove("bin/rt_det/app_a");
    rt_remove("bin/rt_det/app_b");
    rt_remove("bin/rt_det/libfoo_a.so");
    rt_remove("bin/rt_det/libfoo_b.so");
    rt_remove("bin/rt_det/foo.o");
    rt_remove("bin/rt_det/main.o");
    rt_remove("bin/rt_det/foo.c");
    rt_remove("bin/rt_det/start.c");
    rt_rmdir("bin/rt_det");
}

void test_linux_runtime_readelf_inspection(void) {
    rt_detect_env();
    if (g_rt_env == RT_ENV_NONE) {
        printf("test_linux_runtime_readelf_inspection: skipped (no Linux/WSL)\n");
        g_tests_run++;
        return;
    }

    const char *win_dir = "bin/rt_insp";
    const char *wsl_dir = "/tmp/nybit_rt_insp";
    rt_mkdir(win_dir);
    rt_setup_wsl_test_dir(wsl_dir);

    rt_write_file("bin/rt_insp/foo.c", RT_FOO_C);
    rt_write_file("bin/rt_insp/start.c", RT_START_FOO_C);

    TEST_ASSERT(rt_wsl_compile("bin/rt_insp/foo.c", "bin/rt_insp/foo.o", "-fPIC -ffreestanding"));
    TEST_ASSERT(rt_wsl_compile("bin/rt_insp/start.c", "bin/rt_insp/main.o", "-fPIE -ffreestanding -nostdlib"));

    char link_cmd[4096];
#if defined(_WIN32) || defined(_WIN64)
    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --shared --soname=libfoo.so bin/rt_insp/foo.o -o bin/rt_insp/libfoo.so 2>nul");
    TEST_ASSERT_EQ(system(link_cmd), 0);

    snprintf(link_cmd, sizeof(link_cmd),
             "bin\\nybit.exe link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_insp -lfoo bin/rt_insp/main.o -o bin/rt_insp/app 2>nul",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#else
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --shared --soname=libfoo.so bin/rt_insp/foo.o -o bin/rt_insp/libfoo.so");
    TEST_ASSERT_EQ(system(link_cmd), 0);
    snprintf(link_cmd, sizeof(link_cmd),
             "./bin/nybit link --target=elf64 --pie --dynamic-linker=%s --rpath=$ORIGIN --entry=_start -Lbin/rt_insp -lfoo bin/rt_insp/main.o -o bin/rt_insp/app",
             g_rt_interp);
    TEST_ASSERT_EQ(system(link_cmd), 0);
#endif

    rt_copy_to_wsl("bin/rt_insp/app", wsl_dir);
    rt_copy_to_wsl("bin/rt_insp/libfoo.so", wsl_dir);

    char insp_script[2048];
    snprintf(insp_script, sizeof(insp_script),
             "#!/bin/bash\n"
             "readelf -h '%s/libfoo.so' >/dev/null && "
             "readelf -l '%s/libfoo.so' >/dev/null && "
             "readelf -d '%s/libfoo.so' >/dev/null && "
             "readelf -s '%s/libfoo.so' >/dev/null && "
             "readelf -r '%s/libfoo.so' >/dev/null && "
             "readelf -h '%s/app' >/dev/null && "
             "readelf -l '%s/app' >/dev/null && "
             "readelf -d '%s/app' >/dev/null && "
             "readelf -s '%s/app' >/dev/null && "
             "readelf -r '%s/app' >/dev/null\n",
             wsl_dir, wsl_dir, wsl_dir, wsl_dir, wsl_dir,
             wsl_dir, wsl_dir, wsl_dir, wsl_dir, wsl_dir);
    int rc = rt_run_script(insp_script, nullptr, 0);
    TEST_ASSERT_EQ(rc, 0);

    rt_remove("bin/rt_insp/app");
    rt_remove("bin/rt_insp/libfoo.so");
    rt_remove("bin/rt_insp/foo.o");
    rt_remove("bin/rt_insp/main.o");
    rt_remove("bin/rt_insp/foo.c");
    rt_remove("bin/rt_insp/start.c");
    rt_rmdir("bin/rt_insp");
    rt_cleanup_wsl_dir(wsl_dir);
}

