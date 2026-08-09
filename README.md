# OpenGauge

OpenGauge is a proposed free/open-source ESP32 vehicle instrumentation and telemetry platform. Its baseline architecture separates a listen-only CAN/J1939 gateway from independently operating wireless gauge displays.

## Project status

Architecture/bootstrap phase. The transport-neutral OpenGauge-to-OpenTrail critical-alert v0 codec, application-acknowledged delivery outbox, and mirrored `OGK0` critical-alert ACK codec have deterministic host evidence. A passive Classical CAN receive abstraction/fake, bounded Classical J1939 identifier parser, fixed decoder registry with one EEC1 engine-speed fixture, normalized signal model, thread-safe fixed-capacity telemetry cache, fixed 16-rule alarm engine, cache-to-alarm evaluator, allowlisted alarm-to-critical-alert exporter, fake encrypted-unicast ESP-NOW transport contract, explicit 96-byte gateway-to-gauge telemetry codec, per-gauge subscription/deadband/rate scheduler, cache-to-radio publisher, bounded CAN-to-radio gateway loop, authenticated-metadata gauge receiver/store, fail-visible eight-widget view model and four-series trend buffer, fixed-memory typed diagnostics core, versioned recoverable two-slot gauge layout store, transport-neutral GPS fix/quality/age tracker, OTA trial-confirmation/rollback guard, and opaque-handle peer approval/authorization registry are also host-tested. There is still no production firmware, ESP-IDF CAN/radio/storage/GNSS/boot binding, physical key provisioning, validated CAN hardware, supported display or GPS source, frozen production protocol, supported-vehicle list, physical alert/ACK transport, or validated OTA flow.

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

Read [the architecture](docs/ARCHITECTURE.md), [project status and assumptions](docs/PROJECT_STATUS.md), [the hardware evidence inventory](hardware/INVENTORY.md), [the diagnostics foundation](docs/diagnostics/DIAGNOSTICS_FOUNDATION_V0.md), [the recoverable gauge layout store](docs/configuration/GAUGE_LAYOUT_STORAGE_V0.md), [the GPS fix tracker](docs/gps/GPS_FIX_TRACKER_V0.md), [the OTA trial guard](docs/update/UPDATE_BOOT_GUARD_V0.md), [the peer authorization model](docs/security/PEER_AUTHORIZATION_V0.md), [the passive CAN receiver contract](docs/can/CAN_RECEIVER_V0.md), [the J1939 identifier contract](docs/can/J1939_IDENTIFIER_V0.md), [the decoder registry and EEC1 fixture](docs/can/J1939_DECODER_REGISTRY_V0.md), [the normalized signal contract](docs/telemetry/NORMALIZED_SIGNAL_MODEL_V0.md), [the telemetry cache contract](docs/telemetry/TELEMETRY_CACHE_V0.md), [the alarm engine contract](docs/alarm/ALARM_ENGINE_V0.md), [the cache-to-alarm evaluator](docs/alarm/ALARM_CACHE_EVALUATOR_V0.md), [the alarm-to-critical-alert exporter](docs/integration/CRITICAL_ALARM_EXPORTER_V0.md), [the critical-alert outbox](docs/integration/CRITICAL_ALERT_OUTBOX_V0.md), [the mirrored critical-alert ACK contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_ACK_V0.md), [the ESP-NOW transport contract](docs/wireless/ESP_NOW_TRANSPORT_V0.md), [the telemetry packet v0 contract](docs/wireless/TELEMETRY_PACKET_V0.md), [the publication scheduler v0 contract](docs/wireless/TELEMETRY_PUBLISH_SCHEDULER_V0.md), [the gateway publisher composition](docs/wireless/TELEMETRY_GATEWAY_PUBLISHER_V0.md), [the bounded gateway loop](docs/gateway/GATEWAY_TELEMETRY_LOOP_V0.md), [the gauge receiver/store](docs/wireless/GAUGE_TELEMETRY_RECEIVER_V0.md), [the gauge view model](docs/display/GAUGE_VIEW_MODEL_V0.md), [the gauge trend buffer](docs/display/GAUGE_TREND_BUFFER_V0.md), [the OpenTrail critical-alert v0 contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md), and [the backlog](tasks/BACKLOG.md). The next core work is an ESP-IDF/display/GNSS/boot/identity adapter proof and physical authenticated alert/ACK transport for the completed host components, while hardware candidates follow their arrival checklist.

## License and contributions

OpenGauge is free/open-source software licensed under the
[Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues
and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting
code or hardware evidence and use [SECURITY.md](SECURITY.md) for sensitive
security reports.
