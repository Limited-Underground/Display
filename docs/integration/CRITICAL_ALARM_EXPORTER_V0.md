# Alarm-to-Critical-Alert Exporter v0

Status: host-tested semantic composition, 2026-08-09. This does not select or
validate a physical transport, peer trust, replay protection, delivery, LoRa
airtime, vehicle rule, or emergency workflow.

## Boundary

`CriticalAlarmExporter` maps an allowlisted subset of local normalized alarm
transitions to the existing fixed 64-byte OpenGauge-to-OpenTrail critical-alert
frame. It has at most 16 rule mappings and no dynamic allocation.

The exporter never receives raw CAN/J1939 data. It does not export every local
warning and cannot cause vehicle control. Mapping is explicit by nonzero alarm
rule ID to one critical alert type, critical/emergency severity, and optional
stable numeric diagnostic code.

## Exportable lifecycle

Only these local events cross the boundary:

- `asserted` while lifecycle is active and the condition is present;
- final `cleared` while lifecycle is inactive and the condition is absent.

Reminder, acknowledgement, and `condition_cleared_latched` events remain local.
A latching alarm is not remotely cleared until final acknowledgement/clear.
Duplicate assertion, clear without an active exported condition, malformed
lifecycle flags, and unmapped rules fail without consuming IDs.

Each successful assertion allocates a nonzero condition ID. Its successful
clear uses the same condition ID. Every successful transition receives a
different nonzero event ID. IDs are caller-provisioned opaque sequences; they
must not encode a VIN, MAC/Bluetooth address, user identity, or unkeyed hash of
one. Exhaustion fails closed and requires a reviewed new session/identity plan.

## Quality and units

The OpenTrail v0 contract permits an assertion only from valid or suspect data.
Local alarms intentionally asserted because input is stale, unavailable, error,
out-of-range, or unknown remain local: they cannot be misrepresented as a
valid remote vehicle condition. A final clear may carry unavailable/error
quality with no numeric value.

Normalized values convert as follows:

| OpenGauge normalized unit | Critical-alert milli-unit | Conversion |
| --- | --- | --- |
| `milli_celsius` | degrees Celsius | unchanged |
| `pascal` | kilopascals | unchanged because 1 Pa = 1 milli-kPa |
| `millivolt` | volts | unchanged |
| `milli_percent` | percent | unchanged |
| `milli_revolutions_per_minute` | RPM | unchanged |
| Boolean with unit `none` | Boolean | `0/1` becomes `0/1000` |

Count, milliampere, and millimetres/second have no v0 critical-alert unit and
fail if a value is present. Numeric output must fit signed 32-bit and then pass
the existing alert codec's type-specific range/unit validation. Codec failure
does not commit condition state or advance either ID.

## Time and identity

Start requires nonzero opaque producer/vehicle IDs and nonzero first event and
condition IDs. The exporter currently emits no UTC field. It derives mandatory
age from one monotonic clock and accepts `0 <= age <= 120000 ms`; future-time or
older events fail without state change.

Stop removes active lifecycle state. A restart requires a new explicitly
provided identity sequence. Production code must not restart into IDs that can
collide with a receiver's retained duplicate window.

## Host evidence

`tests/host/critical_alarm_exporter_tests.cpp` covers seven groups:

1. mapping/type/severity/identity validation, duplicate/capacity, and lifecycle;
2. assertion/clear encoding and independent decode with unique/stable IDs and
   derived age;
3. local-only reminder/ack/latch events not consuming identity;
4. nonvalid assertion and malformed lifecycle/value rejection;
5. all six canonical unit conversions plus unsupported units;
6. type/unit codec rejection without lifecycle or ID commit;
7. clear-before-assert, duplicate assert, monotonic/freshness bounds, restart,
   and identity exhaustion.

## Remaining integration gates

- feed allowlisted events from the cache/alarm evaluator into this exporter
  with reviewed per-vehicle mappings;
- choose a framed serial or authenticated local-wireless adapter and bind its
  producer authorization to the opaque producer ID;
- add replay protection, key lifecycle, delivery/backpressure policy, restart
  ID persistence, and redacted diagnostics outside the semantic frame;
- run the mirrored OpenTrail ingress over that physical path and measure loss,
  delay, reboot recovery, and LoRa rate behavior;
- preserve the independent OpenTrail trust, freshness, duplicate, and rate
  checks; exporter acceptance is never an emergency-delivery guarantee.
