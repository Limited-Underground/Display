# Gauge Layout Local Import v0

Status: deterministic host-tested semantic boundary, updated 2026-08-12. This
is not a file picker, removable-media adapter, network upload endpoint,
authorization policy, authenticity mechanism, renderer, or physical-device
result.

## Accepted input

`GaugeLayoutChangeWorkflow::stage_import_record` accepts exactly one
`kGaugeLayoutRecordBytes` (576-byte) `OGL0` schema-v1 record. The existing
decoder validates exact length, magic, schema version, canonical reserved and
unused bytes, CRC-32, layout fields, widget fields, and duplicate widget IDs.
Its typed codec error is returned unchanged.

CRC-32 detects accidental corruption only. It does not authenticate the record
or authorize whoever supplied it.

## Preview and staging

Successful decode returns a bounded structural summary:

- source generation;
- layout ID;
- theme;
- brightness; and
- widget count.

Widget labels and the raw record are not copied into that summary. The decoded
layout is staged internally under the same one-pending, strictly increasing
request-ID and exact-time confirmation rules as any other layout change.
Staging performs no storage write.

The source generation is informational. Exact confirmation calls the existing
store-owned update path, which compares canonical content and allocates the
normal next local generation. An imported generation cannot force rollback,
skip ahead, or exhaust local generation allocation.

## Failure behavior

- Failed decode does not stage anything or consume the request ID.
- Failed decode returns a read-only current workflow projection.
- Corrupt input cannot replace or cancel an existing live prompt.
- Mismatched, expired, cancelled, replayed, or clock-regressed confirmation
  remains governed by the existing workflow.
- Persistence failure and commit uncertainty retain their existing distinct
  retry/restart behavior.

## Evidence

The cumulative tenth workflow group proves:

- short input returns `invalid_argument`, causes no write, and leaves the same
  request ID available;
- a valid exact record returns its bounded summary and a live confirmation
  prompt with no stage-time write;
- checksum-corrupt input cannot disturb that prompt;
- exact confirmation writes one local generation with zero erases; and
- a source generation of `UINT64_MAX` becomes local generation 1 on empty
  storage and survives restart.

The strict focused suite passes 100/100 repeats plus the complete 47-executable
host matrix including publication safety. Source authorization/authenticity,
target file/UI adapters, physical input, and on-device storage remain required.
