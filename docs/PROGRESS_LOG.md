# OpenGauge Progress Log

Progress is grouped by calendar day, newest first. Detailed acceptance criteria
remain in [the engineering backlog](../tasks/BACKLOG.md); this log is the concise
public chronology.

## 2026-08-12

### Product roles and display alternatives

- Added one public role/dependency map separating the vehicle gateway from
  gauge endpoints and optional GNSS, additional display, OpenTrail bridge, and
  auxiliary roles.
- Clarified that the ordered 1.75-inch round touch unit can serve as a complete
  compact gauge endpoint. A larger touchscreen is an alternative renderer for
  more simultaneous content and navigation, not a base requirement.
- Required both display shapes to consume the same normalized telemetry,
  explicit stale/error state, alarms, and semantic configuration boundaries;
  each still needs its own readability/touch/resource/power acceptance.
- Preserved independent failure: display loss cannot stop gateway acquisition;
  GNSS/OpenTrail/auxiliary absence cannot stop core instrumentation; and raw
  CAN/J1939 never crosses into OpenTrail.

### Cross-store canonical layout transfer proof

- Composed the existing canonical exporter and confirmed importer across two
  independent fake stores without adding a hidden transfer shortcut.
- Source generation 2 exports exactly, while a destination already at
  generation 10 performs no write during preview and stores the confirmed
  content as its own generation 11. Restart selects that destination record.
- Re-importing the same source record under a new confirmation token returns
  unchanged at generation 11 with no extra write; destination re-export carries
  generation 11 and the transferred content.
- Eleven cumulative workflow groups pass 100/100 focused repeats plus the
  complete 47-executable host matrix including publication safety. File
  transport, source/destination authorization, and physical behavior remain
  open.

### Fail-closed canonical layout export

- Added `GaugeLayoutStore::export_current`, which reuses normal boot selection
  and returns the exact load source, both slot states, and recovery requirement
  with one canonical 576-byte `OGL0` record.
- Known empty/corrupt storage can export the separately validated safe default;
  one known valid slot can be exported with recovery visibly required. A valid
  older slot is not exported when the other slot is unreadable because it may
  hide a newer committed generation.
- Invalid output capacity, invalid safe default, equal-generation conflict, and
  I/O degradation fail without changing the caller's sentinel-filled buffer.
- Thirteen cumulative layout groups pass 100/100 focused repeats plus the
  complete 47-executable host matrix including publication safety.
  File/download adapters, access/confidentiality policy, and physical behavior
  remain open.

### Strict confirmed local layout import

- Added `stage_import_record` to the layout-change workflow. It accepts only
  the exact 576-byte canonical `OGL0` record and preserves the decoder's typed
  length, magic, version, canonical-form, checksum, and layout errors.
- Successful decode returns only a bounded summary for preview and then stages
  the decoded content under the existing single-use confirmation token. The
  record's source generation is informational; confirmation allocates the
  normal next local generation.
- Short input cannot consume a request ID. Corrupt input performs no write and
  cannot replace an already-live confirmation prompt. Even a source generation
  of `UINT64_MAX` becomes local generation 1 on empty storage after exact
  confirmation, with zero erases.
- Ten cumulative workflow groups pass 100/100 focused repeats plus the complete
  47-executable host matrix including publication safety. File selection,
  source authorization/authenticity, renderer/input binding, and physical
  target behavior remain open.

### Confirmed restore-default path without erase

- Added `stage_restore_default` to the existing layout-change workflow, sharing
  its one pending request, exact confirmation window, same-boot replay rules,
  generation allocation, uncertainty handling, and operator projection.
- A confirmed compiled default is stored as an ordinary next generation. Host
  restart selects it as newest; a repeated restore is unchanged and causes no
  write; invalid default content fails before persistence.
- The workflow test explicitly proves zero slot erases during stage, confirm,
  restart, unchanged repeat, and invalid request. Low-level two-slot erase
  remains a separate service/replacement primitive rather than a casual UI
  reset.
- Nine cumulative workflow groups pass 100/100 focused repeats plus the complete
  47-executable matrix including publication safety. Renderer text/input,
  source authorization, target concurrency, and physical behavior remain open.

### Typed gauge-layout reset uncertainty

- Changed the two-slot layout reset path to preserve `commit_uncertain` when
  either erase reaches a failed backend commit; ordinary erase failure remains
  distinct, and both slot erases are still attempted.
- Expanded the target-shaped key/value suite across failed commit on slot A or
  slot B, with the erase applied and unapplied in each position. Restart chooses
  safe default only when both keys are actually gone; otherwise it selects the
  surviving prior generation with recovery required.
- The eleven adapter groups and twelve core groups pass 100/100 focused repeats
  plus the complete 47-executable matrix including publication safety. Reset
  confirmation, ESP-IDF behavior, physical interruption, and endurance remain
  open.

### Atomic layout-change workflow facade

- Added one facade that owns the coordinator and returns operation error,
  persistence evidence, projection error, and operator status together for
  start, stop, snapshot, stage, confirm, cancel, and expiry service.
- The projection is derived before the method returns from the coordinator's
  immediate post-operation status. Callers no longer supply or later re-pair
  an operation observation with mutable state.
- Eleven groups cover lifecycle/policy, exact prompt staging, mismatch without
  prompt loss, apply/unchanged persistence, cancel/expiry, ordinary failure
  versus uncertainty, clock rollback, same-boot request replay, and confirmed
  default restoration without slot erase, strict confirmed import with
  non-authoritative source generation, and independent export/import transfer.
  Both underlying error and projected next action remain available.
- The suite passes 100/100 focused repeats plus the complete 47-executable host
  matrix including publication safety. The facade owns no lock, RTOS task,
  renderer, local-input proof, source authority, diagnostic policy, or target
  backend.

### Redacted layout-change diagnostic event

- Added a separate magic/versioned 32-bit diagnostic encoding for coarse
  layout-change operator state, requested action, attention, confirmation, and
  rejected-action flags.
- Struct and codec boundaries omit request ID, remaining confirmation time,
  layout generation/content, widget labels, and counters. A fixed redaction bit
  is required; nonzero reserved bits, unknown enums, and incoherent flag/state
  combinations fail closed.
- The adapter records through the existing fixed diagnostic ring under the
  configuration event family. Normal lifecycle state is info, expiry/rejection
  is warning, and persistence failure/uncertainty or clock fault is error.
- Eight groups pass 100/100 focused repeats and remain in the complete 47-executable host
  matrix including publication safety. Target logger binding, persistent
  retention/export, and physical failure capture remain open.

### Display-neutral layout-change operator status

- Added a pure fixed-shape projection from coordinator status, one observed
  operation result, and boot-local time into ready, confirmation-required,
  applied, unchanged, cancelled, expired, rejected, persistence-failed,
  restart-required, clock-fault, or unavailable state.
- The pending request token and exact remaining milliseconds exist only while
  confirmation is allowed. At the exact deadline the projection removes both
  and requests coordinator expiry service; an earlier clock sample fails
  visibly without inventing a valid countdown. A rejected stale/mismatched
  action flags the rejection without hiding the still-valid pending prompt.
- Successful persistence requires coherent active-slot/generation evidence.
  Ordinary failure asks for a newly staged request, while uncertain commit asks
  for restart reconciliation. Impossible state/operation/store combinations
  are rejected instead of becoming plausible UI state.
- Eight groups pass 100/100 focused repeats and remain in the complete 47-executable host
  matrix including publication safety. Rendering, localized text, diagnostic
  retention, source authority, target concurrency, and physical input remain
  open.

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
- Ten deterministic groups pass 100/100 focused repeats and remain in the
  complete 47-executable host matrix including publication safety. This does not prove
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
  complete 47-executable host matrix including publication safety. ESP-IDF
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
  47-executable host matrix including publication safety. ESP-IDF binding, protected keys,
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
