# OpenGauge Agent Guide

## Applicability and project identity

C:\lu\AGENTS.md applies in full. This file adds OpenGauge-specific constraints and may not weaken the workspace rules. Stop and report any conflict.

- Canonical engineering root: C:\lu\OpenGauge
- The customer-facing repository and product name is Display; OpenGauge, opengauge, and OG- remain the engineering identifiers.
- Do not create a separate C:\lu\Display directory or place OpenGauge source in D:\ESP32, C:\lu\OpenTrail, or a shared root directory.
- OpenGauge owns listen-only CAN/J1939 ingestion, normalized vehicle telemetry, gauge rendering, and vehicle validation.
- Do not begin a complete gauge UI or vehicle integration until the applicable interfaces and safety boundaries have been reviewed and accepted.

After the minimal workspace bootstrap, read only project documents relevant to the task. For implementation that changes product behavior or architecture, consult README.md, docs\ARCHITECTURE.md, docs\PROJECT_STATUS.md, and tasks\BACKLOG.md as applicable.

## Architecture boundaries

1. Isolate board, CAN controller/transceiver, display/touch, storage, and radio code behind interfaces.
2. Keep gateway, gauge display, GPS, and auxiliary/APU roles as separate target compositions.
3. Avoid giant .ino files. Keep J1939 parsing, decoding, normalization, telemetry caching, alarms, and wire codecs bounded and host-testable.
4. Never transmit raw C or C++ structs over ESP-NOW. Use explicit, versioned serialization with length and range validation.
5. Treat vehicle data as untrusted and possibly stale, unavailable, not installed, invalid, or erroneous. Represent those states explicitly and never invent a numeric value.
6. Early CAN work is listen-only. Do not perform safety-critical vehicle control without a separately reviewed and accepted fail-safe design, explicit scope, and explicit authorization.
7. Do not hard-code credentials, pairing secrets, private keys, or vehicle-specific identifiers.

## OpenTrail relationship

- Only bounded, normalized, versioned alerts may cross into OpenTrail.
- Raw CAN/J1939, VINs, unrestricted text, credentials, keys, and vehicle-control commands must not enter OpenTrail, LoRa, or Trail Server.
- Cross-project contracts must be versioned, independently implemented, and backed by mirrored normative fixtures.
- Transport authentication, authorization, replay protection, and key lifecycle must be explicit; CRC alone is only corruption detection.
- OpenGauge and OpenTrail remain optional to one another.

## Vehicle and hardware safety

- Parser, decoder, cache, alarm, and protocol work requires deterministic host tests including malformed, boundary, timeout, and compatibility cases.
- Build every affected target and record the exact board and toolchain configuration.
- Start CAN/J1939 work with captured or synthetic frames.
- Before physical vehicle connection, verify the correct transceiver, voltage compatibility, protection, isolation assessment, termination, and explicit owner authorization.
- Record exact hardware, wiring, termination, bus conditions, firmware/toolchain, and observed results for physical tests.
- Report display compatibility using measured boot time, memory, frame/update performance, touch behavior, power, and recovery rather than specifications alone.
- Gauge warnings are supplemental instrumentation. Stale or missing gateway data must be conspicuous.
- Gateway or display loss must not affect vehicle operation.
- APU or auxiliary control remains outside the core telemetry path and requires authentication, authorization, interlocks, and independent safety analysis.

## Brand and trademark safeguards

- Limited Underground and Limited Underground Business are owner-approved working identities pending attorney clearance, not cleared or registered names.
- Use LU only as a monogram visibly paired with Limited Underground. Do not create or publish LU Link, LU Studio, or an LU-plus-number public model name such as LU300, LU-300, or LU 300.
- Never use ® without documented federal registration. Use ™ only where appropriate for an unregistered mark.
- New public product or family names require preliminary screening, explicit owner approval, and professional clearance before permanent marking, packaging, sales, or another hard-to-reverse release.
- Keep working names out of protocol fields, compatibility identifiers, device IDs, persistent schemas, API contracts, cryptographic material, and board identifiers.
- Existing OG- identifiers remain technical identifiers, not public model names.

## Documentation and progress authority

- docs\PROJECT_STATUS.md owns detailed current engineering state.
- tasks\BACKLOG.md owns project sequencing and acceptance gates.
- docs\V1_PROGRESS.json owns detailed V1 progress mechanics.
- C:\lu\.tracker\PROJECTS\OpenGauge.md owns only a dated workspace summary and links.
- C:\lu\.tracker\CURRENT-FOCUS.md solely owns any active objective, blocker, next action, and acceptance criteria.
- Planning, code volume, and unvalidated implementation do not increase progress.
- Progress history is append-only; milestone weights must remain positive and total 100; regressions may lower completion.
- The public website is a generated sanitized projection and never progress authority.

## Completion and publication

- Follow C:\lu\AGENTS.md and C:\lu\.tracker for workspace authority.
- Documentation does not authorize commit, push, PR creation, website work, deployment, or remote verification.
- When a distinct publication operation is explicitly authorized, publish only validated public-ready scope through the intended repository and independently verify the remote result only when that verification is separately authorized.
- Website synchronization or deployment requires separate explicit scope and authorization.
- State explicitly when accepted evidence did not change public website status.
- If implementation is complete but a required publication operation is unavailable or unauthorized, report implementation complete; publication pending and name the exact remaining action.
- Never bundle unrelated or unvalidated dirty-worktree changes and never publish private or unsafe material.