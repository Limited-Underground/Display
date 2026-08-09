# Normalized-Signal Alarm Engine v0

Status: host-tested fixed-capacity state machine, 2026-08-09. This is a
supplemental instrumentation contract, not a safety-critical control system or
a validated vehicle warning implementation.

## Boundary

The alarm engine consumes `CachedSignalSnapshot` values after acquisition,
decode, normalization, and freshness evaluation. It never reads raw CAN frames,
invents a numeric value for missing data, drives vehicle outputs, discovers
signals, or owns a clock/task.

The single-owner engine holds at most 16 rules and performs no dynamic
allocation. A caller supplies event storage; capacity must cover every rule
matching the evaluated signal so a multi-rule evaluation either begins with
enough output space or changes no state.

## Rule model

Every rule has a nonzero ID and one canonical normalized signal ID, expected
value type/unit, severity, lifecycle policy, and one of these inclusive
comparisons:

- `above_or_equal`: assert at `value >= threshold`;
- `below_or_equal`: assert at `value <= threshold`;
- `outside_inclusive_range`: assert at `value <= low` or `value >= high`.

Single-threshold rules require the unused high threshold to be zero. An outside
range requires `low < high` and a hysteresis no greater than half its width.
Boolean rules accept only thresholds zero/one and do not support outside-range
comparison. Unsigned rules reject negative thresholds.

Thresholds use the normalized signal's canonical integer unit. For example,
`milli_celsius` uses 87,500 for 87.5 C. A signal with a different type or unit
fails closed rather than being converted implicitly.

## Hysteresis and debounce

Hysteresis applies only after assertion:

- an above alarm clears at `threshold - hysteresis`;
- a below alarm clears at `threshold + hysteresis`;
- a low-range alarm clears at `low + hysteresis`;
- a high-range alarm clears at `high - hysteresis`.

Zero hysteresis clears immediately after moving to the non-triggering side.
Arithmetic uses ordered unsigned differences, so signed 64-bit endpoint rules
do not overflow. During clear debounce, moving back into the active hysteresis
band cancels the pending clear. After a final clear, a new alarm must again
cross its assertion threshold.

Assert and clear debounce are independently configurable from zero through 24
hours. A condition must be observed on successive monotonic evaluations through
the exact `elapsed >= debounce` boundary. A missing evaluation does not prove a
condition remained true; no background timer asserts alarms by itself.

## Nonvalid and stale input

Only effective quality `valid` participates in numeric comparison. `suspect`,
`unavailable`, `error`, `out_of_range`, `stale`, and `unknown` use one explicit
per-rule policy:

- `clear_condition`: treat nonvalid state as a clear input;
- `hold_state`: preserve an active alarm, but cancel incomplete assert/clear
  debounce so missing evidence cannot finish a transition;
- `assert_alarm`: assert with the nonvalid quality and no numeric value.

The effective cache quality must be consistent with the stored signal quality;
only valid/suspect stored values may become stale. Inconsistent snapshots,
unknown enums, invalid normalized data, unit/type mismatch, or clock regression
fail without changing any matching rule.

## Latching and acknowledgement

A nonlatching alarm emits `asserted` and `cleared` transitions. A latching alarm
whose condition clears first emits `condition_cleared_latched` and remains
visible until acknowledgement. Acknowledging that safe latched state emits the
final `cleared` event. Acknowledging while the condition is still present emits
`acknowledged`; the alarm remains active and then clears normally without
relatching after its clear condition/debounce succeeds.

If a condition returns while awaiting acknowledgement, the same alarm becomes
active again without inventing a second assertion event. Duplicate, inactive,
unknown-rule, and regressing-time acknowledgements are explicit errors.

## Reminder rate

An optional reminder interval from one millisecond through 24 hours emits at
most one reminder per evaluation and only at the exact
`now - last_event >= interval` boundary. Assertion, condition-cleared, final
clear, and acknowledgement transitions are not delayed by the reminder limit.
This prevents periodic-event spam without hiding lifecycle changes.

## Events and diagnostics

Events include rule ID, kind, severity, post-event lifecycle, effective signal
quality, canonical value/unit when valid, monotonic event time, active duration,
condition-present state, and acknowledgement state. Counters cover evaluations,
assertions, final clears, acknowledgements, and reminders. Restart retains rule
configuration while clearing runtime state and counters.

## Host evidence

`tests/host/alarm_engine_tests.cpp` covers ten groups:

1. rule validation, duplicate/capacity bounds, and lifecycle;
2. inclusive above threshold plus exact hysteresis clear;
3. below and both sides of an outside range;
4. assert/clear debounce, chatter cancellation, and hysteresis during pending
   clear;
5. condition-cleared latch, safe acknowledgement, active acknowledgement, and
   final clear;
6. clear/hold/assert behavior for stale and unavailable input with no invented
   value;
7. exact reminder intervals while final clear remains immediate;
8. atomic multi-rule output capacity, no-match, and monotonic clock rejection;
9. incompatible/invalid snapshot and Boolean-rule validation;
10. stop/restart rule retention with clean runtime counters.

## Remaining integration gates

- bind cache change/stale polling to alarm evaluation with a measured task
  budget;
- map selected rule events to display behavior and separately to the bounded
  critical-alert exporter;
- define reviewed vehicle-specific signals, thresholds, severities, default
  nonvalid policies, and acknowledgement UX;
- persist/version rule configuration and prove safe migration/reset;
- validate representative warnings, stale states, input loss, restart, and
  recovery on selected display/gateway hardware.
