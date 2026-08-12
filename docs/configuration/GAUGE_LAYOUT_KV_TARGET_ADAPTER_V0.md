# Gauge Layout Key/Value Target Adapter v0

Status: backend-neutral target-shaped storage boundary, 2026-08-12. No
ESP-IDF backend, physical interruption, endurance, migration, configuration
authenticity, or on-device result is claimed.

This adapter maps the existing two-slot
[`OGL0` gauge-layout store](GAUGE_LAYOUT_STORAGE_V0.md) onto the same generic blob
backend contract used by coordinated recovery, without sharing its namespace or
changing layout validation, generation selection, safe-default, or reset rules.

## Fixed binding

- partition: `og_config`
- namespace: `gauge_layout`
- slot keys: `ogl0_a` and `ogl0_b`
- value size: exactly 576 bytes

All identifiers fit the ESP-IDF NVS 15-character limit. Configuration and
system-recovery records use separate namespaces. CRC-32 detects accidental
corruption; it does not authenticate a layout or provide access control.

## Durable-operation boundary

- Missing keys remain distinct from backend I/O failures.
- Reads accept only one complete 576-byte value.
- A write reports success only after the full blob is staged and backend commit
  reports success.
- A present-key erase reports success only after commit; missing-key erase is
  idempotent and creates no redundant commit.
- A failed commit remains uncertain. On restart the existing store reads both
  slots and selects the unique newest valid generation rather than assuming the
  operation either failed or succeeded.

Eight deterministic groups cover fixed binding and invalid arguments, exact /
missing / wrong-sized / failed reads, write/erase commit behavior, backend
failure mapping, real store rotation and restart, selection of an applied
failed commit, preservation of the prior layout after an unapplied failed
commit, and real two-key reset with safe-default fallback. The focused suite
passes 100/100 repeats and the complete 43-executable host matrix passes under
strict C++17 warnings-as-errors including publication safety.

## Remaining target obligations

The target still owns ESP-IDF initialization and handles, exclusive locking,
native error translation, partition sizing, flash/security policy, physical
power-cut behavior, latency, wear/endurance, authorized import/reset UX, schema
migration, unchanged-write suppression, and configuration authenticity. This
adapter is durable-operation plumbing, not evidence for any particular board.
