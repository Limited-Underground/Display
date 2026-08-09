# Gauge Telemetry Receiver v0

Status: host-tested bounded composition, 2026-08-09. This is not an ESP-IDF
radio binding, physical RF result, provisioned key, display implementation, or
supported gauge board claim.

## Boundary

`GaugeTelemetryReceiver` is the display-side counterpart to the gateway
publisher. It consumes explicit 96-byte `OGT0` datagrams from an already
provisioned ESP-NOW transport and maintains a fixed latest-state store for at
most 16 signals.

The receiver does not discover or pair peers, persist keys, configure Wi-Fi,
render widgets, acknowledge application delivery, or compare unsynchronized
gateway and gauge clocks. The target must provision the radio peer and
encryption material before starting this composition.

## Admission policy

Configuration fixes one expected unicast gateway address, nonzero encoded
gateway ID, channel 1-14, and a drain budget from one through four datagrams per
cycle. Every datagram must satisfy all of these independent checks:

1. receive metadata source equals the provisioned gateway peer;
2. receive metadata says the unicast link was encrypted;
3. receive metadata channel equals the configured channel;
4. the packet passes magic/version/length/reserved/CRC/signal validation;
5. the encoded opaque gateway ID equals the provisioned gateway identity;
6. the per-gateway session/sequence tracker accepts forward progress.

Failure at one stage is counted and cannot mutate stored signals. Authentication
strength still depends on the future ESP-IDF adapter, protected key storage,
pairing/revocation workflow, and replay policy outside this codec.

## Bounded cooperative service

One service call advances the transport once and drains no more than the
configured datagram budget. Malformed or unauthorized traffic consumes budget,
preventing hostile input from creating unbounded CPU work. Remaining queued
datagrams are reported and handled on a later cycle.

An accepted packet atomically replaces the latest state for each of its one to
three signal codes. Duplicate and out-of-order sequences do not mutate the
store. Forward gaps update state but count the exact number of missing packets;
periodic refresh and explicit staleness, not blocking retransmit, recover the
display.

A boot-session change clears every old stored signal before applying the new
packet. Signals absent from the first new-session packet therefore become
missing rather than appearing current from an earlier gateway boot. Capacity
preflight treats this clear as part of the same bounded transition.

## Receiver-local freshness

Each stored entry retains:

- explicit wire signal code, canonical type/unit/value/quality;
- source age measured by the gateway;
- gauge-local packet receive time;
- gateway boot session and packet sequence.

Read adds receiver-local elapsed time to source age using saturating arithmetic.
At the exact `age >= stale_after` boundary, a valid/suspect value becomes
quality `stale`, numeric presence is removed, and raw value is zero. This works
even when the gauge has just booted and the source age exceeds its own uptime;
no subtraction into an impossible local timestamp is required.

Invalid stale threshold and receiver-clock regression are explicit read errors.
A caller must never keep rendering an older successful read after a new read
reports missing, stale, or error state.

## Diagnostics

Cycle and cumulative status count datagrams, accepted packets, signal updates,
unauthorized sources, unencrypted traffic, channel and encoded-identity
mismatches, malformed packets, stream failures, duplicates, out-of-order
packets, gaps/missing packets, session resets, and current signal count. These
are host contracts, not measured embedded counter cost or persistent logs.

## Host evidence

`tests/host/gauge_telemetry_receiver_tests.cpp` covers eight groups:

1. configuration/lifecycle bounds, empty service, missing/offline reads;
2. authorized encrypted fake-radio delivery and exact receiver-local freshness;
3. metadata source, encryption, channel, and encoded gateway identity checks;
4. malformed packet rejection without store mutation;
5. duplicate, exact gap/missing count, and out-of-order behavior;
6. boot-session reset clearing signals absent from the new session;
7. two-packet cycle budget preserving a third queued datagram;
8. zero stale threshold and receiver-clock regression read failures.

## Hardware gates

- bind receive/callback queues to ESP-IDF with one owner and measured task time,
  stack, queue depth, loss, and watchdog behavior;
- provision, replace, revoke, and recover encrypted peers without logging keys;
- characterize channel coexistence, interference, peer count, update rate,
  latency, loss, reboot/session recovery, and backpressure on selected boards;
- feed reads into a view model that makes missing/stale/error conspicuous;
- verify a gauge reboot or failure cannot affect gateway acquisition or other
  gauges.
