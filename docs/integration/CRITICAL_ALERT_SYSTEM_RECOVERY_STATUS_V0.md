# Critical Alert System-Recovery Operator Status v0

Status: host-tested redacted status boundary, 2026-08-09. No target logging,
display rendering, persistent audit store, or physical operator workflow is
claimed.

## Purpose

`CriticalAlertSystemRecoveryStatus` converts boot, normal-save, and degraded-
repair results into one fixed-shape record suitable for a later target log or
operator display. It preserves the distinctions needed for action without
exporting sensitive recovery identity.

The record exposes:

- operation: boot, save, or repair;
- coarse state, reason, and next action;
- slot A/B health;
- observed and trusted checkpoint generations;
- transport, attention, and repair flags; and
- the typed protected-key failure category when applicable.

It deliberately has no string, pointer, peer ID, key handle, address,
credential, vehicle identifier, or raw checkpoint field. When an internal key
failure carried a peer ID, only `sensitive_detail_redacted=true` survives.

## Operator states and actions

| State | Typical action |
| --- | --- |
| `first_boot` | Provision through an authorized workflow |
| `operational` | None |
| `operational_degraded` | Repair known slot redundancy |
| `reboot_reconcile_required` | Reboot and reconcile before transport |
| `safe_mode` | Service rollback/conflict evidence |
| `service_required` | Diagnose protected storage, trust, or invalid state |

Unknown enum values and internally inconsistent results fail closed to
`service_required / invalid_result`; they never inherit a transport-enabled
flag.

## Host evidence

`tests/host/critical_alert_system_recovery_status_tests.cpp` covers seven
groups:

1. operational and known-degraded boot status;
2. clean first-boot provisioning guidance;
3. protected-key failure category with peer identity redacted;
4. safe-mode mapping and incoherent boot fail-close;
5. committed and reboot-reconcile save outcomes;
6. repaired, stale, and uncertain repair outcomes; and
7. unknown or incoherent save/repair fail-close behavior.

Compile-time checks prove the record remains trivially copyable, bounded to at
most 40 bytes, and has no peer-ID or key-handle member.

## Remaining gates

- bind the record to a bounded target logger without adding sensitive fields;
- define persistent audit retention, rollover, and authorized export;
- render action text and accessibility behavior on candidate displays;
- validate behavior during physical storage and trust failures; and
- define the service workflow for unreadable media and lost protected state.
