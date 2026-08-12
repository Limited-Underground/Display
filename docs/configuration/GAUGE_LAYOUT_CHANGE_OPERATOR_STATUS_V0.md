# Gauge Layout Change Operator Status v0

## Purpose

The layout-change coordinator returns precise machine errors, storage results,
and internal counters. A renderer should not independently reinterpret those
combinations. This host-tested projection produces one fixed semantic record
from:

1. the coordinator status immediately after an operation;
2. the operation and returned error;
3. the persistence result returned by confirmation, when applicable; and
4. the current boot-local monotonic time.

It does not mutate coordinator state or format operator text.

## Semantic states and actions

The projected states are:

- `unavailable`: the coordinator is stopped;
- `ready`: a new request may be staged;
- `confirmation_required`: one unexpired local prompt is active;
- `applied` or `unchanged`: confirmation completed with exact store evidence;
- `cancelled`, `expired`, or `rejected`: no persistence occurred;
- `persistence_failed`: stage and confirm a new request before retrying;
- `restart_required`: commit is uncertain and restart reconciliation is needed;
- `clock_fault`: monotonic time evidence is unsafe.

The action is separately typed as none, confirm-or-cancel, stage-new-request,
service-expiry, restart-and-reconcile, or service-clock. Renderers must not infer
an action from prose.

## Live prompt rule

Only `confirmation_required` can contain a nonzero `pending_request_id`, a
nonzero `confirmation_remaining_ms`, and `confirmation_allowed=true`. The
request ID is an opaque local action token, not text, identity, or authority.
A rejected mismatched action against a still-live prompt keeps this state and
token but sets `last_operation_rejected=true`; the valid pending prompt is not
hidden or converted into a terminal rejection.

At the exact deadline the projection emits `expired`, removes the request token
and countdown, disables confirmation, and requests expiry service. If `now_ms`
is earlier than the recorded opening time, it emits `clock_fault` and never
invents a valid countdown.

The target renderer must bind actions to the exact successfully displayed
token. The projection cannot flush old touch/button events or prove physical
presence.

## Coherence checks

Coordinator state is rejected when stopped/idle state carries pending fields,
running state has no confirmation window, or a pending request does not equal
the latest successfully staged request.

Operation evidence is also checked. Examples include:

- successful stage requires pending coordinator state;
- successful confirmation requires idle state, a successful store result, a
  real active slot, and nonzero generation;
- ordinary persistence failure cannot carry commit uncertainty;
- uncertain persistence must carry the typed `commit_uncertain` store error;
- mismatch while a prompt is no longer live is incoherent rather than a
  plausible rejection;
- cancel and service cannot carry persistence results.

Invalid combinations return `invalid_status` or `invalid_observation` with no
plausible operator state.

## Evidence and non-claims

Eight deterministic groups cover stopped/ready snapshots, exact pending time,
stage success/rejection, applied/unchanged confirmation, ordinary/uncertain
persistence, mismatch/expiry/clock/invalid-state confirmation, cancel/service,
and malformed or incoherent evidence. The suite passes 100/100 focused repeats
and the complete 45-executable strict host matrix.

This does not validate text, localization, accessibility, display timing,
input debounce, physical-presence proof, source authorization, concurrent target
tasks, diagnostics retention, ESP-IDF binding, or hardware behavior.
