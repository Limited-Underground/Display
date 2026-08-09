# Normalized Signal Model v0

Status: host-tested software contract, 2026-08-09. No production decoder,
cache, display, or physical data source uses this model yet.

## Purpose

OpenGauge consumers should not need to know whether a value originated in
J1939, generic OBD-II, GPS, or a test fixture. The v0 model creates one bounded
contract for typed values, units, quality, provenance, and time.

## Signal identity

Signal IDs are fixed-capacity, lowercase, namespaced strings such as
`engine.speed` or `engine.coolant_temperature`.

- maximum length: 47 bytes
- first character and each segment's first character: `a` through `z`
- remaining characters: lowercase letters, digits, or underscore
- at least one `.` namespace separator
- empty segments and trailing separators are invalid

The fixed capacity avoids unbounded allocation in the embedded core. A later
registry will version the actual supported names.

## Values and units

Values are Boolean, signed integer, or unsigned integer. Physical quantities
use integral canonical units to avoid floating-point and serialization
ambiguity. Examples:

| Quantity | Unit | Example representation |
| --- | --- | --- |
| Temperature | milli-celsius | 87.5 C = `87500` |
| Voltage | millivolt | 13.8 V = `13800` |
| Percent | milli-percent | 50% = `50000` |
| Engine speed | milli-rpm | 1250 rpm = `1250000` |
| Speed | millimetres/second | 10 m/s = `10000` |

The current numeric bounds are defensive model limits, not promises about a
specific vehicle or sensor. A decoder must still enforce the SPN/PID's own
validity and unavailable/error encodings before normalization.

## Quality contract

Quality is one of `valid`, `suspect`, `unavailable`, `error`, `out_of_range`,
`stale`, or `unknown`.

- `valid` and `suspect` require a typed value.
- every other quality carries no value.
- a Boolean value is exactly zero or one and has no physical unit.
- an unsigned value cannot be negative.

This prevents invalid wire encodings from becoming plausible gauge readings.

## Source metadata

Every signal records a source protocol and bus index. Protocol-specific fields
fail closed:

- J1939 requires source address, Classical PGN, and SPN.
- OBD-II requires a parameter identifier and forbids J1939 metadata.
- GPS and synthetic sources forbid parameter metadata in v0.

This is provenance, not transport security or sensor trust.

## Time and freshness

`received_at_ms` is always present. A separate sample time is optional but
cannot be later than receive time. Freshness uses sample time when present and
receive time otherwise.

At the exact boundary `age >= stale_after`, a `valid` or `suspect` signal is
reported as `stale`. A zero threshold and a clock earlier than the reference
time are explicit errors. Freshness evaluation does not mutate the stored
signal.

## Host evidence

`tests/host/normalized_signal_tests.cpp` exercises six scenario groups:

1. namespaced ID grammar and capacity
2. valid integer and Boolean signals
3. value/quality/type/unit consistency
4. protocol-specific source metadata
5. sample/receive time consistency
6. exact freshness boundaries and clock regression

Run all host suites with `tools/Test-Host.ps1`.

## Next boundary

OG-006 will produce these values from a small, explicit PGN decoder fixture.
OG-008 will own capacity, per-signal stale thresholds, transitions,
subscriptions, and restart behavior.
