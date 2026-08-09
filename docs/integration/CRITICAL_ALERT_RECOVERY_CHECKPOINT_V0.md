# Coordinated Critical Alert Recovery Checkpoint v0

Status: canonical generation envelope plus serialized live dual export/import
with deterministic host evidence, 2026-08-09. Recoverable storage, physical
power-loss behavior, authenticated integrity, and rollback resistance are not
yet implemented.

## Boundary

The fixed 960-byte `OCR0` record keeps one exact 280-byte `OAI0` ACK
authorization/session/replay checkpoint and one exact 640-byte `OOC0` outbox
checkpoint under a single nonzero caller-owned generation. This prevents a
future store from independently selecting replay state from one save and
queued/in-flight delivery state from another.

It stores no raw keys, addresses, PINs, free text, vehicle identifiers,
locations, or absolute monotonic timestamps. `OAI0` authorization epochs and
`OOC0`'s derived policy fingerprint remain the compatibility boundaries.

## Wire layout

| Offset | Size | Meaning |
| ---: | ---: | --- |
| 0 | 4 | ASCII `OCR0` |
| 4 | 1 | version 0 |
| 5 | 1 | header size, 24 |
| 6 | 2 | ACK checkpoint size, 280 |
| 8 | 8 | nonzero generation |
| 16 | 2 | outbox checkpoint size, 640 |
| 18 | 6 | zero |
| 24 | 280 | exact `OAI0` bytes |
| 304 | 640 | exact `OOC0` bytes |
| 944 | 12 | zero |
| 956 | 4 | CRC-32 over bytes 0-955 |

Encoding validates nonzero generation, `OAI0` identity/version/inner CRC, and
the complete `OOC0` decoder before changing caller output. Decoding validates
size, magic, version, declared nested sizes, canonical padding, outer CRC, and
both nested checkpoints before atomically replacing caller output.

## Evidence and remaining gates

Four codec groups cover deterministic layout/round trip, generation and nested
input rejection with unchanged output, outer shape/version/padding/CRC, and
nested tamper with atomic decode. Five live-coordinator groups cover exact
same-generation export/restore, invalid/stopped export atomicity, prepared-send
refusal, policy mismatch, authorization-epoch mismatch, corruption, exact
restored retry readiness, and replay preservation. The full 31-executable
matrix and 100 focused coordinator repeats pass.

The coordinator requires exclusive ownership of ingress and outbox for the
operation. Export changes caller output only after both component exports and
outer encoding succeed. Import decodes once, then runs each exact boot import
against a private component copy. Only after both preflights succeed does it
restore the outbox and ACK replay binding from those same bytes. No concurrent
component mutation is permitted between preflight and commit.

Next place `OCR0` in a verified two-slot store with newest-unique-generation
selection and interruption injection. Target storage, power-cut/wear evidence,
secure integrity, and trusted rollback protection remain separate gates.
