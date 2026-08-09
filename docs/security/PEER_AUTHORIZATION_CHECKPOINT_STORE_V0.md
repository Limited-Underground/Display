# Peer Authorization Checkpoint Store v0

Status: recoverable two-slot host storage boundary, 2026-08-09. This is not an
ESP-IDF/NVS binding, protected key storage, authenticated integrity, trusted
rollback protection, or physical power-loss evidence.

## Purpose

`OPS0` wraps one exact 256-byte `OPA0` peer-authorization checkpoint in a
fixed 288-byte generation envelope. `PeerAuthorizationCheckpointStore` owns two
storage slots so one prior generation can remain recoverable while the other is
being replaced.

The store still contains only logical authorization metadata and opaque secure
key handles. Raw keys and device-specific radio/Bluetooth identities remain
outside this interface.

## Record layout

All integers are little-endian. Reserved bytes must be zero.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OPS0` |
| 4 | 1 | Store version `0` |
| 5 | 1 | Header size `24` |
| 6 | 2 | Nested checkpoint size `256` |
| 8 | 8 | Nonzero generation |
| 16 | 8 | Reserved zero |
| 24 | 256 | Exact `OPA0` checkpoint |
| 280 | 4 | Reserved zero |
| 284 | 4 | CRC-32 over bytes 0 through 283 |

CRC detects accidental corruption only. The target adapter must add appropriate
authenticated integrity and rollback controls.

## Save and restore rules

- Normal `save_next` begins at generation 1 and advances from the greatest valid
  slot generation. Generation exhaustion fails before export or write.
- An explicit save must be strictly newer than every valid slot.
- Empty or invalid slots are replaced first; otherwise the older valid slot is
  replaced.
- Every successful write is read back, compared byte-for-byte, decoded, and
  compared with the exported nested checkpoint.
- A write error or verification failure reports the intended slot/generation as
  `commit_uncertain`; boot inspection decides whether a full record committed.
- Restore selects the newest unique valid outer record. One invalid or
  unreadable slot is visible as recovery/degraded evidence while the other good
  slot can still restore.
- Equal generations containing different bytes fail closed as a conflict.
- The selected nested `OPA0` is imported through the registry's boot-only atomic
  validation. Policy mismatch or a nonempty registry is rejected without peer
  mutation.
- Reset attempts to erase both slots and reports any erase failure.

An outer-valid record with an invalid nested checkpoint is rejected rather than
silently falling back. This is conservative because the outer CRC is not a
security boundary and rollback authority is not yet defined.

## Host evidence

`tests/host/peer_authorization_checkpoint_store_tests.cpp` covers ten groups:

1. canonical first save and exact nested identity;
2. monotonic slot rotation and newest active/revoked restore;
3. ten interrupted-write boundaries preserving the prior generation;
4. full write followed by I/O error reconciled as committed at boot;
5. corrupt-slot fallback and readable degraded-I/O evidence;
6. incompatible policy and nonempty-registry atomic rejection;
7. invalid, stale, and exhausted generation behavior;
8. equal-generation conflict rejection;
9. corrupt readback as verification failure plus uncertain commit; and
10. empty and reset behavior, including erase failure.

The complete 34-executable host matrix passes, and the focused store suite passes
100 consecutive repeats.

## Remaining gates

- bind both slots to a selected ESP-IDF protected-storage adapter;
- define a single coordinated generation for peer authorization, ACK replay, and
  alert outbox recovery without partial live mutation;
- define trusted generation/reset authority and authenticated integrity;
- prove opaque handle resolution, key-store loss, replacement, and factory reset;
- inject physical power loss at record boundaries and measure wear/recovery on
  exact target hardware.
