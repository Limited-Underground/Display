# Gauge Trend Buffer v0

Status: deterministic host-tested display-neutral short-history core,
2026-08-09. This is not a chart renderer, persistent logger, downsampler,
analytics store, on-device memory benchmark, or accepted display feature.

## Boundary

`GaugeTrendBuffer` accepts already validated `GaugeWidgetSnapshot` state from
the display view-model boundary. It holds at most four configured trend series,
each with 2 through 120 fixed points and a nonzero local minimum sampling
interval. It allocates no dynamic memory.

Configuration uses a nonzero trend ID, one registered telemetry signal code,
point capacity, and minimum interval. Multiple trends may intentionally share a
signal with different capacities or intervals. The buffer does not own layout,
labels, scales, unit conversion, precision, color, axes, interpolation, or touch.

## Points and gaps

Every retained point contains local capture time, display value-state, expected
canonical type/unit, source boot session and packet sequence, and either:

- a numeric/Boolean value for valid or suspect state; or
- an explicit gap with no value for missing, stale, unavailable, error,
  out-of-range, or unknown state.

The input must use the registered signal type/unit. Valid/suspect requires a
present value and nonzero source session; every nonvalue state requires absent
canonical-zero value. Unknown state codes and clock regression fail before any
series mutates.

A renderer must break a line at gap points. It may distinguish suspect from
valid visually, but it must not interpolate through a gap as if measurements
continued.

## Sampling and retention

The first matching sample is appended immediately. Later samples append at
exact `elapsed >= minimum_interval`; earlier input is counted and coalesced away.
When a configured series is full, one new point overwrites exactly the oldest.
Reads return oldest-first and require capacity for the whole retained series,
leaving caller output/count untouched on failure.

Clearing one trend removes its points and interval baseline without changing
its configuration or the global diagnostic counters. Restarting the buffer
starts all series empty. This is volatile display history, not event retention.

## Host evidence

`tests/host/gauge_trend_buffer_tests.cpp` covers eight groups:

1. configuration, four-trend capacity, duplicates, and lifecycle;
2. valid/suspect values and source metadata;
3. stale/error gap points with no number;
4. exact sampling interval and coalesced-input counters;
5. ring overwrite plus oldest-first order;
6. independent intervals for two trends sharing one signal;
7. malformed state and clock failure without partial mutation;
8. atomic read capacity and per-series clear.

The suite also repeated 100 times with zero failures.

## Remaining display gates

- measure exact RAM/PSRAM, append/read time, task ownership, and locking on each
  display candidate;
- choose visible history duration and sampling rate per signal without hiding
  fast hazards or exhausting memory;
- implement gap-aware axes/decimation/interpolation and accessible valid,
  suspect, stale, error, and no-data treatment;
- test gateway/session restart, long gaps, time wrap policy, alarm overlay,
  touch interaction, brightness/theme, burn-in, and frame/update budgets;
- decide whether any history is persisted, with explicit privacy, retention,
  wear, corruption, reset, and export policy.
