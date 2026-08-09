# Telemetry Publish Scheduler v0

Status: host-tested cooperative contract, 2026-08-09. This is not an ESP-IDF
task, physical RF measurement, or production update-rate selection.

## Purpose

The gateway needs to turn latest normalized cache state into bounded `OGT0`
packets without broadcasting every CAN frame or blocking newer data behind
retries. The v0 scheduler owns that selection policy while remaining separate
from the ESP-NOW adapter:

```text
normalized cache snapshot
        |
latest-signal coalescing + subscription/deadband/deadline policy
        |
prepare one OGT0 packet -> caller attempts nonblocking transport send
        |
commit local queue accepted/rejected
```

The scheduler never sleeps, performs radio I/O, waits for a delivery callback,
or treats a MAC result as an application acknowledgement. It is cooperative and
single-owner; a firmware target must serialize calls from its acquisition/radio
task boundary.

## Fixed limits

| Resource | v0 limit |
| --- | ---: |
| Gauge peers | 8 |
| Subscriptions per gauge | 8 |
| Latest signal states | 16 |
| Signals per packet | 3 (`OGT0` limit) |
| Per-gauge packet interval | 50-60,000 ms |

The hard 50 ms lower bound caps each gauge at 20 packets/s. At 96 bytes per
packet, that is at most 1,920 serialized payload bytes/s per gauge and 15,360
payload bytes/s across eight gauges. Those are application bytes only; physical
OG-010A testing must still measure MAC/encryption/retry/coexistence overhead.

An eight-signal initial sync is therefore spread over three packets and at
least 100 ms rather than emitted as an unbounded same-tick burst.

## Peer subscription policy

Each subscription fixes:

- one registered v0 signal code;
- minimum interval between accepted publications of that signal;
- maximum interval for periodic refresh;
- stale threshold; and
- raw canonical-unit deadband.

The maximum interval must be nonzero and at least the minimum interval. The
stale threshold must be nonzero. Duplicate or unknown signal codes fail peer
configuration. Peers keep independent subscription state and packet sequence
counters because unicast packets sent to another gauge must not appear as gaps.

## Due ordering and coalescing

Only the latest state of a signal is retained. Intermediate high-rate updates
may be coalesced before publication. When the per-peer packet interval permits
another packet, due entries are selected in this order:

1. never-published state or a quality/value-presence/type/unit transition;
2. numeric change meeting the raw deadband after the signal minimum interval;
3. maximum-interval periodic refresh.

Selection is stable in configured subscription order. At most three entries
are selected. After that plan is accepted and committed, remaining due entries
can fill the next rate-bounded packet. A quality transition bypasses the
per-signal minimum interval but not the 50 ms peer packet ceiling, so
stale/unavailable/error recovery is prompt without permitting a burst loop.

## Age and stale behavior

`update_signal` stores a cache snapshot plus the local observation time. At
prepare time, the scheduler adds local elapsed time to the snapshot's source
age. At the exact subscription stale threshold it changes valid/suspect to
`stale` and removes the numeric value, even if the cache has not supplied
another update. Ages beyond the packet's 32-bit range saturate at
`0xFFFFFFFF`; a value cannot remain valid that long because the configured
stale threshold is also 32-bit and nonzero.

Gateway uptime in the packet is derived from the configured local boot start.
It is diagnostic only and is not compared with a gauge's unsynchronized clock.

## Prepare/commit transaction

Only one plan may be pending for a peer.

1. `prepare(peer, now)` selects due entries, assigns the peer's current
   sequence, and returns a nonzero plan token and validated `TelemetryBatch`.
2. The caller encodes the batch and attempts one nonblocking transport send.
3. `commit(peer, token, accepted, now)` records the outcome.

If the local queue rejects the frame, the plan is released, publication state
does not advance, and the same sequence remains available for a later retry.
If the local queue accepts it, selected values/timestamps are recorded and the
peer sequence increments—even if the later radio delivery result reports loss.
This prevents blocking retries from starving fresher state. The gauge observes
the gap when a later sequence arrives, while the maximum refresh deadline makes
current state eligible again.

An update arriving while a plan is pending remains in the latest-signal table.
Commit records the exact planned values, so a changed latest value remains due
under its normal quality/deadband/deadline policy.

## Host evidence

Eight deterministic scenario groups cover:

1. lifecycle, invalid/duplicate policies, peer capacity, and removal;
2. signal validation, coalescing, semantic revision, and clock regression;
3. three-plus-one initial batching, packet ceiling, and sequence progression;
4. minimum interval, deadband, and exact maximum refresh;
5. exact stale, fresh recovery, and unavailable no-value transitions;
6. pending/token/clock rejection and same-sequence local retry;
7. independent peer subscriptions and sequence streams; and
8. encoded fake encrypted-unicast delivery, injected radio loss after local
   acceptance, and the receiver's exact one-packet gap.

The common peer-address validation helpers now live in production source rather
than the fake adapter, preventing a target binding from depending on test-only
definitions.

## Deferred work

- ESP-IDF task/queue and ESP-NOW adapter binding
- cache cursor polling and acquisition-task integration
- reviewed signal-specific rates/deadbands for a real vehicle and gauge layout
- subscription negotiation/capability format and authorization
- congestion feedback, diagnostics counters in production firmware, and any
  application acknowledgement policy
- physical multi-gauge latency, loss, interference, reboot, and power evidence
