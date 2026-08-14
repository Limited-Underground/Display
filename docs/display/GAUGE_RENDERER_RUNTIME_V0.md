# Gauge Renderer Runtime v0

Status: host-tested display-neutral renderer boundary and cooperative
composition, 2026-08-14. This is not a pixel renderer, an ESP-IDF task, a
physical display result, or an accepted hardware implementation.

## Responsibility

The renderer boundary and runtime connect the existing atomic dashboard frame
to one nonblocking presentation owner:

```text
GaugeDashboardLoop -> newest runtime pending frame -> GaugeRenderer queue
                                                     -> presented/front frame
```

`GaugeRenderer` owns renderer lifecycle and already accepted work. An accepted
`offer` synchronously copies one complete `GaugeDashboardFrame`; every other
offer result consumes nothing. `service` advances only accepted renderer work
and never calls back into the dashboard. A service failure retains the exact
in-flight frame for retry and preserves the prior complete front frame.

`GaugeRendererRuntime` is the single cooperative owner of one exact dashboard
loop and renderer. Its direct state is fixed-capacity and introduces no dynamic
allocation. Concrete dashboard dependencies, renderer backends, drivers, and
target adapters retain their own separate allocation and locking obligations.

## Lifecycle and one bounded cycle

Start refuses pre-started runtime, dashboard, or renderer state. It starts the
dashboard first and the renderer second. If renderer start fails, the runtime
stops both components, including a renderer that reports failure after leaving
partial state. Stop clears pending and accepted-frame bookkeeping; restart
reloads the dashboard's normal persisted-layout selection.

A nondecreasing-time `service(now_ms)` call performs this exact order:

1. service the dashboard exactly once;
2. if the dashboard published a complete frame, copy and observe it;
3. attempt at most one offer of the newest pending frame;
4. service the renderer exactly once.

Caller-time regression fails before the dashboard, renderer, pending frame, or
runtime counters change. Otherwise later work still runs after an earlier
cycle error. The cycle's primary error records the first failure in the order
above, while the nested dashboard, offer, and renderer-service results preserve
their exact underlying errors for diagnosis.

## Backpressure and complete-frame retention

Busy and hard offer failures retain the newest complete pending frame. A later
dashboard frame atomically replaces older pending work rather than growing an
unbounded queue. Once an offer is accepted, only the renderer owns that queued
copy; the runtime may then retain a newer pending frame while the renderer is
still busy. Renderer service failure preserves its in-flight frame and prior
presented frame, while subsequent cycles continue to service the dashboard so
telemetry still ages to exact-boundary stale/no-value.

Every published dashboard frame is offered, including one whose visible
content is semantically equal to the last pending or accepted frame. Fieldwise
semantic comparison is diagnostic only: it excludes publication sequence,
publication time, and unused widget capacity, while comparing every active
renderer-visible layout and widget field. Publication sequence is therefore
not treated as a uniqueness key.

Runtime and fake-renderer diagnostic counters saturate instead of wrapping.
The focused suite verifies their ordinary increments and state transitions; it
does not force each counter to its maximum value.

## Host evidence

`tests/host/gauge_renderer_runtime_tests.cpp` covers eleven scenario groups:

1. fieldwise semantic equality, metadata exclusion, active-widget bounds, and
   malformed label rejection;
2. fake renderer copy ownership, lifecycle, busy behavior, front-frame
   preservation, failure retention/retry, restart, and nonunique maximum
   publication sequence;
3. runtime lifecycle, pre-started dependency rejection, partial renderer-start
   rollback, stop/restart reset, and persisted-layout reload;
4. exact dashboard, offer, renderer-service order;
5. equivalent published frames still being offered and counted;
6. busy backpressure, bounded latest-frame replacement, and later presentation;
7. hard offer failure retention while renderer service and dashboard work
   continue;
8. renderer service failure retaining in-flight work while dashboard aging
   continues;
9. busy rendering plus rejected, unencrypted, malformed, transport-failed, and
   timestamp-regressed traffic still reaching exact stale/no-value;
10. caller-clock rollback without downstream or pending-frame mutation;
11. accepted-offer ownership transfer while a newer runtime frame remains
    pending until the renderer becomes available.

The suite compiles as C++17 with warnings-as-errors and passes 100/100 focused
repeats. It joins the complete 49-executable Windows host matrix.

## Explicit limits

This contract draws no pixels and defines no fonts, localized text, animation,
dirty-region strategy, accessibility behavior, touch/input workflow, or
operator interaction. It provides no ESP-IDF task, display driver, DMA/buffer
policy, concrete allocation proof, or physical timing, memory, power, heat,
recovery, readability, or compatibility evidence. Those remain target and
OG-012/OG-012A acceptance gates.
