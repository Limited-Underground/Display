# Gauge Layout Canonical Export v0

Status: deterministic host-tested storage boundary, updated 2026-08-12. This is
not a file writer, download endpoint, removable-media adapter, access-control or
confidentiality policy, signature, encryption mechanism, or physical-device
result.

## Selection

`GaugeLayoutStore::export_current` uses the same two-slot selection as normal
boot load. It returns the complete `GaugeLayoutLoadResult` with:

- selected safe-default/slot source;
- slot A and slot B states; and
- recovery-required state.

If storage is known empty or corrupt, the separately validated compiled safe
default can be exported. If exactly one slot is valid and the other is known
empty/invalid, that valid record can be exported with recovery visibly required.

## Fail-closed behavior

No output bytes change when:

- the pointer or capacity is invalid;
- the supplied safe default is invalid;
- two different records claim the same generation; or
- either slot is unreadable.

An older valid slot beside an unreadable peer does not authorize export because
the unreadable slot could contain a newer committed generation. The load result
still preserves the usable source and recovery evidence for operator handling,
but `exported()` remains false.

## Record

Success encodes exactly one 576-byte canonical `OGL0` record through the same
schema-v1 codec used by storage and local import. The record includes its
selected generation and full validated layout. CRC-32 detects accidental
corruption only; it provides no authenticity or confidentiality.

The boundary only fills caller-owned memory. A target file/download adapter
must separately define destination ownership, overwrite behavior, access,
privacy, space/error handling, interruption cleanup, and user-visible status.

## Evidence

The cumulative thirteenth layout group proves:

- insufficient capacity preserves a sentinel-filled buffer;
- empty storage exports a validated safe-default record and recovery evidence;
- a corrupted newer slot exports the prior valid slot with recovery required;
- unreadable peer-slot uncertainty fails without changing output;
- equal-generation conflict fails without changing output; and
- invalid safe default fails without changing output.

The focused suite passes 100/100 repeats plus the complete 47-executable host
matrix including publication safety. File/download binding,
access/confidentiality policy, and physical target behavior remain required.

The separate [transfer proof](GAUGE_LAYOUT_TRANSFER_V0.md) composes this
memory-level output with confirmed local import on an independent store. It
does not add file transport or trust.
