# Atomic confirmed running-layout activation v0

Status: deterministic host-tested display-neutral composition, 2026-08-14.
This is not a renderer, input workflow, ESP-IDF task or storage binding, or
physical-display result.

## Responsibility and evidence layers

`GaugeLayoutActivationWorkflow` owns the existing local-change workflow for
one exact `GaugeLayoutStore` and composes it with the renderer runtime bound to
that same store:

```text
exact local confirmation
          |
          v
persisted OGL0 generation ----> exact-generation model activation
                                         |
                                         v
                              presentation remains pending
                                         |
                                         v
                              exact-generation completion
```

The result keeps three claims separate:

- **persisted** means exact local confirmation returned a successful changed
  or unchanged `GaugeLayoutUpdateResult` with a nonzero active generation;
- **model activated** means the dashboard reloaded that exact generation from
  its bound store and atomically applied its widgets and visible layout
  metadata; and
- **presentation pending** means a later runtime cycle must still publish,
  offer, and successfully present a frame for that generation.

The separate
[presentation-completion contract](GAUGE_LAYOUT_PRESENTATION_COMPLETION_V0.md)
defines the exact one-shot receipt that closes the final obligation.

Persistence success alone is not model activation, and model activation alone
is not evidence that a renderer front frame or physical pixel changed.

## Preflight before persistence

`confirm_and_activate` rejects the operation before confirmation is consumed
or storage is written unless all of the following are true:

- the workflow and dashboard runtime are bound to the same exact layout store;
- the runtime, dashboard, and renderer report running; and
- the renderer owns no accepted frame still in flight.

It also rejects before confirmation or storage whenever an earlier changed
activation still has an exact presentation generation pending. Every other
facade mutation is blocked at the same boundary until that presentation
completes or reconstruction reconciles a detected ownership divergence.

An already presented renderer front frame may exist. Runtime-owned pending work
that has not been offered may also exist; it remains under the activation rules
below. Calls are serialized by the target owner, so no other operation may
interleave between preflight, confirmation, persistence, and activation.

## Persistence and exact-generation activation

After preflight, the existing workflow performs the exact confirmation and
store-owned update. An ordinary persistence failure or confirmation error does
not attempt activation. Commit uncertainty has no trustworthy generation: it
latches restart-required state, performs no activation, and blocks every new
layout mutation until the complete composition is reconstructed and storage is
reconciled.

A definite persistence success supplies the sole expected generation. The
dashboard reloads normal store selection and accepts only an actual slot whose
generation equals that value; a safe default is start-time fallback, not
activation authority. Read failure, conflict, a different generation, or an
unusable selection fails without changing the live model or current dashboard
frame.

The view model validates a complete candidate widget set before replacing any
live widget. On success, changed model or visible slot/recovery metadata
invalidates the prior dashboard copy. The runtime discards only its older
unoffered pending frame and latches the new presentation generation. It does
not call renderer `offer` or `service`, advance time, receive telemetry, or
publish a frame during activation. A previously presented front frame may
therefore remain visible until a later new-generation frame is successfully
presented.

An exact no-op preserves the dashboard copy and runtime pending work. It does
not create a new presentation obligation, although an obligation already
latched by an earlier activation remains visible in runtime status.

## Definite failure and zero-write retry

If persistence definitely succeeded but exact-generation model activation
failed, the facade latches that exact nonzero generation and reports retry and
restart attention. All new stage, import, restore-default, confirmation,
cancellation, expiry-service, start, and stop mutations are blocked while the
latch exists.

`retry_activation()` performs no confirmation and no storage write. It retries
only the latched generation through the same readiness and exact-load checks.
Temporary renderer/runtime unavailability, load failure, or generation drift
keeps the latch and the prior model/frame. A successful retry clears the latch;
changed model or metadata still remains presentation-pending until later
runtime service presents that exact generation. Unknown-generation commit
uncertainty cannot use this retry path and requires reconstruction/restart
reconciliation.

## Exact-generation presentation completion

Changed model or renderer-visible metadata activation latches the same exact
generation in this facade and the renderer runtime. While latched,
`service_presentation(now_ms)` is the sole serialized path through runtime
service. A successful renderer service produces a cycle-local tracked receipt
only for a frame previously accepted by that runtime. The facade clears its
latch only when the completed generation exactly matches its expected
generation; the receipt cannot replay.

Busy, offer, renderer-service, and clock failures preserve a coherent pending
obligation. An exact receipt may still complete when an unrelated earlier
dashboard or frame-observation error remains in the nested cycle result. Direct
runtime service or stop, facade/runtime latch mismatch, and a tracked
wrong-generation presentation instead fail closed to restart-required
reconstruction and keep new mutation blocked. An untracked or spurious
renderer signal is not proof and remains ordinary pending. See the
[exact-generation presentation contract](GAUGE_LAYOUT_PRESENTATION_COMPLETION_V0.md)
for the complete ownership and recovery rules.

## Host evidence

`tests/host/gauge_layout_activation_workflow_tests.cpp` covers ten scenario
groups:

1. exact store identity and running-state preflight before persistence;
2. renderer in-flight rejection preserving confirmation and zero writes;
3. changed confirmation, atomic live-model activation, and later presentation;
4. metadata-only pending-frame discard versus exact no-op preservation;
5. unchanged persistence reconciling a lagged running model with zero writes;
6. definite post-commit load failure and successful zero-write retry;
7. generation drift and conflicted storage failing closed without model loss;
8. ordinary failure, commit uncertainty, and clock fault never activating;
9. mutation blocking plus a not-ready retry preserving the exact latch; and
10. eight-widget activation and restart reload of the exact persisted layout.

The focused suite compiles as strict C++17 and passes 100/100 independent
repeats. It joins the complete 50-executable Windows host matrix; the host
script compiles 51 named programs because one physical CLI is intentionally
not executed.

## Explicit limits

This composition owns no mutex, RTOS task, renderer backend, text or
localization, touch/input adapter, physical-presence proof, source
authorization/authenticity, ESP-IDF storage implementation, or diagnostic
retention policy. It proves no pixels, touch, timing, memory, stability, power,
heat, interruption durability, recovery, candidate compatibility, or supported
hardware. Those remain target and OG-012/OG-012A acceptance gates.
