# Exact-generation layout presentation completion v0

Status: 11 focused deterministic host-test groups and the complete host matrix
pass, 2026-08-14. This is a display-neutral ownership and completion contract,
not proof that a physical pixel changed.

## Responsibility and evidence layers

`GaugeLayoutActivationWorkflow` keeps the three acceptance layers separate:

```text
confirmed local change
        |
        v
persisted OGL0 generation
        |
        v
exact-generation model activation
        |
        v
presentation latch ----> serialized runtime service
                              |
                              v
                    exact-generation receipt
```

- **persisted** means the confirmed store operation definitely succeeded and
  returned one nonzero active generation;
- **model activated** means that exact stored generation was loaded and
  atomically installed in the running dashboard model; and
- **presentation completed** means the renderer successfully reported
  presenting a runtime-tracked accepted frame for the exact latched
  generation.

The final receipt proves renderer-front ownership of one complete frame. It
does not prove display-driver transfer, panel scanout, visible pixels,
readability, timing, touch behavior, or hardware compatibility.

## Mutation gate before confirmation or storage

A changed model or renderer-visible metadata activation latches its exact
nonzero generation in both the renderer runtime and activation facade. While
either owner reports presentation pending, the facade rejects new stage,
restore-default, import, confirm, cancel, expiry service, start, and stop
mutations before a confirmation token can be consumed or storage can be
written. Snapshot remains read-only.

The latch is not a request to replay persistence. It is an obligation to
finish presenting the already persisted and activated generation. New layout
work becomes eligible only after one exact completion receipt, or after the
entire composition is reconstructed and storage is reconciled when ownership
has diverged.

## Sole serialized service and one-shot receipt

While the facade latch exists, `service_presentation(now_ms)` is the sole
serialized route through `GaugeRendererRuntime::service`. The call checks that
the runtime is running and that the facade and runtime hold the same nonzero
pending generation before service. It then returns the complete nested runtime
cycle and evaluates the presentation transition before another layout
operation can interleave.

The runtime records the layout generation when an offered frame is accepted by
the renderer. A successful renderer service that reports `frame_presented`
creates a cycle-local tracked receipt only when such an accepted frame was
actually in flight. The receipt carries that accepted frame's generation.
Neither a renderer's unpaired signal nor a duplicate signal can invent one.

The runtime clears its pending-presentation latch only when the tracked
generation exactly equals its expected generation. The facade independently
accepts completion only when the runtime's completed generation equals the
facade latch. It then clears its own latch and returns one
`completion_receipt`. That receipt exists in one returned call only; a later
call cannot replay it and does not service the runtime when no presentation is
pending.

## Failure, divergence, and recovery

Busy offers, hard offer failures, and renderer-service failures retain the
coherent pending obligation for a later call. Caller-clock regression changes
neither runtime nor facade latch. These ordinary cycle failures are reported
without consuming the obligation.

An exact presentation receipt takes precedence over an unrelated earlier
dashboard or frame-observation error in the same runtime cycle. The completion
is still returned, while the nested runtime result preserves that first cycle
error for diagnosis. Offer failures retain the pending obligation but cannot
coexist with presentation of a tracked in-flight frame in this single-owner
cycle. A failed renderer service itself cannot create a tracked presentation
receipt.

The following are ownership divergence, not ordinary retry:

- the runtime was directly serviced and cleared the expected presentation
  before the facade could issue its receipt;
- the runtime was directly stopped;
- facade and runtime pending state or generation differ before or after
  service; or
- a runtime-tracked frame is presented for a generation other than the facade
  generation.

Any such condition fails closed as `restart_required`, clears the facade's
ordinary presentation latch, keeps every new mutation blocked, and requires
complete composition reconstruction plus persisted-state reconciliation. A
spurious untracked renderer signal does not establish divergence and leaves
the ordinary presentation obligation pending. Direct runtime layout
activation also cannot overwrite an existing runtime presentation generation.

## No-op and activation-retry semantics

An exact activation no-op changes neither model nor renderer-visible metadata,
creates no new presentation obligation, and may report presentation complete
immediately at the model-activation layer. A metadata-only source/recovery
change is renderer-visible and therefore requires an exact presentation
receipt even when widget content is unchanged.

A definite post-persistence activation failure retains its separate exact
activation-retry generation. `retry_activation()` performs no confirmation and
no storage write. If retry later changes model or visible metadata, the
activation latch transitions into the same exact-generation presentation gate;
new mutation remains blocked until presentation completes.

## Host evidence

`tests/host/gauge_layout_presentation_gate_tests.cpp` covers 11 scenario
groups:

1. changed activation latching the exact generation and distinct semantics;
2. every facade mutation blocked before token consumption or storage write;
3. exact one-shot completion receipt and later mutation release;
4. busy, offer-failure, and renderer-service-failure retention and recovery;
5. exact completion retained despite an unrelated dashboard cycle failure;
6. wrong-generation divergence versus harmless spurious presentation signals;
7. caller-clock rollback preserving runtime and facade state;
8. zero-write activation retry transitioning into presentation ownership;
9. metadata-only proof versus exact-no-op completion;
10. direct runtime activation unable to overwrite the pending generation; and
11. direct runtime service or stop requiring reconstruction.

The focused suite compiles as strict C++17 and passes 100/100 independent
repeats. The inherited renderer-runtime 11 groups and activation-workflow ten
groups also pass. The complete Windows host script passes with 51 executed
test binaries and 52 compiled names because one physical CLI is intentionally
not executed; its publication-safety scans pass in the same run.

## Explicit limits

The facade still owns no mutex, RTOS task, renderer backend, text or
localization, input or physical-presence adapter, source authorization,
ESP-IDF storage binding, target logger, or recovery implementation. This
contract proves no pixels, timing, memory, stability, power, heat, interruption
durability, candidate compatibility, or supported hardware. Those remain
target and OG-012/OG-012A acceptance gates.
