param(
    [string]$Compiler = "$PSScriptRoot\tools\mingw\bin\gcc.exe",
    [switch]$RunTests,
    [switch]$RunMain
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

if (-not (Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}

$coreSources = (Get-ChildItem -Recurse -Filter *.c src | Where-Object { $_.Name -ne "main.c" }).FullName
$testSources = (Get-ChildItem -Recurse -Filter *.c tests).FullName

& $Compiler -std=c23 -Wall -Wextra -Werror -g -Iinclude src/main.c $coreSources -o bin/nybit.exe
if ($LASTEXITCODE -ne 0) { exit 1 }

& $Compiler -std=c23 -Wall -Wextra -Werror -g -Iinclude -Itests $testSources $coreSources -o bin/test_runner.exe
if ($LASTEXITCODE -ne 0) { exit 1 }

if ($RunTests) {
    & "bin/test_runner.exe"
}

if ($RunMain) {
    & "bin/nybit.exe"
}
