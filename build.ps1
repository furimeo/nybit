param(
    [string]$Compiler = "$PSScriptRoot\tools\mingw\bin\gcc.exe",
    [switch]$RunTests,
    [switch]$RunMain,
    [switch]$RunBench
)
# SPDX-License-Identifier: MPL-2.0
# Copyright (c) 2026 Le Hung Quang Minh (furimeo)

if (-not (Test-Path $Compiler)) {
    $Compiler = "gcc"
}

$compilerDir = Split-Path $Compiler
if ($compilerDir -and (Test-Path $compilerDir)) {
    $env:PATH = "$compilerDir;$env:PATH"
}

$Ar = Join-Path $compilerDir "ar.exe"
if (-not (Test-Path $Ar)) {
    $Ar = "ar"
}

if (-not (Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}

$objDir = "bin/obj"
if (-not (Test-Path $objDir)) {
    New-Item -ItemType Directory -Path $objDir | Out-Null
}

$coreSources = (Get-ChildItem -Recurse -Filter *.c src | Where-Object { $_.Name -ne "main.c" }).FullName
$testSources = (Get-ChildItem -Recurse -Filter *.c tests | Where-Object { $_.Name -ne "bench_main.c" }).FullName

$coreObjs = @()
foreach ($src in $coreSources) {
    $rel = Resolve-Path -Relative $src
    $objName = ($rel -replace '[\\/:]', '_') -replace '\.c$', '.o'
    $objPath = "$objDir/$objName"
    $coreObjs += $objPath
    & $Compiler -std=c23 -Wall -Wextra -Werror -g -Iinclude -c $src -o $objPath
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

& $Ar rcs bin/nygen.lib $coreObjs
if ($LASTEXITCODE -ne 0) { exit 1 }

& $Compiler -std=c23 -Wall -Wextra -Werror -g -Iinclude src/main.c bin/nygen.lib -o bin/nybit.exe
if ($LASTEXITCODE -ne 0) { exit 1 }

& $Compiler -std=c23 -Wall -Wextra -Werror -g -Iinclude -Itests $testSources bin/nygen.lib -o bin/test_runner.exe
if ($LASTEXITCODE -ne 0) { exit 1 }

if (Test-Path "tests/bench_main.c") {
    & $Compiler -std=c23 -Wall -Wextra -Werror -g -Iinclude tests/bench_main.c bin/nygen.lib -o bin/bench.exe
    if ($LASTEXITCODE -ne 0) { exit 1 }
}

if ($RunTests) {
    & "bin/test_runner.exe"
}

if ($RunBench) {
    & "bin/bench.exe"
}

if ($RunMain) {
    & "bin/nybit.exe"
}
