# Critical Alert System-Recovery Key/Value Target Adapter v0

Status: backend-neutral target-shaped storage boundary, 2026-08-12. No
ESP-IDF backend, protected key store, authenticated integrity, trusted
generation source, physical interruption, endurance, or on-device boot result
is claimed.

This adapter maps the existing two-slot
[`ORS0` recovery store](CRITICAL_ALERT_SYSTEM_RECOVERY_STORE_V0.md) onto exact
key/value operations without changing generation allocation, restore,
protected-key validation, boot, save, repair, or reset policy.

## Fixed binding

- partition: `og_state`
- namespace: `og_recovery`
- slot keys: `ors0_a` and `ors0_b`
- value size: exactly 1280 bytes

All identifiers fit the ESP-IDF NVS 15-character limit. The namespace prevents
accidental collision; it does not make `ORS0` confidential, authenticated,
access-controlled, or rollback-resistant.

## Durable operations and restart behavior

- Missing keys remain distinct from backend I/O failures.
- Reads accept only one complete 1280-byte value.
- A write reports success only after the full blob is staged and the backend
  commit reports success.
- A present-key erase reports success only after backend commit; missing-key
  erase is idempotent and does not create a redundant commit.
- Failed commits remain uncertain. The upper store inspects both durable slots
  after restart rather than retrying or rolling back blindly.

Thirteen deterministic groups cover fixed names and public arguments, exact /
missing / wrong-sized / failed reads, write and commit failures, durable and
idempotent erase, real `ORS0` first-save and slot rotation, discovery of a full
record after an applied-then-failed commit, confirmed empty state after an
unapplied failed commit, real two-key store reset, and restart-visible erase
after an applied-then-failed commit. The same suite now composes the real boot
and verified-save coordinators through restarted adapter/store instances. It
proves normal one-slot and two-slot restart, advances separate trust only after
discovering an applied uncertain commit, and preserves the prior trusted boot
after an unapplied uncertain commit. The focused suite passes 100/100 repeats
and the complete 43-executable host matrix passes under strict C++17
warnings-as-errors including publication safety.

## Remaining target obligations

The target still owns ESP-IDF initialization, handles, exclusive locking,
native error translation, partition/security policy, flash encryption,
authenticated integrity, protected opaque-key resolution, the independent
trusted generation source, coordinated authorized reset/replacement, latency
and wear evidence, and physical reset/power-loss testing. An ordinary NVS
binding alone cannot satisfy the protected recovery design.
