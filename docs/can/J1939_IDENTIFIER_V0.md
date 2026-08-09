# Classical J1939 Identifier Parser v0

Status: host-tested software contract, 2026-08-09. No CAN controller,
transceiver, vehicle, or captured vehicle traffic has been validated.

## Scope

The v0 parser accepts a CAN identifier plus an explicit standard/extended frame
marker. It decodes the Classical J1939 fields needed to route a frame to a
later PGN decoder:

- priority
- data page (DP)
- PDU format (PF)
- PDU specific (PS)
- source address (SA)
- PDU1/PDU2 classification
- destination address for PDU1 frames
- parameter group number (PGN)

It does not decode payload bytes, transport-protocol messages, SPNs,
proprietary data, or J1939-22 CAN FD.

## PGN rules

The parser applies the J1939 PDU boundary at `PF = 240`:

- PDU1 (`PF < 240`): PS is a destination address and the PGN's low byte is
  zero.
- PDU2 (`PF >= 240`): PS is the group extension and is included in the PGN.

DP becomes PGN bit 16. The resulting supported PGN range is `0x00000` through
`0x1FFFF`.

## Fail-closed inputs

The parser returns a typed error and no parsed identifier for:

- a standard 11-bit CAN frame
- a numeric identifier above the 29-bit maximum `0x1FFFFFFF`
- an identifier with extended data page/reserved bit 25 set

Rejecting bit 25 keeps the contract bounded to Classical J1939 and prevents a
J1939-22 frame from being silently interpreted under the older rules.

## Host evidence

`tests/host/j1939_identifier_tests.cpp` exercises five scenario groups:

1. PDU1 global request `0x18EAFF00`
2. PDU2 engine-temperature PGN `0x18FEEE2A`
3. the `PF = 0xEF`/`0xF0` boundary
4. a DP=1 PGN
5. standard, out-of-range, and extended-data-page rejection

Run all host suites with `tools/Test-Host.ps1`.

## Next boundary

OG-006 will consume the parsed PGN through an explicit decoder registry. That
work must preserve unavailable/error encodings and begin with synthetic or
legally obtained captured frames before any listen-only vehicle test.
