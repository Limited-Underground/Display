# Gauge Layout Storage v0

Status: deterministic host-tested codec and recoverable two-slot composition,
2026-08-09. This is not an ESP-IDF/NVS binding, flash-endurance result,
filesystem, remote import format, schema migration implementation, or physical
power-interruption test.

## Layout record

The `OGL0` record is exactly 576 bytes and explicitly serialized little-endian.
It contains:

- schema version 1 and a fixed 32-byte header;
- nonzero 64-bit caller-owned generation and 32-bit layout ID;
- brightness from 1 through 100 and dark/light/high-contrast theme;
- one through eight validated gauge-widget configurations;
- fixed 64-byte widget slots with canonical zero padding;
- canonical zero unused slots and tail-reserved bytes;
- CRC-32 over the first 572 bytes.

Each widget preserves its stable ID, registered telemetry signal code, kind,
bounded ASCII label, stale threshold, and canonical raw scale. The same widget
validator used by the runtime view model is the codec authority. Duplicate
widget IDs, unsupported signal/kind combinations, invalid scale/status use,
noncanonical padding, malformed length, bad magic/version, and CRC corruption
fail before caller output changes.

CRC is accidental-corruption detection only. It is not authenticity,
authorization, encryption, rollback prevention, or safe remote import.

## Two-slot recovery

`GaugeLayoutStore` reads two fixed backend slots. Boot selection is:

1. choose the valid record with the highest generation;
2. if only one slot is valid, use it and request repair;
3. if neither is valid/available, use a separately validated compiled safe
   default and request repair;
4. if two different records claim the same generation, reject the ambiguity,
   use the safe default, and report `generation_conflict`.

An I/O failure is retained in the result even when a valid slot or safe default
provides a usable layout. Callers can therefore render a conservative layout
while keeping storage degradation visible.

Save validates and encodes the entire record, rejects generations that do not
strictly advance the highest valid stored generation, targets an invalid/empty
slot or the older valid slot, writes the full record, reads it back, compares
every byte, and decodes it again. A partial or corrupt write cannot displace the
other valid slot. No record is declared saved until verification completes.

The strategy alternates writes between slots after both exist. It does not yet
deduplicate semantically unchanged layouts, persist/allocate generation values,
handle 64-bit generation exhaustion, quantify flash wear, or guarantee backend
atomicity.

## Host evidence

`tests/host/gauge_layout_tests.cpp` covers nine groups:

1. explicit wire offsets plus signed-range and two-widget round trip;
2. layout/range/duplicate validation and output arguments;
3. bad magic/version, corruption, noncanonical unused slots, and atomic decode;
4. empty storage and safe-default validation;
5. slot rotation, strict generations, newest selection, and write counts;
6. equal-generation conflict fallback;
7. interrupted partial write preserving the last good slot;
8. corrupt post-write verification preserving the other good slot;
9. explicit read/erase failure and partial-reset behavior.

The suite also repeated 100 times with zero failures.

## Remaining persistence gates

- implement and host-test version migration before introducing schema 2;
- define safe import/export, validation feedback, reset confirmation, and UI
  recovery workflows;
- bind the two slots to the exact board storage API with task ownership,
  synchronization, size/alignment, erase-block, and commit semantics;
- measure normal-save wear, unchanged-save suppression, rapid user edits,
  brownout at each write phase, corrupt sectors, full storage, and recovery;
- persist generation safely and define exhaustion behavior;
- authorize and authenticate any nonlocal configuration source.
