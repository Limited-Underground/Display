#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>

#include "opengauge/diagnostics.hpp"

namespace {

using namespace opengauge::diagnostics;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

void test_lifecycle_and_configuration_validation() {
    DiagnosticsService service{};
    EXPECT(service.record(LogLevel::error, EventCode::health_checkpoint,
                          MetricCode::none, 0, 0).error ==
           DiagnosticsError::invalid_state);
    EXPECT(service.start({static_cast<LogLevel>(0), ResetReason::power_on}, 0) ==
           DiagnosticsError::invalid_argument);
    EXPECT(service.start({LogLevel::trace, static_cast<ResetReason>(99)}, 0) ==
           DiagnosticsError::invalid_argument);
    EXPECT(service.start({LogLevel::info, ResetReason::power_on}, 10) ==
           DiagnosticsError::none);
    EXPECT(service.start({LogLevel::info, ResetReason::power_on}, 10) ==
           DiagnosticsError::invalid_state);
    EXPECT(service.status().running);
    EXPECT(service.status().event_count == 1);
    EXPECT(service.status().counters[static_cast<std::size_t>(
               CounterCode::reset_count)] == 1);
    service.stop();
    EXPECT(service.clear_events() == DiagnosticsError::invalid_state);
}

void test_threshold_filtering_and_monotonic_time() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::warning, ResetReason::software}, 100) ==
           DiagnosticsError::none);
    EXPECT(service.status().event_count == 0);
    const auto filtered = service.record(
        LogLevel::info, EventCode::health_checkpoint,
        MetricCode::count, 1, 101);
    EXPECT(filtered.accepted());
    EXPECT(!filtered.stored);
    const auto warning = service.record(
        LogLevel::warning, EventCode::can_state_changed,
        MetricCode::state_code, 2, 102);
    EXPECT(warning.stored);
    EXPECT(warning.sequence == 1);
    EXPECT(service.record(LogLevel::error, EventCode::wireless_state_changed,
                          MetricCode::error_code, 3, 101).error ==
           DiagnosticsError::time_regression);
    EXPECT(service.set_threshold(LogLevel::trace) == DiagnosticsError::none);
    EXPECT(service.record(LogLevel::trace, EventCode::render_timing,
                          MetricCode::duration_us, 4000, 102).stored);
}

void test_ring_overwrite_and_sequence_order() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::trace, ResetReason::watchdog}, 0) ==
           DiagnosticsError::none);
    for (std::uint64_t index = 1; index <= kDiagnosticEventCapacity + 5; ++index) {
        EXPECT(service.record(LogLevel::debug, EventCode::health_checkpoint,
                              MetricCode::count,
                              static_cast<std::int64_t>(index), index).stored);
    }
    std::array<DiagnosticEvent, kDiagnosticEventCapacity> events{};
    const auto snapshot = service.snapshot_events(events.data(), events.size());
    EXPECT(snapshot.copied());
    EXPECT(snapshot.event_count == kDiagnosticEventCapacity);
    EXPECT(events.front().sequence == 7);
    EXPECT(events.front().value == 6);
    EXPECT(events.back().sequence == kDiagnosticEventCapacity + 6);
    EXPECT(events.back().value ==
           static_cast<std::int64_t>(kDiagnosticEventCapacity + 5));
    EXPECT(service.status().counters[static_cast<std::size_t>(
               CounterCode::log_records_dropped)] == 6);
}

void test_counters_saturate_and_internal_counters_are_reserved() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::error, ResetReason::brownout}, 0) ==
           DiagnosticsError::none);
    EXPECT(service.increment(CounterCode::can_frames_received,
                             std::numeric_limits<std::uint32_t>::max()) ==
           DiagnosticsError::none);
    EXPECT(service.increment(CounterCode::can_frames_received, 10) ==
           DiagnosticsError::none);
    EXPECT(service.status().counters[static_cast<std::size_t>(
               CounterCode::can_frames_received)] ==
           std::numeric_limits<std::uint32_t>::max());
    EXPECT(service.increment(CounterCode::reset_count) ==
           DiagnosticsError::invalid_argument);
    EXPECT(service.increment(CounterCode::log_records_dropped) ==
           DiagnosticsError::invalid_argument);
    EXPECT(service.increment(CounterCode::can_errors, 0) ==
           DiagnosticsError::invalid_argument);
    EXPECT(service.increment(static_cast<CounterCode>(99)) ==
           DiagnosticsError::invalid_argument);
}

void test_startup_reset_reason_and_canonical_record() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::info, ResetReason::panic}, 55) ==
           DiagnosticsError::none);
    std::array<DiagnosticEvent, kDiagnosticEventCapacity> events{};
    EXPECT(service.snapshot_events(events.data(), events.size()).event_count == 1);
    EXPECT(events[0].code == EventCode::startup);
    EXPECT(events[0].metric == MetricCode::reset_reason);
    EXPECT(events[0].value == static_cast<std::int64_t>(ResetReason::panic));
    EXPECT(events[0].monotonic_ms == 55);
    EXPECT(service.record(LogLevel::info, EventCode::health_checkpoint,
                          MetricCode::none, 1, 56).error ==
           DiagnosticsError::invalid_argument);
    EXPECT(service.record(static_cast<LogLevel>(99),
                          EventCode::health_checkpoint,
                          MetricCode::none, 0, 56).error ==
           DiagnosticsError::invalid_argument);
}

void test_snapshot_is_atomic_and_clear_preserves_counters() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::info, ResetReason::power_on}, 0) ==
           DiagnosticsError::none);
    std::array<DiagnosticEvent, 1> output{};
    output[0].sequence = 99;
    EXPECT(service.record(LogLevel::error, EventCode::can_state_changed,
                          MetricCode::error_code, 4, 1).stored);
    EXPECT(service.snapshot_events(output.data(), output.size()).error ==
           DiagnosticsError::insufficient_output_capacity);
    EXPECT(output[0].sequence == 99);
    EXPECT(service.increment(CounterCode::can_errors, 2) ==
           DiagnosticsError::none);
    const auto next_sequence = service.status().next_event_sequence;
    EXPECT(service.clear_events() == DiagnosticsError::none);
    EXPECT(service.status().event_count == 0);
    EXPECT(service.status().first_event_sequence == 0);
    EXPECT(service.status().next_event_sequence == next_sequence);
    EXPECT(service.status().counters[static_cast<std::size_t>(
               CounterCode::can_errors)] == 2);
    EXPECT(service.snapshot_events(nullptr, 0).copied());
}

void test_restart_resets_runtime_and_clock() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::trace, ResetReason::software}, 1000) ==
           DiagnosticsError::none);
    EXPECT(service.increment(CounterCode::wireless_send_failed, 4) ==
           DiagnosticsError::none);
    service.stop();
    EXPECT(service.start({LogLevel::info, ResetReason::power_on}, 5) ==
           DiagnosticsError::none);
    const auto status = service.status();
    EXPECT(status.running);
    EXPECT(status.last_monotonic_ms == 5);
    EXPECT(status.event_count == 1);
    EXPECT(status.counters[static_cast<std::size_t>(
               CounterCode::wireless_send_failed)] == 0);
    EXPECT(status.counters[static_cast<std::size_t>(
               CounterCode::reset_count)] == 1);
}

void test_event_payload_is_fixed_and_pointer_free() {
    static_assert(std::is_trivially_copyable_v<DiagnosticEvent>);
    static_assert(!std::is_pointer_v<decltype(DiagnosticEvent::value)>);
    EXPECT(sizeof(DiagnosticEvent) <= 40);
}

}  // namespace

int main() {
    test_lifecycle_and_configuration_validation();
    test_threshold_filtering_and_monotonic_time();
    test_ring_overwrite_and_sequence_order();
    test_counters_saturate_and_internal_counters_are_reserved();
    test_startup_reset_reason_and_canonical_record();
    test_snapshot_is_atomic_and_clear_preserves_counters();
    test_restart_resets_runtime_and_clock();
    test_event_payload_is_fixed_and_pointer_free();

    if (failures != 0) {
        std::cerr << failures << " diagnostics assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 diagnostics scenario groups\n";
    return EXIT_SUCCESS;
}
