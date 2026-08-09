# OpenGauge Project Status, Assumptions, and Open Questions

Status date: 2026-08-09

## Conceptual goals

- Separate ESP32 CAN/J1939 gateway and round touchscreen gauge nodes
- Normalized, validated telemetry delivered over ESP-NOW
- Configurable gauges, warnings, local persistence, and stale-data behavior
- Optional GPS, APU/auxiliary, OpenTrail event, and recoverable OTA modules

The critical-alert semantic interface has bounded host validation in this and
the OpenTrail repository. The Classical J1939 identifier parser, a
fixed-capacity decoder registry with one EEC1 engine-speed fixture, and the
normalized signal model now have deterministic host tests. Vehicle
acquisition, normalization from captured/real signals, physical transport,
displays, and hardware remain unvalidated.

## Decisions captured

- Gateway and displays are separate roles with independent failure boundaries.
- The initial CAN path is passive/listen-only.
- Raw J1939 frames are not the default gauge-network payload; the gateway publishes normalized selected signals.
- Hardware adapters are isolated from parsing, decoding, caching, alarms, UI, and protocols.
- J1939 unavailable/error/stale states remain explicit and cannot become plausible numeric readings.
- The v0 J1939 parser is explicitly bounded to 29-bit Classical J1939: standard frames, out-of-range identifiers, and the J1939-22 extended data page fail closed.
- The v0 decoder registry rejects duplicate, over-capacity, noncanonical PDU1, unknown, and malformed dispatch. Its single EEC1/SPN 190 fixture preserves reserved/error/unavailable encodings as nonnumeric quality states and still requires licensed-standard and captured-frame reconciliation.
- The v0 normalized signal model uses fixed-capacity namespaced IDs, integer canonical units, explicit quality, protocol-specific provenance, and an exact `age >= threshold` stale boundary.
- ESP-NOW and persistent formats use explicit versioned serialization, not raw C/C++ memory layouts.
- Optional GPS and APU behavior remains modular; control functions are outside the initial core.
- OpenTrail receives normalized critical events and never needs J1939 knowledge.
- The OpenTrail alert v0 frame is a fixed 64-byte explicit codec with canonical units and separate event/condition IDs. CRC is corruption detection only; transport authentication and authorization remain mandatory.
- OTA is not accepted until rollback and physical recovery are designed and tested.
- Project software and documentation are published under Apache-2.0; external contributions follow `CONTRIBUTING.md`, and sensitive reports follow `SECURITY.md`.

## Candidate but unverified hardware

| Item | Current status | Required evidence |
| --- | --- | --- |
| 2 x Waveshare ESP32-S3-Touch-AMOLED-1.75-B, SKU 31262 | Owner reports ordered; not received or tested | Exact unit/revision, display/touch/IMU, flash/PSRAM, pins, USB recovery, power, build and performance tests |
| Espressif ESP32-S3-DevKitC-1-N8R8 | Owner reports ordered as a bench mule; not received or tested | Exact revision, USB/serial recovery, synthetic telemetry and interface smoke test |
| Veepeak OBDCheck BLE, ASIN B073XKQQQW | Owner reports on hand; OpenGauge compatibility untested | Exact firmware/services and read-only generic OBD-II capability; not assumed to provide J1939 or raw CAN |
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
- Signal-ID registry/versioning, telemetry batching, deadbands/rates, subscriptions, packet schema, peer discovery/pairing, security, and Wi-Fi coexistence
- Alarm ownership between gateway and display, acknowledgement/latching behavior, and event history

### UI, persistence, and updates

- Graphics/UI framework, configuration format/workflow, touch gestures, theme/brightness, trends/history capacity, and safe interaction constraints
- OTA transport, signing/key custody, partition plan, rollback health criteria, recovery procedure, and fleet sequencing

### Optional integrations and governance

- GPS topology, APU protocol/control safety, OpenTrail physical event transport/key lifecycle/replay protection, cellular/SMS scope; the semantic v0 schema is host-tested
- Code of conduct, CI, release process, supported hardware/vehicle evidence, and safety/legal disclaimers; Apache-2.0 licensing, contribution guidance, and security reporting are established

## Next decision checkpoint

Exercise the EEC1 fixture's normalized output through the bounded cache
(OG-008), while recording the exact target vehicle/use case and reconciling the
fixture against licensed/current J1939 data and legally obtained captured
traffic. The completed OG-005, OG-006, OG-007, and OG-018 contracts are inputs
to later layers rather than substitutes for physical CAN, display, or
transport validation. Incoming candidate boards follow
`hardware/INVENTORY.md` before any support claim.
