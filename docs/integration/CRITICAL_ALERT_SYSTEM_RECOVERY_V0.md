# Critical Alert System Recovery v0

Status: deterministic host-tested coordinated recovery boundary, 2026-08-09.
This is not durable target storage, authenticated integrity, trusted rollback
protection, or physical power-loss evidence.

## Purpose

`ORS0` binds the authorization state required by ACK replay validation to the
existing ACK/outbox `OCR0` recovery state in one nonzero generation. It prevents
a caller from independently selecting:

- an `OPA0` peer-authorization generation;
- an `OAI0` ACK binding/replay generation; and
- an `OOC0` alert-outbox generation.

The record is fixed at 1280 bytes and contains no raw key material. `OPA0`
contains only logical policy metadata and opaque protected-key handles.

## Canonical layout

All integers are little-endian. Reserved bytes must be zero.

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | ASCII magic `ORS0` |
| 4 | 1 | Version `0` |
| 5 | 1 | Header size `24` |
| 6 | 2 | Authorization checkpoint size `256` |
| 8 | 2 | Critical recovery checkpoint size `960` |
| 10 | 6 | Reserved zero |
| 16 | 8 | Nonzero generation |
| 24 | 256 | Exact `OPA0` authorization checkpoint |
| 280 | 960 | Exact `OCR0` ACK/outbox recovery checkpoint |
| 1240 | 36 | Reserved zero |
| 1276 | 4 | CRC-32 over bytes 0 through 1275 |

The outer generation must exactly match the generation inside nested `OCR0`.
Both nested identities and CRCs are validated before decode succeeds. CRC is
only accidental-corruption detection.

## Dependency-correct atomic preflight

ACK ingress owns pointers to the peer-authorization registry and alert outbox.
Copying the ingress object alone would retain pointers to the original owners,
so `ORS0` does not use that unsafe shortcut.

Import under caller-provided exclusive ownership performs these steps:

1. decode and validate the complete `ORS0` envelope;
2. import `OPA0` into a private registry candidate;
3. import nested `OOC0` into a private outbox candidate;
4. construct a temporary ACK ingress with the live ingress configuration but
   pointed at those two private candidates;
5. import nested `OAI0` into that temporary ingress, including authorization-
   epoch revalidation;
6. only after every preflight succeeds, deterministically import authorization,
   outbox, and ACK state into the three live owners.

The live ingress must itself remain eligible for boot import. A stopped or
already processed ingress fails before any live component import. Exclusive
ownership is mandatory for the complete operation so state cannot change
between preflight and commit.

## Host evidence

`tests/host/critical_alert_system_recovery_tests.cpp` covers six groups:

1. exact layout, nested identities, decode, and generation binding;
2. joint restoration of peer authorization, ACK replay, and queued retry state;
3. outer and nested corruption with zero live-owner mutation;
4. a restored peer epoch that conflicts with `OAI0`, rejected before any live
   import;
5. peer policy, outbox policy, and live-ingress eligibility failures with all
   live owners unchanged; and
6. invalid-generation and pending-approval export failures with unchanged
   output.

The complete 35-executable host matrix passes, and the focused `ORS0` suite
passes 100 consecutive repeats.

The separate [two-slot system-recovery store](CRITICAL_ALERT_SYSTEM_RECOVERY_STORE_V0.md)
now owns normal `ORS0` generations and adds recoverable interruption handling
around this exact record.

## Remaining gates

- bind the two-slot store to selected ESP-IDF protected storage;
- define authenticated integrity, trusted rollback, reset, and replacement
  authority across both protected keys and logical checkpoints;
- inject power loss at all outer/nested record boundaries on exact hardware;
- prove boot composition with an authenticated on-device transport adapter.
