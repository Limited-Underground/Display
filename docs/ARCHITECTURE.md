# OpenGauge Initial Architecture

Status: proposed foundation, 2026-08-08. This architecture defines modular boundaries; it does not claim working vehicle or display hardware.

## System roles

```text
Vehicle CAN/J1939
       |
[CAN gateway target]
 driver -> frame parser -> decoder registry -> normalized signal cache
                                      |                 |
                               diagnostics         alarm engine
                                      \                 /
                             versioned telemetry/event publisher
                                             |
                                          ESP-NOW
                                             |
                  +--------------------------+-------------------+
                  |                                              |
          [gauge display target]                         [other gauge targets]

[optional GPS target] ------ normalized navigation telemetry
[optional APU target] ------ separate authenticated module/interface
[OpenTrail] <--------------- normalized critical-event interface only
```

Roles are separately deployable target applications assembled from bounded components. A failure or restart in one display cannot block the gateway or other displays.

## Layer boundaries

### Hardware adapters

Interfaces cover CAN controller/transceiver, clock, ESP-NOW/radio, display, touch, storage, power, and logging. Concrete board bindings do not leak into decoder, cache, alarm, or layout logic.

### CAN/J1939 acquisition

Initial physical operation is passive/listen-only. The CAN adapter returns frame ID, extended/standard form, DLC/data, receive time, error state, and overflow information. A later non-J1939 adapter may feed the same normalization layer.

The host-tested v0 receiver interface deliberately has no transmit method. Its
bounded fake exercises policy/filtering, FIFO capture, error-warning/passive,
bus-off, overflow, injected hardware failure, restart, and monotonic timestamps.
This proves the software boundary, not electrical passivity; every production
adapter must separately prove that its controller and transceiver cannot
acknowledge or drive the vehicle bus during initial bring-up.

J1939 processing is decomposed into:

1. 29-bit identifier parsing (priority, data page, PDU format/specific, source address, destination where applicable, and PGN).
2. A decoder registry keyed by PGN and source/context rules.
3. SPN extraction using explicit bit/byte layout, scaling, offset, units, and J1939 unavailable/error encodings.
4. Validation and conversion to normalized signals with quality metadata.

No decoder may silently turn unavailable, error, out-of-range, or stale data into a plausible number.

### Normalized signal model and cache

A signal has a stable namespaced ID, type, value when valid, canonical unit, quality/state, source, receive/sample age, and optional diagnostic metadata. The cache owns freshness thresholds and publishes changes/subscriptions. Display nodes also maintain a local cache so gateway loss produces explicit stale/unknown states without freezing the UI.

### Alarm engine

Alarm rules consume normalized signals, not raw frames. The host-tested fixed
16-rule engine provides inclusive above/below/outside-range comparisons, exact
hysteresis and debounce boundaries, severity, latching/acknowledgement,
clear/hold/assert behavior for nonvalid or stale input, and bounded periodic
reminders. Invalid quality never becomes a numeric alarm input. Display alerts
and exported critical events derive from the same validated state but remain
independently deliverable. A host-tested cache evaluator scans all 16 latest
states each cycle so unchanged values still advance debounce, exact staleness,
and reminder time; it preflights the whole poll and resets runtime on a cache
epoch change without inventing a clear event. A host-tested allowlist exporter
maps only final alarm assertions/clears into the existing 64-byte critical-alert
codec with stable condition IDs, unique event IDs, canonical unit conversion,
quality/age checks, and no lifecycle commit on codec failure. Target-task
timing, display behavior, and physical critical-event transport remain later
work.

## ESP-NOW telemetry protocol

The transport and semantic codec are separate. Never serialize compiler-dependent structs directly.

The host-tested telemetry v0 packet now provides an explicit fixed-length
version/type, gateway and boot-session identity, sequence, gateway uptime,
three registered signal entries with source age, canonical reserved bytes, and
CRC corruption detection. It remains a pre-production contract: CRC is not
authentication, target authorization comes from provisioned unicast peers, and
capabilities, events/alarms, heartbeat/time, pairing/configuration, diagnostics,
and update coordination still require separate formats.

Telemetry publication now has a host-tested cooperative v0 scheduler. Per-gauge
subscriptions apply signal minimum/maximum intervals, raw canonical deadbands,
quality-first ordering, latest-value coalescing, three-entry batching, and a
hard 50 ms per-peer packet floor. A host-tested gateway composition performs
bounded cache cursor sync, registered-ID mapping, packet encoding, and one
nonblocking transport enqueue. A two-phase prepare/local-queue-commit rule
advances per-peer sequence only when the local transport accepts the frame;
later radio loss does not create a blocking retry that starves newer data.
Gauges use sequence/session and source age plus receiver-local elapsed time for
loss and staleness. ESP-IDF target-task binding, pairing, real peer limits,
encryption/key handling, channel coexistence, and recovery remain unresolved.

A host-tested cooperative gateway loop now composes passive receive, J1939
dispatch, normalized cache writes, one cache poll, at most one enqueue for each
of eight peers, and one transport service call. Its configurable drain limit is
at most 16 CAN frames per cycle, preventing sustained bus input from starving
radio work. Input faults do not suppress cache aging, so bus-off still produces
an explicit stale/no-value publication. ESP-IDF task/ISR ownership, watchdog,
stack, timing, physical adapters, and concurrency remain unresolved.

The host-tested gauge receiver admits only the configured peer, encrypted
metadata, channel, and encoded gateway identity; drains at most four datagrams
per cycle; tracks duplicate/out-of-order/gap/session transitions; and stores 16
latest wire signals with source age plus receiver-local elapsed time. A new
gateway session clears old store state, and exact staleness removes numeric
display values. ESP-IDF callbacks/keys/RF remain.

## Gauge rendering

The rendering layer consumes a view model derived from the local signal cache. Layout definitions refer to stable signal IDs and units, never J1939 offsets. Proposed widgets include needle, number, bar, multi-value, warning, trend, and status.

The host-tested v0 projection configures eight registered-signal widgets and
emits atomic display-neutral snapshots with distinct valid, suspect, missing,
stale, unavailable, error, out-of-range, and unknown states. Only valid/suspect
states retain a value; missing still carries expected type/unit for stable UI
chrome without inventing a measurement. Rendering, localization, touch, and
hardware performance remain unresolved.

Rendering must be non-blocking relative to receive/cache updates. It should use bounded allocation, measurable frame/update budgets, dirty-region or suitable refresh strategies, and a conspicuous stale/error presentation. Configuration is schema-versioned, validated, recoverable, and stored locally.

The host-tested `OGL0` layout record explicitly serializes one through eight
validated widgets into 576 canonical bytes. A two-slot store selects the unique
highest generation, falls back visibly on corruption/I/O/equal-generation
conflict, writes only an empty/invalid or older slot, and requires full
readback, byte comparison, and decode before accepting a save. CRC detects
accidental corruption only. Schema migration, generation persistence,
unchanged-write suppression, backend binding, security, and physical
power-cut/endurance evidence remain unresolved.

## GPS and auxiliary modules

The GPS role publishes normalized speed, position, altitude, heading, UTC, fix quality, and age. Consumers must distinguish unavailable/stale values.

The host-tested v0 tracker accepts an adapter-neutral sample with explicit field
presence, fix quality, source boot session/sequence, and source age. It counts
loss, rejects duplicate/backward samples and receiver-clock regression, accepts
normal sequence wrap/restart, and combines source age only with local monotonic
elapsed time. At the exact stale boundary it removes position, motion,
accuracy, and UTC values. Local-gateway, dedicated-node, authorized
OpenTrail/MeshCore bridge, and gauge-local GNSS topologies remain possible; the
ordered Wio Tracker L1 Pro is only a candidate. Transport/authentication,
parser/driver, rate, privacy, and physical GNSS evidence remain unresolved.

APU/auxiliary support is a separate module and protocol. Any future start/stop or actuation requires authenticated authorization, replay protection, interlocks, fail-safe defaults, auditability, and a dedicated safety review. It is not part of the initial telemetry proof of concept.

## OpenTrail boundary

OpenGauge exports normalized critical events, not raw frames. A transport-neutral event should contain schema version, type, severity, source/vehicle identity, event time/age, optional typed value/unit, validity, and diagnostic context. OpenTrail independently validates/rate-limits the input and adds its own GPS/radio context.

The bounded v0 contract is specified in
`docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md`. OpenGauge explicitly encodes
the fixed 64-byte frame and rejects invalid type/severity/unit/quality,
identity, lifecycle, and range combinations. The identical normative fixtures
round-trip in OpenTrail's independent decoder. CRC-32 detects corruption only;
the selected serial or local-wireless adapter must provide authenticated and
authorized producer identity plus replay protection before production use.

## Updates and recovery

OTA design must include image authenticity/integrity, hardware target compatibility, version policy, sufficient partition/storage layout, atomic boot selection, health confirmation, rollback, interruption handling, and a documented USB/physical recovery path. Update coordination must never simultaneously remove all useful instrumentation by default.

The host-tested v0 boot guard admits only a separately verified newer image for
the exact hardware and inactive slot, then requires full-image readback and
persisted boot-selection evidence. A trial must accumulate the configured
role-health mask for a minimum stable interval strictly before an exact
deadline. Boot mismatch, timeout, attempt limit, or explicit health failure
requires rollback, and rollback completes only after observing the exact
original version/slot. Image verification, flash/partition/bootloader adapters,
persistent lifecycle, physical interruption/recovery, and fleet rollout remain
unresolved.

## Diagnostics and failure behavior

Logging levels are `ERROR`, `WARN`, `INFO`, `DEBUG`, and `TRACE`, with release filtering and secret redaction. Counters should cover CAN errors/overflow, unknown/invalid PGNs/SPNs, cache stale transitions, ESP-NOW send/receive/loss, decoder time, render timing, reset reason, and update state.

The host-tested v0 diagnostics core provides a typed 32-record overwrite ring,
five-level filtering, monotonic time, reset-cause capture, atomic oldest-first
snapshots, and 16 saturating health counters. Its event API has no free-form
text, buffers, addresses, credentials, or identifiers. ESP-IDF adapter binding,
task ownership, timing, formatting/persistence, and an end-to-end production
redaction audit remain unresolved.

- Gateway loss makes gauge values stale/unknown; it does not freeze the last value as current.
- Display failure does not affect acquisition or other displays.
- GPS/APU/OpenTrail absence does not impair core instrumentation.
- Corrupt/incompatible wireless messages are safely rejected.
- OpenGauge does not alter vehicle operation in the initial architecture.

## Architecture gates before product firmware

1. Confirm candidate boards, CAN transceiver/protection/isolation, display/touch interfaces, memory, power, and environmental constraints.
2. Host-test J1939 ID parsing and normalized signal semantics using synthetic/captured frames.
3. Define freshness, units, invalid/unavailable states, and a small reference signal set.
4. Measure ESP-NOW delivery/rate/coexistence with explicit serialization before freezing protocol v1.
5. Benchmark the candidate display with representative gauges and stale/warning states.
6. Threat-model pairing, configuration, OTA, exported alerts, and any future control path.
