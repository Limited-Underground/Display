# Gauge Layout Change Workflow v0

## Purpose

The coordinator and operator projection are separately host-testable. A target
caller should not execute a coordinator operation, release ownership, and later
pair that old result with a newer status snapshot.

`GaugeLayoutChangeWorkflow` owns the coordinator and derives the operator
projection from its immediate post-operation status before returning one result.

## Combined result

Every method returns:

- the exact coordinator operation error;
- the exact persistence update result when confirmation attempted storage;
- the projection error; and
- the projected operator status.

The methods cover start, stop, read-only snapshot, stage, confirm, cancel, and
expiry service. Start policy errors still return a coherent unavailable
projection; a repeated start reports the operation error while preserving the
current live projection.

## Preserved behavior

The facade does not reinterpret or hide underlying results:

- staging returns the exact request token and full initial confirmation window;
- a mismatch flags rejection while preserving a still-valid prompt;
- changed and unchanged confirmation return their exact active generation;
- read-only expiry asks for service, while service consumes expiry and asks for
  a newly staged request;
- ordinary persistence failure permits only a newly confirmed request;
- commit uncertainty requests restart reconciliation;
- clock rollback consumes the prompt and projects a clock fault; and
- same-boot request reuse is a terminal rejected proposal.

## Ownership boundary

The facade does not contain a mutex, RTOS task, queue, ISR bridge, renderer,
input adapter, debounce/hold policy, source authorization, diagnostics call, or
ESP-IDF storage backend. A target composition must give one serialized owner
exclusive access to the facade and bind its prompt token to the exact
successfully displayed frame and local action.

## Evidence

Eight deterministic groups cover lifecycle/policy, exact prompt stage,
mismatched confirmation, changed/unchanged persistence, cancellation/expiry,
ordinary failure versus uncertain commit, clock rollback, and request replay.
The focused suite passes 100/100 repeats and the complete 47-executable strict
host matrix.

This is application-boundary evidence, not target task serialization, physical
presence, display readability, authenticated configuration, storage durability,
or hardware validation.
