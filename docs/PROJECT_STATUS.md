# OpenGauge Project Status, Assumptions, and Open Questions

Status date: 2026-08-09

## Conceptual goals

- Separate ESP32 CAN/J1939 gateway and round touchscreen gauge nodes
- Normalized, validated telemetry delivered over ESP-NOW
- Configurable gauges, warnings, local persistence, and stale-data behavior
- Optional GPS, APU/auxiliary, OpenTrail event, and recoverable OTA modules

The critical-alert semantic interface has bounded host validation in this and
the OpenTrail repository. Two strengthened role-reversed Heltec/SenseCAP cycles
also carried 2/2 exact normative `OGA0` frames and 2/2 correlated `OGK0`
responses with zero loss/duplicates/errors; each returned ACK passed production
OpenGauge peer authorization, session binding, replay/correlation ingress, and
completed its exact reconstructed in-flight outbox entry. The host supplied
trust and reconstructed state rather than running a persistent on-device
pipeline. A passive Classical CAN receiver interface and
bounded 16-frame fake, the Classical J1939 identifier parser, a
fixed-capacity decoder registry with one EEC1 engine-speed fixture, normalized
signal model, fixed-capacity thread-safe telemetry cache, a fixed 16-rule alarm
engine, bounded full-state cache evaluator, allowlisted alarm-to-critical-alert
exporter, an opaque
encrypted-unicast ESP-NOW transport contract/fake, an explicit 96-byte
telemetry packet codec, a bounded per-gauge publication scheduler, a
cache-cursor-to-radio publisher, bounded CAN-to-radio gateway loop, bounded
gauge receiver/latest-state store, fail-visible eight-widget view model and
four-series trend buffer, fixed-memory typed diagnostics core, versioned recoverable two-slot gauge
layout store, transport-neutral GPS fix/quality/age tracker, OTA
trial-confirmation/rollback guard, opaque-handle peer authorization registry,
authenticated-metadata/replay/outbox critical-alert ACK ingress, its
authorization-epoch-bound replay checkpoint with a recoverable two-slot host
store, and bounded negative-ACK policy now have deterministic host tests.
Physical vehicle acquisition,
normalization from captured/real signals,
on-device performance, authenticated target transport, keys, displays, and
hardware remain unvalidated.

## Decisions captured

- Gateway and displays are separate roles with independent failure boundaries.
- The initial CAN path is passive/listen-only. Its v0 host interface contains no transmit operation and carries canonical Classical frames, capture time, bus state, and overflow metadata. Eight fake-receiver groups cover lifecycle, policy/filtering, malformed frames, FIFO/clock behavior, overflow, bus-off, hardware failure, restart, and EEC1 receive-to-cache integration. This is not evidence of an electrically passive production adapter.
- Raw J1939 frames are not the default gauge-network payload; the gateway publishes normalized selected signals.
- Hardware adapters are isolated from parsing, decoding, caching, alarms, UI, and protocols.
- J1939 unavailable/error/stale states remain explicit and cannot become plausible numeric readings.
- The v0 J1939 parser is explicitly bounded to 29-bit Classical J1939: standard frames, out-of-range identifiers, and the J1939-22 extended data page fail closed.
- The v0 decoder registry rejects duplicate, over-capacity, noncanonical PDU1, unknown, and malformed dispatch. Its single EEC1/SPN 190 fixture preserves reserved/error/unavailable encodings as nonnumeric quality states and still requires licensed-standard and captured-frame reconciliation.
- The v0 normalized signal model uses fixed-capacity namespaced IDs, integer canonical units, explicit quality, protocol-specific provenance, and an exact `age >= threshold` stale boundary.
- The v0 cache holds 16 latest states, rejects invalid/older/conflicting/full writes, serializes concurrent access, materializes stale transitions for polling subscribers, and invalidates cursors on clear. It is not history, persistence, or a firmware-performance claim.
- The v0 ESP-NOW boundary is opaque, unicast, fixed-capacity, nonblocking, and encrypted-by-default. Radio-delivery completion is explicitly not an application acknowledgement. The fake models channel/security agreement, loss, receiver rejection, and queue backpressure without claiming an ESP-IDF or RF result.
- The v0 telemetry packet is an explicit fixed 96-byte little-endian batch with a four-signal registry, three entries per packet, gateway/session/sequence identity, source age, canonical unused bytes, and CRC corruption detection. Receiver logic detects gaps/duplicates/out-of-order/restarts and adds local elapsed time to source age without comparing unsynchronized clocks. Exact stale boundaries strip numeric display values. The 10 Hz/eight-peer payload estimate is 7,680 bytes/s before radio overhead and is not physical rate evidence.
- The v0 cooperative publisher holds at most eight gauge peers, eight subscriptions each, and sixteen latest signals. It prioritizes first/quality transitions, then deadband changes, then periodic refresh; derives stale state at prepare time; batches three; and enforces a 50 ms minimum peer packet interval (20 packets/s, 1,920 payload bytes/s per peer). Local queue rejection retains the sequence/state for retry; local acceptance advances the peer even if later radio delivery is lost, which receiver gap tests cover. ESP-IDF task/queue integration and physical rate evidence remain.
- The gateway publisher composition polls cache cursors into registered scheduler state, skips/counts unregistered IDs, resets source/publication baselines on cache epoch change without resetting peer sequence, encodes one packet, and commits one local transport enqueue result. Seven host groups cover full/incremental sync, clear/reload, same-sequence local retry, paced initial sync, stale without repoll, accepted radio loss producing a receiver gap, and actual EEC1 `engine.speed` decode through cache to wire code 1. Firmware task ownership and on-device timing remain.
- The gateway telemetry loop composes at most 16 CAN receives, eight decoded signals/frame, cache writes plus one poll, one enqueue attempt for each of eight peers, and one transport service call per cooperative cycle. Nine host groups cover rollback/restart, bounded fairness, bad/unsupported traffic, real EEC1-to-fake-radio delivery, unavailable/stale no-value behavior, bus-off, retry, and overflow diagnostics. ESP-IDF task/ISR ownership, timing, and physical adapters remain.
- The gauge receiver admits only one configured peer with encrypted metadata, expected channel, and encoded gateway ID; drains at most four datagrams; rejects malformed/unauthorized input without store mutation; tracks duplicate/out-of-order/gap/session sequence state; and stores 16 latest signals with gateway source age plus receiver-local elapsed time. Eight host groups cover trust metadata, fake delivery, exact stale/no-value reads, malformed input, sequence gaps, session clear, drain budget, and freshness errors. ESP-IDF RF/key binding remains.
- The display-neutral gauge view model validates at most eight registered-signal widgets and atomically projects expected type/unit, session/sequence/age, and distinct valid, suspect, missing, stale, unavailable, error, out-of-range, or unknown state. Values survive only valid/suspect state, and all nonvalid state is fail-visible. Seven host groups cover configuration/lifecycle, missing metadata, value/attention behavior, exact staleness, invalid-quality metadata, shared signals, and atomic output preservation on capacity/receiver failure. Rendering, touch, persistence, localization, and physical display performance remain.
- The display-neutral trend core holds four fixed 2-120-point rings with independent exact sampling intervals, oldest-first reads, coalescing and overwrite counters, and shared-signal support. Valid/suspect points keep values; every nonvalid state is a no-value gap. Eight host groups plus 100 repeat runs cover lifecycle/configuration, values/gaps, interval/ring order, shared signals, malformed/clock atomicity, read capacity, and clear. Renderer axes/decimation, RAM/timing/locking, persistence/privacy, and physical display acceptance remain.
- The typed diagnostics core stores 32 oldest-first fixed events with five-level filtering, monotonic time, reset-cause capture, overwrite accounting, atomic snapshots, and 16 saturating subsystem counters. It accepts no text, byte buffers, addresses, credentials, or identifiers. Eight host groups plus 100 repeat runs cover lifecycle, filtering/time, wrap/order/sequences, counter ownership/saturation, canonical records, snapshot/clear, restart, and fixed pointer-free payload. Adapter binding, concurrency/timing, formatting/persistence, and production redaction audit remain.
- The explicit 576-byte `OGL0` schema-v1 gauge layout record serializes one through eight validated widgets with generation/layout identity, brightness/theme, canonical zero padding, and CRC-32. A two-slot store writes only an empty/invalid or older slot and requires readback, byte equality, and decode before commit; boot selects the unique highest generation and visibly falls back on empty/corrupt/I/O/equal-generation conflict. Nine host groups plus 100 repeat runs cover codec/malformed/atomic decode, safe default, strict generations, slot rotation/write counts, interruption/corrupt-success recovery, I/O, and reset. Migration, import UX, unchanged-write suppression, generation persistence/exhaustion, backend binding, power-cut/endurance, and configuration authenticity remain.
- The transport-neutral GPS latest-fix tracker validates no-fix/2D/3D/differential/RTK/estimated quality and explicit integer position/altitude/speed/heading/accuracy/UTC presence, tracks source session/sequence/gaps/wrap, combines bounded source age only with receiver-local elapsed time, and strips every position/time value at exact staleness. Eight host groups plus 100 repeat runs cover boundaries, partial/no-fix state, lifecycle, loss/order/restart, clock regression, and age overflow. Candidate topology/rate/privacy are documented; parser/transport/authentication and physical GNSS/antenna/accuracy/power remain.
- The OTA trial guard admits only separately verified newer exact-hardware inactive-slot candidates, requires full-image readback and persisted boot-selection evidence, accumulates target-specific health for a minimum stable interval strictly before an exact deadline, bounds trial boots, and accepts rollback completion only after observing the original version/slot. Eight host groups plus 100 repeat runs cover policy/candidate/write evidence, health/time, timeout, repeated boot, mismatch/rollback, and clocks. Signature/download/write/partition/boot adapters, persistent state, physical interruption/rollback/USB recovery, and fleet acceptance remain.
- The logical peer registry holds eight role-scoped entries and one exact local approval window; stores only opaque logical IDs, secure-key handles, and epochs; authorizes exact peer+handle+channel+permission; and supports revoke/forget/replacement/rotation with atomic snapshots. Eight host groups plus 100 repeat runs cover lifecycle/validation/timeout, admission/denial, capacity/duplicates, revoke/replace, key epoch, and bounded storage shape. Discovery UI/local-presence proof, protected raw-key storage/crypto, persistent recovery/reset, rate limits, ESP-NOW binding, and physical provisioning remain.
- The v0 alarm engine holds 16 normalized-signal rules with inclusive above/below/outside-range comparison, exact hysteresis and assert/clear debounce, four severities, nonvalid clear/hold/assert policy, latching/acknowledgement, atomic bounded events, and periodic reminders. Ten host groups cover thresholds, signed-safe hysteresis, chatter, stale/unavailable no-value behavior, latch/ack paths, clock/type/unit rejection, capacity, diagnostics, and restart. Cache-task, display, critical-event, persistence, and reviewed vehicle-rule composition remain.
- The cache-to-alarm evaluator scans all 16 latest snapshots at each monotonic poll rather than only changed generations, so unchanged values advance debounce, exact cache staleness, and reminders. It preflights the whole poll, aggregates up to 16 events, skips unruled signals, and treats a cache epoch as a runtime-reset boundary without inventing clears. Seven host groups cover capacity/lifecycle, unchanged state, stale/reminder boundaries, reset/reassert, aggregation, incompatible input, cache failure, and clock regression. Target-task timing and event consumers remain.
- ESP-NOW and persistent formats use explicit versioned serialization, not raw C/C++ memory layouts.
- Optional GPS and APU behavior remains modular; control functions are outside the initial core.
- OpenTrail receives normalized critical events and never needs J1939 knowledge.
- The OpenTrail alert v0 frame is a fixed 64-byte explicit codec with canonical units and separate event/condition IDs. CRC is corruption detection only; transport authentication and authorization remain mandatory.
- The alarm-to-critical-alert exporter maps 16 allowlisted rule IDs to only final assert/clear transitions, keeps stable condition and unique event IDs, derives monotonic age, converts six canonical units, and refuses local reminder/ack/latch events, nonvalid assertions, lifecycle conflicts, unsupported values, and codec failures without state/ID commit. Seven host groups cover mapping/capacity, codec round-trip, local-only events, quality/lifecycle, units, rollback, freshness, restart, and exhaustion. Reviewed mappings, persistent ID allocation, and physical authenticated transport remain.
- The eight-entry critical-alert outbox reserves emergency capacity and priority, validates retained `OGA0` event uniqueness, separates local queue rejection/acceptance from exact event+condition+lifecycle application ACK, and bounds abandoned prepare tokens, backoff, attempts, late ACK, lifetime, and terminal failure. Eight host groups plus 100 refined repeat runs cover malformed/capacity/order/priority, two-phase send, exact timers, retry/failure, late/mismatched ACK, and clocks. OG-018I reconstructed the exact sent physical frame as in-flight and completed it only through production authorization/replay/correlation ingress in both endpoint roles. Persistent live target state and authenticated on-device OpenTrail delivery remain.
- The mirrored 64-byte `OGK0` ACK codec carries accepted/rejected disposition and canonical reason, original lifecycle, consumer/producer/event/condition identity, consumer boot session/sequence, observed age, reserved zeros, and CRC. A fixed eight-consumer ingress requires adapter-authenticated metadata, exact logical peer/key-handle/channel/permission authorization, explicit consumer/boot-session binding, configured producer/age checks, a 32-sequence replay window, and exact outbox lifecycle correlation. Its canonical 280-byte checkpoint atomically restores bindings/replay only against the exact live authorization epoch, so rotation/revoke invalidates stale state. A 320-byte generation envelope and two-slot store now recover the newest unique valid checkpoint after one corrupt/interrupted slot and revalidate it through ingress before changing state. Only accepted/none is delivery success. Rate-limited/internal-error rejections release the retained event after normal backoff; unauthorized, stale, duplicate, conflict, malformed, and unsupported rejections terminate with typed correlation/reason/attempt evidence, and retryable responses terminate at the attempt limit. Codec, ingress, checkpoint, storage, and rejection policy total thirty-eight host groups; the storage, checkpoint, and ingress suites each repeat 100 times. OG-018I admitted both role-reversed returned physical ACKs through this production ingress and completed both reconstructed outbox entries, but host metadata is not physical authentication; ESP-IDF/NVS binding, coordinated durable authorization/outbox persistence, secure rollback resistance, operator presentation, and authenticated on-device ACK delivery remain.
- OTA is not accepted until rollback and physical recovery are designed and tested.
- Project software and documentation are published under Apache-2.0; external contributions follow `CONTRIBUTING.md`, and sensitive reports follow `SECURITY.md`.

## Candidate but unverified hardware

| Item | Current status | Required evidence |
| --- | --- | --- |
| 2 x Waveshare ESP32-S3-Touch-AMOLED-1.75-B, SKU 31262 | Owner reports ordered; not received or tested. Vendor identifies this as the standard non-GPS board in its protective case | Follow the prepared arrival procedure for exact unit/revision, shipping-demo preservation, display/touch/IMU, flash/PSRAM, USB recovery, power, paired independence, build and performance tests |
| Espressif ESP32-S3-DevKitC-1-N8R8 | Owner reports ordered as a bench mule; not received or tested | Exact revision, USB/serial recovery, synthetic telemetry and interface smoke test |
| Seeed Studio Wio Tracker L1 Pro MeshCore companion | Owner reports ordered; not received or tested. Shipping MeshCore/GNSS behavior and OpenGauge adapter are unverified | Preserve/record the shipping image, prove USB recovery, baseline MeshCore and GNSS no-fix/fix/reacquisition/rate/power, then test an authenticated normalized adapter without publishing identifiers or coordinates |
| Veepeak OBDCheck BLE, ASIN B073XKQQQW | Owner reports on hand; OpenGauge compatibility untested. Vendor documents Classic Bluetooth for Windows and no MS-CAN/SW-CAN | Follow the prepared allowlisted read-only discovery; exact variant/firmware, Windows serial path, vehicle/PID support, rates, and failure behavior; never assume J1939 or raw CAN |
| CAN/J1939 gateway interface | Not selected | MCU/board, TWAI/external controller, transceiver, protection, isolation, connector, power conditioning, environmental suitability |

No hardware is considered supported until repeatable test evidence is recorded.

## Assumptions to validate

- ESP-NOW can meet the required latency, update rate, peer count, and coexistence needs in a vehicle environment.
- The display candidate can render representative layouts within memory, frame-time, boot-time, and power budgets.
- Vehicle data needed by initial gauges is actually present and correctly documented for the target vehicle(s).
- An ESP32 plus suitable automotive interface/power/protection can be engineered safely; a development board alone is not an automotive installation.
- Normalized signal IDs and canonical units can serve J1939 now and other CAN sources later.

## Unresolved decisions

### Use case and hardware

- First target vehicle/engine, desired initial signals/alarms, available database/documentation, bus speed, connector, and electrical environment
- Exact display, gateway MCU, CAN controller/transceiver, isolation/protection, power supply, enclosure, temperature/vibration, and EMC strategy
- Number of gauges/peers, desired update rates, acceptable latency, and power-on behavior

### Data and wireless protocol

- Initial PGNs/SPNs, source-address behavior, transport protocol needs, proprietary PGNs, units, and freshness thresholds
- Signal-ID registry/versioning beyond four v0 entries, subscription negotiation, vehicle/layout-specific deadbands/rates, peer discovery/pairing, security, and Wi-Fi coexistence; batching and bounded local publication policy now have host evidence
- Alarm ownership between gateway and display, acknowledgement/latching behavior, and event history

### UI, persistence, and updates

- Graphics/UI framework, configuration format/workflow, touch gestures, theme/brightness, trends/history capacity, and safe interaction constraints
- OTA transport, signing/key custody, partition plan, rollback health criteria, recovery procedure, and fleet sequencing

### Optional integrations and governance

- GPS topology, APU protocol/control safety, OpenTrail physical event transport/key lifecycle/replay protection, cellular/SMS scope; the semantic v0 schema is host-tested
- Code of conduct, CI, release process, supported hardware/vehicle evidence, and safety/legal disclaimers; Apache-2.0 licensing, contribution guidance, and security reporting are established

## Next decision checkpoint

Bind the completed host gateway and alarm-cache loops to selected ESP-IDF tasks
and CAN/radio adapters
while recording the exact target vehicle/use case and reconciling the EEC1 fixture against
licensed/current J1939 data and legally obtained captured traffic. The
completed OG-004, OG-005, OG-006, OG-007, OG-008, OG-009, OG-010, OG-010B, OG-010C, OG-010D, OG-010E, OG-011A, OG-012B, OG-012C, OG-013A, OG-014, OG-014A, OG-015, OG-016A, OG-017, OG-018, OG-018A, OG-018B, OG-018C, OG-018D, OG-018E, OG-018F, OG-018G, OG-018H, and OG-018I evidence are inputs to
later layers rather than substitutes for physical CAN, on-device performance,
display, or transport validation. Incoming candidate boards follow
`hardware/INVENTORY.md` before any support claim.
