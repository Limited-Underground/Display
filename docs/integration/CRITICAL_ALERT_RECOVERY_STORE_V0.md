# Coordinated Critical Alert Recovery Store v0

Status: recoverable two-slot host storage composition with deterministic
failure-injection evidence, 2026-08-09. No ESP-IDF/NVS backend, physical
power-cut/endurance result, authenticated integrity, or trusted anti-rollback
mechanism is claimed.

## Storage contract

Each of two backend slots stores one exact canonical 960-byte `OCR0` record.
There is no second wrapper or separately selectable ACK/outbox record. The
backend provides exact-size read, write, and erase operations and distinguishes
not-found, invalid request, and I/O failure.

Normal save owns generation allocation: it inspects both slots under exclusive
ownership, refuses an I/O-degraded or equal-generation-conflict baseline,
chooses `highest valid + 1` (or 1 when empty), and reports 64-bit exhaustion
before export or write. The lower-level explicit-generation operation remains
available for deterministic recovery tooling but requires a nonzero value
strictly greater than every valid stored generation. The store targets an empty
or invalid slot before the older valid slot. It then:

1. exports both live owners into one `OCR0` generation;
2. writes all 960 bytes;
3. reads the same slot back in full;
4. requires exact byte equality;
5. decodes the complete record and checks its generation.

A failed or partially written target never overwrites the newest known-good
slot. A backend reporting write success with corrupt bytes is a verification
failure, not a successful save.

## Boot recovery

Restore decodes both slots independently. It selects the unique greatest valid
generation. Equal generations with different bytes fail closed as a conflict.
One valid slot can restore when the other is empty, corrupt, or unreadable;
`recovery_required` remains visible, and an unreadable peer slot returns a
degraded storage result even when coordinated live restore succeeds.

The selected bytes go through the preflighted `OCR0` live coordinator. Policy
fingerprint mismatch, authorization-epoch mismatch, corrupt nested state, or a
non-boot owner rejects the restore without partial ACK/outbox mutation.

## Host evidence

Ten groups cover explicit first-save layout and newest-generation restore,
empty/invalid-generation/prepared-export handling, partial write recovery,
corrupt-success readback, degraded I/O with valid restore, policy and
authorization rejection atomicity, stale generation/reset failure, and equal
generation conflict, plus automatic 1/2/3 allocation/rotation and exact
exhaustion without a write. The full 32-executable host matrix and 100 focused
store repeats pass.

## Remaining gates

- bind the exact interface to a selected ESP-IDF storage backend;
- define factory-reset/replacement authority and whether generation continuity
  must survive deliberate erase;
- inject physical power cuts at backend-specific erase/write/commit boundaries;
- measure wear, save rate, latency, and boot recovery on selected hardware;
- add authenticated integrity and trusted rollback protection; CRC is only
  accidental-corruption detection;
- coordinate persisted peer-authorization registry state and protected key
  lifecycle with the already epoch-bound ACK checkpoint.
