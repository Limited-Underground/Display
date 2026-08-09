# Gateway Telemetry Loop v0

Status: host-tested cooperative composition, 2026-08-09. This is not an ESP-IDF
task, production firmware, physical CAN result, or ESP-NOW RF result.

## Responsibility

`GatewayTelemetryLoop` composes the already bounded interfaces for:

```text
passive CAN receive -> J1939 registry -> normalized cache
                                          |
                                          v
                              telemetry publisher -> ESP-NOW enqueue
```

It owns only sequencing and budgets. Concrete controller/transceiver setup,
ESP-NOW initialization and encrypted peer provisioning, keys, clocks, logging,
delivery-receipt consumption, watchdog policy, and task scheduling remain target
responsibilities.

## Start and stop

Start requires:

- a nonempty decoder registry;
- a valid nonzero gateway/session identity;
- a nonzero cache freshness threshold;
- a CAN-frame cycle budget from one through 16;
- a listen policy accepted by the CAN adapter.

The receiver starts first in listen-only mode, followed by the publisher. A
publisher-start failure stops the receiver again. After both succeed, the cache
is cleared so a prior boot's value cannot silently become current in a new
session. Stop takes the receiver and publisher offline and removes the loop's
peer-service list. Restart resets cumulative loop counters and again clears the
cache.

Radio peers and encryption material must already be provisioned in the
transport. Adding a peer to this loop adds only its normalized-signal
subscriptions and bounded service slot; it does not discover, trust, or key a
device.

## One cooperative cycle

Every call to `service(now_ms)` performs bounded work in this order:

1. Receive no more than the configured CAN frame budget.
2. Dispatch each frame through the J1939 registry into at most eight normalized
   signals.
3. Upsert decoded states into the 16-signal cache with the configured freshness
   threshold.
4. Poll the cache cursor once into the telemetry publication scheduler.
5. Attempt at most one nonblocking packet enqueue for each of at most eight
   configured gauge peers.
6. Call the cooperative transport service once.

The loop never sleeps, dynamically allocates, retries inside a cycle, or drains
an unbounded hardware queue. A saturated CAN queue is revisited on the next
cycle so radio and other target work still receives time.

## Failure behavior

Unsupported PGNs are normal counted traffic and do not stop the cycle. Invalid
identifiers, decoder failures, cache-write rejection, receiver faults,
cache-publisher faults, and local radio enqueue rejection are returned and
counted. The first abnormal stage identifies the cycle error, while later
bounded stages still run where safe.

In particular, an empty or bus-off CAN receiver does not prevent cache polling
and peer service. Previously valid telemetry therefore crosses its exact stale
boundary and is published as no-value/stale rather than remaining a plausible
frozen number. Local transport rejection retains the scheduler plan/sequence
for a later cycle; local acceptance advances it even if later radio delivery
fails.

Receiver status contributes current bus state and cumulative drop-newest
overflow. Loop status accumulates cycle, receive, decode, cache, packet, and
peer-service counts. These are a foundation for OG-017 diagnostics, not a
complete logging or persistence system.

## Host evidence

`tests/host/gateway_telemetry_loop_tests.cpp` covers nine groups:

1. configuration, start/rollback, stop, restart, and offline service;
2. fixed CAN drain budget, FIFO progress, and latest cache state;
3. counted unsupported PGNs and fail-visible invalid frames;
4. real EEC1 receive, decode, cache, encode, enqueue, fake-radio delivery, and
   packet decode;
5. unavailable EEC1 replacing a prior number with explicit no-value quality;
6. bus-off status while exact-boundary stale publication continues;
7. local transport rejection followed by same-sequence retry;
8. receiver queue overflow propagation into cycle and cumulative status;
9. stop/restart clearing cache, peer slots, and counters.

## ESP-IDF binding gate

The next adapter must preserve these budgets and semantics while proving:

- the selected ESP32 CAN peripheral/transceiver is electrically passive;
- ISR/driver queues deliver canonical timestamps and overflow/error state;
- the task period, priority, stack, watchdog, and worst-case execution time are
  measured on the selected gateway board;
- ESP-NOW callbacks cannot block or race the single-owner publisher;
- reset, bus-off, radio backpressure, disconnect, and brownout recovery work on
  device;
- attaching or failing OpenGauge does not alter vehicle operation.
