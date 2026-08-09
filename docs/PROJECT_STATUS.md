# OpenGauge Project Status, Assumptions, and Open Questions

Status date: 2026-08-09

## Conceptual goals

- Separate ESP32 CAN/J1939 gateway and round touchscreen gauge nodes
- Normalized, validated telemetry delivered over ESP-NOW
- Configurable gauges, warnings, local persistence, and stale-data behavior
- Optional GPS, APU/auxiliary, OpenTrail event, and recoverable OTA modules

The critical-alert semantic interface has bounded host validation in this and
the OpenTrail repository. Vehicle acquisition, normalization from real signals,
physical transport, displays, and hardware remain unvalidated.

## Decisions captured

- Gateway and displays are separate roles with independent failure boundaries.
- The initial CAN path is passive/listen-only.
- Raw J1939 frames are not the default gauge-network payload; the gateway publishes normalized selected signals.
- Hardware adapters are isolated from parsing, decoding, caching, alarms, UI, and protocols.
- J1939 unavailable/error/stale states remain explicit and cannot become plausible numeric readings.
- ESP-NOW and persistent formats use explicit versioned serialization, not raw C/C++ memory layouts.
- Optional GPS and APU behavior remains modular; control functions are outside the initial core.
- OpenTrail receives normalized critical events and never needs J1939 knowledge.
- The OpenTrail alert v0 frame is a fixed 64-byte explicit codec with canonical units and separate event/condition IDs. CRC is corruption detection only; transport authentication and authorization remain mandatory.
- OTA is not accepted until rollback and physical recovery are designed and tested.

## Candidate but unverified hardware

| Item | Current status | Required evidence |
| --- | --- | --- |
| ~1.75-inch round 466×466 touch AMOLED ESP32-S3, ~8 MB PSRAM/~16 MB flash | Candidate class only; exact board unknown | Exact SKU, display/touch drivers, flash/PSRAM, pins, power, availability, build and performance tests |
| CAN/J1939 gateway ESP32 | Not selected | MCU/board, TWAI/external controller, transceiver, protection, isolation, connector, power conditioning, environmental suitability |
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
- License, contribution/security policy, CI, releases, supported hardware/vehicle evidence, and safety/legal disclaimers

## Next decision checkpoint

Define a small reference signal set and obtain representative synthetic or captured J1939 frames. Complete OG-005 and OG-007 semantics on the host before selecting or attaching a physical CAN gateway to a vehicle. The completed OG-018 semantic contract becomes an input to the later alarm exporter and authenticated physical adapter rather than a substitute for them.
