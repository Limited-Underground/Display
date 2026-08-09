# Critical Alert System-Recovery Save Coordinator v0

Status: host-tested persistence-ordering boundary, 2026-08-09. No ESP-IDF
storage, trusted monotonic backend, power-cut result, or physical durability is
claimed.

## Purpose

`CriticalAlertSystemRecoverySaveCoordinator` applies the save-side half of the
target recovery contract. It prevents a normal runtime save from silently
recreating missing state, overwriting a rollback condition, or advancing trust
before a new `ORS0` generation is verified.

## Required pre-save state

The coordinator first reads initialized, nonzero trusted state and then performs
a non-mutating two-slot inspection. A normal save proceeds only when the newest
valid local generation exactly equals the trusted generation.

| Local relationship to trust | Result |
| --- | --- |
| no valid local checkpoint | `service_required / recovery_missing` |
| local generation below trust | `service_required / rollback_detected` |
| local generation above trust | `reboot_reconcile_required / trusted_reconciliation_required` |
| equal-generation slot conflict | `service_required / generation_conflict` |
| unreadable storage | `service_required / storage_failure` |
| exact agreement | proceed with the next generation |

This API is for normal operation after a successful boot. Authorized first
provisioning and factory reset remain separate target workflows.

## Commit ordering

Under exclusive ownership:

1. read initialized, nonzero trusted generation;
2. inspect the store and require exact local/trusted agreement;
3. call `save_next_after` with the trusted generation;
4. require exact slot readback and checkpoint decode;
5. advance the separate trusted source to the committed generation;
6. read trust back and require exact equality; and
7. report committed and permit transport only after every step succeeds.

A partial/full write reported as failed, corrupt readback, failed trust advance,
or stale/unreadable trust readback never reports committed. The result requires
reboot reconciliation; a later boot coordinator can select the newest valid
slot, validate keys, and finish trusted-floor catch-up.

## Host evidence

`tests/host/critical_alert_system_recovery_save_tests.cpp` covers eight groups:

1. verified next-generation save, trust advance, and exact readback;
2. uninitialized/unreadable/zero trust and initialized trust with missing local
   recovery, all without writes;
3. equal-generation conflict and generation exhaustion before a new write;
4. checkpoint export rejection without a false commit;
5. local-ahead reconciliation versus local-behind rollback without writes;
6. full-write failure and corrupt verification as commit-uncertain outcomes;
7. failed trust advance or stale readback with the verified slot retained and
   transport disabled; and
8. successful next-boot reconciliation after a failed trust advance.

The complete 38-executable host matrix passes, and the focused save suite passes
100 consecutive repeats.

## Remaining target gates

- exact ESP-IDF storage and trusted-generation implementations;
- physical interruption at every slot and trust update boundary;
- task-level exclusive ownership and transport disablement;
- bounded repair/retry and operator service workflow;
- endurance and generation-exhaustion policy; and
- authorized first provisioning, reset, and replacement.
