[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$compilerCandidates = @()
if ($env:OPENGAUGE_MSYS2_ROOT) {
    $compilerCandidates += @(
        (Join-Path $env:OPENGAUGE_MSYS2_ROOT 'ucrt64\bin\g++.exe'),
        (Join-Path $env:OPENGAUGE_MSYS2_ROOT 'mingw64\bin\g++.exe')
    )
}
$compilerCandidates += @(
    'C:\msys64\ucrt64\bin\g++.exe',
    'C:\msys64\mingw64\bin\g++.exe'
)
$pathCompiler = Get-Command g++.exe -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($pathCompiler) {
    $compilerCandidates += $pathCompiler.Source
}
$compiler = $compilerCandidates |
    Select-Object -Unique |
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
        [string[]] $IncludeDirectories,

        [Parameter(Mandatory)]
        [string[]] $Sources,

        [bool] $Run = $true
    )

    $output = Join-Path $buildDirectory "$Name.exe"
    $arguments = @(
        '-std=c++17',
        '-Wall',
        '-Wextra',
        '-Wpedantic',
        '-Werror',
        '-O2'
    )
    foreach ($includeDirectory in $IncludeDirectories) {
        $arguments += @('-I', $includeDirectory)
    }

    & $compiler @arguments @Sources '-o' $output
    if ($LASTEXITCODE -ne 0) {
        throw "$Description compilation failed with exit code $LASTEXITCODE."
    }

    if ($Run) {
        & $output
        if ($LASTEXITCODE -ne 0) {
            throw "$Description tests failed with exit code $LASTEXITCODE."
        }
    }
}

Invoke-HostTest `
    -Name 'critical_alert_round_trip_cli' `
    -Description 'Critical alert physical round-trip verifier CLI' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'tools\CriticalAlertRoundTripCli.cpp')
    ) `
    -Run $false

Invoke-HostTest `
    -Name 'critical_alert_export_tests' `
    -Description 'Critical alert exporter' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_export_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_outbox_tests' `
    -Description 'Application-acknowledged critical alert outbox' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_outbox_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_outbox_checkpoint_tests' `
    -Description 'Critical alert outbox checkpoint codec' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_outbox_checkpoint_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_ack_tests' `
    -Description 'Critical alert acknowledgement codec' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_ack_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_recovery_checkpoint_tests' `
    -Description 'Coordinated critical alert recovery checkpoint codec' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_recovery_checkpoint_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_system_recovery_tests' `
    -Description 'Atomic authorization, ACK, and outbox recovery' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_system_recovery_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_system_recovery_store_tests' `
    -Description 'Recoverable atomic alert-system state store' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_store.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support\fake_critical_alert_system_recovery_storage.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_system_recovery_store_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_system_recovery_kv_storage_tests' `
    -Description 'Target-shaped alert-system recovery key/value adapter' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_store.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_kv_storage.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_boot.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_save.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_system_recovery_kv_storage_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_system_recovery_boot_tests' `
    -Description 'Typed alert-system boot recovery coordinator' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_store.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_boot.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support\fake_critical_alert_system_recovery_storage.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_system_recovery_boot_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_system_recovery_save_tests' `
    -Description 'Verified alert-system save and trusted-floor ordering' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_store.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_boot.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_save.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support\fake_critical_alert_system_recovery_storage.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_system_recovery_save_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_system_recovery_repair_tests' `
    -Description 'Known-degraded alert-system recovery repair' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_store.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_boot.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_save.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_repair.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support\fake_critical_alert_system_recovery_storage.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_system_recovery_repair_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_system_recovery_status_tests' `
    -Description 'Redacted alert-system recovery operator status' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_system_recovery_status.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_system_recovery_status_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_ack_ingress_tests' `
    -Description 'Authenticated critical alert ACK ingress and correlation' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_ack_ingress_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_recovery_tests' `
    -Description 'Coordinated live critical alert recovery' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_recovery_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_ack_checkpoint_tests' `
    -Description 'Critical alert ACK binding and replay checkpoint' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_ack_checkpoint_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_recovery_store_tests' `
    -Description 'Recoverable coordinated critical alert storage' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_recovery_store.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support\fake_critical_alert_recovery_storage.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_recovery_store_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_ack_rejection_policy_tests' `
    -Description 'Critical alert ACK rejection retry and terminal policy' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_ack_rejection_policy_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alert_ack_checkpoint_store_tests' `
    -Description 'Critical alert ACK recoverable checkpoint store' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_outbox_checkpoint.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_ingress.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert_ack_checkpoint_store.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\test_support\fake_critical_alert_ack_checkpoint_storage.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_ack_checkpoint_store_tests.cpp')
    )

Invoke-HostTest `
    -Name 'diagnostics_tests' `
    -Description 'Bounded diagnostics foundation' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\diagnostics\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\diagnostics\src\diagnostics.cpp'),
        (Join-Path $projectRoot 'tests\host\diagnostics_tests.cpp')
    )

Invoke-HostTest `
    -Name 'recovery_status_diagnostics_tests' `
    -Description 'Versioned redacted recovery status diagnostics' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\diagnostics\include'),
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\diagnostics\src\diagnostics.cpp'),
        (Join-Path $projectRoot 'firmware\components\diagnostics\src\recovery_status_diagnostics.cpp'),
        (Join-Path $projectRoot 'tests\host\recovery_status_diagnostics_tests.cpp')
    )

Invoke-HostTest `
    -Name 'critical_alarm_exporter_tests' `
    -Description 'Alarm-to-critical-alert exporter' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\alarm\include'),
        (Join-Path $projectRoot 'firmware\components\integration\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alarm_exporter.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alarm_exporter_tests.cpp')
    )

Invoke-HostTest `
    -Name 'alarm_engine_tests' `
    -Description 'Bounded normalized-signal alarm engine' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\alarm\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\alarm\src\alarm_engine.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'tests\host\alarm_engine_tests.cpp')
    )

Invoke-HostTest `
    -Name 'alarm_cache_evaluator_tests' `
    -Description 'Cache-to-alarm bounded evaluator' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\alarm\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\alarm\src\alarm_engine.cpp'),
        (Join-Path $projectRoot 'firmware\components\alarm\src\alarm_cache_evaluator.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\telemetry_cache.cpp'),
        (Join-Path $projectRoot 'tests\host\alarm_cache_evaluator_tests.cpp')
    )

Invoke-HostTest `
    -Name 'j1939_identifier_tests' `
    -Description 'J1939 identifier parser' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\can\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_identifier.cpp'),
        (Join-Path $projectRoot 'tests\host\j1939_identifier_tests.cpp')
    )

Invoke-HostTest `
    -Name 'normalized_signal_tests' `
    -Description 'Normalized signal model' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\telemetry\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'tests\host\normalized_signal_tests.cpp')
    )

Invoke-HostTest `
    -Name 'j1939_decoder_tests' `
    -Description 'J1939 decoder registry' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\can\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_identifier.cpp'),
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_decoder.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\telemetry_cache.cpp'),
        (Join-Path $projectRoot 'tests\host\j1939_decoder_tests.cpp')
    )

Invoke-HostTest `
    -Name 'gps_fix_tracker_tests' `
    -Description 'Normalized GPS fix/quality/age tracker' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\gps\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\gps\src\gps_fix_tracker.cpp'),
        (Join-Path $projectRoot 'tests\host\gps_fix_tracker_tests.cpp')
    )

Invoke-HostTest `
    -Name 'can_receiver_tests' `
    -Description 'Passive CAN receiver abstraction' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\can\include'),
        (Join-Path $projectRoot 'firmware\components\can\test_support'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\can\src\can_receiver.cpp'),
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_identifier.cpp'),
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_decoder.cpp'),
        (Join-Path $projectRoot 'firmware\components\can\test_support\fake_can_receiver.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\telemetry_cache.cpp'),
        (Join-Path $projectRoot 'tests\host\can_receiver_tests.cpp')
    )

Invoke-HostTest `
    -Name 'telemetry_cache_tests' `
    -Description 'Telemetry cache' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\telemetry\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\telemetry_cache.cpp'),
        (Join-Path $projectRoot 'tests\host\telemetry_cache_tests.cpp')
    )

Invoke-HostTest `
    -Name 'esp_now_transport_tests' `
    -Description 'ESP-NOW transport contract' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\wireless\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\wireless\src\esp_now_transport.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support\fake_esp_now_transport.cpp'),
        (Join-Path $projectRoot 'tests\host\esp_now_transport_tests.cpp')
    )

Invoke-HostTest `
    -Name 'peer_authorization_tests' `
    -Description 'Bounded peer approval and authorization registry' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'tests\host\peer_authorization_tests.cpp')
    )

Invoke-HostTest `
    -Name 'telemetry_packet_tests' `
    -Description 'Gateway-to-gauge telemetry packet' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\telemetry\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\esp_now_transport.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_packet.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support\fake_esp_now_transport.cpp'),
        (Join-Path $projectRoot 'tests\host\telemetry_packet_tests.cpp')
    )

Invoke-HostTest `
    -Name 'peer_authorization_checkpoint_tests' `
    -Description 'Peer authorization restart checkpoint' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'tests\host\peer_authorization_checkpoint_tests.cpp')
    )

Invoke-HostTest `
    -Name 'peer_authorization_checkpoint_store_tests' `
    -Description 'Recoverable peer authorization checkpoint store' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\identity\include'),
        (Join-Path $projectRoot 'firmware\components\identity\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization.cpp'),
        (Join-Path $projectRoot 'firmware\components\identity\src\peer_authorization_checkpoint_store.cpp'),
        (Join-Path $projectRoot 'firmware\components\identity\test_support\fake_peer_authorization_checkpoint_storage.cpp'),
        (Join-Path $projectRoot 'tests\host\peer_authorization_checkpoint_store_tests.cpp')
    )

Invoke-HostTest `
    -Name 'gauge_telemetry_receiver_tests' `
    -Description 'Bounded gauge telemetry receiver' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\telemetry\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\esp_now_transport.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_packet.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\gauge_telemetry_receiver.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support\fake_esp_now_transport.cpp'),
        (Join-Path $projectRoot 'tests\host\gauge_telemetry_receiver_tests.cpp')
    )

Invoke-HostTest `
    -Name 'gauge_view_model_tests' `
    -Description 'Display-neutral gauge view model' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\display\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\display\src\gauge_view_model.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\esp_now_transport.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_packet.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\gauge_telemetry_receiver.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support\fake_esp_now_transport.cpp'),
        (Join-Path $projectRoot 'tests\host\gauge_view_model_tests.cpp')
    )

Invoke-HostTest `
    -Name 'gauge_trend_buffer_tests' `
    -Description 'Bounded fail-visible gauge trend buffer' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\display\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\display\src\gauge_trend_buffer.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_packet.cpp'),
        (Join-Path $projectRoot 'tests\host\gauge_trend_buffer_tests.cpp')
    )

Invoke-HostTest `
    -Name 'gauge_layout_tests' `
    -Description 'Versioned recoverable gauge layout storage' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\configuration\include'),
        (Join-Path $projectRoot 'firmware\components\configuration\test_support'),
        (Join-Path $projectRoot 'firmware\components\display\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\configuration\src\gauge_layout.cpp'),
        (Join-Path $projectRoot 'firmware\components\configuration\test_support\fake_gauge_layout_storage.cpp'),
        (Join-Path $projectRoot 'firmware\components\display\src\gauge_view_model.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\esp_now_transport.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_packet.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\gauge_telemetry_receiver.cpp'),
        (Join-Path $projectRoot 'tests\host\gauge_layout_tests.cpp')
    )

Invoke-HostTest `
    -Name 'telemetry_publish_scheduler_tests' `
    -Description 'Telemetry publish scheduler' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\telemetry\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\esp_now_transport.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_packet.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_publish_scheduler.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support\fake_esp_now_transport.cpp'),
        (Join-Path $projectRoot 'tests\host\telemetry_publish_scheduler_tests.cpp')
    )

Invoke-HostTest `
    -Name 'update_boot_guard_tests' `
    -Description 'OTA trial confirmation and rollback guard' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\update\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\update\src\update_boot_guard.cpp'),
        (Join-Path $projectRoot 'tests\host\update_boot_guard_tests.cpp')
    )

Invoke-HostTest `
    -Name 'telemetry_gateway_publisher_tests' `
    -Description 'Telemetry gateway publisher composition' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\can\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_identifier.cpp'),
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_decoder.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\telemetry_cache.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\esp_now_transport.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_packet.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_publish_scheduler.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_gateway_publisher.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support\fake_esp_now_transport.cpp'),
        (Join-Path $projectRoot 'tests\host\telemetry_gateway_publisher_tests.cpp')
    )

Invoke-HostTest `
    -Name 'gateway_telemetry_loop_tests' `
    -Description 'Bounded gateway telemetry loop' `
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\can\include'),
        (Join-Path $projectRoot 'firmware\components\can\test_support'),
        (Join-Path $projectRoot 'firmware\components\gateway\include'),
        (Join-Path $projectRoot 'firmware\components\telemetry\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\include'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\can\src\can_receiver.cpp'),
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_identifier.cpp'),
        (Join-Path $projectRoot 'firmware\components\can\src\j1939_decoder.cpp'),
        (Join-Path $projectRoot 'firmware\components\can\test_support\fake_can_receiver.cpp'),
        (Join-Path $projectRoot 'firmware\components\gateway\src\gateway_telemetry_loop.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\normalized_signal.cpp'),
        (Join-Path $projectRoot 'firmware\components\telemetry\src\telemetry_cache.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\esp_now_transport.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_packet.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_publish_scheduler.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\src\telemetry_gateway_publisher.cpp'),
        (Join-Path $projectRoot 'firmware\components\wireless\test_support\fake_esp_now_transport.cpp'),
        (Join-Path $projectRoot 'tests\host\gateway_telemetry_loop_tests.cpp')
    )

$python = Get-Command python -ErrorAction SilentlyContinue
if ($null -eq $python) {
    throw 'Python was not found for publication-safety validation.'
}

& $python.Source (Join-Path $projectRoot 'tests\host\publication_safety_tests.py')
if ($LASTEXITCODE -ne 0) {
    throw "Publication-safety scanner tests failed with exit code $LASTEXITCODE."
}

& $python.Source (Join-Path $projectRoot 'tools\Test-PublicationSafety.py') --root $projectRoot
if ($LASTEXITCODE -ne 0) {
    throw "Publication-safety tracked-content scan failed with exit code $LASTEXITCODE."
}
