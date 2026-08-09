# Recovery Status Diagnostic Event v0

Status: host-tested redacted diagnostics adapter, 2026-08-09. No target log
backend, persistent audit retention, remote export, or physical operator
workflow is claimed.

## Purpose

`record_recovery_status` converts the fixed recovery operator status into one
atomic `configuration_recovery / state_code` event in the existing 32-record
diagnostics ring. The event remains numeric and pointer-free; it does not accept
or serialize peer IDs, key handles, addresses, credentials, raw checkpoint
bytes, or vehicle identifiers.

Checkpoint generations remain available in the live operator-status record but
are intentionally omitted from this event. One complete coarse outcome is more
useful than a multi-event generation trace that could be partially overwritten.

## Versioned 32-bit word

| Bits | Field |
| --- | --- |
| 0-1 | operation: boot, save, or repair |
| 2-4 | operator state |
| 5-9 | operator reason |
| 10-12 | required action |
| 13-14 | slot A state |
| 15-16 | slot B state |
| 17-19 | protected-key failure category |
| 20 | transport allowed |
| 21 | operator attention required |
| 22 | redundancy repair required |
| 23 | sensitive internal detail was redacted |
| 24-27 | format version, currently 0 |
| 28-31 | fixed magic nibble `0xA` |

Decode rejects the wrong magic, unsupported versions, unknown enum values, and
incoherent combinations. For example, an `operational` event must allow
transport, require no attention or repair, and request no action. A malformed
word never becomes a plausible operational status.

## Severity and ring behavior

| State | Diagnostic level |
| --- | --- |
| operational | info |
| first boot, degraded, reboot/reconcile | warning |
| safe mode, service required | error |

Normal diagnostics threshold filtering still applies. A filtered record is an
accepted non-write; stopped service and monotonic-time regression remain typed
diagnostics errors. Encoding failure occurs before the diagnostics ring changes.

## Host evidence

`tests/host/recovery_status_diagnostics_tests.cpp` covers eight groups:

1. exact operational word and decode round trip;
2. degraded and redacted protected-key outcomes;
3. one canonical event at info/warning/error severity;
4. threshold filtering without false failure;
5. incoherent or unknown status rejected without a record;
6. bad magic, version, enum, and flag combinations rejected;
7. stopped-service and time-regression errors preserved; and
8. fixed, trivially copyable, identifier-free payload shape.

The focused suite passes 100 consecutive repeats, and the complete
41-executable host matrix passes locally.

## Remaining gates

- bind the diagnostics ring to an exact target logger and rollover policy;
- define authorized persistent audit export and retention;
- prove power-loss behavior for any persistent log backend;
- render decoded actions accessibly on candidate displays; and
- validate service-state capture during physical storage/trust failures.
