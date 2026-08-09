# Telemetry Gateway Publisher v0

Status: host-tested composition, 2026-08-09. This connects existing host
contracts; it is not an ESP-IDF target, CAN acquisition loop, or physical-radio
result.

## Purpose

The gateway publisher composes four independently tested boundaries into one
cooperative service path:

```text
TelemetryCache.collect_changes(cursor, now)
       |
registered signal mapping; unknown IDs skipped
       |
TelemetryPublishScheduler.prepare(peer, now)
       |
encode fixed 96-byte OGT0 packet
       |
EspNowTransport.send(peer, packet, now)
       |
commit(local queue accepted or rejected)
```

CAN acquisition/upsert, peer/key provisioning, transport `service()`, delivery
callback polling, logging, and firmware task ownership remain outside this
component.

## Cache cursor behavior

The first poll starts at generation zero of the cache's current epoch, giving a
bounded full latest-state sync. Later polls request only states changed after
the saved cursor. The cache and output array both have capacity 16, so a full
sync remains allocation-free and bounded.

Only the four codes in the v0 wire registry are copied to the scheduler.
Otherwise-valid normalized signals remain in the cache but are counted as
skipped and never assigned an invented wire code. The cursor still advances, so
an unregistered signal does not cause an infinite polling loop.

When `TelemetryCache.clear()` changes the epoch, the publisher:

1. detects the cursor epoch mismatch;
2. clears the scheduler's latest source states;
3. invalidates each subscription's last-publication baseline while preserving
   peer configuration and packet sequence;
4. begins a bounded full sync at generation zero of the new epoch.

If the new cache is empty, no unavailable value is invented or transmitted;
gauges age their last packet state stale. When even the same numeric state is
inserted in the new epoch, it is first-state due and publishes promptly after
the peer packet floor. An epoch reset refuses to proceed while a scheduler plan
is pending, preventing an already prepared old-epoch packet from being silently
reclassified.

## One peer service attempt

`service_peer()` does at most one unit of work and never sleeps:

- no due scheduler plan returns `no_data`;
- a prepared batch is encoded into an exact 96-byte stack buffer;
- one transport `send()` is attempted;
- local rejection releases the plan without advancing sequence/publication;
- local acceptance records the exact planned state and advances that peer's
  sequence once.

The transport contract copies an accepted payload into its bounded local queue,
so the stack buffer need not outlive the call. A later MAC loss remains a
delivery result, not a reason for this service call to block or roll sequence
back. The gauge detects the gap and the publisher's periodic refresh makes
latest state eligible later.

## Result evidence

Cache polling reports collected, registered-updated, unregistered-skipped, and
epoch-change counts plus the underlying cache/scheduler error when applicable.
Peer service reports packet sequence, encoded bytes, local transport token,
queue acceptance, and the exact scheduler/codec/transport failure layer.

Seven deterministic scenario groups cover:

1. lifecycle, initial full sync, registered mapping, unregistered skip, and
   unchanged cursor poll;
2. cache clear/epoch reset, empty-state silence, and immediate same-value reload;
3. offline local transport rejection followed by same-sequence queue retry;
4. four-signal initial sync paced as three then one across the 50 ms peer floor;
5. exact stale/no-value publication without another cache poll; and
6. cache update through encoded encrypted fake delivery, accepted-but-injected
   radio loss, later sequence delivery, and exact receiver gap detection; and
7. the actual EEC1/SPN 190 decoder output `engine.speed` flowing through cache,
   registered wire-code mapping, packet encode, fake radio delivery, and gauge
   decode without an ID alias or manual translation.

## Deferred target work

- select an ESP-IDF task owner and bounded wake/poll cadence
- connect real decoder/cache writes and ESP-NOW adapter service
- add production diagnostics for cursor resets, skipped IDs, local queue
  rejection, delivery loss, and refresh latency
- provision authorized peers/keys before traffic
- measure CPU, lock time, queue latency, RF behavior, current, and coexistence on
  selected gateway/display boards
