# Gauge Layout Change Confirmation v0

## Purpose

OpenGauge must not persist a layout merely because a configuration source sent
one valid record. This host-tested boundary separates a proposed change from
the local confirmation that permits one persistence attempt.

The coordinator is deliberately display- and input-neutral. It does not decide
what a prompt looks like or prove that a human is physically present.

## State and operations

The coordinator has three states: `stopped`, `idle`, and `pending`.

- `start(policy)` requires a nonzero confirmation window and resets boot-local
  counters and time observation.
- `stage(request_id, layout, now_ms)` accepts one nonzero request ID, validates
  canonical layout content without trusting its generation, requires the ID to
  be strictly greater than every successfully staged ID in this coordinator
  start cycle, and records the monotonic opening time.
- `confirm(request_id, now_ms)` accepts only the exact pending ID before the
  exact timeout boundary and consumes it before calling the layout store.
- `cancel(request_id)` consumes only the exact pending request without writing.
- `service(now_ms)` expires a pending request even when no input arrives.
- `stop()` consumes pending state and returns to `stopped`.

Only one request may be pending. Invalid layouts and proposals blocked by an
existing pending request do not consume their ID. Once staging succeeds, its ID
cannot be reused after confirmation, cancellation, expiry, clock rollback, or
persistence failure. Reaching `uint32` maximum exhausts this boot-local sequence
until the coordinator is explicitly stopped and started by the composition.

Request IDs correlate local UI state; they are not credentials, signatures,
authorization, cross-boot session identity, or replay protection for a remote
protocol.

## Exact time rule

If a request opens at `opened_ms` with window `window_ms`, confirmation is valid
only while:

```text
now_ms - opened_ms < window_ms
```

The exact `window_ms` boundary is expired. Equal monotonic samples are allowed.
Any sample lower than the last observed value consumes the pending request,
increments the clock-fault counter, and fails closed. The coordinator will not
admit another request until time catches up to the last accepted sample or the
composition restarts it.

## Single-use persistence rule

The coordinator clears the pending ID and layout before calling
`GaugeLayoutStore::save_next_if_changed`. Therefore:

- a successful changed layout increments `applied_count`;
- canonical content already active increments `unchanged_count` and causes no
  layout write;
- ordinary persistence failure increments `failed_count` and requires a newly
  staged and locally confirmed request;
- `commit_uncertain` increments `uncertain_count` and requires restart
  inspection before any newly confirmed attempt.

An applied uncertain commit is discovered as the active generation after
restart. If a newly confirmed request asks for the same content, the store
returns unchanged and performs no additional write. If the failed commit was
not applied, a newly confirmed request may perform one normal next-generation
write.

## Target composition requirements

A concrete target must still provide and validate all of the following:

1. one serialized task or equivalent owner for stage/confirm/cancel/service;
2. a monotonic boot-local clock with documented reset and sleep behavior;
3. a successfully rendered prompt bound to the exact request ID and content;
4. deliberate local physical-presence input with debounce and hold policy;
5. obsolete display actions and input queues flushed across boot/restart;
6. source authorization and integrity before any remote or imported proposal;
7. restart reconciliation and clear operator status for uncertain persistence;
8. an ESP-IDF storage backend and physical interruption/endurance evidence.

The target must not treat a network acknowledgement, request ID, touch event
from an old screen, or ordinary button edge as sufficient confirmation.

## Evidence

Ten deterministic host groups cover lifecycle/policy, validation, one-pending
admission, same-boot request replay/exhaustion, exact confirmation,
unchanged-write suppression, mismatch/cancel, the exact expiry boundary, clock
rollback, ordinary failure, and applied uncertainty followed by restart
reconciliation. The suite passes 100/100
focused repeats and the complete 44-executable host matrix under strict GCC
warnings-as-errors.

This evidence does not validate physical input, display readability, cross-boot
input flushing, concurrent target tasks, authenticated configuration, ESP-IDF
storage, power cuts, flash endurance, or any supported hardware.
