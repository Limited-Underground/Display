# Critical Alert System Recovery Store v0

Status: recoverable two-slot host store, 2026-08-09. This is not an ESP-IDF
storage adapter, protected-key persistence, authenticated integrity, trusted
rollback protection, or physical power-loss/wear evidence.

## Purpose

`CriticalAlertSystemRecoveryStore` writes exact 1280-byte `ORS0` records into
two caller-supplied slots. Because each `ORS0` already contains its nonzero
generation, nested identities, and outer CRC, the store does not add another
envelope.

One stored generation contains the peer authorization, ACK binding/replay, and
alert-outbox state needed for dependency-correct boot recovery. Raw keys remain
outside the record and must be resolved through protected storage by opaque
handle.

## Save policy

- `save_next` owns normal generation allocation: 1 on empty storage, then the
  greatest valid generation plus one.
- `save_next_after` accepts a caller-owned last-trusted generation and allocates
  strictly above both it and the greatest valid local slot. A maximum trusted
  value reports exhaustion before export or write.
- Explicit saves must be nonzero and strictly newer than every valid slot.
- Generation conflict, unreadable baseline, and 64-bit exhaustion fail before
  export or write.
- An empty/invalid slot is selected first; otherwise the older valid generation
  is replaced, preserving the newest known-good slot during the write.
- The store exports one exact `ORS0`, writes it, reads back all 1280 bytes,
  compares them exactly, decodes them again, and verifies the generation.
- Write and verification failures report the intended slot/generation as
  `commit_uncertain` so boot inspection, not a blind retry, determines outcome.

## Restore policy

- Both slots are inspected and decoded before selection.
- The newest unique valid generation is selected.
- `restore_at_or_above` refuses the selected valid generation when it is below a
  caller-supplied trusted minimum. It reports `rollback_detected` with the local
  generation and imports none of the three live owners.
- One invalid or unreadable slot remains visible as recovery/degraded evidence
  while the other valid slot can still restore.
- Equal generations with different bytes fail closed.
- The selected record is passed through the full dependency-correct `ORS0`
  preflight and live import. A peer/outbox/ingress policy rejection leaves all
  three live owners unchanged.
- `restore_at_or_above_validating_keys` additionally requires every active
  restored opaque key handle to pass an injected protected-key validator before
  any live import. The nested typed key failure is returned as checkpoint
  rejection; revoked entries are not resolved.
- `inspect` reports both slot states, selected generation/source, degradation,
  conflict, and storage failure without importing any live owner. The boot
  coordinator uses it only when deciding whether uninitialized trust plus local
  state is a genuine first boot.
- Reset attempts to erase both slots and reports either failure.

The interface requires exclusive ownership for inspection, save, and restore.
It does not define concurrent writers, rollback authority, or factory-reset
policy.

## Host evidence

`tests/host/critical_alert_system_recovery_store_tests.cpp` covers eleven groups:

1. canonical first save and empty-store behavior;
2. monotonic rotation and newest joint three-owner restore;
3. eleven interrupted-write boundaries across header, `OPA0`, `OCR0`, tail,
   and outer CRC, each preserving generation 1;
4. a complete generation-2 write followed by I/O error, selected as committed
   at boot;
5. corrupt and unreadable newest slots with visible fallback/degradation;
6. incompatible peer policy rejected without live-owner mutation;
7. invalid/stale/exhausted generations and equal-generation conflict; and
8. corrupt readback as uncertain verification failure plus reset failure;
9. accepted exact-floor restore and rejected below-floor rollback with no live
   mutation; and
10. new-save allocation above trusted/local generations and trusted exhaustion;
    and
11. protected-key rejection with zero live mutation followed by successful
    validated restore.

The current complete 43-executable host matrix passes, and the focused store
suite passes 100 consecutive repeats. A separate
[key/value target-adapter suite](CRITICAL_ALERT_SYSTEM_RECOVERY_KV_TARGET_ADAPTER_V0.md)
exercises this real store through exact 1280-byte blobs, including save,
rotation, reset, and applied/unapplied failed-commit restart discovery across
nine groups and 100/100 repeats.

## Remaining gates

- bind the target-shaped key/value boundary to a selected ESP-IDF
  protected-storage backend;
- define and persist the authenticated trusted monotonic/rollback authority that
  supplies these floor values;
- coordinate logical record reset with protected key erasure and replacement;
- inject power interruption at the eleven modeled boundaries and additional
  backend-specific commit boundaries on exact target hardware;
- measure endurance and prove authenticated transport boot composition.
