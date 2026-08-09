# Gauge View Model v0

Status: host-tested display-framework-neutral projection, 2026-08-09. This is
not a renderer, layout file format, touch workflow, font/theme, animation,
hardware benchmark, or accepted display implementation.

## Boundary

`GaugeViewModel` converts the bounded gauge receiver/store into stable snapshots
for at most eight configured widgets. It does not perform radio I/O, decode
packets, allocate dynamically, format localized text, draw pixels, handle touch,
or persist configuration.

This separation lets LVGL, vendor graphics examples, or another measured
renderer consume the same fail-visible state without learning packet/session
rules. The display can fail or restart without affecting gateway acquisition or
other gauges.

## Configuration

Each widget has:

- nonzero unique widget ID;
- one registered telemetry signal code;
- printable ASCII label up to 24 bytes;
- numeric, needle, bar, or status kind;
- nonzero stale threshold;
- optional raw canonical scale range for needle/bar only.

Needle/bar ranges require `min < max` and a non-Boolean signal. Numeric/status
widgets require canonical zero unused scale fields. Status requires a Boolean
registered signal; the current four-signal v0 registry has no Boolean code yet,
so no current status widget can pass configuration until the registry is
explicitly extended.

Labels are bounded data, not printf/HTML strings. Localization, glyph coverage,
units, precision, conversion preferences, and theme are future renderer/config
concerns.

## Projected state

Every successful refresh produces one ordered snapshot per widget with label,
kind, signal code, expected canonical type/unit, scale, state, value when
permitted, age, gateway session, and packet sequence.

States remain distinct:

- `valid`: numeric/Boolean value present, no attention flag;
- `suspect`: value present, attention required;
- `missing`: signal absent from receiver store, no value;
- `stale`: exact age boundary crossed, no value;
- `unavailable`, `error`, `out_of_range`, or `unknown`: no value.

Missing state retains the expected type/unit from the signal registry so a
renderer can preserve stable label/unit chrome, but numeric presence remains
false and raw value remains zero. Only valid/suspect state may increment the
numeric-value count. Every state except valid requests attention; the eventual
UI must define accessible visual treatment rather than relying on color alone.

## Atomic refresh

The caller supplies storage for every configured widget. Insufficient capacity
fails before reading. The view model builds into an internal fixed temporary
array and copies to caller output only after every receiver read succeeds.
Receiver freshness/clock failure therefore leaves the previous caller snapshot
untouched instead of publishing a partially new dashboard.

Missing signals are normal successful projections. Receiver offline/freshness
errors fail the complete refresh and increment a diagnostic counter.

## Host evidence

`tests/host/gauge_view_model_tests.cpp` covers seven groups:

1. label/widget/range/kind validation, duplicate/capacity, and lifecycle;
2. missing state with expected type/unit but no number;
3. valid versus suspect numeric/attention behavior;
4. exact stale boundary removing the value;
5. unavailable/error states plus session/sequence projection;
6. two differently configured widgets safely sharing one signal;
7. output-capacity and receiver-clock failure preserving prior output.

## Display hardware gates

- bind snapshots to a representative numeric, needle, bar, warning, missing,
  stale, and error visual system;
- define precision/unit conversion and localization without altering canonical
  stored values;
- measure cold boot, first useful value, frame/update time, dirty-region/full
  refresh, memory/PSRAM, touch latency, brightness, power, heat, and burn-in
  strategy on each exact candidate unit;
- test packet loss, gateway restart, receiver restart, long staleness, alert
  overlay, touch failure, brownout, and USB recovery;
- version/persist layouts with validation, migration, safe defaults, reset, and
  write-wear evidence.
