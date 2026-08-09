#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/gauge_trend_buffer.hpp"

namespace {

using namespace opengauge;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

display::GaugeTrendConfiguration trend(
    std::uint16_t id,
    std::uint16_t capacity = 4,
    std::uint64_t interval = 100,
    wireless::TelemetrySignalCode code =
        wireless::TelemetrySignalCode::engine_speed) {
    return {id, code, capacity, interval};
}

display::GaugeWidgetSnapshot snapshot(
    display::GaugeValueState state,
    std::int64_t value = 1000000,
    std::uint32_t sequence = 1,
    wireless::TelemetrySignalCode code =
        wireless::TelemetrySignalCode::engine_speed) {
    display::GaugeWidgetSnapshot result{};
    result.widget_id = 1;
    result.signal_code = code;
    result.state = state;
    const auto* descriptor = wireless::telemetry_signal_descriptor(code);
    EXPECT(descriptor != nullptr);
    result.unit = descriptor->unit;
    result.display_value.type = descriptor->value_type;
    result.boot_session_id = 7;
    result.packet_sequence = sequence;
    if (state == display::GaugeValueState::valid ||
        state == display::GaugeValueState::suspect) {
        result.display_value.present = true;
        result.display_value.raw_value = value;
    }
    return result;
}

void test_configuration_capacity_and_lifecycle() {
    display::GaugeTrendBuffer buffer{};
    EXPECT(buffer.start() == display::GaugeTrendError::invalid_state);
    EXPECT(buffer.add_trend(trend(0)) ==
           display::GaugeTrendError::invalid_configuration);
    EXPECT(buffer.add_trend(trend(1, 1)) ==
           display::GaugeTrendError::invalid_configuration);
    for (std::uint16_t id = 1; id <= display::kMaximumGaugeTrends; ++id) {
        EXPECT(buffer.add_trend(trend(id)) == display::GaugeTrendError::none);
    }
    EXPECT(buffer.add_trend(trend(1)) ==
           display::GaugeTrendError::duplicate_trend);
    EXPECT(buffer.add_trend(trend(9)) ==
           display::GaugeTrendError::trend_capacity_full);
    EXPECT(buffer.start() == display::GaugeTrendError::none);
    EXPECT(buffer.add_trend(trend(9)) ==
           display::GaugeTrendError::invalid_state);
    buffer.stop();
    EXPECT(buffer.clear_trends() == display::GaugeTrendError::none);
}

void test_valid_and_suspect_points_preserve_values() {
    display::GaugeTrendBuffer buffer{};
    EXPECT(buffer.add_trend(trend(1)) == display::GaugeTrendError::none);
    EXPECT(buffer.start() == display::GaugeTrendError::none);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::valid, 100), 0)
               .trends_appended == 1);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::suspect, 200), 100)
               .trends_appended == 1);
    std::array<display::GaugeTrendPoint, 4> points{};
    std::size_t count = 0;
    EXPECT(buffer.read(1, points.data(), points.size(), count) ==
           display::GaugeTrendError::none);
    EXPECT(count == 2);
    EXPECT(!points[0].gap && points[0].value.raw_value == 100);
    EXPECT(!points[1].gap && points[1].value.raw_value == 200);
    EXPECT(points[1].state == display::GaugeValueState::suspect);
}

void test_nonvalid_states_are_explicit_gaps() {
    display::GaugeTrendBuffer buffer{};
    EXPECT(buffer.add_trend(trend(1)) == display::GaugeTrendError::none);
    EXPECT(buffer.start() == display::GaugeTrendError::none);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::stale), 0)
               .accepted());
    EXPECT(buffer.append(snapshot(display::GaugeValueState::error), 100)
               .accepted());
    std::array<display::GaugeTrendPoint, 4> points{};
    std::size_t count = 0;
    EXPECT(buffer.read(1, points.data(), points.size(), count) ==
           display::GaugeTrendError::none);
    EXPECT(count == 2);
    EXPECT(points[0].gap && !points[0].value.present);
    EXPECT(points[0].state == display::GaugeValueState::stale);
    EXPECT(points[1].gap && points[1].value.raw_value == 0);
}

void test_exact_interval_and_skips() {
    display::GaugeTrendBuffer buffer{};
    EXPECT(buffer.add_trend(trend(1, 4, 100)) ==
           display::GaugeTrendError::none);
    EXPECT(buffer.start() == display::GaugeTrendError::none);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::valid), 100)
               .trends_appended == 1);
    const auto skipped =
        buffer.append(snapshot(display::GaugeValueState::valid), 199);
    EXPECT(skipped.trends_interval_skipped == 1);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::valid), 200)
               .trends_appended == 1);
    EXPECT(buffer.status().points_appended == 2);
    EXPECT(buffer.status().interval_skips == 1);
}

void test_ring_overwrite_preserves_oldest_first_order() {
    display::GaugeTrendBuffer buffer{};
    EXPECT(buffer.add_trend(trend(1, 3, 1)) ==
           display::GaugeTrendError::none);
    EXPECT(buffer.start() == display::GaugeTrendError::none);
    for (std::uint32_t index = 0; index < 5; ++index) {
        EXPECT(buffer.append(
                   snapshot(display::GaugeValueState::valid,
                            static_cast<std::int64_t>(index), index),
                   index).accepted());
    }
    std::array<display::GaugeTrendPoint, 3> points{};
    std::size_t count = 0;
    EXPECT(buffer.read(1, points.data(), points.size(), count) ==
           display::GaugeTrendError::none);
    EXPECT(count == 3);
    EXPECT(points[0].value.raw_value == 2);
    EXPECT(points[2].value.raw_value == 4);
    EXPECT(buffer.status().points_overwritten == 2);
}

void test_multiple_trends_can_share_one_signal() {
    display::GaugeTrendBuffer buffer{};
    EXPECT(buffer.add_trend(trend(1, 4, 10)) ==
           display::GaugeTrendError::none);
    EXPECT(buffer.add_trend(trend(2, 4, 20)) ==
           display::GaugeTrendError::none);
    EXPECT(buffer.start() == display::GaugeTrendError::none);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::valid), 0)
               .trends_appended == 2);
    const auto second =
        buffer.append(snapshot(display::GaugeValueState::valid), 10);
    EXPECT(second.trends_appended == 1);
    EXPECT(second.trends_interval_skipped == 1);
}

void test_invalid_snapshot_and_clock_failure_are_atomic() {
    display::GaugeTrendBuffer buffer{};
    EXPECT(buffer.add_trend(trend(1)) == display::GaugeTrendError::none);
    EXPECT(buffer.start() == display::GaugeTrendError::none);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::valid), 100)
               .accepted());
    auto invalid = snapshot(display::GaugeValueState::stale);
    invalid.display_value.present = true;
    invalid.display_value.raw_value = 99;
    EXPECT(buffer.append(invalid, 200).error ==
           display::GaugeTrendError::invalid_snapshot);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::valid), 99).error ==
           display::GaugeTrendError::clock_regression);
    std::array<display::GaugeTrendPoint, 4> points{};
    std::size_t count = 0;
    EXPECT(buffer.read(1, points.data(), points.size(), count) ==
           display::GaugeTrendError::none);
    EXPECT(count == 1);
}

void test_read_capacity_and_clear_are_explicit() {
    display::GaugeTrendBuffer buffer{};
    EXPECT(buffer.add_trend(trend(1)) == display::GaugeTrendError::none);
    EXPECT(buffer.start() == display::GaugeTrendError::none);
    EXPECT(buffer.append(snapshot(display::GaugeValueState::valid), 0)
               .accepted());
    std::array<display::GaugeTrendPoint, 1> points{};
    points[0].captured_at_ms = 99;
    std::size_t count = 77;
    EXPECT(buffer.read(1, points.data(), 0, count) ==
           display::GaugeTrendError::insufficient_output_capacity);
    EXPECT(points[0].captured_at_ms == 99 && count == 77);
    EXPECT(buffer.clear_points(1) == display::GaugeTrendError::none);
    EXPECT(buffer.read(1, nullptr, 0, count) ==
           display::GaugeTrendError::none);
    EXPECT(count == 0);
    EXPECT(buffer.clear_points(99) ==
           display::GaugeTrendError::trend_not_found);
}

}  // namespace

int main() {
    test_configuration_capacity_and_lifecycle();
    test_valid_and_suspect_points_preserve_values();
    test_nonvalid_states_are_explicit_gaps();
    test_exact_interval_and_skips();
    test_ring_overwrite_preserves_oldest_first_order();
    test_multiple_trends_can_share_one_signal();
    test_invalid_snapshot_and_clock_failure_are_atomic();
    test_read_capacity_and_clear_are_explicit();

    if (failures != 0) {
        std::cerr << failures << " gauge trend assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 gauge trend scenario groups\n";
    return EXIT_SUCCESS;
}
