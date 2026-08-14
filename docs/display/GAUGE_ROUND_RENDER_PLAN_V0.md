# Round Gauge Render Plan v0

Status: host-tested fixed-capacity display plan compiler, 2026-08-14. This is
not a pixel renderer, target-driver binding, physical display result, or
accepted hardware implementation.

## Boundary

`compile_gauge_round_render_plan` converts one complete
`GaugeDashboardFrame` into a deterministic, backend-neutral sequence of
bounded primitives for a circular gauge profile. It uses only fixed arrays,
performs no dynamic allocation, and commits the caller's output only after the
complete candidate validates and compiles.

The plan carries renderer-visible layout identity, source, theme, brightness,
recovery state, widget metadata, bounded geometry, and safe measurement or
state primitives. Publication sequence and publication time are copied only as
diagnostics. Receiver age, gateway session, and packet sequence are not plan
fields.

## Logical profile and capacity

The v0 logical viewport is a half-open 466 by 466 pixel-edge coordinate space:
`[0, 466) x [0, 466)`. Backends must also apply the circular clip centered at
`(233, 233)` with radius 233 pixel-edge units. This logical profile does not
prove the resolution, clipping behavior, or compatibility of any physical
display.

One through eight widgets use a fixed geometry table. Each widget emits exactly
three primitives in order:

1. one widget panel;
2. one bounded label;
3. one valid measurement primitive or one no-value state badge.

An optional recovery badge follows all widget primitives. It is marked for
attention and does not overlap any widget panel in any supported one-through-
eight-widget geometry. The fixed plan capacity is therefore 25 primitives:
three for each of eight widgets plus one recovery badge. The caller may supply
a smaller backend budget; insufficient capacity reports the exact required
count and leaves the previous output unchanged.

## Measurement and state safety

Numeric, needle, and bar widgets compile only from validated dashboard
snapshots. A `valid` widget emits its measurement value and canonical unit.
Needle and bar primitives also carry a clamped position from 0 through
1,000,000. The normalization is overflow-safe across the complete signed
64-bit value and scale range.

Every other state emits only an attention-bearing state badge:

- `suspect`;
- `missing`;
- `stale`;
- `unavailable`;
- `error`;
- `out_of_range`;
- `unknown`.

These badges have no value, no unit, and a zero normalized position. In
particular, the compiler deliberately strips the value that the upstream view
model may retain for `suspect`; a backend cannot accidentally render it as a
plausible measurement.

The current telemetry registry has no Boolean signal, so a status widget cannot
be produced by a valid dashboard configuration. A patched or malformed status
snapshot fails closed rather than creating a status-indicator primitive. Status
support requires a separately reviewed Boolean registry entry and tests.

## Validation and atomic output

Compilation rejects an invalid frame before changing the caller's prior plan.
Validation includes:

- nonzero layout generation and layout ID;
- a known persisted or safe-default layout source, theme, and brightness from
  1 through 100 percent;
- one through eight widgets with unique nonzero IDs;
- canonical bounded printable labels;
- registered signal, exact type/unit, known kind/state, coherent value shape,
  attention flag, and needle/bar scale;
- fixed boxes inside the viewport and circular profile; and
- sufficient caller-declared primitive capacity.

The compiler builds a zero-initialized local candidate and assigns it to the
caller only on success. A later successful smaller plan therefore also clears
the inactive fixed-capacity tail rather than retaining old widget data.

Semantic equality compares every active renderer-visible field, including
layout identity and every active primitive. It intentionally ignores only the
diagnostic publication sequence/time and unused fixed-capacity tail.

## Host evidence

`tests/host/gauge_round_render_plan_tests.cpp` covers nine scenario groups:

1. bounded circular geometry for every one-through-eight-widget layout and a
   nonoverlapping recovery badge;
2. numeric, needle, and bar valid representations plus diagnostic metadata;
3. suspect and every nonvalid state becoming no-value state badges;
4. status failing closed without a registered Boolean signal;
5. exact primitive capacity, recovery attention, and atomic capacity failure;
6. malformed metadata, identity, label, signal, kind, state, value, attention,
   and scale rejection with prior-output preservation;
7. exact overflow-free normalization across the full signed 64-bit range;
8. smaller recompilation clearing inactive tail and excluding receiver-private
   metadata; and
9. semantic equality ignoring only diagnostics and inactive tail.

The suite compiles under strict C++17 warnings-as-errors and passes 100/100
focused repeats. The complete `tools/Test-Host.ps1` run passes 52 executed host
binaries, 53 compiled named programs, and both publication-safety scans; one
compiled physical CLI is intentionally not executed. Public CI remains a
separate publication gate.

## Explicit limits

The plan does not draw pixels, allocate a framebuffer, select a graphics
framework, call a driver, or define DMA/buffer/dirty-region policy. It does not
define fonts, number formatting, unit conversion, localization, animation,
accessibility treatment, color-only semantics, touch/input behavior, or an
operator workflow. It proves no ESP-IDF task ownership, locking, target memory
or timing, clipping implementation, readability, brightness, power, heat,
burn-in, recovery, or physical compatibility. Those remain OG-012 and
OG-012A target and hardware gates.
