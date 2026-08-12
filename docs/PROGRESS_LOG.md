# OpenGauge Progress Log

Progress is grouped by calendar day, newest first. Detailed acceptance criteria
remain in [the engineering backlog](../tasks/BACKLOG.md); this log is the concise
public chronology.

## 2026-08-12

### Single-use local gauge-layout confirmation

- Added a semantic coordinator that stages at most one validated layout change
  under an exact nonzero request ID and a caller-supplied monotonic timestamp.
- Confirmation must repeat that ID before the exact configured boundary.
  Successfully staged IDs must increase strictly within one coordinator start
  cycle; invalid or already-pending proposals do not consume an ID, while
  cancelled, failed, uncertain, and completed proposals do. Mismatch, replay,
  cancellation, expiry, and clock rollback leave storage untouched; clock
  rollback also consumes any pending request.
- The request is consumed before persistence. Ordinary failure therefore needs
  a new local confirmation, while commit uncertainty requires restart
  inspection rather than replaying an old approval. A newly confirmed request
  after an applied uncertain commit observes the stored generation and performs
  no additional write.
- Ten deterministic groups pass 100/100 focused repeats and the complete
  44-executable host matrix including publication safety. This does not prove
  physical-presence input, rendering, cross-boot input flushing, target task
  serialization, authenticated configuration, ESP-IDF storage, or power-cut
  behavior.

### Target-shaped `OGL0` gauge-layout key/value storage

- Added one shared backend-neutral key/value blob contract and moved the
  existing `ORS0` adapter onto it without changing its public behavior.
- Added exact 576-byte `og_config` / `gauge_layout` / `ogl0_a|b` layout
  binding with explicit commit after write/present-key erase, idempotent
  missing-key erase, strict value size, and distinct missing versus I/O errors.
- Ten deterministic groups exercise exact operations, backend failures, real
  layout rotation/reset, and restart selection after both applied and
  unapplied failed commits. A store-owned update path now ignores caller
  generation, allocates highest-valid plus one only for changed canonical
  content, rejects conflict/I/O/exhaustion before writing, and performs zero
  backend write/commit calls for unchanged content. Backend commit failure now
  returns a distinct uncertain result: restart suppresses a rewrite when the
  new generation exists and performs one normal retry when it does not. Ten
  adapter and twelve core groups pass 100/100 focused repeats and remain in the
  complete 44-executable host matrix including publication safety. ESP-IDF
  binding, physical power cuts, wear, migration, and configuration authenticity
  remain open.

### Target-shaped `ORS0` key/value storage

- Added a backend-neutral adapter for the two exact 1280-byte alert-system
  recovery slots with fixed `og_state` / `og_recovery` / `ors0_a|b` binding.
- Required backend commit after full-blob write or present-key erase, preserved
  missing-key erase as idempotent, and kept failed commits uncertain for
  restart inspection instead of blind retry or rollback.
- Thirteen deterministic groups exercise exact reads, failure mapping, real
  `ORS0` save/rotation/reset, boot/save composition after restart, and both
  applied and unapplied uncertain commits. Applied uncertainty is reconciled
  by advancing separate trust after restart; unapplied uncertainty restores the
  prior trusted generation without inventing a newer record.
  The focused suite passes 100/100 repeats and remains in the complete
  44-executable host matrix including publication safety. ESP-IDF binding, protected keys,
  authenticated integrity, trusted generation, locking, physical interruption,
  endurance, and on-device boot evidence remain open.

## 2026-08-10

### Cross-project field-test planning

- Linked OpenTrail's deterministic four-client, four-plus-repeater, and
  eight-plus-repeater load model as separate planning evidence. It does not
  substitute for OpenGauge ESP-NOW, display, CAN, or vehicle validation.
- Confirmed OpenTrail public run `31415094006` passes 33 strict C++
  executables, three verifier/planning CLI builds, four MeshCore lease groups,
  six field-plan/evidence, nine pilot-result, eight crypto-benchmark, twelve
  local-interface, eleven power-state, eight portable-client composition,
  eight secure-randomness, and eight monotonic-clock groups.
- Linked OpenTrail's portable-client composition preflight as separate platform
  groundwork. Its whole-contract review binds ten target-facing interfaces and
  caught the need for separate 64-byte protocol/configuration and 704-byte
  replay-checkpoint storage surfaces. It performs no mutable adapter I/O and
  supplies no concrete ESP-IDF adapter, board target, or physical result; it is
  not evidence for OpenGauge display, input, storage, power, ESP-NOW, CAN, or
  vehicle behavior.
- Linked OpenTrail's fail-closed secure-random source as separate security
  groundwork: typed readiness, bounded 1-64-byte requests, and complete output
  or no change. Its deterministic fake and host evidence do not prove target
  entropy/DRBG behavior or any OpenGauge target security property.
- Linked OpenTrail's checked monotonic-clock boundary as separate platform
  groundwork. It keeps boot-local elapsed time separate from UTC, permits equal
  ticks and temporary not-ready recovery, and latches rollback/source failure.
  No ESP-IDF timer/task/deep-sleep/brownout or physical timing evidence exists,
  and this does not prove any OpenGauge target property.
- Linked OpenTrail's fail-visible power-state boundary as separate platform
  groundwork. It preserves charging, missing, stale, fault, and invalid states
  without estimating percentage from voltage. Board adapters, thresholds,
  charger behavior, endurance, and all OpenGauge target power behavior remain
  unproved.
- Linked OpenTrail's revision-bound semantic display/input contract as separate
  platform groundwork. It rejects stale screen actions and requires hold-only
  critical confirmation without choosing a renderer. This does not validate
  OpenGauge display/input hardware, rendering, accessibility, vehicle use, or
  critical-alert delivery.
- Linked OpenTrail's ten-group rollback-safe outbound-counter prerequisite as
  separate packet-security groundwork. It is not evidence for OpenGauge target
  storage, keys, ESP-NOW, CAN, display, or vehicle behavior.
- Linked OpenTrail's strict crypto-benchmark evidence boundary and protected-
  packet budget, which now has nine groups. The latter exposes a
  36-byte candidate authenticated header plus 16-byte tag and a separate
  64-byte signed-group source-authentication candidate.
- Linked OpenTrail's zero/one-repeater decision and nine-group exact-byte host
  policy. It does not rewrite a protected TTL and does not establish OpenGauge
  ESP-NOW overhead, security, routing, or target behavior.
- Linked OpenTrail's nine-group reboot-safe repeater replay coordinator. It
  persists each eligible observation before queue release, repairs known host-
  checkpoint degradation before operation, and blocks transmit after uncertain
  persistence. Its volatile frame-loss tradeoff, protected target storage,
  power cuts, wear, and durable outbox remain OpenTrail gates; none of this is
  OpenGauge target evidence.
- Linked OpenTrail's ten-group context-bound `ODS0/v1` store. The same 704-byte
  slots now embed group context and epoch; mismatched and legacy unbound media
  require service without restore or overwrite. Protected integrity, rollback,
  reset/migration, and physical storage evidence remain OpenTrail gates and do
  not validate OpenGauge persistence.
- Linked OpenTrail's versioned four-person standalone plan and deterministic
  pass/fail/ineligible/invalid result evaluator. The plan requires no OpenGauge
  device or vehicle connection and remains blocked on an exact four-unit client
  hardware/firmware freeze; it is not OpenGauge CAN, ESP-NOW, display, or
  vehicle evidence.

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
- Bound that redacted status to the existing diagnostics ring as one atomic,
  magic/versioned 32-bit `configuration_recovery / state_code` event. The word
  round-trips coarse outcome and severity while omitting generations and all
  identity-bearing fields; malformed or incoherent words fail closed. Eight
  groups, the complete 41-executable matrix, and 100 focused repeats pass
  locally.

### Public validation and project operations

- Added public GitHub Actions validation for every `main` push and pull request.
  The Windows 2025/UCRT64 workflow uses commit-pinned actions and runs the
  complete matrix; its current-main run passes all 41 executables with zero
  annotations.
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
