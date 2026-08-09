# OpenGauge

OpenGauge is a proposed free/open-source ESP32 vehicle instrumentation and telemetry platform. Its baseline architecture separates a listen-only CAN/J1939 gateway from independently operating wireless gauge displays.

## Project status

Architecture/bootstrap phase. The transport-neutral OpenGauge-to-OpenTrail critical-alert v0 codec now has deterministic host evidence in both projects, but there is no production firmware, validated CAN hardware, selected display board, frozen ESP-NOW protocol, supported-vehicle list, physical alert transport, or validated OTA flow.

The original display concept is approximately three round 1.75-inch, 466×466 touch AMOLED ESP32-S3 devices with roughly 8 MB PSRAM and 16 MB flash. This describes a candidate class, not a locked or tested board.

## Intended capabilities

- A gateway that receives CAN/J1939, decodes selected PGNs/SPNs, validates them, and publishes normalized telemetry
- ESP-NOW distribution of selected signals rather than broadcasting every raw CAN frame
- Configurable analog, numeric, bar, multi-value, warning, trend, and status gauge layouts
- Local configuration, stale-data detection, alarms, and recovery after gateway loss
- Optional GPS and APU/auxiliary roles with explicit module boundaries
- Recoverable, version-aware wireless updates
- Normalized critical events that OpenTrail can consume without understanding J1939

These are design goals, not verified capabilities.

The exception is the bounded critical-alert semantic interface: its 64-byte
codec, producer validation, mirrored fixtures, and OpenTrail ingress policy are
host-tested. This does not validate vehicle acquisition or physical delivery.

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

Read [the architecture](docs/ARCHITECTURE.md), [project status and assumptions](docs/PROJECT_STATUS.md), [the OpenTrail critical-alert v0 contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md), and [the backlog](tasks/BACKLOG.md). The next core implementation remains a host-tested J1939 identifier parser and normalized signal model using synthetic/captured frames, before connecting to a vehicle.

## License and contributions

OpenGauge is free/open-source software licensed under the
[Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues
and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting
code or hardware evidence and use [SECURITY.md](SECURITY.md) for sensitive
security reports.
