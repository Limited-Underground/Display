# Critical Alert System-Recovery Repair Coordinator v0

Status: host-tested known-degraded repair boundary, 2026-08-09. No ESP-IDF
storage backend, physical repair durability, or operator service workflow is
claimed.

## Purpose

`CriticalAlertSystemRecoveryRepairCoordinator` restores two-slot redundancy
after a successful operational boot from one known good `ORS0` generation when
the peer slot is known empty or checksum-invalid. It does not repair unreadable
media, safe-mode/service conditions, or arbitrary caller-supplied state.

## Admission rules

Repair proceeds only when all of these remain true under exclusive ownership:

1. the supplied boot result is `restored_degraded`, operational, and marked
   repair-required;
2. the boot load did not contain a storage I/O failure;
3. active and trusted boot generations are equal and nonzero;
4. a fresh store inspection still selects that exact generation;
5. exactly one slot is valid and the peer slot is known empty or invalid; and
6. no slot is unreadable.

Healthy, service-required, safe-mode, unreadable, or stale boot evidence returns
without writing.

## Repair ordering

1. re-inspect and validate the boot evidence;
2. delegate the next-generation write and trusted-floor ordering to
   `CriticalAlertSystemRecoverySaveCoordinator`;
3. require committed checkpoint and exact trusted readback;
4. inspect both slots again; and
5. report repaired only when both are valid, degradation is cleared, and the
   selected generation equals the committed repair generation.

An uncertain checkpoint commit or failed trust update returns
`reboot_reconcile_required`. A post-commit redundancy verification failure is a
service condition. Transport is allowed by the repair result only after complete
success.

## Host evidence

`tests/host/critical_alert_system_recovery_repair_tests.cpp` covers five groups:

1. successful repair of known empty and checksum-invalid peer slots;
2. refusal of healthy and unreadable/service boot results;
3. stale boot evidence rejected without another write;
4. full-write I/O uncertainty routed to reboot reconciliation; and
5. failed trusted-generation advancement routed to reboot reconciliation.

The complete 39-executable host matrix passes, and the focused repair suite
passes 100 consecutive repeats.

## Remaining gates

- physical storage interruption during repair and post-repair inspection;
- endurance limits and bounded repair scheduling;
- operator presentation of repaired, reconcile, and service states;
- persistent audit evidence without sensitive identifiers; and
- authorized recovery/replacement for unreadable or lost protected state.
