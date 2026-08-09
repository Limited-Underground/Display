# OpenGauge

OpenGauge is a proposed free/open-source ESP32 vehicle instrumentation and telemetry platform. Its baseline architecture separates a listen-only CAN/J1939 gateway from independently operating wireless gauge displays.

## Project status

Architecture/bootstrap phase. The transport-neutral OpenGauge-to-OpenTrail critical-alert v0 codec, application-acknowledged delivery outbox, mirrored `OGK0` ACK codec, authenticated-metadata/replay/outbox ACK ingress, authorization-epoch-bound replay checkpoint with a recoverable two-slot host store, and bounded negative-ACK retry/terminal policy have deterministic host evidence. Two strengthened role-reversed Heltec/SenseCAP cycles carried 2/2 exact normative `OGA0` frames and 2/2 correlated `OGK0` responses with zero loss, duplicates, or errors; each returned ACK independently passed the real OpenGauge authorization/replay/correlation ingress and completed the exact reconstructed outbox entry. The host still supplied trust and reconstructed state rather than running a persistent on-device pipeline. A passive Classical CAN receive abstraction/fake, bounded Classical J1939 identifier parser, fixed decoder registry with one EEC1 engine-speed fixture, normalized signal model, thread-safe fixed-capacity telemetry cache, fixed 16-rule alarm engine, cache-to-alarm evaluator, allowlisted alarm-to-critical-alert exporter, fake encrypted-unicast ESP-NOW transport contract, explicit 96-byte gateway-to-gauge telemetry codec, per-gauge subscription/deadband/rate scheduler, cache-to-radio publisher, bounded CAN-to-radio gateway loop, authenticated-metadata gauge receiver/store, fail-visible eight-widget view model and four-series trend buffer, fixed-memory typed diagnostics core, versioned recoverable two-slot gauge layout store, transport-neutral GPS fix/quality/age tracker, OTA trial-confirmation/rollback guard, and opaque-handle peer approval/authorization registry are also host-tested. There is still no production firmware, ESP-IDF CAN/radio/storage/GNSS/boot binding, physical key provisioning, validated CAN hardware, supported display or GPS source, frozen production protocol, supported-vehicle list, authenticated on-device alert/ACK transport, or validated OTA flow.

## Latest verified checkpoint — 2026-08-09

- Two role-reversed accepted-ACK cycles completed the exact reconstructed outbox through real peer authorization, session, replay, and correlation checks.
- Two additional role-reversed stale-rejection cycles were processed as explicit terminal failures with zero delivery acknowledgements and `outbox_completed=false`.
- Two role-reversed rate-limit rejection cycles released exactly one queued retry with zero acknowledgements/completions and no terminal failure.
- Two four-leg role-reversed sequences then enforced exact backoff, prepared the same frame, retransmitted it, and completed only after a second physical accepted ACK.
- The latest two sequences started one OpenGauge process before the first alert and kept its real authorization, replay, and outbox state live through all four physical legs.
- Restart recovery now reaches the live outbox: boot-only atomic `OOC0` import/export reconstructs queued retry readiness, in-flight ACK timeout, maximum lifetime, exact frame, state, and attempts across a new monotonic-clock session. Its nonzero compatibility fingerprint is derived canonically from all timers, attempt limit, and emergency reserve instead of trusted caller input. Prepared sends, corrupt records, policy mismatch, and unrepresentable timers fail closed; durable storage is not yet connected.
- Across each two-cycle set, radio loss/duplicates/errors were zero, SenseCAP recorded exact aggregate +4 flood RX/TX, repeat stayed enabled, and cleanup passed 4/4.

The latest checkpoint proves deterministic outbox reconstruction in a new host object, while the physical test still used host-supplied trust. It is not yet a coordinated durable or authenticated on-device restart. See [the live-state physical evidence](tests/hardware/OG-018M-2026-08-09.md) and [the outbox checkpoint integration](docs/integration/CRITICAL_ALERT_OUTBOX_CHECKPOINT_V0.md).

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

Read [the latest live-state physical evidence](tests/hardware/OG-018M-2026-08-09.md), [the reconstructed retry evidence](tests/hardware/OG-018L-2026-08-09.md), [the terminal stale-rejection evidence](tests/hardware/OG-018J-2026-08-09.md), [the accepted completion evidence](tests/hardware/OG-018I-2026-08-09.md), [the architecture](docs/ARCHITECTURE.md), [project status and assumptions](docs/PROJECT_STATUS.md), [the hardware evidence inventory](hardware/INVENTORY.md), [the diagnostics foundation](docs/diagnostics/DIAGNOSTICS_FOUNDATION_V0.md), [the recoverable gauge layout store](docs/configuration/GAUGE_LAYOUT_STORAGE_V0.md), [the GPS fix tracker](docs/gps/GPS_FIX_TRACKER_V0.md), [the OTA trial guard](docs/update/UPDATE_BOOT_GUARD_V0.md), [the peer authorization model](docs/security/PEER_AUTHORIZATION_V0.md), [the passive CAN receiver contract](docs/can/CAN_RECEIVER_V0.md), [the J1939 identifier contract](docs/can/J1939_IDENTIFIER_V0.md), [the decoder registry and EEC1 fixture](docs/can/J1939_DECODER_REGISTRY_V0.md), [the normalized signal contract](docs/telemetry/NORMALIZED_SIGNAL_MODEL_V0.md), [the telemetry cache contract](docs/telemetry/TELEMETRY_CACHE_V0.md), [the alarm engine contract](docs/alarm/ALARM_ENGINE_V0.md), [the cache-to-alarm evaluator](docs/alarm/ALARM_CACHE_EVALUATOR_V0.md), [the alarm-to-critical-alert exporter](docs/integration/CRITICAL_ALARM_EXPORTER_V0.md), [the critical-alert outbox](docs/integration/CRITICAL_ALERT_OUTBOX_V0.md), [the mirrored critical-alert ACK contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_ACK_V0.md), [the ACK ingress and correlation policy](docs/integration/CRITICAL_ALERT_ACK_INGRESS_V0.md), [the ACK replay checkpoint](docs/integration/CRITICAL_ALERT_ACK_CHECKPOINT_V0.md), [the recoverable checkpoint store](docs/integration/CRITICAL_ALERT_ACK_CHECKPOINT_STORE_V0.md), [the negative-ACK policy](docs/integration/CRITICAL_ALERT_ACK_REJECTION_POLICY_V0.md), [the ESP-NOW transport contract](docs/wireless/ESP_NOW_TRANSPORT_V0.md), [the telemetry packet v0 contract](docs/wireless/TELEMETRY_PACKET_V0.md), [the gateway publisher composition](docs/wireless/TELEMETRY_GATEWAY_PUBLISHER_V0.md), [the bounded gateway loop](docs/gateway/GATEWAY_TELEMETRY_LOOP_V0.md), [the gauge receiver/store](docs/wireless/GAUGE_TELEMETRY_RECEIVER_V0.md), [the gauge view model](docs/display/GAUGE_VIEW_MODEL_V0.md), [the OpenTrail critical-alert v0 contract](docs/integration/OPENGAUGE_CRITICAL_ALERT_V0.md), and [the backlog](tasks/BACKLOG.md). The next core work is coordinated durable restart recovery plus target adapters and authenticated on-device transport.

## License and contributions

OpenGauge is free/open-source software licensed under the
[Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues
and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting
code or hardware evidence and use [SECURITY.md](SECURITY.md) for sensitive
security reports.
