# OpenGauge Engineering Backlog

Statuses: `done` means the documented acceptance criteria are evidenced; `planned` means no implementation claim.

## Foundation

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-001 | done | Project bootstrap and repository structure | Self-contained directories, agent guide, README, architecture, status, and backlog exist |
| OG-002 | done | Initial architecture documentation | Roles, layers, safety/failure boundaries, protocols, and architecture gates documented |
| OG-003 | planned | Hardware abstraction contracts | CAN, clock, ESP-NOW, display/touch, storage, power, and logging contracts reviewed with fakes |
| OG-003A | planned | Hardware/use-case inventory | Candidate acquisition inventory now records two Waveshare SKU 31262 displays, an Espressif DevKitC bench mule, and the on-hand Veepeak adapter with evidence boundaries. Remains planned until the exact vehicle, desired signals, CAN interface, wiring, power, and environment constraints are recorded |
| OG-017 | planned | Diagnostics/logging foundation | Levels/filtering/redaction plus CAN, wireless, cache, decoder, render, and reset counters tested |

## CAN/J1939 and telemetry core

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-004 | planned | CAN driver abstraction | Listen-only receive/error/overflow metadata and fake-frame source demonstrated; no vehicle dependency |
| OG-005 | done | J1939 identifier parser | Five host-test groups validate priority/DP/PF/PS/SA, PDU1 destination and PGN zeroing, PDU2 group extension, the PF boundary, DP=1, and fail-closed standard/out-of-range/J1939-22 identifiers |
| OG-006 | done | PGN decoder registry | A fixed eight-entry registry rejects invalid/noncanonical/duplicate/full/unknown dispatch, revalidates normalized output, and hosts one narrowly bounded EEC1 engine-speed fixture. Five host-test groups cover valid/highest-valid, reserved/out-of-range, error, unavailable, frame/length/capacity, and bad-decoder paths. Licensed/current J1939 data plus captured traffic remain required before a vehicle-support claim |
| OG-007 | done | Normalized signal model | Six host-test groups validate fixed namespaced IDs, typed integer/Boolean values, canonical units and bounds, explicit no-value quality states, protocol-specific J1939/OBD/GPS/synthetic provenance, sample/receive time, and exact stale/clock boundaries |
| OG-008 | done | Telemetry cache/subscriptions | Six cache host-test groups cover fixed 16-signal capacity, revalidation, duplicate/update/conflict/out-of-order rules, per-signal exact stale boundaries, polling state cursors, insufficient output, clear/restart epochs, and four concurrent writers. A decoder integration group proves valid-to-stale-to-unavailable engine speed without a plausible invalid value. On-device locking/performance and network subscriptions remain later gates |
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

Complete the missing vehicle/power/CAN portions of OG-003A while beginning
OG-009 and OG-010 with fake transport, explicit serialization, and a measured
packet/rate budget. Only then bind OG-004 to selected physical CAN hardware.
Incoming display work may begin with OG-012A vendor-example and recovery
evidence, but does not bypass the normalized-data path.
