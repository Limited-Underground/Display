# Gauge Dashboard Loop v0

Status: host-tested cooperative composition, 2026-08-14. This is not a
renderer, an ESP-IDF task, a physical display result, or an accepted hardware
implementation.

## Responsibility

`GaugeDashboardLoop` composes three existing bounded surfaces:

```text
recoverable OGL0 layout -> GaugeViewModel -> atomic dashboard frame
                               ^
                               |
                   GaugeTelemetryReceiver
                               ^
                               |
                  encrypted admitted packets
```

The caller supplies the layout store, receiver, and view model. Start rejects a
view model that is not bound to the exact supplied receiver, so the loop cannot
service one telemetry source while rendering another. The caller serializes all
operations and owns transport provisioning, clocks, target task scheduling,
rendering, input, and hardware initialization.

The loop's direct state and service path use fixed-capacity storage and do not
allocate dynamically. Injected storage, transport, and target adapters retain
their own separate allocation obligations.

## Start and layout selection

Start validates the compiled safe layout and receiver policy before touching
storage. It then uses the normal two-slot `OGL0` selection rules:

- known-empty storage boots the validated safe default;
- the unique newest valid slot wins;
- one valid slot with a missing or invalid peer remains usable while exposing
  `recovery_required`;
- equal-generation conflict, unreadable storage, invalid safe content, or
  corrupt storage with no valid slot fails closed.

The selected layout's generation, ID, source, theme, brightness, recovery
state, widget count, and exact widget order are retained in every published
frame. Receiver or view startup failure rolls back owned state and exposes no
frame.

## One bounded cycle

For every accepted nondecreasing `now_ms`, one `service` call:

1. services the receiver exactly once under its existing one-to-four packet
   drain budget;
2. refreshes every one-to-eight configured widget into a temporary fixed
   frame;
3. publishes that frame only after the full refresh succeeds.

No-data is a normal cycle and republishes current projected state: absent
signals remain explicitly missing, non-value states remain distinct, and
eligible retained valid/suspect values age to stale at their exact threshold.
Unauthorized, unencrypted, malformed, timestamp-regressed, and transport-
failure evidence remain typed and counted. They cannot overwrite an accepted
value, but the view still refreshes from current receiver state so elapsed time
can cross the exact stale boundary and remove the number.

A regressed caller time is rejected before receiver, transport, or view work.
View freshness/clock failure preserves the prior complete frame. A bad packet
followed by a good packet within one bounded drain may report the first receiver
failure while still publishing the accepted later state.

Gateway-session change clears signals absent from the new session. Stop clears
the frame and runtime state; restart reloads persisted layout selection and
begins frame sequencing again.

## Host evidence

`tests/host/gauge_dashboard_loop_tests.cpp` covers twelve groups:

1. lifecycle, validation, stopped calls, unreadable-storage failure, and
   failed-start rollback;
2. receiver/view identity mismatch rejected before storage or startup;
3. safe-default, newest, degraded, conflict, and unusable layout selection;
4. fixed eight-widget order and layout/frame metadata;
5. real encrypted fake delivery through packet decoder and receiver into
   numeric, needle, and bar snapshots;
6. exact `age >= stale_after_ms` conversion to stale/no-value;
7. unauthorized, unencrypted, malformed, and transport failures retaining good
   state while later time makes it stale;
8. repeated receive timestamp regressions aging retained state to stale and a
   later accepted packet in the same bounded drain winning publication;
9. atomic view-refresh failure, semantic frame preservation, and recovery;
10. caller-time regression rejected before receiver/transport/view/frame work;
11. gateway-session change clearing absent prior-session values;
12. stop/restart clearing runtime and reloading the newly persisted layout.

The suite compiles as C++17 with warnings-as-errors. Physical RF, target task
timing, target storage, allocation behavior of concrete adapters, renderer
pixels, touch, accessibility, memory, frame time, power, heat, and recovery
remain separate acceptance gates.
