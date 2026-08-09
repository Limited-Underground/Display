# OpenGauge Engineering Backlog

Statuses: `done` means the documented acceptance criteria are evidenced; `planned` means no implementation claim.

## Foundation

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-001 | done | Project bootstrap and repository structure | Self-contained directories, agent guide, README, architecture, status, and backlog exist |
| OG-002 | done | Initial architecture documentation | Roles, layers, safety/failure boundaries, protocols, and architecture gates documented |
| OG-003 | planned | Hardware abstraction contracts | CAN, clock, ESP-NOW, display/touch, storage, power, and logging contracts reviewed with fakes |
| OG-003A | planned | Hardware/use-case inventory | Exact vehicle, desired signals, board/display/CAN candidates, wiring/power/environment constraints recorded |
| OG-017 | planned | Diagnostics/logging foundation | Levels/filtering/redaction plus CAN, wireless, cache, decoder, render, and reset counters tested |

## CAN/J1939 and telemetry core

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-004 | planned | CAN driver abstraction | Listen-only receive/error/overflow metadata and fake-frame source demonstrated; no vehicle dependency |
| OG-005 | planned | J1939 identifier parser | Recommended first task: host-tested 29-bit ID fields and PGN rules with PDU1/PDU2, boundary, malformed/unsupported cases |
| OG-006 | planned | PGN decoder registry | Explicit decoder registration and one small reference PGN fixture set; unavailable/error encodings tested |
| OG-007 | planned | Normalized signal model | Stable IDs, typed values, canonical units, quality/source/time/age, and invalid/unknown semantics tested |
| OG-008 | planned | Telemetry cache/subscriptions | Fresh/stale transitions, per-signal thresholds, update/change notifications, capacity, concurrency, and restart behavior tested |
| OG-014 | planned | Alarm framework | Threshold, hysteresis, debounce, severity, latch/ack, stale behavior, and rate-limited events tested |

## Wireless gauge network

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-009 | planned | ESP-NOW transport abstraction | Peer/send/receive/errors/metadata contract plus fake transport; channel/security constraints documented |
| OG-010 | planned | Gateway-to-gauge packet format | Explicit versioned codec, byte/rate budget, telemetry batching, sequence/age, corrupt/incompatible fixtures |
| OG-011 | planned | Gauge pairing and identity | Threat model, discovery/approval, key storage, replacement/reset/revoke/recovery, and peer-limit tests |
| OG-010A | planned | Physical ESP-NOW characterization | Packet loss, latency, peers, interference/coexistence, reboot recovery, and update-rate evidence on selected boards |

## Display and persistence

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-012 | planned | Gauge rendering framework | View-model boundary and representative numeric/needle/bar/warning/stale screens benchmarked on candidate hardware |
| OG-013 | planned | Persistent gauge configuration | Versioning, validation, migration, safe defaults, write wear, import/reset/recovery tested |
| OG-012A | planned | Display hardware feasibility | Exact board build plus measured RAM/flash, boot, frame/update time, touch, brightness, power, and recovery |

## Modules and lifecycle

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-015 | planned | GPS node architecture | Normalized fix/quality/age model, topology, update rate, time, loss, and simulation documented |
| OG-016 | planned | OTA/recovery architecture | Authenticity, compatibility, partitions, interruption, health confirmation, rollback, fleet sequence, and USB recovery validated |
| OG-018 | done | OpenTrail alert interface | Mirrored specifications and three normative fixtures define an explicit 64-byte frame for seven alert types, canonical milli-units, assert/clear lifecycle IDs, optional UTC/value fields, and CRC-32. Four OpenGauge exporter scenario groups and nine independent OpenTrail ingress groups round-trip identical bytes and enforce invalid semantics, authenticated/authorized producer context, stale/future time, duplicate/conflict, fixed-rate/emergency-reserve, monotonic-time, and fail-closed producer-capacity behavior. Physical transport, key lifecycle, replay protection, and field delivery remain later gates |
| OG-019 | planned | APU/auxiliary boundary | Telemetry-only scope first; any control has separate auth, replay, interlock, fail-safe, and hazard review |

## Recommended sequence

Complete OG-003A, then OG-005 and OG-007 together, followed by OG-006 and OG-008. Only then bind OG-004 to selected physical CAN hardware. Start with synthetic/captured traffic and listen-only vehicle access.
