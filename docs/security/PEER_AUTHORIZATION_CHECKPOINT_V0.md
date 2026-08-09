# Peer Authorization Checkpoint v0

Status: canonical host-tested restart record, 2026-08-09. This is not a raw-key
backup, protected target storage, authenticated integrity, or physical
power-loss evidence.

## Purpose and boundary

`OPA0` is a fixed 256-byte checkpoint for the logical
`PeerAuthorizationRegistry`. It preserves the authorization facts that the ACK
replay checkpoint must revalidate after restart:

- logical peer ID;
- role and permission mask;
- expected radio channel;
- active or revoked state;
- nonzero authorization epoch; and
- an opaque secure-key handle for active peers.

It never contains raw keys, PINs, passphrases, MAC addresses, Bluetooth
identities, discovery payloads, or approval codes. A target adapter must resolve
the opaque handle inside protected key storage.

Pending approval is deliberately not persistent. Export fails while an approval
is pending, so a restart always requires a new local approval action.

## Canonical layout

All integers are little-endian. Reserved and unused bytes must be zero.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `OPA0` |
| 4 | 1 | Version `0` |
| 5 | 1 | Stored peer count, 0 through 8 |
| 6 | 1 | Active peer count |
| 7 | 1 | Reserved zero |
| 8 | 8 | Approval-window policy in milliseconds |
| 16 | 8 | Reserved zero |
| 24 | 192 | Eight fixed 24-byte peer entries |
| 216 | 36 | Reserved zero |
| 252 | 4 | CRC-32 over bytes 0 through 251 |

Each peer entry contains state, role, channel, permissions, logical peer ID,
opaque key handle, and authorization epoch. Active entries require a nonzero key
handle. Revoked entries require a zero key handle but retain their epoch so a
restart cannot silently turn forgotten history into a fresh authorization.
Unused entries are all zero.

CRC detects accidental corruption only. It is not authentication and does not
provide rollback resistance.

## Import policy

Import is boot-only. The registry must be running, empty, have no pending
approval, and have performed no operational authorization mutation. A rejected
checkpoint leaves all peers unchanged and can be corrected and retried.

Import fails closed on:

- wrong size, magic, version, or approval-window policy;
- CRC mismatch;
- noncanonical padding or counts;
- unknown roles, invalid role permissions, or invalid channels;
- zero IDs or epochs;
- duplicate logical peer IDs;
- duplicate active key handles; or
- active/revoked key-handle invariant violations.

All entries are decoded into a private candidate array before the live registry
changes. `validate_checkpoint_import` performs the same checks on a copy without
mutating the live owner.

## Host evidence

`tests/host/peer_authorization_checkpoint_tests.cpp` covers eight groups:

1. active, revoked, rotated-key, and epoch round trip;
2. deterministic canonical empty output;
3. pending/stopped export refusal with output preservation;
4. shape, version, policy, CRC, and atomic failure handling;
5. padding and active-count validation;
6. duplicate and per-entry invariant rejection;
7. boot-only import and nonmutating preflight; and
8. full eight-peer capacity round trip.

The complete 33-executable host matrix passes, and the focused checkpoint suite
passes 100 consecutive repeats.

## Remaining gates

- compose `OPA0` with ACK replay/outbox recovery without a partial live restore;
- bind the record to a selected ESP-IDF protected-storage adapter;
- define authenticated integrity and trusted rollback protection;
- validate factory reset, replacement, revoke, and key-store loss behavior;
- inject physical power cuts and measure wear/recovery on exact target hardware;
- prove that opaque handles still resolve to the intended protected keys after
  restart without exporting reusable secrets.
