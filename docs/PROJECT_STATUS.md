# OpenGauge Project Status, Assumptions, and Open Questions

Status date: 2026-08-09

## Conceptual goals

- Separate ESP32 CAN/J1939 gateway and round touchscreen gauge nodes
- Normalized, validated telemetry delivered over ESP-NOW
- Configurable gauges, warnings, local persistence, and stale-data behavior
- Optional GPS, APU/auxiliary, OpenTrail event, and recoverable OTA modules

The critical-alert semantic interface has bounded host validation in this and
the OpenTrail repository. A passive Classical CAN receiver interface and
bounded 16-frame fake, the Classical J1939 identifier parser, a
fixed-capacity decoder registry with one EEC1 engine-speed fixture, normalized
signal model, fixed-capacity thread-safe telemetry cache, an opaque
encrypted-unicast ESP-NOW transport contract/fake, an explicit 96-byte
telemetry packet codec, a bounded per-gauge publication scheduler, a
cache-cursor-to-radio publisher, and a bounded CAN-to-radio gateway loop now
have deterministic host tests. Physical vehicle acquisition,
normalization from captured/real signals,
on-device performance, physical transport, keys, displays, and hardware remain
unvalidated.

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
- ESP-NOW and persistent formats use explicit versioned serialization, not raw C/C++ memory layouts.
- Optional GPS and APU behavior remains modular; control functions are outside the initial core.
- OpenTrail receives normalized critical events and never needs J1939 knowledge.
- The OpenTrail alert v0 frame is a fixed 64-byte explicit codec with canonical units and separate event/condition IDs. CRC is corruption detection only; transport authentication and authorization remain mandatory.
- OTA is not accepted until rollback and physical recovery are designed and tested.
- Project software and documentation are published under Apache-2.0; external contributions follow `CONTRIBUTING.md`, and sensitive reports follow `SECURITY.md`.

## Candidate but unverified hardware

| Item | Current status | Required evidence |
| --- | --- | --- |
| 2 x Waveshare ESP32-S3-Touch-AMOLED-1.75-B, SKU 31262 | Owner reports ordered; not received or tested. Vendor identifies this as the standard non-GPS board in its protective case | Follow the prepared arrival procedure for exact unit/revision, shipping-demo preservation, display/touch/IMU, flash/PSRAM, USB recovery, power, paired independence, build and performance tests |
| Espressif ESP32-S3-DevKitC-1-N8R8 | Owner reports ordered as a bench mule; not received or tested | Exact revision, USB/serial recovery, synthetic telemetry and interface smoke test |
| Veepeak OBDCheck BLE, ASIN B073XKQQQW | Owner reports on hand; OpenGauge compatibility untested. Vendor documents Classic Bluetooth for Windows and no MS-CAN/SW-CAN | Follow the prepared allowlisted read-only discovery; exact variant/firmware, Windows serial path, vehicle/PID support, rates, and failure behavior; never assume J1939 or raw CAN |
| CAN/J1939 gateway interface | Not selected | MCU/board, TWAI/external controller, transceiver, protection, isolation, connector, power conditioning, environmental suitability |
| GPS ESP32/module | Not selected | Receiver/module, update rate, antenna, interfaces, cold start, accuracy, power |

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

Bind the completed host gateway loop to selected ESP-IDF CAN/radio adapters
while recording the exact target vehicle/use case and reconciling the EEC1 fixture against
licensed/current J1939 data and legally obtained captured traffic. The
completed OG-004, OG-005, OG-006, OG-007, OG-008, OG-009, OG-010, OG-010B, OG-010C, OG-010D, and OG-018 contracts are inputs to
later layers rather than substitutes for physical CAN, on-device performance,
display, or transport validation. Incoming candidate boards follow
`hardware/INVENTORY.md` before any support claim.
