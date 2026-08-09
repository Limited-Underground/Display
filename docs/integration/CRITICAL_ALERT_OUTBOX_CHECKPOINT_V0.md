# Critical Alert Outbox Checkpoint v0

Status: canonical codec and atomic live-outbox import/export with deterministic
host evidence, 2026-08-09. This is not yet stored durably, coordinated with ACK
replay/authorization state, authenticated, or rollback-resistant.

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

## Live outbox integration

`CriticalAlertOutbox::export_checkpoint` advances the live monotonic clock and
exports only unambiguous queued/in-flight state. It preserves the exact frame,
attempt count, remaining event lifetime, queued backoff, or in-flight ACK
timeout. A prepared send has an unresolved local adapter outcome and therefore
fails closed instead of persisting its process-local token. Zero fingerprints,
expired entries, and timing policies larger than the v0 32-bit fields are also
refused without changing caller output.

`import_checkpoint` is boot-only: the outbox must be running but must not have
accepted a clock value or entry. Decode and policy validation finish into a
candidate array before any live state changes. The expected nonzero
configuration fingerprint must match exactly. Import reconstructs elapsed
lifetime and in-flight state age against the caller's new monotonic origin, so
an ACK timeout, retry deadline, and maximum lifetime occur after their exact
persisted remaining duration rather than being restarted.

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

Seven codec groups cover deterministic round trip, canonical empty state,
configuration/entry invariants, frame/event validation, shape/version/CRC/
padding rejection, count/atomicity, and inactive-slot canonicalization. Five
integration groups cover queued retry, in-flight ACK timeout, maximum lifetime,
boot-only atomic corruption/fingerprint handling, prepared-send refusal, and
unrepresentable timing policy. The full 29-executable matrix and 100 focused
repeats pass.

## Remaining gates

- Coordinate this with the authorization-epoch-aware ACK replay checkpoint as
  one generation/transaction.
- Add recoverable two-slot target storage, readback verification, wear policy,
  power-loss injection, and restart composition.
- Add authenticated integrity and trusted rollback protection; CRC is neither.
