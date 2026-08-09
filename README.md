# OpenGauge

OpenGauge is a proposed free/open-source ESP32 vehicle instrumentation and telemetry platform. Its baseline architecture separates a listen-only CAN/J1939 gateway from independently operating wireless gauge displays.

## Current snapshot — 2026-08-09

- **Phase:** architecture plus host-tested components and bounded three-radio
  bench integration; no production firmware or supported vehicle yet.
- **Bench hardware used by the shared OpenTrail link:** two Heltec V4 OLED USB
  companions and one Seeed SenseCAP Solar repeater.
- **Latest physical result:** accepted, terminal-rejection, retryable-rejection,
  retry-to-accept, and live-state alert/ACK cycles completed with zero observed
  message loss, duplicates, or new radio errors.
- **Latest software result:** the recoverable `ORS0` store now enforces a
  caller-supplied trusted generation floor. Restore refuses an old-but-valid
  record, and new saves advance beyond both trusted and local generations. The
  complete 36-executable host matrix and 100 focused repeats pass.
- **Still unproved:** ESP-IDF target adapters, protected on-device keys/storage,
  physical power-cut behavior, real CAN/J1939 vehicle input, displays, and field
  radio performance.

Progress is organized by date in the [public progress log](docs/PROGRESS_LOG.md).

## Project status

Architecture/bootstrap phase. The transport-neutral OpenGauge-to-OpenTrail critical-alert v0 codec, application-acknowledged delivery outbox, mirrored `OGK0` ACK codec, authenticated-metadata/replay/outbox ACK ingress, authorization-epoch-bound replay checkpoint with a recoverable two-slot host store, and bounded negative-ACK retry/terminal policy have deterministic host evidence. Two strengthened role-reversed Heltec/SenseCAP cycles carried 2/2 exact normative `OGA0` frames and 2/2 correlated `OGK0` responses with zero loss, duplicates, or errors; each returned ACK independently passed the real OpenGauge authorization/replay/correlation ingress and completed the exact reconstructed outbox entry. The host still supplied trust and reconstructed state rather than running a persistent on-device pipeline. A passive Classical CAN receive abstraction/fake, bounded Classical J1939 identifier parser, fixed decoder registry with one EEC1 engine-speed fixture, normalized signal model, thread-safe fixed-capacity telemetry cache, fixed 16-rule alarm engine, cache-to-alarm evaluator, allowlisted alarm-to-critical-alert exporter, fake encrypted-unicast ESP-NOW transport contract, explicit 96-byte gateway-to-gauge telemetry codec, per-gauge subscription/deadband/rate scheduler, cache-to-radio publisher, bounded CAN-to-radio gateway loop, authenticated-metadata gauge receiver/store, fail-visible eight-widget view model and four-series trend buffer, fixed-memory typed diagnostics core, versioned recoverable two-slot gauge layout store, transport-neutral GPS fix/quality/age tracker, OTA trial-confirmation/rollback guard, and opaque-handle peer approval/authorization registry are also host-tested. There is still no production firmware, ESP-IDF CAN/radio/storage/GNSS/boot binding, physical key provisioning, validated CAN hardware, supported display or GPS source, frozen production protocol, supported-vehicle list, authenticated on-device alert/ACK transport, or validated OTA flow.

## Latest verified checkpoint — 2026-08-09

- Two role-reversed accepted-ACK cycles completed the exact reconstructed outbox through real peer authorization, session, replay, and correlation checks.
- Two additional role-reversed stale-rejection cycles were processed as explicit terminal failures with zero delivery acknowledgements and `outbox_completed=false`.
- Two role-reversed rate-limit rejection cycles released exactly one queued retry with zero acknowledgements/completions and no terminal failure.
- Two four-leg role-reversed sequences then enforced exact backoff, prepared the same frame, retransmitted it, and completed only after a second physical accepted ACK.
- The latest two sequences started one OpenGauge process before the first alert and kept its real authorization, replay, and outbox state live through all four physical legs.
- Restart recovery now reaches the live outbox: boot-only atomic `OOC0` import/export reconstructs queued retry readiness, in-flight ACK timeout, maximum lifetime, exact frame, state, and attempts across a new monotonic-clock session. Its nonzero compatibility fingerprint is derived canonically from all timers, attempt limit, and emergency reserve instead of trusted caller input. Prepared sends, corrupt records, policy mismatch, and unrepresentable timers fail closed; durable storage is not yet connected.
- Coordinated restart now has host-tested [live `OCR0` export/import](docs/integration/CRITICAL_ALERT_RECOVERY_CHECKPOINT_V0.md): one generation contains the exact ACK replay/authorization and outbox checkpoints, both boot imports preflight on private copies before either live owner changes, and exact retry readiness plus replay state are restored together. A recoverable two-slot store remains next; this is not yet target durability.
- A host-tested [two-slot `OCR0` recovery store](docs/integration/CRITICAL_ALERT_RECOVERY_STORE_V0.md) now writes one coordinated generation with full readback/byte/decode verification, restores the newest unique valid slot, preserves the prior good generation after partial/corrupt writes, and reports degraded I/O even when restore succeeds. ESP-IDF/NVS binding and physical power-cut/wear evidence remain.
- The store now owns next-generation allocation: empty storage starts at 1, successful saves rotate monotonically across slots, conflicted/unreadable baselines fail closed, and 64-bit exhaustion is reported before a write. Callers no longer choose recovery generations.
- Write errors now report `commit_uncertain` with the intended slot/generation. Sixteen interrupted-overwrite boundaries preserve the prior good generation, while a full write followed by an I/O error is reconciled on boot as committed state instead of being blindly retried.
- Peer authorization now has a host-tested [canonical `OPA0` restart checkpoint](docs/security/PEER_AUTHORIZATION_CHECKPOINT_V0.md). It persists only logical policy metadata and opaque key handles, preserves revoked peers and authorization epochs, refuses pending approvals, and imports atomically only into a clean boot registry. The full 33-executable matrix and 100 focused repeats pass; protected target storage and coordinated `OPA0`/`OCR0` restore remain.
- Peer authorization now also has a host-tested [recoverable two-slot `OPS0` store](docs/security/PEER_AUTHORIZATION_CHECKPOINT_STORE_V0.md). Normal saves allocate generations, rotate away from the newest good slot, require byte/decode readback, preserve the prior generation across ten interrupted-write boundaries, and reconcile a full write followed by an I/O error at boot. The full 34-executable matrix and 100 focused repeats pass; this is not yet protected ESP32 storage.
- Coordinated recovery now has a host-tested [`ORS0` system envelope](docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_V0.md). One generation binds exact `OPA0` peer authorization to exact `OCR0` ACK/outbox state. A temporary ACK ingress is constructed against private restored registry/outbox candidates so epoch and pointer dependencies are validated before any of the three live owners changes. The full 35-executable matrix and 100 focused repeats pass; recoverable `ORS0` storage remains next.
- Exact `ORS0` generations now have a host-tested [recoverable two-slot system store](docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_STORE_V0.md). The store owns normal generations, preserves the newest good slot across eleven interrupted-write boundaries, verifies exact readback/decode, exposes degraded reads, fails closed on conflict/exhaustion, and reconciles a full write followed by I/O error as committed at boot. The full 36-executable matrix and 100 focused repeats pass; target durability is still unproved.
- The system store now accepts an external trusted generation boundary: `restore_at_or_above` rejects a selected valid record below the minimum without importing any owner, while `save_next_after` advances beyond both the trusted value and every valid local slot. Ten focused groups, the unchanged 36-executable matrix, and 100 repeats pass. The hardware-backed trusted source itself is intentionally not invented by this host layer.
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

Read [the dated progress log](docs/PROGRESS_LOG.md), [the latest live-state physical evidence](tests/hardware/OG-018M-2026-08-09.md), [the recoverable system store](docs/integration/CRITICAL_ALERT_SYSTEM_RECOVERY_STORE_V0.md), [the architecture](docs/ARCHITECTURE.md), [project status and assumptions](docs/PROJECT_STATUS.md), [the hardware evidence inventory](hardware/INVENTORY.md), [the peer authorization model](docs/security/PEER_AUTHORIZATION_V0.md), and [the backlog](tasks/BACKLOG.md). Detailed component specifications and physical evidence remain organized under `docs/` and `tests/`. The next core work is target storage/adapters and authenticated on-device transport.

## License and contributions

OpenGauge is free/open-source software licensed under the
[Apache License 2.0](LICENSE). Contributions are welcome through GitHub issues
and pull requests; read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting
code or hardware evidence and use [SECURITY.md](SECURITY.md) for sensitive
security reports.
