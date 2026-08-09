# OpenGauge Engineering Backlog

Statuses: `done` means the documented acceptance criteria are evidenced; `planned` means no implementation claim.

## Foundation

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-001 | done | Project bootstrap and repository structure | Self-contained directories, agent guide, README, architecture, status, and backlog exist |
| OG-002 | done | Initial architecture documentation | Roles, layers, safety/failure boundaries, protocols, and architecture gates documented |
| OG-003 | planned | Hardware abstraction contracts | CAN, clock, ESP-NOW, display/touch, storage, power, and logging contracts reviewed with fakes |
| OG-003A | planned | Hardware/use-case inventory | Candidate acquisition inventory plus arrival procedures now record two Waveshare SKU 31262 non-GPS displays, an Espressif DevKitC bench mule, and the on-hand Veepeak generic OBD-II adapter with recovery/read-only evidence boundaries. Remains planned until exact received units, vehicle, desired signals, CAN interface, wiring, power, and environment constraints are recorded |
| OG-017 | planned | Diagnostics/logging foundation | Levels/filtering/redaction plus CAN, wireless, cache, decoder, render, and reset counters tested |

## CAN/J1939 and telemetry core

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-004 | done | CAN driver abstraction | A transmit-incapable passive Classical CAN interface and 16-frame fake expose listen policy, canonical frame/capture data, bus/error/overflow metadata, FIFO/drop-newest behavior, filtering, monotonic time, bus-off/hardware failure, and restart. Eight host groups include EEC1 receive-to-decoder/cache integration. Physical controller/transceiver passivity and ESP-IDF binding remain separate gates |
| OG-005 | done | J1939 identifier parser | Five host-test groups validate priority/DP/PF/PS/SA, PDU1 destination and PGN zeroing, PDU2 group extension, the PF boundary, DP=1, and fail-closed standard/out-of-range/J1939-22 identifiers |
| OG-006 | done | PGN decoder registry | A fixed eight-entry registry rejects invalid/noncanonical/duplicate/full/unknown dispatch, revalidates normalized output, and hosts one narrowly bounded EEC1 engine-speed fixture. Five host-test groups cover valid/highest-valid, reserved/out-of-range, error, unavailable, frame/length/capacity, and bad-decoder paths. Licensed/current J1939 data plus captured traffic remain required before a vehicle-support claim |
| OG-007 | done | Normalized signal model | Six host-test groups validate fixed namespaced IDs, typed integer/Boolean values, canonical units and bounds, explicit no-value quality states, protocol-specific J1939/OBD/GPS/synthetic provenance, sample/receive time, and exact stale/clock boundaries |
| OG-008 | done | Telemetry cache/subscriptions | Six cache host-test groups cover fixed 16-signal capacity, revalidation, duplicate/update/conflict/out-of-order rules, per-signal exact stale boundaries, polling state cursors, insufficient output, clear/restart epochs, and four concurrent writers. A decoder integration group proves valid-to-stale-to-unavailable engine speed without a plausible invalid value. On-device locking/performance and network subscriptions remain later gates |
| OG-014 | planned | Alarm framework | Threshold, hysteresis, debounce, severity, latch/ack, stale behavior, and rate-limited events tested |

## Wireless gauge network

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-009 | done | ESP-NOW transport abstraction | An opaque 250-byte encrypted-unicast contract and deterministic fake provide explicit start/peer/send/receive/completion/status behavior; fixed eight-peer and four-frame fake capacities; channel/encryption agreement; latency/RSSI; missing/offline/mismatch/rejection/loss outcomes; buffer preservation; and completion backpressure across eight host-test groups. Radio completion is not an application ACK. ESP-IDF binding, keys, physical traffic, channel coexistence, and capacity measurements remain later gates |
| OG-010 | done | Gateway-to-gauge packet format | An explicit fixed 96-byte `OGT0` codec carries up to three registered signals with canonical type/unit/quality, gateway/session/sequence identity, source age, zeroed reserved/unused bytes, and CRC-32. Eight host-test groups cover a normative golden vector, cache-to-wire no-value rules, malformed/incompatible/corrupt frames, sequence gap/duplicate/out-of-order/wrap/restart behavior, exact receiver-local staleness, and fake encrypted delivery plus loss. Its 10 Hz/eight-peer 7,680 payload-byte/s estimate excludes radio overhead and is not physical evidence |
| OG-010B | done | Telemetry publication scheduler | A cooperative fixed-capacity scheduler holds 8 peers, 8 subscriptions/peer, and 16 latest signals; prioritizes first/quality, deadband change, then periodic refresh; derives exact stale/no-value state; batches 3; and enforces a hard 50 ms peer packet floor. Two-phase prepare/local-queue commit keeps the same sequence after local rejection and advances after acceptance even when injected radio loss creates a receiver gap. Eight host groups cover lifecycle/capacity, coalescing, initial batching, min/deadband/max timing, stale/recovery/unavailable, token/retry, independent peers, and encoded fake transport loss |
| OG-010C | done | Cache-to-radio gateway publisher | A cooperative composition performs bounded cache full/incremental cursor polls, maps only four registered IDs, counts/skips unknowns, safely resets source/publication baselines on cache epoch change, encodes one 96-byte packet, attempts one nonblocking send, and commits local acceptance/rejection. Seven host groups cover initial/unchanged mapping, clear plus same-value reload, same-sequence local retry, paced 3+1 sync, stale without repoll, accepted injected loss followed by an exact receiver gap, and the actual EEC1 `engine.speed` decoder-to-cache-to-wire path |
| OG-011 | planned | Gauge pairing and identity | Threat model, discovery/approval, key storage, replacement/reset/revoke/recovery, and peer-limit tests |
| OG-010A | planned | Physical ESP-NOW characterization | Packet loss, latency, peers, interference/coexistence, reboot recovery, and update-rate evidence on selected boards |

## Display and persistence

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-012 | planned | Gauge rendering framework | View-model boundary and representative numeric/needle/bar/warning/stale screens benchmarked on candidate hardware |
| OG-013 | planned | Persistent gauge configuration | Versioning, validation, migration, safe defaults, write wear, import/reset/recovery tested |
| OG-012A | planned | Display hardware feasibility | The SKU 31262 two-unit arrival plan defines shipping-demo preservation, exact revision, silicon/flash/security, BOOT/RESET recovery, vendor display/touch/power/IMU baseline, representative valid/warning/unavailable/error/stale rendering, resource/timing/power/heat, independent failure, and restore evidence. No unit has arrived or been tested |

## Modules and lifecycle

| ID | Status | Task | Acceptance evidence |
| --- | --- | --- | --- |
| OG-015 | planned | GPS node architecture | Normalized fix/quality/age model, topology, update rate, time, loss, and simulation documented |
| OG-016 | planned | OTA/recovery architecture | Authenticity, compatibility, partitions, interruption, health confirmation, rollback, fleet sequence, and USB recovery validated |
| OG-018 | done | OpenTrail alert interface | Mirrored specifications and three normative fixtures define an explicit 64-byte frame for seven alert types, canonical milli-units, assert/clear lifecycle IDs, optional UTC/value fields, and CRC-32. Four OpenGauge exporter scenario groups and nine independent OpenTrail ingress groups round-trip identical bytes and enforce invalid semantics, authenticated/authorized producer context, stale/future time, duplicate/conflict, fixed-rate/emergency-reserve, monotonic-time, and fail-closed producer-capacity behavior. Physical transport, key lifecycle, replay protection, and field delivery remain later gates |
| OG-019 | planned | APU/auxiliary boundary | Telemetry-only scope first; any control has separate auth, replay, interlock, fail-safe, and hazard review |

## Recommended sequence

Complete the missing vehicle/power/CAN portions of OG-003A while binding
OG-010C and completed OG-004 to a bounded target task. Then bind OG-004 to a
selected ESP-IDF CAN adapter or OG-009 to ESP-IDF radio without confusing a
host-tested interface with on-device or RF acceptance.
Incoming display work may begin with OG-012A vendor-example and recovery
evidence, but does not bypass the normalized-data path.
