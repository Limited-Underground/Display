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
expiry service. `stage_restore_default` is an explicit semantic entrypoint that
passes the validated compiled default through the same stage/confirm path; it
does not call storage reset or erase. `stage_import_record` first decodes one
exact canonical `OGL0` record, returns its codec error and bounded summary, and
stages it through the same path with source generation treated only as preview
metadata. Start policy errors still return a coherent unavailable
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

## Restore default versus storage reset

First-release “restore default layout” is a normal confirmed configuration
change. The compiled default is validated, caller generation is ignored, and
the store either writes it at highest-valid plus one or reports unchanged with
no write. The prior slot remains available for recovery until later rotation.

The restore-default path never calls `GaugeLayoutStore::reset`. Destructive
two-slot erase remains a service/replacement primitive with separate uncertain-
commit restart semantics and is not an ordinary user action.

## Local import

Import accepts exactly `kGaugeLayoutRecordBytes`. The existing decoder is the
sole record authority for magic, version, canonical padding, checksum, widget,
and layout validation. A failed decode returns a current read-only workflow
projection, does not consume a request ID, and cannot replace a pending prompt.

A successful decode returns source generation, layout ID, theme, brightness,
and widget count as a bounded preview summary. It does not expose labels in the
summary. Staging still performs no persistence. Exact local confirmation passes
the decoded layout to `save_next_if_changed`, which ignores source generation
and allocates the normal next local generation.

This boundary does not select/read a file, authenticate its origin, authorize a
source, format preview text, or prove physical presence. CRC is corruption
detection only and cannot make an imported record trusted.

## Ownership boundary

The facade does not contain a mutex, RTOS task, queue, ISR bridge, renderer,
input adapter, debounce/hold policy, source authorization, diagnostics call, or
ESP-IDF storage backend. A target composition must give one serialized owner
exclusive access to the facade and bind its prompt token to the exact
successfully displayed frame and local action.

## Evidence

Eleven deterministic groups cover lifecycle/policy, exact prompt stage,
mismatched confirmation, changed/unchanged persistence, cancellation/expiry,
ordinary failure versus uncertain commit, clock rollback, request replay, and
confirmed default restoration across restart with zero erases, exact local
import validation and non-authoritative source generation, plus independent
export/import transfer with destination-local generation allocation and repeat
suppression. The focused suite passes 100/100 repeats plus the complete
47-executable strict host matrix.

This is application-boundary evidence, not target task serialization, physical
presence, display readability, authenticated configuration, storage durability,
or hardware validation.
