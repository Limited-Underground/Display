#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

#include "opengauge/alarm_engine.hpp"

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

alarm::AlarmRule rule(
    std::uint16_t id,
    std::string_view signal_id = "engine.test") {
    alarm::AlarmRule result{};
    result.id = id;
    EXPECT(telemetry::make_signal_id(signal_id, result.signal_id) ==
           telemetry::SignalModelError::none);
    result.value_type = telemetry::SignalValueType::signed_integer;
    result.unit = telemetry::SignalUnit::count;
    result.comparison = alarm::AlarmComparison::above_or_equal;
    result.threshold_low = 1000;
    result.severity = alarm::AlarmSeverity::warning;
    return result;
}

telemetry::CachedSignalSnapshot snapshot(
    std::int64_t value,
    std::uint64_t received_at_ms,
    telemetry::SignalQuality effective_quality =
        telemetry::SignalQuality::valid,
    std::string_view signal_id = "engine.test") {
    telemetry::CachedSignalSnapshot result{};
    EXPECT(telemetry::make_signal_id(signal_id, result.signal.id) ==
           telemetry::SignalModelError::none);
    result.signal.value = {
        telemetry::SignalValueType::signed_integer,
        value,
        true};
    result.signal.unit = telemetry::SignalUnit::count;
    result.signal.quality = telemetry::SignalQuality::valid;
    result.signal.source.protocol =
        telemetry::SignalSourceProtocol::synthetic;
    result.signal.received_at_ms = received_at_ms;
    result.effective_quality = effective_quality;
    result.age_ms = 0;
    return result;
}

telemetry::CachedSignalSnapshot unavailable_snapshot(
    std::uint64_t received_at_ms,
    std::string_view signal_id = "engine.test") {
    auto result = snapshot(0, received_at_ms,
                           telemetry::SignalQuality::unavailable,
                           signal_id);
    result.signal.quality = telemetry::SignalQuality::unavailable;
    result.signal.value.present = false;
    result.signal.value.raw_value = 0;
    return result;
}

void test_rule_validation_capacity_and_lifecycle() {
    alarm::AlarmEngine engine{};
    EXPECT(engine.start() == alarm::AlarmError::invalid_state);
    auto invalid = rule(0);
    EXPECT(alarm::validate_alarm_rule(invalid) ==
           alarm::AlarmError::invalid_rule);
    invalid = rule(1);
    invalid.threshold_high = 1;
    EXPECT(alarm::validate_alarm_rule(invalid) ==
           alarm::AlarmError::invalid_rule);
    invalid = rule(1);
    invalid.comparison = alarm::AlarmComparison::outside_inclusive_range;
    invalid.threshold_low = 100;
    invalid.threshold_high = 200;
    invalid.hysteresis_raw = 51;
    EXPECT(alarm::validate_alarm_rule(invalid) ==
           alarm::AlarmError::invalid_rule);

    for (std::uint16_t id = 1; id <= alarm::kMaximumAlarmRules; ++id) {
        EXPECT(engine.add_rule(rule(id)) == alarm::AlarmError::none);
    }
    EXPECT(engine.add_rule(rule(1)) == alarm::AlarmError::duplicate_rule);
    EXPECT(engine.add_rule(rule(17)) == alarm::AlarmError::capacity_full);
    EXPECT(engine.start() == alarm::AlarmError::none);
    EXPECT(engine.add_rule(rule(17)) == alarm::AlarmError::invalid_state);
    EXPECT(engine.clear_rules() == alarm::AlarmError::invalid_state);
    EXPECT(engine.status().rule_count == alarm::kMaximumAlarmRules);
    engine.stop();
    EXPECT(engine.clear_rules() == alarm::AlarmError::none);
    EXPECT(engine.status().rule_count == 0);

    auto unsigned_rule = rule(1);
    unsigned_rule.value_type = telemetry::SignalValueType::unsigned_integer;
    unsigned_rule.threshold_low = -1;
    EXPECT(alarm::validate_alarm_rule(unsigned_rule) ==
           alarm::AlarmError::invalid_rule);

    alarm::AlarmEngine signed_boundary{};
    auto edge = rule(1);
    edge.threshold_low = std::numeric_limits<std::int64_t>::max() - 1;
    edge.hysteresis_raw = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    EXPECT(signed_boundary.add_rule(edge) == alarm::AlarmError::none);
    EXPECT(signed_boundary.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};
    EXPECT(signed_boundary.evaluate(
               snapshot(std::numeric_limits<std::int64_t>::max(), 0),
               0, events.data(), events.size()).events_emitted == 1);
    EXPECT(signed_boundary.evaluate(snapshot(0, 1), 1,
                                    events.data(), events.size()).events_emitted == 0);
    EXPECT(signed_boundary.evaluate(snapshot(-1, 2), 2,
                                    events.data(), events.size()).events_emitted == 1);
}

void test_above_threshold_and_exact_hysteresis_clear() {
    alarm::AlarmEngine engine{};
    auto high = rule(1);
    high.hysteresis_raw = 100;
    EXPECT(engine.add_rule(high) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};

    auto result = engine.evaluate(snapshot(999, 0), 0,
                                  events.data(), events.size());
    EXPECT(result.evaluated());
    EXPECT(result.events_emitted == 0);
    result = engine.evaluate(snapshot(1000, 1), 1,
                             events.data(), events.size());
    EXPECT(result.events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::asserted);
    EXPECT(events[0].value.raw_value == 1000);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::active);

    result = engine.evaluate(snapshot(950, 2), 2,
                             events.data(), events.size());
    EXPECT(result.events_emitted == 0);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::active);
    result = engine.evaluate(snapshot(900, 3), 3,
                             events.data(), events.size());
    EXPECT(result.events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::cleared);
    EXPECT(events[0].active_duration_ms == 2);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);
}

void test_below_and_outside_range_hysteresis() {
    alarm::AlarmEngine engine{};
    auto low = rule(1, "engine.pressure");
    low.comparison = alarm::AlarmComparison::below_or_equal;
    low.threshold_low = 100;
    low.hysteresis_raw = 10;
    auto range = rule(2, "engine.temperature");
    range.comparison = alarm::AlarmComparison::outside_inclusive_range;
    range.threshold_low = 0;
    range.threshold_high = 100;
    range.hysteresis_raw = 10;
    EXPECT(engine.add_rule(low) == alarm::AlarmError::none);
    EXPECT(engine.add_rule(range) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 2> events{};

    EXPECT(engine.evaluate(snapshot(100, 0,
                                    telemetry::SignalQuality::valid,
                                    "engine.pressure"),
                           0, events.data(), events.size()).events_emitted == 1);
    EXPECT(engine.evaluate(snapshot(105, 1,
                                    telemetry::SignalQuality::valid,
                                    "engine.pressure"),
                           1, events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.evaluate(snapshot(110, 2,
                                    telemetry::SignalQuality::valid,
                                    "engine.pressure"),
                           2, events.data(), events.size()).events_emitted == 1);

    auto result = engine.evaluate(snapshot(0, 3,
                                           telemetry::SignalQuality::valid,
                                           "engine.temperature"),
                                  3, events.data(), events.size());
    EXPECT(result.events_emitted == 1);
    EXPECT(engine.state(2).condition_present);
    EXPECT(engine.evaluate(snapshot(5, 4,
                                    telemetry::SignalQuality::valid,
                                    "engine.temperature"),
                           4, events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.evaluate(snapshot(10, 5,
                                    telemetry::SignalQuality::valid,
                                    "engine.temperature"),
                           5, events.data(), events.size()).events_emitted == 1);
    EXPECT(engine.evaluate(snapshot(100, 6,
                                    telemetry::SignalQuality::valid,
                                    "engine.temperature"),
                           6, events.data(), events.size()).events_emitted == 1);
    EXPECT(engine.evaluate(snapshot(90, 7,
                                    telemetry::SignalQuality::valid,
                                    "engine.temperature"),
                           7, events.data(), events.size()).events_emitted == 1);
}

void test_assert_and_clear_debounce_with_chatter() {
    alarm::AlarmEngine engine{};
    auto debounced = rule(1);
    debounced.assert_debounce_ms = 100;
    debounced.clear_debounce_ms = 100;
    debounced.hysteresis_raw = 100;
    EXPECT(engine.add_rule(debounced) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};

    EXPECT(engine.evaluate(snapshot(1100, 0), 0,
                           events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.state(1).lifecycle ==
           alarm::AlarmLifecycle::pending_assert);
    EXPECT(engine.evaluate(snapshot(1100, 99), 99,
                           events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.evaluate(snapshot(1100, 100), 100,
                           events.data(), events.size()).events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::asserted);

    EXPECT(engine.evaluate(snapshot(900, 150), 150,
                           events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.state(1).lifecycle ==
           alarm::AlarmLifecycle::pending_clear);
    EXPECT(engine.evaluate(snapshot(950, 200), 200,
                           events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::active);
    EXPECT(engine.evaluate(snapshot(900, 250), 250,
                           events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.evaluate(snapshot(900, 349), 349,
                           events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.evaluate(snapshot(900, 350), 350,
                           events.data(), events.size()).events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::cleared);
    EXPECT(events[0].active_duration_ms == 250);
}

void test_latching_and_acknowledgement_paths() {
    alarm::AlarmEngine engine{};
    auto latched = rule(1);
    latched.latching = true;
    EXPECT(engine.add_rule(latched) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};

    EXPECT(engine.evaluate(snapshot(1100, 0), 0,
                           events.data(), events.size()).events_emitted == 1);
    auto result = engine.evaluate(snapshot(900, 50), 50,
                                  events.data(), events.size());
    EXPECT(result.events_emitted == 1);
    EXPECT(events[0].kind ==
           alarm::AlarmEventKind::condition_cleared_latched);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::latched);
    const auto acknowledged = engine.acknowledge(1, 60);
    EXPECT(acknowledged.acknowledged());
    EXPECT(acknowledged.event.kind == alarm::AlarmEventKind::cleared);
    EXPECT(acknowledged.event.acknowledged);
    EXPECT(acknowledged.event.active_duration_ms == 60);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);
    EXPECT(engine.acknowledge(1, 61).error == alarm::AlarmError::not_active);

    EXPECT(engine.evaluate(snapshot(1100, 100), 100,
                           events.data(), events.size()).events_emitted == 1);
    const auto active_ack = engine.acknowledge(1, 110);
    EXPECT(active_ack.event.kind == alarm::AlarmEventKind::acknowledged);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::active);
    EXPECT(engine.acknowledge(1, 111).error ==
           alarm::AlarmError::already_acknowledged);
    result = engine.evaluate(snapshot(900, 120), 120,
                             events.data(), events.size());
    EXPECT(result.events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::cleared);
    EXPECT(engine.status().acknowledgements == 2);
}

void test_nonvalid_clear_hold_and_assert_policies() {
    alarm::AlarmEngine engine{};
    auto clear = rule(1);
    clear.nonvalid_behavior =
        alarm::NonvalidSignalBehavior::clear_condition;
    auto hold = rule(2);
    hold.nonvalid_behavior = alarm::NonvalidSignalBehavior::hold_state;
    auto assert = rule(3);
    assert.nonvalid_behavior =
        alarm::NonvalidSignalBehavior::assert_alarm;
    EXPECT(engine.add_rule(clear) == alarm::AlarmError::none);
    EXPECT(engine.add_rule(hold) == alarm::AlarmError::none);
    EXPECT(engine.add_rule(assert) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 3> events{};

    auto result = engine.evaluate(snapshot(1100, 0), 0,
                                  events.data(), events.size());
    EXPECT(result.events_emitted == 3);
    result = engine.evaluate(snapshot(1100, 100,
                                      telemetry::SignalQuality::stale),
                             100, events.data(), events.size());
    EXPECT(result.events_emitted == 1);
    EXPECT(events[0].rule_id == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::cleared);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);
    EXPECT(engine.state(2).lifecycle == alarm::AlarmLifecycle::active);
    EXPECT(engine.state(3).lifecycle == alarm::AlarmLifecycle::active);

    engine.stop();
    EXPECT(engine.start() == alarm::AlarmError::none);
    result = engine.evaluate(unavailable_snapshot(200), 200,
                             events.data(), events.size());
    EXPECT(result.events_emitted == 1);
    EXPECT(events[0].rule_id == 3);
    EXPECT(events[0].signal_quality ==
           telemetry::SignalQuality::unavailable);
    EXPECT(!events[0].value.present);
    EXPECT(engine.state(2).lifecycle == alarm::AlarmLifecycle::inactive);
}

void test_reminders_are_rate_bounded_but_clear_is_immediate() {
    alarm::AlarmEngine engine{};
    auto repeating = rule(1);
    repeating.reminder_interval_ms = 100;
    EXPECT(engine.add_rule(repeating) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};

    EXPECT(engine.evaluate(snapshot(1100, 0), 0,
                           events.data(), events.size()).events_emitted == 1);
    EXPECT(engine.evaluate(snapshot(1100, 99), 99,
                           events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.evaluate(snapshot(1100, 100), 100,
                           events.data(), events.size()).events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::reminder);
    EXPECT(engine.evaluate(snapshot(1100, 199), 199,
                           events.data(), events.size()).events_emitted == 0);
    EXPECT(engine.evaluate(snapshot(1100, 200), 200,
                           events.data(), events.size()).events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::reminder);
    EXPECT(engine.evaluate(snapshot(900, 210), 210,
                           events.data(), events.size()).events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::cleared);
    EXPECT(engine.status().reminders == 2);
}

void test_multi_rule_output_is_atomic_and_clock_regression_fails() {
    alarm::AlarmEngine engine{};
    EXPECT(engine.add_rule(rule(1)) == alarm::AlarmError::none);
    EXPECT(engine.add_rule(rule(2)) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 2> events{};
    events[0].rule_id = 99;

    auto result = engine.evaluate(snapshot(1100, 100), 100,
                                  events.data(), 1);
    EXPECT(result.error == alarm::AlarmError::insufficient_output_capacity);
    EXPECT(result.rules_matched == 2);
    EXPECT(events[0].rule_id == 99);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);
    result = engine.evaluate(snapshot(1100, 100), 100,
                             events.data(), events.size());
    EXPECT(result.events_emitted == 2);
    EXPECT(engine.evaluate(snapshot(1100, 99), 99,
                           events.data(), events.size()).error ==
           alarm::AlarmError::clock_regressed);
    EXPECT(engine.acknowledge(1, 99).error ==
           alarm::AlarmError::clock_regressed);
    EXPECT(engine.evaluate(snapshot(1100, 101,
                                    telemetry::SignalQuality::valid,
                                    "other.signal"),
                           101, events.data(), events.size()).error ==
           alarm::AlarmError::no_matching_rule);
}

void test_incompatible_invalid_and_boolean_inputs_fail_closed() {
    alarm::AlarmEngine engine{};
    EXPECT(engine.add_rule(rule(1)) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};
    auto incompatible = snapshot(1100, 0);
    incompatible.signal.unit = telemetry::SignalUnit::millivolt;
    EXPECT(engine.evaluate(incompatible, 0,
                           events.data(), events.size()).error ==
           alarm::AlarmError::incompatible_signal);
    auto invalid = snapshot(1100, 0);
    invalid.effective_quality =
        static_cast<telemetry::SignalQuality>(99);
    EXPECT(engine.evaluate(invalid, 0,
                           events.data(), events.size()).error ==
           alarm::AlarmError::invalid_signal);
    invalid = unavailable_snapshot(0);
    invalid.effective_quality = telemetry::SignalQuality::valid;
    EXPECT(engine.evaluate(invalid, 0,
                           events.data(), events.size()).error ==
           alarm::AlarmError::invalid_signal);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);

    auto boolean_rule = rule(2, "engine.switch");
    boolean_rule.value_type = telemetry::SignalValueType::boolean;
    boolean_rule.unit = telemetry::SignalUnit::none;
    boolean_rule.threshold_low = 1;
    EXPECT(alarm::validate_alarm_rule(boolean_rule) ==
           alarm::AlarmError::none);
    boolean_rule.comparison =
        alarm::AlarmComparison::outside_inclusive_range;
    boolean_rule.threshold_low = 0;
    boolean_rule.threshold_high = 1;
    EXPECT(alarm::validate_alarm_rule(boolean_rule) ==
           alarm::AlarmError::invalid_rule);
}

void test_stop_restart_retains_rules_and_resets_runtime_counters() {
    alarm::AlarmEngine engine{};
    EXPECT(engine.add_rule(rule(1)) == alarm::AlarmError::none);
    EXPECT(engine.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};
    EXPECT(engine.evaluate(snapshot(1100, 0), 0,
                           events.data(), events.size()).events_emitted == 1);
    EXPECT(engine.status().assertions == 1);
    engine.stop();
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);
    EXPECT(engine.start() == alarm::AlarmError::none);
    EXPECT(engine.status().rule_count == 1);
    EXPECT(engine.status().evaluations == 0);
    EXPECT(engine.status().assertions == 0);
    EXPECT(engine.status().active_or_latched_count == 0);
}

}  // namespace

int main() {
    test_rule_validation_capacity_and_lifecycle();
    test_above_threshold_and_exact_hysteresis_clear();
    test_below_and_outside_range_hysteresis();
    test_assert_and_clear_debounce_with_chatter();
    test_latching_and_acknowledgement_paths();
    test_nonvalid_clear_hold_and_assert_policies();
    test_reminders_are_rate_bounded_but_clear_is_immediate();
    test_multi_rule_output_is_atomic_and_clock_regression_fails();
    test_incompatible_invalid_and_boolean_inputs_fail_closed();
    test_stop_restart_retains_rules_and_resets_runtime_counters();

    if (failures != 0) {
        std::cerr << failures << " alarm engine assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 alarm engine scenario groups\n";
    return EXIT_SUCCESS;
}
