# OpenGauge Agent Guide

## Scope

This directory is the complete boundary for OpenGauge. Do not place OpenGauge files in `D:\ESP32`, `OpenTrail`, or a root-level shared directory. A reusable dependency should be evaluated as a separately versioned library rather than an informal root folder.

## Current phase

OpenGauge is in architecture and proof-of-concept planning. Candidate displays and network roles are not selected or validated. Do not begin a complete gauge UI or vehicle integration before the interfaces and safety boundaries are reviewed.

## Brand and trademark safeguards

- `Limited Underground` and `Limited Underground Business` are owner-approved working identities pending attorney clearance, not cleared or registered names.
- Use `LU` only as a monogram visibly paired with the full words `Limited Underground`. Do not create or publish `LU Link`, `LU Studio`, or an `LU`-plus-number public model name such as `LU300`, `LU-300`, or `LU 300`.
- Never use `®` without documented federal registration for the exact mark and relevant goods or services. Use `™` only where an unregistered trademark symbol is appropriate.
- New public product or family names require documented preliminary screening and explicit owner approval; obtain professional clearance before permanent hardware marking, packaging, sales, or another hard-to-reverse release.
- Keep working names out of protocol fields, compatibility identifiers, device IDs, persistent schemas, API contracts, cryptographic material, and board identifiers so branding remains replaceable.
- Existing `OG-` engineering, protocol, test, and inventory identifiers remain allowed. They are OpenGauge technical identifiers, not `LU` model names.

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

## Completion and publication gate

- Follow the workspace-wide completion and publication gate in `D:\ESP32\AGENTS.md`.
- Once an OpenGauge task is implemented and validated, update every affected canonical record and dated public progress entry, commit and push the relevant public-ready OpenGauge changes, and verify the remote commit before calling the task complete.
- If accepted evidence changes public project status or V1 progress, synchronize and validate the Limited Underground website projection, commit and push the website update, deploy it, and verify the live OpenGauge status before calling the task complete.
- If no public website status changed, say so explicitly in the completion report. If any required push, synchronization, deployment, or verification is blocked, report `implementation complete; publication pending` and identify the remaining step.
- Do not bundle unrelated or unvalidated dirty-worktree changes merely to satisfy this gate, and never publish private or unsafe material.

## V1 progress completion gate

- `docs/V1_PROGRESS.json` is the canonical OpenGauge V1 progress record. Do not maintain a separate percentage in the README, firmware, or website source.
- Before calling any task complete, compare its accepted evidence with every affected V1 milestone. Planning, code volume, or an unvalidated implementation does not increase completion.
- When evidence changes a milestone, update its completion, evidence references, next gate, and `as_of` date; append a dated `change_log` entry with the newly calculated weighted overall. Never rewrite prior history.
- Milestone weights must remain positive and total exactly 100. A regression or newly discovered blocker may lower completion and must be recorded just like an increase.
- The public website projection is generated separately from this canonical record. After changing it, run the Limited Underground website's V1 sync/check flow and publish the result when website publication is in scope. If publication is not authorized or available, report the pending website synchronization explicitly.
