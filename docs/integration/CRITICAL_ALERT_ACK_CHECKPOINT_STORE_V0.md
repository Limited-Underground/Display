# Critical Alert ACK Checkpoint Store v0

Status: deterministic host-tested recoverable storage composition, 2026-08-09.
This is not an ESP-IDF/NVS binding, flash-endurance measurement, secure counter,
rollback-proof journal, or physical brownout result.

## Stored record

`CriticalAlertAckCheckpointStore` wraps the exact 280-byte `OAI0` payload in a
fixed 320-byte little-endian `OAS0` envelope:

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OAS0` |
| 4 | 1 | envelope version, currently 0 |
| 5 | 1 | header length, exactly 24 |
| 6 | 2 | checkpoint length, exactly 280 |
| 8 | 8 | nonzero caller-owned generation |
| 16 | 8 | reserved zero |
| 24 | 280 | exact `OAI0` checkpoint |
| 304 | 12 | reserved zero |
| 316 | 4 | CRC-32/ISO-HDLC over bytes 0 through 315 |

The outer CRC detects envelope/payload corruption. The inner checkpoint CRC
and full ingress import still run before state changes. Neither CRC proves
authenticity or prevents a deliberate replacement.

## Two-slot behavior

The store reads two fixed backend slots. Restore chooses the valid record with
the unique highest generation. One valid slot is usable while an empty,
invalid, or unreadable peer slot remains visible as recovery-required. Two
different records claiming the same generation fail closed with no import.
No valid slot yields explicit `no_checkpoint` or `storage_failure`; there is no
invented default replay state.

The selected payload is imported through `CriticalAlertAckIngress`, which
rechecks producer identity, canonical checkpoint structure/CRC, every live
authorization epoch, and boot-only timing atomically. A valid envelope with a
rejected inner checkpoint never replaces current bindings.

Save rejects generation zero, stale/equal generation, storage I/O degradation,
and pre-existing equal-generation conflict before export/write. It exports from
the live ingress, targets an empty/invalid slot or the older valid slot, writes
the full record, reads it back, compares every byte, and decodes generation and
payload again. A partial or corrupt write cannot displace the other good slot.

Generation is caller-owned. This prevents accidental rollback through the
normal API, but it is not secure anti-rollback: a backend or attacker capable of
restoring both older slots can restore an older generation. A trusted monotonic
counter or authenticated secure storage is required for that stronger claim.

## Host evidence

`tests/host/critical_alert_ack_checkpoint_store_tests.cpp` covers ten groups:

1. explicit envelope offsets and first-slot save;
2. empty restore, generation zero, and stopped-ingress export;
3. two generations, newest selection, boot import, and replay rejection;
4. interrupted partial write preserving the last good slot;
5. corrupt readback preserving the other good slot;
6. visible I/O degradation while a valid slot restores;
7. key rotation rejecting the old epoch without state replacement;
8. outer-CRC-repaired inner tamper rejected atomically by ingress;
9. stale generation plus explicit partial reset failure; and
10. different records with the same generation failing closed.

The full OpenGauge host matrix passes with warnings treated as errors. This
store suite and the affected checkpoint and ingress suites each repeated 100
times with zero failures.

## Remaining gates

- select and implement exact ESP-IDF/NVS slot, task ownership, alignment,
  erase, and commit semantics;
- coordinate authorization, replay, and any retained outbox commit order;
- allocate/persist generation safely and define exhaustion;
- add authenticated replacement and trusted anti-rollback if required; and
- measure flash wear and physical power loss at every write/erase phase.
