# OpenGauge

OpenGauge is a proposed free/open-source ESP32 vehicle instrumentation and telemetry platform. Its baseline architecture separates a listen-only CAN/J1939 gateway from independently operating wireless gauge displays.

## Project status

Architecture/bootstrap phase. The transport-neutral OpenGauge-to-OpenTrail critical-alert v0 codec has deterministic host evidence in both projects. A passive Classical CAN receive abstraction/fake, bounded Classical J1939 identifier parser, fixed decoder registry with one EEC1 engine-speed fixture, normalized signal model, thread-safe fixed-capacity telemetry cache, fake encrypted-unicast ESP-NOW transport contract, explicit 96-byte gateway-to-gauge telemetry codec, per-gauge subscription/deadband/rate scheduler, and cache-to-radio publisher composition are also host-tested. There is still no production firmware, ESP-IDF CAN/radio binding, validated CAN hardware, supported display, frozen production protocol, supported-vehicle list, physical alert transport, or validated OTA flow.

Two Waveshare ESP32-S3-Touch-AMOLED-1.75-B units (SKU 31262) are reported ordered for evaluation. They remain candidate hardware until received, identified, built, benchmarked, and recovery-tested. Other candidate and missing hardware is tracked in [the evidence inventory](hardware/INVENTORY.md).

## Intended capabilities

- A gateway that receives CAN/J1939, decodes selected PGNs/SPNs, validates them, and publishes normalized telemetry
- ESP-NOW distribution of selected signals rather than broadcasting every raw CAN frame
- Configurable analog, numeric, bar, multi-value, warning, trend, and status gauge layouts
- Local configuration, stale-data detection, alarms, and recovery after gateway loss
- Optional GPS and APU/auxiliary roles with explicit module boundaries
- Recoverable, version-aware wireless updates
- Normalized critical events that OpenTrail can consume without understanding J1939

These are design goals, not verified capabilities.

The bounded critical-alert semantic interface, passive CAN receiver contract,
Classical J1939 identifier
rules, one narrow EEC1 engine-speed fixture, normalized signal invariants,
cache state/staleness/concurrency rules, an opaque wireless transport fake,
the telemetry packet's serialization/sequence/age rules, and bounded publication
selection have host evidence.
These contracts do not validate vehicle acquisition,
captured vehicle data, a decoder catalog, on-device cache/radio performance,
display hardware, encryption keys, or physical delivery.

## Repository layout

| Path | Purpose |
| --- | --- |
| `docs/` | Architecture, assumptions, decisions, and specifications |
| `firmware/components/` | Reusable drivers, protocol, telemetry, alarm, and UI components |
| `firmware/targets/` | Gateway, gauge, GPS, or auxiliary deployable applications |
| `hardware/` | Board inventory, CAN interface, wiring, power, display, and compatibility evidence |
| `tests/` | Host, integration, protocol, captured-frame, and hardware tests |
| `tools/` | Capture, decode, provisioning, packaging, and diagnostic utilities |
| `prototypes/` | Time-bounded feasibility experiments |
| `tasks/` | Prioritized engineering backlog and acceptance criteria |

## Design boundary

OpenGauge owns vehicle acquisition, decode/normalization, gauge display, vehicle alarms, and its local telemetry network. OpenTrail receives only documented normalized events. An APU/auxiliary controller is an optional module and must not be hard-wired into the telemetry core.

## Start here

Read [the architecture](docs/ARCHITECTURE.md), [project status and assumptions](docs/PROJECT_STATUS.md), [the hardware evidence inventory](hardware/INVENTORY.md), [the passive CAN receiver contract](docs/can/CAN_RECEIVER_V0.md), [the J1939 identifier contract](docs/can/J1939_IDENTIFIER_V0.md), [the decoder registry and EEC1 fixture](docs/can/J1939_DECODER_REGISTRY_V0.md), [the normalized signal contract](docs/telemetry/NORMALIZED_SIGNAL_MODEL_V0.md), [the telemetry cache contract](docs/telemetry/TELEMETRY_CACHE_V0.md), [the ESP-NOW transport contract](docs/wireless/ESP_NOW_TRANSPORT_V0.md), [the telemetry packet v0 contract](docs/wireless/TELEMETRY_PACKET_V0.md), [the publication scheduler v0 contract](docs/wireless/TELEMETRY_PUBLISH_SCHEDULER_V0.md), [the gateway publisher composition](docs/wireless/TELEMETRY_GATEWAY_PUBLISHER_V0.md), [the OpenTrail critical-alert v0 contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md), and [the backlog](tasks/BACKLOG.md). The next core work is a bounded gateway task composition and ESP-IDF adapter proof, while hardware candidates follow their arrival checklist.

## License and contributions

OpenGauge is free/open-source software licensed under the
[Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues
and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting
code or hardware evidence and use [SECURITY.md](SECURITY.md) for sensitive
security reports.
