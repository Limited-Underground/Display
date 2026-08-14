# OpenGauge

[![Host validation](https://github.com/nbjelanovic/OpenGauge/actions/workflows/host-validation.yml/badge.svg)](https://github.com/nbjelanovic/OpenGauge/actions/workflows/host-validation.yml)

OpenGauge is a free and open-source ESP32 platform for modular vehicle gauges and telemetry. A separate, initially listen-only gateway reads vehicle data, converts it into validated signals, and sends only the information each gauge needs.

The goal is a dependable base system that can operate with compact round gauges or larger displays while keeping GPS, remote alerts, and future auxiliary functions optional.

## Project status

| Area | Current state |
| --- | --- |
| Phase | Architecture, host-tested components, and bounded bench integration |
| Proven so far | A 49-executable Windows host-test matrix and limited three-radio alert/acknowledgement evidence |
| Not yet proven | Vehicle CAN/J1939 hardware, ESP32 target adapters, production security, physical gauge displays, power interruption, or field use |
| Next focus | Select exact gateway hardware and bind the existing CAN-to-radio path to target adapters; evaluate arriving display candidates separately |

OpenGauge is not production-ready and does not yet support a specific vehicle. See the [dated progress log](docs/PROGRESS_LOG.md) for recent work and the [engineering backlog](tasks/BACKLOG.md) for exact acceptance evidence and remaining gates.

## How it fits together

```text
Vehicle CAN/J1939
        |
  listen-only gateway
        |
  normalized telemetry
        |
  +-----+-------------------+
  |                         |
round gauge          larger display(s)

Optional: GNSS, OpenTrail critical-event bridge, auxiliary modules
```

- The **gateway** owns vehicle acquisition, decoding, validation, alarms, and wireless publication.
- A **gauge endpoint** owns its local display, layout, stale-data behavior, and recovery. A 1.75-inch round touch unit can be a complete compact gauge; a larger screen is an alternative, not a requirement.
- **Optional modules** must fail independently. OpenTrail receives only documented normalized events—not raw CAN/J1939 frames.

Read the [product role and display map](docs/PRODUCT_BOUNDARIES_V0.md) for required versus optional hardware and degraded behavior.

## Intended capabilities

- Passive CAN/J1939 acquisition through a protected vehicle gateway
- Validated, normalized signals with explicit missing, stale, unavailable, and error states
- Selected telemetry distributed over ESP-NOW instead of broadcasting raw CAN traffic
- Configurable numeric, needle, bar, warning, trend, and status displays
- Local alarms, configuration recovery, diagnostics, and version-aware updates
- Optional GNSS and normalized critical-event integration with OpenTrail

These are design goals unless the linked evidence explicitly says otherwise.

## Start here

- [Documentation guide](docs/README.md) — organized entry point for every technical area
- [Architecture](docs/ARCHITECTURE.md) — system roles, interfaces, and failure boundaries
- [Project status and open decisions](docs/PROJECT_STATUS.md) — current assumptions and unresolved choices
- [Dated progress log](docs/PROGRESS_LOG.md) — concise chronology, newest day first
- [Engineering backlog](tasks/BACKLOG.md) — task status and acceptance evidence
- [Hardware evidence inventory](hardware/INVENTORY.md) — candidate, missing, and tested equipment
- [Contributing](CONTRIBUTING.md) and [security reporting](SECURITY.md)

## Hardware status

No hardware is supported yet. Candidate equipment includes two owner-reported-on-hand Waveshare ESP32-S3 1.75-inch round touch displays, an owner-reported-ordered ESP32-S3 development board, an owner-reported-ordered Wio Tracker L1 Pro GNSS/LoRa unit, and an on-hand consumer OBD-II adapter. The round-display evidence now records public labels and one powered demo plus a bounded read-only pass on one unassociated unit: ESP32-S3, 16 MB flash, disabled Secure Boot and flash encryption, repeat USB re-enumeration, and an exact-size private backup retained outside Git. The second unit remained untouched. No visual post-reset application confirmation, manual recovery, flash write, peripheral behavior, or compatibility result exists, so every item remains unverified pending its exact revision, recovery path, interfaces, performance, power behavior, and failure handling. See the [dated Phase-B evidence](tests/hardware/OG-012A-PHASE-B-2026-08-14.md).

See the [hardware inventory](hardware/INVENTORY.md) and prepared bring-up procedures before testing or making compatibility claims.

## Repository layout

| Path | Purpose |
| --- | --- |
| `docs/` | Architecture, decisions, specifications, and dated project records |
| `firmware/components/` | Host-testable protocol, telemetry, alarm, storage, and UI components |
| `firmware/targets/` | Separately composed gateway, gauge, GNSS, and optional target applications |
| `hardware/` | Candidate inventory, bring-up procedures, wiring, power, and compatibility evidence |
| `tests/` | Host, fixture, integration, and physical hardware evidence |
| `tools/` | Validation, diagnostics, provisioning, and support utilities |
| `tasks/` | Prioritized engineering backlog and acceptance criteria |

## Safety boundary

OpenGauge is supplemental instrumentation. Early CAN work is listen-only, stale or missing data must remain conspicuous, and gateway or display failure must not affect vehicle operation. Any future control function requires a separate fail-safe design, authentication, authorization, interlocks, and safety review.

## License and contributions

OpenGauge is licensed under the [Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) first and use [SECURITY.md](SECURITY.md) for sensitive reports.
