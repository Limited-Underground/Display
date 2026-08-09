# Critical Alert System-Recovery Boot Coordinator v0

Status: host-tested policy boundary, 2026-08-09. No ESP-IDF boot task,
protected-key backend, trusted monotonic source, or on-device result is claimed.

## Purpose

`CriticalAlertSystemRecoveryBootCoordinator` turns the separate `ORS0` store,
protected-key validator, trusted-generation source, and local provisioning state
into one fail-visible boot decision. It does not initialize hardware, invent
defaults, erase state, or enable a physical transport.

## Outcomes

| State | Meaning | Transport |
| --- | --- | --- |
| `first_boot` | Trust is uninitialized, caller says unprovisioned, and both inspected slots are exactly empty | disabled |
| `restored` | A complete key-validating restore matches the trusted floor | allowed |
| `restored_degraded` | Restore succeeds from a surviving slot while the peer slot is known empty or invalid; repair remains required | allowed, with degradation visible |
| `safe_mode` | Rollback, equal-generation conflict, or non-key checkpoint rejection | disabled |
| `service_required` | Provisioning ambiguity, missing/unreadable trust or recovery, any unreadable slot, protected-key failure, storage failure, or failed trust reconciliation | disabled |

The result retains the nested store, checkpoint, authorization, and key error so
operator or target code does not have to infer the cause from the coarse state.

## Boot ordering

Under exclusive boot-time ownership:

1. reject unknown provisioning state without reading or changing recovery;
2. read the separate trusted-generation source;
3. when trust is uninitialized, inspect both slots without importing state and
   accept first boot only if provisioning is explicitly unprovisioned and both
   slots are exactly empty;
4. require nonzero initialized trust and provisioned local state;
5. run `restore_at_or_above_validating_keys`, which validates active opaque key
   handles and all private authorization/outbox/ACK candidates before live
   import;
6. map rollback/conflict/checkpoint/key/storage outcomes to typed safe or service
   states;
7. if either slot is unreadable, retain any imported state only under exclusive
   ownership, require service, and do not advance trust or enable transport;
8. if the selected generation is newer than trusted state, advance the trusted
   source and require an exact readback; and
9. allow transport only after every required step succeeds.

If trusted advancement fails after live import, the recovered owners remain
present under exclusive ownership but transport stays disabled. Target code must
not expose them until service or a subsequent successful reconciliation.

## Host evidence

`tests/host/critical_alert_system_recovery_boot_tests.cpp` covers ten groups:

1. genuine first boot versus corrupt local state;
2. unknown provisioning, unreadable trust, zero trust, and provisioning/trust
   conflict;
3. exact two-slot restore and transport enablement;
4. visible degraded restore from one surviving generation;
5. an unreadable slot hiding a newer generation remains service-only;
6. rollback and equal-generation conflict safe mode;
7. protected-key loss with no live import;
8. successful interrupted trusted-floor reconciliation and readback;
9. failed trusted advance or stale readback with transport disabled; and
10. initialized trust with missing local recovery.

The complete 38-executable host matrix passes, and the focused boot suite passes
100 consecutive repeats.

## Remaining target gates

- exact ESP32/ESP-IDF board and task composition;
- protected-key and rollback-resistant generation implementations;
- physical power interruption at storage/trust update boundaries;
- authorized repair, reset, and replacement workflow;
- operator-visible safe-mode and service presentation; and
- proof that the physical radio transport stays disabled until operational.
