# OpenGauge Agent Guide

## Scope

This directory is the complete boundary for OpenGauge. Do not place OpenGauge files in `D:\ESP32`, `OpenTrail`, or a root-level shared directory. A reusable dependency should be evaluated as a separately versioned library rather than an informal root folder.

## Current phase

OpenGauge is in architecture and proof-of-concept planning. Candidate displays and network roles are not selected or validated. Do not begin a complete gauge UI or vehicle integration before the interfaces and safety boundaries are reviewed.

## Working rules

1. Read `README.md`, `docs/ARCHITECTURE.md`, `docs/PROJECT_STATUS.md`, and `tasks/BACKLOG.md` first.
2. Preserve existing work. Do not delete, rename, or broadly restructure without a documented reason.
3. Isolate board, CAN controller/transceiver, display/touch, storage, and radio code behind interfaces.
4. Keep gateway, gauge display, GPS, and auxiliary/APU roles as separate target compositions.
5. Avoid giant `.ino` files. Keep J1939 parsing, decoding, normalization, telemetry caching, alarms, and wire codecs host-testable.
6. Never transmit raw C/C++ structs over ESP-NOW. Use explicit, versioned serialization and length/range validation.
7. Treat vehicle data as untrusted and possibly stale, unavailable, not installed, or erroneous. Never invent a numeric value.
8. This project must not perform safety-critical vehicle control without a separately reviewed fail-safe design. Early CAN work is listen-only.
9. Do not hard-code credentials, pairing secrets, private keys, or vehicle-specific identifiers.
10. Record exact hardware, wiring, termination, bus conditions, firmware/toolchain, and observed results for physical tests.

## Validation expectations

- Parser/decoder/cache/alarm/protocol work: deterministic host tests including malformed, boundary, timeout, and compatibility cases.
- Firmware: build every affected target and record the board/toolchain configuration.
- CAN/J1939 work: start with captured/synthetic frames; physical connection requires correct transceiver, voltage compatibility, isolation assessment, termination, and explicit authorization.
- Display compatibility: report measured boot time, memory, frame/update performance, touch behavior, power, and recovery—not specifications alone.

## Safety boundary

A gauge warning is supplemental instrumentation. Stale or missing gateway data must be conspicuous. Gateway/display loss must not affect vehicle operation. APU or auxiliary control remains outside the core telemetry path and requires authentication, authorization, interlocks, and independent safety analysis.
