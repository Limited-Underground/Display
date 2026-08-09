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
        [string[]] $IncludeDirectories,

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
        '-O2'
    )
    foreach ($includeDirectory in $IncludeDirectories) {
        $arguments += @('-I', $includeDirectory)
    }

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
    -IncludeDirectories @(
        (Join-Path $projectRoot 'firmware\components\integration\include')
    ) `
    -Sources @(
        (Join-Path $projectRoot 'firmware\components\integration\src\critical_alert.cpp'),
        (Join-Path $projectRoot 'tests\host\critical_alert_export_tests.cpp')
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
