[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$compilerCandidates = @(
    'C:\msys64\ucrt64\bin\g++.exe',
    'C:\msys64\mingw64\bin\g++.exe'
)
$compiler = $compilerCandidates |
    Where-Object { Test-Path -LiteralPath $_ } |
    Select-Object -First 1

if (-not $compiler) {
    throw 'A native GCC compiler was not found. Install the MSYS2 UCRT64 GCC toolchain first.'
}

$compilerDirectory = Split-Path -Parent $compiler
$env:Path = "$compilerDirectory;$env:Path"

$buildDirectory = Join-Path $projectRoot "build\host-tests\run-$PID"
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null

function Invoke-HostTest {
    param(
        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [string] $Description,

        [Parameter(Mandatory)]
        [string] $IncludeDirectory,

        [Parameter(Mandatory)]
        [string[]] $Sources
    )

    $output = Join-Path $buildDirectory "$Name.exe"
    $arguments = @(
        '-std=c++17',
        '-Wall',
        '-Wextra',
        '-Wpedantic',
        '-Werror',
        '-O2',
        '-I', $IncludeDirectory
    )

    & $compiler @arguments @Sources '-o' $output
    if ($LASTEXITCODE -ne 0) {
        throw "$Description compilation failed with exit code $LASTEXITCODE."
    }

    & $output
    if ($LASTEXITCODE -ne 0) {
        throw "$Description tests failed with exit code $LASTEXITCODE."
    }
}

Invoke-HostTest `
    -Name 'critical_alert_export_tests' `
    -Description 'Critical alert exporter' `
    -IncludeDirectory (Join-Path $projectRoot 'firmware\components\integration\include') `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_export_tests.cpp')
    )

Invoke-HostTest `
    -Name 'j1939_identifier_tests' `
    -Description 'J1939 identifier parser' `
    -IncludeDirectory (Join-Path $projectRoot 'firmware\components\can\include') `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_identifier.cpp'),
        (Join-Path $projectRoot 'tests\host\j1939_identifier_tests.cpp')
    )

Invoke-HostTest `
    -Name 'normalized_signal_tests' `
    -Description 'Normalized signal model' `
    -IncludeDirectory (Join-Path $projectRoot 'firmware\components\telemetry\include') `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'tests\host\normalized_signal_tests.cpp')
    )
