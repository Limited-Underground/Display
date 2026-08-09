# Diagnostics Foundation v0

Status: deterministic host-tested structured core, 2026-08-09. This is not an
ESP-IDF logger, console format, persistent crash recorder, remote telemetry
service, concurrency claim, or production redaction audit.

## Boundary

`DiagnosticsService` is a fixed-memory health/event boundary shared by future
gateway and gauge compositions. It owns:

- a 32-record oldest-first event ring;
- error, warning, info, debug, and trace threshold filtering;
- monotonic event timestamps and 64-bit event sequences;
- typed event/metric/reset enums with one signed numeric metric;
- 16 saturating subsystem counters;
- all-or-nothing event snapshots.

The API accepts no strings, byte buffers, radio addresses, credentials, vehicle
identifiers, or arbitrary structured payloads. This reduces accidental secret
exposure, but adapter call sites and any eventual rendered/serialized output
still require a redaction review.

## Event behavior

Startup captures a typed reset reason and increments the internal reset
counter. Its informational event is legitimately filtered if the selected
threshold is warning or error; filtered events do not consume sequence numbers
or ring capacity. They do advance the accepted monotonic clock, so a later
event cannot move time backward merely because an earlier one was filtered.

When the ring is full, a new enabled event replaces exactly the oldest record,
advances the first sequence, and saturating-increments `log_records_dropped`.
Snapshots require space for the complete retained ring and leave caller output
untouched on insufficient capacity. Clearing events preserves counters and the
next sequence so a local UI clear cannot disguise accumulated health state.

`MetricCode::none` canonically requires value zero. More specific meanings use
the bounded count, duration, queue-depth, error, state, reset-reason, or age
metric enum. Values have no implicit unit beyond that enum.

## Counters

The fixed registry currently covers:

- received CAN frames, CAN errors, and CAN overflow;
- unknown and invalid decoder input;
- cache stale transitions;
- accepted/failed wireless sends;
- accepted/rejected wireless receives and sequence gaps;
- render updates and deadline misses;
- update failures;
- reset count and overwritten log records.

All counters saturate at `uint32_t` maximum rather than wrapping. Reset count
and overwritten-record count are service-owned and cannot be incremented by a
caller.

## Host evidence

`tests/host/diagnostics_tests.cpp` covers eight groups:

1. lifecycle and configuration validation;
2. exact level filtering and monotonic-time rejection;
3. ring wrap, oldest-first order, sequences, and drop accounting;
4. counter saturation and service-owned counters;
5. reset-reason startup plus canonical typed records;
6. atomic snapshot capacity and clear behavior;
7. restart state/clock reset;
8. fixed, trivially copyable, pointer-free event payload.

The suite also repeated 100 times with zero failures.

## Remaining gates

- bind counters/events to CAN, decoder, cache, wireless, display, update, and
  reset adapters without changing their ownership or blocking behavior;
- define task/concurrency ownership and measure worst-case logging time;
- define release/debug build filtering and an operator-visible formatter;
- audit every adapter mapping and output for secrets, identifiers, location,
  raw traffic, and privacy-sensitive values;
- design bounded persistence/crash retention, export authorization, rate
  limits, wear behavior, corruption recovery, and reset policy;
- test on-device reset reasons, rollover, brownout, full-ring load, and recovery.
