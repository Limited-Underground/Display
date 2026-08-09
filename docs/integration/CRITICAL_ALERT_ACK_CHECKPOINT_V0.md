# Critical Alert ACK Replay Checkpoint v0

Status: deterministic host-tested serialization and atomic-restore contract,
2026-08-09. A separate host-tested two-slot store now wraps this record. This
is not an ESP-IDF/NVS adapter, durable key store, encrypted backup,
rollback-proof counter, or physical reset/power-loss result.

## Boundary

`CriticalAlertAckIngress` exports and imports one fixed 280-byte `OAI0` record
covering its eight consumer/session bindings and 32-sequence replay windows.
The record includes no raw key, key handle, MAC address, PIN, transport address,
vehicle data, event content, location, or free text.

The checkpoint is useful only with the separate peer-authorization registry.
Every active binding records the authorization epoch observed during explicit
session binding. Export and import both require that the currently active peer
still has `publish_alarm_ack` permission and the exact same nonzero epoch.
Rotation, revoke, forget/replacement, or missing authorization therefore fails
closed instead of restoring stale replay trust.

## Fixed record

All multibyte integers are unsigned little-endian. Reserved and inactive bytes
must be zero.

| Offset | Bytes | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OAI0` |
| 4 | 1 | schema version, currently 0 |
| 5 | 3 | reserved zero |
| 8 | 8 | exact local OpenGauge producer ID |
| 16 | 1 | active binding count, 0 through 8 |
| 17 | 3 | reserved zero |
| 20 | 256 | eight fixed 32-byte binding slots |
| 276 | 4 | CRC-32/ISO-HDLC over bytes 0 through 275 |

Each active slot contains active/has-sequence flags, logical peer ID, consumer
ID, consumer boot-session ID, authorization epoch, highest admitted sequence,
and 32-bit replay bitmap. An inactive slot is all zero. A replay bitmap must
have bit zero set when initialized; an uninitialized window has zero highest
sequence and bitmap. Peer and consumer IDs must be unique across active slots.

CRC detects accidental corruption only. Confidentiality, authenticity, anti-
rollback generation, and storage integrity remain adapter responsibilities.

## Export and import

Export builds a zero-initialized candidate, revalidates every active binding
against the live authorization epoch, calculates CRC, and replaces caller
output only on success.

Import is permitted only before the ingress has processed traffic in the new
runtime. It validates size, magic, version, producer identity, reserved bytes,
CRC, active count, flags, nonzero fields, replay invariants, duplicate IDs, and
every live authorization epoch into a temporary eight-slot array. The live
bindings are replaced only after the whole record passes.

After restore, a previously admitted sequence is still rejected as duplicate,
while a new in-window sequence can progress to ordinary outbox correlation.
Explicit rebind after legitimate key rotation captures the new epoch and resets
that consumer's replay window.

## Host evidence

`tests/host/critical_alert_ack_checkpoint_tests.cpp` covers eight groups:

1. canonical empty and bound records plus CRC;
2. replay-window restore rejecting a pre-reboot duplicate;
3. malformed size/reserved bytes with atomic state preservation;
4. version and producer incompatibility;
5. CRC corruption with no mutation;
6. key rotation/revoke invalidation, output preservation, and explicit rebind;
7. import-after-traffic rejection; and
8. replay invariant, active-count, and duplicate-entry rejection.

The complete OpenGauge host matrix passes. Both this suite and the affected ACK
ingress suite repeated 100 times each with zero failures.

## Remaining gates

- bind the host-tested `OAS0` two-slot contract to the selected target backend
  and restore it before ACK traffic;
- coordinate atomic durability with the peer-authorization registry and
  protected key-handle lifecycle;
- define reset, factory-reset, revoke, rotation, lost-peer, backup, and recovery
  operator workflows;
- add physical flash corruption, partial write, power-cut, wear, downgrade, and
  recovery tests; and
- protect durable metadata against disclosure and unauthorized replacement.
