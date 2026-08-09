# J1939 Decoder Registry and EEC1 Fixture v0

Status: host-tested proof-of-concept contract, 2026-08-09. This is not a
supported-vehicle decoder catalog.

## Registry boundary

The fixed-capacity registry maps a canonical Classical J1939 PGN to an
explicit decoder function. It deliberately avoids dynamic registration,
unbounded allocation, silent replacement, and wildcard dispatch.

- capacity is eight PGN decoders in v0
- duplicate PGNs fail closed
- PGNs above `0x1FFFF` fail closed
- a PDU1 PGN with a nonzero low byte fails closed because that byte would be a
  destination address, not part of the PGN
- standard CAN frames, invalid 29-bit IDs, J1939-22 EDP frames, payloads over
  eight bytes, unknown PGNs, insufficient output capacity, and malformed
  decoder output return typed errors
- every decoder output is revalidated against the normalized signal model
  before the dispatch result is accepted

The caller must disregard output storage whenever dispatch returns an error.

## Narrow EEC1 reference fixture

The first fixture handles only engine speed (SPN 190) in EEC1 (PGN 61444). It
requires an eight-byte payload and extracts the two-byte little-endian field at
payload offsets 3 and 4. Valid raw values use 0.125 rpm/bit and are emitted as
integer milli-rpm.

Special raw ranges never become numbers:

| Raw range | Normalized quality | Value present |
| --- | --- | --- |
| `0x0000`-`0xFAFF` | `valid` | yes |
| `0xFB00`-`0xFDFF` | `out_of_range` | no |
| `0xFE00`-`0xFEFF` | `error` | no |
| `0xFF00`-`0xFFFF` | `unavailable` | no |

The output source records the parsed source address, PGN 61444, SPN 190, and
receive time.

## Standards and evidence boundary

SAE's J1939 Digital Annex is the normative catalog for current PGN/SPN fields,
including resolution, offset, range, and units. The repository does not embed
or redistribute that licensed dataset. The EEC1 fixture is intentionally small
and must be reconciled with a licensed/current annex plus legally obtained
captured traffic before any vehicle-support claim. See the
[SAE J1939 Digital Annex page](https://saemobilus.sae.org/standards/j1939da_202506-j1939-digital-annex).

## Host evidence

`tests/host/j1939_decoder_tests.cpp` exercises five scenario groups:

1. null/out-of-range/noncanonical/duplicate/full registration
2. valid engine speed and the highest valid boundary
3. out-of-range, error, and unavailable encodings
4. standard/EDP/malformed-length/output-capacity rejection
5. unknown PGN and malformed decoder-output rejection

Run all host suites with `tools/Test-Host.ps1`.

## Next boundary

Additional decoders require an explicit data-source/version note and their own
golden fixtures. OG-008 can now consume valid and nonnumeric normalized outputs
without depending on J1939 payload layout.
