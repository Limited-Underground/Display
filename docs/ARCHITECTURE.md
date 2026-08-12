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

A host-tested logical authorization registry now bounds eight peers and one
local approval window. It stores opaque logical IDs and secure-key handles,
never raw keys/PINs/addresses, and checks role-scoped permission plus key handle
and channel on each authorization decision. Exact timeout, capacity, duplicate
peer/key rejection, revoke/forget/replacement, and strictly increasing key
rotation epochs are covered. Discovery/UI/local presence, protected key
storage, persistence/recovery, rate limits, ESP-NOW binding, and physical
provisioning remain unresolved.

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

A host-tested volatile trend core retains up to four fixed series of 2 through
120 points with independent exact minimum intervals. Valid/suspect points keep
their value; missing/stale/unavailable/error/out-of-range/unknown points are
explicit no-value gaps. Rings overwrite only the oldest point and read
oldest-first atomically. Renderer axes/decimation, memory/timing/locking,
persistence/privacy, and physical display acceptance remain unresolved.

Rendering must be non-blocking relative to receive/cache updates. It should use bounded allocation, measurable frame/update budgets, dirty-region or suitable refresh strategies, and a conspicuous stale/error presentation. Configuration is schema-versioned, validated, recoverable, and stored locally.

The host-tested `OGL0` layout record explicitly serializes one through eight
validated widgets into 576 canonical bytes. A two-slot store selects the unique
highest generation, falls back visibly on corruption/I/O/equal-generation
conflict, writes only an empty/invalid or older slot, and requires full
readback, byte comparison, and decode before accepting a save. CRC detects
accidental corruption only. A backend-neutral
[key/value adapter](configuration/GAUGE_LAYOUT_KV_TARGET_ADAPTER_V0.md) now
fixes exact `og_config` / `gauge_layout` / `ogl0_a|b` binding, commits complete
writes and present-key erases, and leaves failed commits for restart selection.
A store-owned update path compares canonical content at the active generation,
suppresses unchanged writes, and allocates the next generation only for a real
change. Backend commit failure remains typed as uncertain until restart reads
both slots; an applied generation is then accepted without rewrite, while an
unapplied generation can be attempted normally. A separate
[local confirmation coordinator](configuration/GAUGE_LAYOUT_CHANGE_CONFIRMATION_V0.md)
admits one validated request under an exact ID and deadline, consumes approval
before persistence, requires successfully staged IDs to increase during each
start cycle, and rejects mismatch, same-boot reuse, expiry, cancellation, and
clock rollback. It does not prove who supplied the request or confirmation:
target composition must bind both to serialized local input and a successfully
shown prompt, and must flush obsolete input across restart. Schema migration,
ESP-IDF binding, security, and physical power-cut/endurance evidence remain
unresolved.

A separate [operator-status projection](configuration/GAUGE_LAYOUT_CHANGE_OPERATOR_STATUS_V0.md)
turns coherent coordinator status and one immediate operation result into a
fixed semantic state/action record. It carries an opaque request token only
while confirmation is still allowed and distinguishes ordinary retry from
restart reconciliation. It formats no text, records no diagnostic, and cannot
substitute for target task ownership or physical-presence evidence.

The [redacted diagnostic adapter](diagnostics/GAUGE_LAYOUT_CHANGE_STATUS_DIAGNOSTIC_EVENT_V0.md)
reduces that live record to coarse state/action/flags in one versioned 32-bit
word. Request tokens, time, generation, layout content, labels, and counters do
not cross the diagnostic boundary. Persistent audit and target log/export
policy remain separate design work.

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

A host-tested eight-entry outbox now separates local transport rejection,
local acceptance, and exact OpenTrail application acknowledgement. It reserves
capacity for emergency alerts, prepares oldest emergency before oldest ready
critical state, counts only locally accepted attempts, accepts a correlated
late ACK before terminal removal, and emits explicit failure at exact
attempt/lifetime bounds.

The mirrored `OGK0` ACK codec now makes disposition/reason, original lifecycle,
consumer/producer/event/condition identity, consumer boot session/sequence, and
observed age explicit in 64 canonical bytes. Three normative fixtures encode
identically in both projects. A bounded ingress composes adapter-authenticated
metadata, logical peer/key-handle/channel/permission authorization, explicit
consumer-session binding, a 32-sequence replay window, observed-age policy, and
exact outbox correlation. Only accepted/none removes an entry; a correlated
rejection remains explicit non-success. CRC still provides corruption
detection only. A fixed canonical checkpoint now exports/imports all eight
bindings and replay bitmaps atomically, tied to the exact live authorization
epoch; rotation or revoke invalidates stale state. Correlated rejection reasons
now have one bounded policy: rate-limited/internal-error responses release the
entry after normal backoff while six deterministic refusals terminate with
typed event/condition/reason/attempt evidence; a retry at the attempt limit also
terminates. A fixed 320-byte `OAS0` envelope now binds the `OAI0` payload to a
strict caller-owned generation and a two-slot host store that verifies full
write/readback/decode, selects the newest unique generation, restores through
the ingress validator, and keeps one corrupt or interrupted slot recoverable.
ESP-IDF/NVS binding, coordinated authorization/outbox persistence, secure
rollback resistance, real cryptographic transport, operator presentation, and
authenticated on-device delivery remain unresolved.

Coordinated restart state is now carried by one exact 1280-byte `ORS0`
generation containing authorization plus ACK/outbox recovery. Its two-slot
store owns generation allocation, exact readback, degraded recovery, and
trusted-floor enforcement. A backend-neutral
[key/value adapter](integration/CRITICAL_ALERT_SYSTEM_RECOVERY_KV_TARGET_ADAPTER_V0.md)
fixes `og_state` / `og_recovery` / `ors0_a|b`, commits complete writes and
present-key erases, and leaves failed commits for restart inspection. This is
target-shaped storage plumbing, not protected integrity, rollback-resistant
trust, ESP-IDF task composition, or physical durability evidence.

External OT-017D evidence carried the public normative `OGA0` fixture through
two role-reversed Heltec/SenseCAP cycles and returned correlated responder-made
`OGK0` bytes with zero loss, duplicates, or new errors. This closes a bounded
wire-fit/physical-byte question only: the host supplied trust and did not run
the OpenGauge exporter, outbox, ACK ingress/policy, or checkpoint store on a
target.

OG-018I then strengthened two later physical cycles by reconstructing the exact
sent alert as an in-flight production `CriticalAlertOutbox` entry and passing
the returned bytes through production peer authorization, explicit consumer
session binding, ACK replay/correlation ingress, and outbox completion. Both
cycles ended with one acknowledgement and no queued/in-flight entry. The host
still reconstructed state after receipt; it did not keep a durable on-device
pipeline alive across the physical wait.

OG-018J exercised the terminal negative branch in two additional role-reversed
cycles. Each exact stale rejection passed authorization/session/replay/
correlation, produced zero delivery acknowledgements and
`outbox_completed=false`, and recorded terminal failure with no retry release.
This proves the host composition does not convert this physical rejection into
success. Retryable rejection and persistent target-state interruption remain.

OG-018K then exercised the retryable negative branch. Two role-reversed
rate-limit rejections passed the same authorization/session/replay/correlation
path, released one queued retry, left zero in flight and zero acknowledgements,
and avoided terminal failure. Durable backoff state and a later physical
retry-to-accept sequence remain unproved.

OG-018L added the subsequent physical retry and accepted ACK in both endpoint
roles. The composition enforced the exact backoff boundary, byte-identical retry
preparation, and final completion only after the next ACK sequence. State was
still reconstructed after both responses, not held durably across the radio
wait.

OG-018M replaced reconstruction with one live OpenGauge host process started
before the first physical send. Its real authorization, replay, and outbox state
remained alive through rejection, exact backoff, retry, and final completion in
both roles. Coordinated durable restart recovery and on-device state remain.

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
