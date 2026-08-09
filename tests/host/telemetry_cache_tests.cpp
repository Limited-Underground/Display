#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "opengauge/telemetry_cache.hpp"

namespace {

using namespace opengauge::telemetry;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

NormalizedSignal signal_at(
    std::string_view id,
    std::uint64_t received_at_ms,
    std::int64_t value) {
    NormalizedSignal signal{};
    const auto id_error = make_signal_id(id, signal.id);
    if (id_error != SignalModelError::none) {
        return signal;
    }
    signal.value = {SignalValueType::signed_integer, value, true};
    signal.unit = SignalUnit::count;
    signal.quality = SignalQuality::valid;
    signal.source.protocol = SignalSourceProtocol::synthetic;
    signal.received_at_ms = received_at_ms;
    return signal;
}

void test_insert_update_duplicate_and_ordering() {
    TelemetryCache cache{};
    auto signal = signal_at("engine.speed", 100, 1);

    auto result = cache.upsert(signal, 1000);
    EXPECT(result.accepted());
    EXPECT(result.disposition == CacheWriteDisposition::inserted);
    EXPECT(result.cursor.generation == 1);
    EXPECT(cache.size() == 1);

    result = cache.upsert(signal, 1000);
    EXPECT(result.accepted());
    EXPECT(result.disposition == CacheWriteDisposition::unchanged);
    EXPECT(result.cursor.generation == 1);

    signal.value.raw_value = 2;
    result = cache.upsert(signal, 1000);
    EXPECT(result.error == CacheError::conflicting_timestamp);

    signal = signal_at("engine.speed", 99, 3);
    result = cache.upsert(signal, 1000);
    EXPECT(result.error == CacheError::out_of_order);

    signal = signal_at("engine.speed", 101, 4);
    result = cache.upsert(signal, 2000);
    EXPECT(result.accepted());
    EXPECT(result.disposition == CacheWriteDisposition::updated);
    EXPECT(result.cursor.generation == 2);

    const auto read = cache.read("engine.speed", 101);
    EXPECT(read.found());
    EXPECT(read.snapshot.signal.value.raw_value == 4);
    EXPECT(read.snapshot.generation == 2);
}

void test_validation_and_capacity_fail_closed() {
    TelemetryCache cache{};
    auto invalid = signal_at("engine.speed", 1, 1);
    invalid.id = {};
    auto result = cache.upsert(invalid, 1000);
    EXPECT(result.error == CacheError::invalid_signal);
    EXPECT(result.signal_error == SignalModelError::invalid_signal_id);

    result = cache.upsert(signal_at("engine.speed", 1, 1), 0);
    EXPECT(result.error == CacheError::invalid_stale_threshold);

    for (std::size_t index = 0; index < kTelemetryCacheCapacity; ++index) {
        const auto id = std::string("test.signal") +
                        std::to_string(index);
        result = cache.upsert(signal_at(id, 1, index), 1000);
        EXPECT(result.accepted());
    }
    EXPECT(cache.size() == kTelemetryCacheCapacity);
    result = cache.upsert(signal_at("test.overflow", 1, 1), 1000);
    EXPECT(result.error == CacheError::capacity_full);

    EXPECT(cache.read("invalid", 1).error ==
           CacheError::invalid_signal);
    EXPECT(cache.read("test.missing", 1).error == CacheError::not_found);
}

void test_read_uses_exact_stale_boundary() {
    TelemetryCache cache{};
    EXPECT(cache.upsert(signal_at("engine.speed", 100, 1), 1000).accepted());

    auto read = cache.read("engine.speed", 1099);
    EXPECT(read.found());
    EXPECT(read.snapshot.age_ms == 999);
    EXPECT(read.snapshot.effective_quality == SignalQuality::valid);

    read = cache.read("engine.speed", 1100);
    EXPECT(read.found());
    EXPECT(read.snapshot.age_ms == 1000);
    EXPECT(read.snapshot.effective_quality == SignalQuality::stale);

    read = cache.read("engine.speed", 99);
    EXPECT(read.error == CacheError::freshness_error);
    EXPECT(read.signal_error == SignalModelError::clock_regressed);
}

void test_change_cursor_materializes_stale_transitions() {
    TelemetryCache cache{};
    const CacheCursor beginning{};
    EXPECT(cache.upsert(signal_at("engine.speed", 100, 1), 1000).accepted());
    EXPECT(cache.upsert(
               signal_at("vehicle.voltage", 100, 2),
               2000).accepted());

    std::array<CachedSignalSnapshot, kTelemetryCacheCapacity> output{};
    auto changes = cache.collect_changes(
        beginning,
        100,
        output.data(),
        output.size());
    EXPECT(changes.collected());
    EXPECT(changes.snapshot_count == 2);
    EXPECT(changes.next_cursor.generation == 2);
    const auto first_cursor = changes.next_cursor;

    changes = cache.collect_changes(
        first_cursor,
        1099,
        output.data(),
        output.size());
    EXPECT(changes.collected());
    EXPECT(changes.snapshot_count == 0);

    changes = cache.collect_changes(
        first_cursor,
        1100,
        output.data(),
        output.size());
    EXPECT(changes.collected());
    EXPECT(changes.snapshot_count == 1);
    EXPECT(signal_id_equals(output[0].signal.id, "engine.speed"));
    EXPECT(output[0].effective_quality == SignalQuality::stale);
    EXPECT(changes.next_cursor.generation == 3);

    changes = cache.collect_changes(
        first_cursor,
        1100,
        nullptr,
        0);
    EXPECT(changes.error == CacheError::insufficient_output_capacity);
    EXPECT(changes.next_cursor.generation == 3);
}

void test_clear_invalidates_old_cursors() {
    TelemetryCache cache{};
    EXPECT(cache.upsert(signal_at("engine.speed", 1, 1), 1000).accepted());
    const auto old_cursor = cache.current_cursor();
    cache.clear();
    EXPECT(cache.size() == 0);
    const auto after_clear = cache.current_cursor();
    EXPECT(after_clear.epoch == old_cursor.epoch + 1U);
    EXPECT(after_clear.generation == 0);

    std::array<CachedSignalSnapshot, 1> output{};
    auto changes = cache.collect_changes(
        old_cursor,
        1,
        output.data(),
        output.size());
    EXPECT(changes.error == CacheError::cursor_epoch_mismatch);
    EXPECT(changes.next_cursor.epoch == after_clear.epoch);

    changes = cache.collect_changes(
        {after_clear.epoch, 1},
        1,
        output.data(),
        output.size());
    EXPECT(changes.error == CacheError::invalid_cursor);

    TelemetryCache restarted{};
    EXPECT(restarted.size() == 0);
    EXPECT(restarted.read("engine.speed", 1).error ==
           CacheError::not_found);
}

void test_concurrent_writers_preserve_latest_timestamp() {
    TelemetryCache cache{};
    std::atomic<bool> start{false};
    std::atomic<int> unexpected_results{0};
    std::vector<std::thread> workers;
    constexpr std::uint64_t worker_count = 4;
    constexpr std::uint64_t writes_per_worker = 100;

    for (std::uint64_t worker = 0; worker < worker_count; ++worker) {
        workers.emplace_back(
            [&cache, &start, &unexpected_results, worker]() {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (std::uint64_t index = 0;
                 index < writes_per_worker;
                 ++index) {
                const auto timestamp =
                    worker * writes_per_worker + index + 1;
                const auto result = cache.upsert(
                    signal_at(
                        "engine.speed",
                        timestamp,
                        static_cast<std::int64_t>(timestamp)),
                    1000);
                if (!result.accepted() &&
                    result.error != CacheError::out_of_order) {
                    unexpected_results.fetch_add(
                        1,
                        std::memory_order_relaxed);
                }
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) {
        worker.join();
    }
    EXPECT(unexpected_results.load(std::memory_order_relaxed) == 0);

    const auto expected = worker_count * writes_per_worker;
    const auto read = cache.read("engine.speed", expected);
    EXPECT(read.found());
    EXPECT(read.snapshot.signal.received_at_ms == expected);
    EXPECT(read.snapshot.signal.value.raw_value ==
           static_cast<std::int64_t>(expected));
    EXPECT(cache.size() == 1);
}

}  // namespace

int main() {
    test_insert_update_duplicate_and_ordering();
    test_validation_and_capacity_fail_closed();
    test_read_uses_exact_stale_boundary();
    test_change_cursor_materializes_stale_transitions();
    test_clear_invalidates_old_cursors();
    test_concurrent_writers_preserve_latest_timestamp();

    if (failures != 0) {
        std::cerr << failures << " telemetry cache assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 telemetry cache scenario groups\n";
    return EXIT_SUCCESS;
}
