#!/usr/bin/env bash
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Le Hung Quang Minh (furimeo)
set -euo pipefail

CC="${CC:-gcc}"
STD_FLAG="-std=c23"
if ! $CC -std=c23 -E -xc /dev/null >/dev/null 2>&1; then
    STD_FLAG="-std=c2x"
fi
CFLAGS="${CFLAGS:-$STD_FLAG -Wall -Wextra -Werror -g -Iinclude -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L}"
AR="${AR:-ar}"
BIN_DIR="bin"
OBJ_DIR="bin/obj"

mkdir -p "${BIN_DIR}" "${OBJ_DIR}"

run_tests=0
run_bench=0
run_main=0
package=0

for arg in "$@"; do
    case "$arg" in
        -RunTests|--run-tests|-t) run_tests=1 ;;
        -RunBench|--run-bench|-b) run_bench=1 ;;
        -RunMain|--run-main|-m) run_main=1 ;;
        -Package|--package|-p) package=1 ;;
    esac
done

nygen_sources=()
while IFS= read -r -d '' f; do
    nygen_sources+=("$f")
done < <(find src -type f -name '*.c' ! -name 'main.c' ! -path 'src/nyjit/*' ! -path 'src/nylink/*' -print0 | sort -z)

nygen_objs=()
for src in "${nygen_sources[@]}"; do
    obj_name=$(echo "$src" | tr '/\\:' '_' | sed 's/\.c$/.o/')
    obj_path="${OBJ_DIR}/${obj_name}"
    nygen_objs+=("${obj_path}")
    $CC $CFLAGS -c "$src" -o "$obj_path"
done

rm -f bin/nygen.a bin/nygen.lib
$AR rcs bin/nygen.a "${nygen_objs[@]}"
cp bin/nygen.a bin/nygen.lib

nyjit_sources=()
while IFS= read -r -d '' f; do
    nyjit_sources+=("$f")
done < <(find src/nyjit -type f -name '*.c' -print0 | sort -z)

nyjit_objs=()
for src in "${nyjit_sources[@]}"; do
    obj_name=$(echo "$src" | tr '/\\:' '_' | sed 's/\.c$/.o/')
    obj_path="${OBJ_DIR}/${obj_name}"
    nyjit_objs+=("${obj_path}")
    $CC $CFLAGS -c "$src" -o "$obj_path"
done

rm -f bin/nyjit.a bin/nyjit.lib
$AR rcs bin/nyjit.a "${nyjit_objs[@]}"
cp bin/nyjit.a bin/nyjit.lib

nylink_sources=()
while IFS= read -r -d '' f; do
    nylink_sources+=("$f")
done < <(find src/nylink -type f -name '*.c' -print0 | sort -z)

nylink_objs=()
for src in "${nylink_sources[@]}"; do
    obj_name=$(echo "$src" | tr '/\\:' '_' | sed 's/\.c$/.o/')
    obj_path="${OBJ_DIR}/${obj_name}"
    nylink_objs+=("${obj_path}")
    $CC $CFLAGS -c "$src" -o "$obj_path"
done

rm -f bin/nylink.a bin/nylink.lib
$AR rcs bin/nylink.a "${nylink_objs[@]}"
cp bin/nylink.a bin/nylink.lib

$CC $CFLAGS src/main.c bin/nylink.a bin/nygen.a -o bin/nybit
cp -f bin/nybit bin/nybit.exe

test_sources=()
while IFS= read -r -d '' f; do
    test_sources+=("$f")
done < <(find tests -type f -name '*.c' ! -name 'bench_main.c' -print0 | sort -z)

$CC $CFLAGS -Itests "${test_sources[@]}" bin/nylink.a bin/nyjit.a bin/nygen.a -o bin/test_runner

if [ -f tests/bench_main.c ]; then
    $CC $CFLAGS tests/bench_main.c bin/nyjit.a bin/nygen.a -o bin/bench
fi

if [ "$run_tests" -eq 1 ]; then
    ./bin/test_runner
fi

if [ "$run_bench" -eq 1 ]; then
    ./bin/bench
fi

if [ "$run_main" -eq 1 ]; then
    ./bin/nybit
fi

if [ "$package" -eq 1 ]; then
    arch=$(uname -m)
    os=$(uname -s | tr '[:upper:]' '[:lower:]')
    pkg_name="nybit-${os}-${arch}"
    dist_dir="dist/${pkg_name}"
    mkdir -p dist
    rm -rf "${dist_dir}" "dist/${pkg_name}.tar.gz"
    mkdir -p "${dist_dir}/bin" "${dist_dir}/lib" "${dist_dir}/include" "${dist_dir}/docs"
    cp bin/nybit "${dist_dir}/bin/"
    cp bin/*.a "${dist_dir}/lib/"
    cp -r include/* "${dist_dir}/include/"
    cp LICENSE "${dist_dir}/"
    cp README.md "${dist_dir}/"
    cp -r docs/* "${dist_dir}/docs/"
    tar -czf "dist/${pkg_name}.tar.gz" -C dist "${pkg_name}"
fi
