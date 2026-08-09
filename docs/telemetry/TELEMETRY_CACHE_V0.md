# Telemetry Cache v0

Status: host-tested in-memory component, 2026-08-09. No ESP32 target,
persistent storage, wireless publisher, or display consumer is connected.

## Purpose and limits

The cache holds the latest normalized state for up to 16 signal IDs. It is a
bounded state store, not a history database or alarm/event log. Every public
operation is protected by one mutex so concurrent host writers cannot expose a
partially updated signal.

The current `std::mutex` implementation is a proof-of-concept concurrency
boundary. A firmware target must build and measure it under the selected
ESP-IDF/toolchain before the implementation is accepted for that target.

## Writes

`upsert` revalidates every signal and requires a nonzero per-signal stale
threshold.

- a new ID is inserted if capacity remains
- a newer receive timestamp updates the complete signal atomically
- an exact duplicate at the same receive timestamp is accepted as unchanged
  without incrementing the generation
- different content or threshold at the same timestamp is a conflict
- an older receive timestamp is rejected as out of order
- a 17th ID fails with `capacity_full`; nothing is evicted silently

This makes concurrent final state deterministic: for one ID, the greatest
accepted receive timestamp wins. Multi-source arbitration and sample-time
ordering are not part of v0.

## Reads and staleness

A read returns a copy of the stored signal plus its effective quality, age, and
last state generation. It evaluates freshness using the normalized model's
sample/receive time rule. At `age >= stale_after`, a valid or suspect signal is
reported stale. The stored input is not rewritten into a fake value.

A clock earlier than the signal's freshness reference returns a typed error.

## Change cursors

`collect_changes` provides bounded latest-state synchronization for polling
consumers:

1. The caller supplies its previous epoch/generation cursor.
2. The cache materializes any fresh-to-stale transitions at the supplied time.
3. It returns the latest snapshot for each ID changed after the cursor.
4. The caller advances to `next_cursor` only after a successful collection.

This is not an event log: repeated intermediate values may collapse into the
latest state. If the output array is too small, collection fails explicitly and
the old cursor can be retried. Snapshot array order is not a protocol promise.

`clear` removes all signals, resets the generation, and changes the cursor
epoch. An old subscriber receives `cursor_epoch_mismatch` and must perform a
full sync. Cursors are process-local and must not be persisted across a device
restart.

## Host evidence

`tests/host/telemetry_cache_tests.cpp` exercises six scenario groups:

1. insert/update/duplicate/conflict/out-of-order rules
2. invalid input and fixed capacity
3. exact fresh/stale/clock-regression reads
4. state cursors and materialized stale transitions
5. clear, epoch invalidation, and empty restart behavior
6. four concurrent writers converging on the greatest timestamp

The J1939 decoder suite adds an integration scenario: a valid EEC1 engine-speed
signal enters the cache, crosses the stale boundary, and is then replaced by an
explicit unavailable signal with no numeric value.

Run all host suites with `tools/Test-Host.ps1`.

## Next boundary

Before firmware use, measure locking and copy cost on the selected ESP32-S3,
decide task ownership, and test sustained update/read load. ESP-NOW
subscriptions and persistent/history storage remain separate components.
