# Cache-to-Alarm Evaluator v0

Status: host-tested cooperative composition, 2026-08-09. This is not an
ESP-IDF task, vehicle-specific rule set, display warning, persistent alarm log,
or physical alert delivery result.

## Why it scans complete state

`AlarmCacheEvaluator` bridges the fixed 16-signal latest-state cache to the
fixed 16-rule alarm engine. Every poll requests a full bounded snapshot from
the current cache epoch instead of only states whose numeric generation
changed. This is required because:

- assert/clear debounce must advance when a condition remains unchanged;
- a fresh value may cross its exact stale boundary without a new CAN frame;
- reminder time must advance while an active value remains unchanged.

The scan is bounded by the cache capacity. It performs no dynamic allocation,
sleeps, raw-frame access, display work, critical-event export, or persistence.

## Poll contract

A poll requires monotonic receiver-local time and caller event storage at least
as large as the configured alarm rule count. This deliberately conservative
capacity check ensures that every matching rule could emit one event.

One poll:

1. detects a cache epoch change;
2. collects at most 16 current snapshots and materializes exact stale quality;
3. preflights every snapshot against matching rule IDs, value types, units, and
   quality consistency before any rule changes state;
4. evaluates each matching snapshot at the same monotonic time;
5. aggregates at most 16 assertion, latch, clear, acknowledgement-independent,
   or reminder events into caller-owned storage.

Snapshots with no rules are counted and skipped. A malformed/incompatible
snapshot, cache freshness failure, output shortage, or clock regression is
returned explicitly. Preflight makes type/unit/quality failure poll-atomic;
debounce clock checks are also poll-atomic because all successful polls use one
monotonic timestamp.

## Cache restart behavior

Cache clear changes its epoch. The evaluator responds by stopping and restarting
alarm runtime while retaining rule configuration. This removes active, pending,
latched, and reminder state from the prior source session. It reports both
`cache_epoch_changed` and `alarm_runtime_reset` and does not invent a `cleared`
event for a condition that was not actually observed clearing.

Consumers must treat the epoch/reset flag as a source-session boundary, discard
prior rendered alarm state, and wait for new snapshots. Reinserting the same
triggering value in the new epoch produces a new assertion through the normal
rule path.

## Host evidence

`tests/host/alarm_cache_evaluator_tests.cpp` covers seven groups:

1. lifecycle, caller-buffer checks, and fail-closed offline behavior;
2. assert debounce reaching its exact boundary across an unchanged cache value;
3. cache freshness reaching stale and asserting without a numeric value;
4. reminder timing across an unchanged cache value;
5. cache epoch reset without a fabricated clear, followed by reassertion;
6. multi-signal aggregation plus unruled snapshot accounting;
7. whole-poll incompatible-snapshot preflight, cache freshness error, and
   monotonic poll rejection.

## Remaining target work

- schedule the poll at a measured period that satisfies configured debounce and
  reminder resolution without starving CAN/radio/display work;
- choose whether the gateway, each display, or both evaluate particular rules
  and define ownership/deduplication;
- map local events to conspicuous UI and selected normalized critical exports;
- version and persist reviewed rule configuration with recovery-safe defaults;
- validate task time, stack, restart, input loss, and representative warnings
  on selected hardware.
