# Gauge Layout Change Status Diagnostic Event v0

Status: host-tested redacted diagnostics adapter, 2026-08-12. No target logger,
persistent audit, export, or physical failure capture is claimed.

## Purpose

The live layout-change operator record contains an opaque confirmation token,
remaining time, and applied generation. Those fields are useful to the local UI
but do not belong in routine diagnostic events.

This adapter validates the complete live record, reduces it to a separate
coarse diagnostic structure, encodes one canonical 32-bit word, and submits it
to the existing fixed diagnostics ring.

## Redaction boundary

The diagnostic structure and word contain only:

- coarse operator state;
- requested semantic action;
- attention-required flag;
- confirmation-allowed flag;
- last-operation-rejected flag; and
- a mandatory sensitive-detail-redacted flag.

They cannot contain request ID, confirmation countdown, layout generation,
layout or widget content, labels, counters, peer identity, address, credential,
or key handle.

## Canonical 32-bit layout

| Bits | Field |
| --- | --- |
| 0-3 | operator state |
| 4-6 | operator action |
| 7 | attention required |
| 8 | confirmation allowed |
| 9 | last operation rejected |
| 10 | sensitive detail redacted; must be 1 |
| 11-23 | reserved; must be 0 |
| 24-27 | version 0 |
| 28-31 | magic `0xB` |

Unknown enums, nonzero reserved bits, wrong magic/version, missing redaction,
and incoherent state/action/flag combinations fail closed.

## Severity and storage

The adapter uses the existing `configuration_recovery` event family and
`state_code` metric so no new open-ended diagnostic payload is introduced.

- unavailable, ready, confirmation-required, applied, unchanged, and cancelled
  are info;
- expired and rejected are warning;
- persistence-failed, restart-required, and clock-fault are error.

Normal diagnostics threshold filtering still applies. Diagnostic-service
stopped and monotonic-time regression errors remain visible in the record
result rather than being rewritten as adapter success.

## Evidence

Eight deterministic groups cover exact applied encoding, live prompt redaction,
all terminal categories, ring severity, threshold filtering, malformed live
status, malformed words, and diagnostics lifecycle/time failure. Compile-time
member checks prove the redacted structure has no request ID, generation, or
remaining-time fields. The focused suite passes 100/100 repeats and the full
46-executable strict host matrix.

This does not validate target log task ownership, persistent retention,
encryption, export authorization, privacy policy, power loss, or physical
failure capture.
