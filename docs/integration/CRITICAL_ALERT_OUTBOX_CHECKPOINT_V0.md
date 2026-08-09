# Critical Alert Outbox Checkpoint v0

Status: canonical codec with deterministic host evidence, 2026-08-09. This is
not yet exported/imported by `CriticalAlertOutbox`, stored durably, coordinated
with ACK replay/authorization state, authenticated, or rollback-resistant.

## Boundary

The fixed 640-byte `OOC0` record carries up to eight queued or in-flight
critical-alert entries across a monotonic-clock restart. Each active entry
contains the exact 64-byte `OGA0` frame, queued or in-flight state, consumed
send-attempt count, remaining maximum lifetime, and remaining time until retry
readiness or acknowledgement timeout.

Absolute monotonic timestamps, prepared send tokens, raw keys, peer addresses,
free text, locations, and diagnostics are excluded. A caller-supplied nonzero
configuration fingerprint binds the record to the exact outbox policy before
later import is permitted.

## Wire layout

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OOC0` |
| 4 | 1 | version 0 |
| 5 | 1 | active entry count, 0–8 |
| 6 | 2 | zero |
| 8 | 4 | nonzero configuration fingerprint |
| 12 | 4 | zero |
| 16 | 608 | eight fixed 76-byte entries |
| 624 | 12 | zero |
| 636 | 4 | CRC-32 over bytes 0–635 |

Each entry contains active marker, state, attempt count, one reserved zero byte,
remaining lifetime, remaining action time, and the exact alert frame. Inactive
entries must be entirely zero. CRC detects accidental corruption only.

## Validation and evidence

Encoding rejects zero configuration fingerprint, unsupported state, zero
lifetime, action time beyond lifetime, in-flight entries with zero attempts,
malformed alert frames, and duplicate event IDs. Decoding additionally rejects
wrong size/magic/version, noncanonical padding/inactive slots, count mismatch,
and CRC failure. Caller output changes only after the candidate passes.

Seven host groups cover deterministic round trip, canonical empty state,
configuration/entry invariants, frame/event validation, shape/version/CRC/
padding rejection, count/atomicity, and inactive-slot canonicalization. The full
29-executable matrix and 100 focused repeats pass.

## Remaining gates

- Add atomic outbox export/import using remaining-time reconstruction and
  fail-closed handling of a prepared send.
- Coordinate this with the authorization-epoch-aware ACK replay checkpoint as
  one generation/transaction.
- Add recoverable two-slot target storage, readback verification, wear policy,
  power-loss injection, and restart composition.
- Add authenticated integrity and trusted rollback protection; CRC is neither.
