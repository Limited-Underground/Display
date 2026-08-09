# OpenGauge Progress Log

Progress is grouped by calendar day, newest first. Detailed acceptance criteria
remain in [the engineering backlog](../tasks/BACKLOG.md); this log is the concise
public chronology.

## 2026-08-09

- Built deterministic host foundations for passive CAN/J1939 acquisition,
  normalized telemetry, alarms, diagnostics, GPS state, gauge presentation,
  update rollback, peer authorization, and the OpenTrail critical-alert path.
- Proved physical `OGA0` alert and `OGK0` acknowledgement delivery through two
  Heltec companions and a SenseCAP repeater, including accepted, terminal stale,
  retryable rate-limit, retry-to-accept, and live host-state lifecycles.
- Added canonical ACK replay and outbox checkpoints, preflighted combined `OCR0`
  recovery, a recoverable two-slot host store, store-owned generations, and
  uncertain-commit reconciliation.
- Added the canonical 256-byte `OPA0` peer-authorization checkpoint. It restores
  active/revoked peers, opaque key handles, and authorization epochs atomically;
  the full 33-executable matrix and 100 focused repeats pass.
- Added the recoverable 288-byte `OPS0` two-slot host store around `OPA0`.
  Automatic generations, exact readback, ten interrupted-write boundaries,
  full-write error reconciliation, conflict/exhaustion handling, the complete
  34-executable matrix, and 100 focused repeats pass.
- Added the 1280-byte `ORS0` system checkpoint, binding exact `OPA0` peer
  authorization to exact `OCR0` ACK/outbox state under one generation. Private,
  dependency-correct candidates preflight all three owners before live import;
  the complete 35-executable matrix and 100 focused repeats pass.
- Added a recoverable two-slot store for exact `ORS0` generations. Store-owned
  allocation, exact readback, eleven interrupted-write boundaries, full-write
  error reconciliation, conflict/exhaustion refusal, the complete 36-executable
  matrix, and 100 focused repeats pass.
- Added an explicit external trusted-generation contract. Restore refuses a
  valid record below the supplied floor without live import; save allocation
  advances beyond both trusted and local generations. Ten focused groups, the
  unchanged 36-executable matrix, and 100 repeats pass.
- Documented the target storage/boot acceptance boundary: exact two-slot
  semantics, protected key-handle resolution, separate trusted-generation
  authority, boot/save ordering, coordinated reset, and physical interruption
  evidence. No ESP-IDF target implementation is claimed.
- Kept all target claims bounded: there is no ESP-IDF storage/radio/CAN binding,
  protected on-device key persistence, physical power-cut evidence, or vehicle
  validation yet.

## 2026-08-08

- OpenGauge work was still represented through the separate OpenTrail planning
  boundary; no standalone OpenGauge firmware repository capability was claimed.
