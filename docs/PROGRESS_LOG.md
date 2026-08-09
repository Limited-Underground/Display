# OpenGauge Progress Log

Progress is grouped by calendar day, newest first. Detailed acceptance criteria
remain in [the engineering backlog](../tasks/BACKLOG.md); this log is the concise
public chronology.

## 2026-08-09

### Connected hardware and integration

- Built deterministic host foundations for passive CAN/J1939 acquisition,
  normalized telemetry, alarms, diagnostics, GPS state, gauge presentation,
  update rollback, peer authorization, and the OpenTrail critical-alert path.
- Proved physical `OGA0` alert and `OGK0` acknowledgement delivery through two
  Heltec companions and a SenseCAP repeater, including accepted, terminal stale,
  retryable rate-limit, retry-to-accept, and live host-state lifecycles.

### Recovery software and safety

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
- Added dependency-injected protected key-handle preflight to direct and stored
  `ORS0` restore. Active handles must validate before any live owner changes;
  revoked peers are skipped, and unavailable/purpose/backend failures remain
  typed. Eight system and eleven store groups plus 100 repeats each pass.
- Added a typed system-recovery boot coordinator. Clean first boot requires an
  uninitialized trust source, unprovisioned state, and two exactly empty slots;
  successful restore, degraded repair, safe mode, and service-required outcomes
  remain distinct. Trusted-floor catch-up must pass exact readback before
  transport is enabled. Nine groups, the 37-executable matrix, and 100 repeats
  pass.
- Added verified system-recovery save ordering. Normal saves require exact
  agreement between the newest local and trusted generation, then verify the
  next `ORS0` before advancing and exactly reading back trust. Ahead/uncertain
  state requires boot reconciliation; behind is rollback; missing state fails
  visibly. Eight groups, the 38-executable matrix, and 100 repeats pass.
- Hardened degraded boot recovery against a hidden-newer-slot case. Empty or
  checksum-invalid peer media remains known degraded state, but an unreadable
  peer slot now requires service with transport disabled and no trusted-floor
  advancement. Ten boot groups, the 38-executable matrix, and 100 repeats pass.
- Added bounded automatic repair for known empty/checksum-invalid peer media.
  Only a current operationally degraded boot result may write; repair commits a
  new `ORS0`, advances/readbacks trust, and verifies both slots valid. Unreadable,
  healthy, service, stale, and uncertain cases fail closed. Five groups, the
  39-executable matrix, and 100 repeats pass.
- Added a fixed-shape, redacted operator status for boot, save, and repair
  outcomes. It preserves coarse state/reason/action, slot health, checkpoint
  generations, protected-key failure category, and transport/repair flags while
  excluding peer IDs, key handles, addresses, credentials, and raw checkpoint
  bytes. Unknown or incoherent results fail closed. Seven groups, the complete
  40-executable matrix, and 100 focused repeats pass locally.

### Public validation and project operations

- Added public GitHub Actions validation for every `main` push and pull request.
  The Windows 2025/UCRT64 workflow uses commit-pinned actions and runs the
  complete matrix; the repository badge is the current-run authority.
- Linked OpenTrail's separate public workflow after it passed both verifier CLI
  builds, 23 C++ test executables, and the four-group Python MeshCore lease suite
  with zero annotations. The two CI scopes remain distinct.

### Remaining gates

- Kept all target claims bounded: there is no ESP-IDF storage/radio/CAN binding,
  protected on-device key persistence, physical power-cut evidence, or vehicle
  validation yet.

## 2026-08-08

- OpenGauge work was still represented through the separate OpenTrail planning
  boundary; no standalone OpenGauge firmware repository capability was claimed.
