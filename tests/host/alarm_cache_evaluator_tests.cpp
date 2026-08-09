#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "opengauge/alarm_cache_evaluator.hpp"

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

telemetry::NormalizedSignal signal(
    std::string_view signal_id,
    std::int64_t value,
    std::uint64_t received_at_ms,
    telemetry::SignalUnit unit = telemetry::SignalUnit::count) {
    telemetry::NormalizedSignal result{};
    EXPECT(telemetry::make_signal_id(signal_id, result.id) ==
           telemetry::SignalModelError::none);
    result.value = {
        telemetry::SignalValueType::signed_integer,
        value,
        true};
    result.unit = unit;
    result.quality = telemetry::SignalQuality::valid;
    result.source.protocol = telemetry::SignalSourceProtocol::synthetic;
    result.received_at_ms = received_at_ms;
    return result;
}

void test_lifecycle_and_output_capacity_are_fail_closed() {
    telemetry::TelemetryCache cache{};
    alarm::AlarmEngine engine{};
    EXPECT(engine.add_rule(rule(1)) == alarm::AlarmError::none);
    alarm::AlarmCacheEvaluator evaluator{cache, engine};
    std::array<alarm::AlarmEvent, 1> events{};
    EXPECT(evaluator.poll(0, events.data(), events.size()).error ==
           alarm::AlarmCacheEvaluatorError::invalid_state);
    EXPECT(evaluator.start() == alarm::AlarmError::none);
    EXPECT(evaluator.start() == alarm::AlarmError::invalid_state);
    EXPECT(evaluator.poll(0, nullptr, events.size()).error ==
           alarm::AlarmCacheEvaluatorError::invalid_argument);
    EXPECT(evaluator.poll(0, events.data(), 0).error ==
           alarm::AlarmCacheEvaluatorError::insufficient_output_capacity);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);
    evaluator.stop();
    EXPECT(!evaluator.status().running);
    EXPECT(!engine.status().running);
}

void test_full_state_poll_advances_debounce_without_value_change() {
    telemetry::TelemetryCache cache{};
    alarm::AlarmEngine engine{};
    auto debounced = rule(1);
    debounced.assert_debounce_ms = 100;
    EXPECT(engine.add_rule(debounced) == alarm::AlarmError::none);
    EXPECT(cache.upsert(signal("engine.test", 1100, 0), 1000).accepted());
    alarm::AlarmCacheEvaluator evaluator{cache, engine};
    EXPECT(evaluator.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};

    auto poll = evaluator.poll(0, events.data(), events.size());
    EXPECT(poll.polled());
    EXPECT(poll.snapshots_collected == 1);
    EXPECT(poll.events_emitted == 0);
    EXPECT(engine.state(1).lifecycle ==
           alarm::AlarmLifecycle::pending_assert);
    poll = evaluator.poll(99, events.data(), events.size());
    EXPECT(poll.snapshots_collected == 1);
    EXPECT(poll.events_emitted == 0);
    poll = evaluator.poll(100, events.data(), events.size());
    EXPECT(poll.events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::asserted);
    EXPECT(evaluator.status().polls_completed == 3);
    EXPECT(evaluator.status().snapshots_evaluated == 3);
}

void test_exact_cache_stale_boundary_asserts_without_numeric_value() {
    telemetry::TelemetryCache cache{};
    alarm::AlarmEngine engine{};
    auto stale_alarm = rule(1);
    stale_alarm.threshold_low = 2000;
    stale_alarm.nonvalid_behavior =
        alarm::NonvalidSignalBehavior::assert_alarm;
    EXPECT(engine.add_rule(stale_alarm) == alarm::AlarmError::none);
    EXPECT(cache.upsert(signal("engine.test", 1000, 0), 100).accepted());
    alarm::AlarmCacheEvaluator evaluator{cache, engine};
    EXPECT(evaluator.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};

    EXPECT(evaluator.poll(99, events.data(), events.size()).events_emitted == 0);
    const auto poll = evaluator.poll(100, events.data(), events.size());
    EXPECT(poll.events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::asserted);
    EXPECT(events[0].signal_quality == telemetry::SignalQuality::stale);
    EXPECT(!events[0].value.present);
}

void test_full_state_poll_advances_rate_bounded_reminders() {
    telemetry::TelemetryCache cache{};
    alarm::AlarmEngine engine{};
    auto repeating = rule(1);
    repeating.reminder_interval_ms = 100;
    EXPECT(engine.add_rule(repeating) == alarm::AlarmError::none);
    EXPECT(cache.upsert(signal("engine.test", 1100, 0), 1000).accepted());
    alarm::AlarmCacheEvaluator evaluator{cache, engine};
    EXPECT(evaluator.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};

    EXPECT(evaluator.poll(0, events.data(), events.size()).events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::asserted);
    EXPECT(evaluator.poll(99, events.data(), events.size()).events_emitted == 0);
    EXPECT(evaluator.poll(100, events.data(), events.size()).events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::reminder);
}

void test_cache_epoch_change_resets_alarm_runtime_without_fake_clear() {
    telemetry::TelemetryCache cache{};
    alarm::AlarmEngine engine{};
    EXPECT(engine.add_rule(rule(1)) == alarm::AlarmError::none);
    EXPECT(cache.upsert(signal("engine.test", 1100, 0), 1000).accepted());
    alarm::AlarmCacheEvaluator evaluator{cache, engine};
    EXPECT(evaluator.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 1> events{};
    EXPECT(evaluator.poll(0, events.data(), events.size()).events_emitted == 1);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::active);

    cache.clear();
    auto poll = evaluator.poll(10, events.data(), events.size());
    EXPECT(poll.polled());
    EXPECT(poll.cache_epoch_changed);
    EXPECT(poll.alarm_runtime_reset);
    EXPECT(poll.snapshots_collected == 0);
    EXPECT(poll.events_emitted == 0);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);
    EXPECT(evaluator.status().epoch_resets == 1);

    EXPECT(cache.upsert(signal("engine.test", 1100, 20), 1000).accepted());
    poll = evaluator.poll(20, events.data(), events.size());
    EXPECT(poll.events_emitted == 1);
    EXPECT(events[0].kind == alarm::AlarmEventKind::asserted);
}

void test_multiple_signals_aggregate_events_and_skip_unruled_state() {
    telemetry::TelemetryCache cache{};
    alarm::AlarmEngine engine{};
    EXPECT(engine.add_rule(rule(1, "engine.one")) == alarm::AlarmError::none);
    EXPECT(engine.add_rule(rule(2, "engine.two")) == alarm::AlarmError::none);
    EXPECT(cache.upsert(signal("engine.one", 1100, 0), 1000).accepted());
    EXPECT(cache.upsert(signal("engine.two", 1200, 0), 1000).accepted());
    EXPECT(cache.upsert(signal("engine.unruled", 1300, 0), 1000).accepted());
    alarm::AlarmCacheEvaluator evaluator{cache, engine};
    EXPECT(evaluator.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 2> events{};

    const auto poll = evaluator.poll(0, events.data(), events.size());
    EXPECT(poll.polled());
    EXPECT(poll.snapshots_collected == 3);
    EXPECT(poll.snapshots_without_rules == 1);
    EXPECT(poll.rules_matched == 2);
    EXPECT(poll.events_emitted == 2);
    EXPECT(events[0].rule_id != events[1].rule_id);
}

void test_alarm_failure_and_poll_clock_regression_are_visible() {
    telemetry::TelemetryCache cache{};
    alarm::AlarmEngine engine{};
    EXPECT(engine.add_rule(rule(1)) == alarm::AlarmError::none);
    EXPECT(engine.add_rule(rule(2, "engine.valid")) ==
           alarm::AlarmError::none);
    EXPECT(cache.upsert(signal("engine.valid", 1200, 0), 1000).accepted());
    EXPECT(cache.upsert(signal("engine.test", 1100, 0,
                               telemetry::SignalUnit::millivolt),
                        1000).accepted());
    alarm::AlarmCacheEvaluator evaluator{cache, engine};
    EXPECT(evaluator.start() == alarm::AlarmError::none);
    std::array<alarm::AlarmEvent, 2> events{};
    auto poll = evaluator.poll(0, events.data(), events.size());
    EXPECT(poll.error == alarm::AlarmCacheEvaluatorError::alarm_failure);
    EXPECT(poll.alarm_error == alarm::AlarmError::incompatible_signal);
    EXPECT(engine.state(1).lifecycle == alarm::AlarmLifecycle::inactive);
    EXPECT(engine.state(2).lifecycle == alarm::AlarmLifecycle::inactive);

    cache.clear();
    EXPECT(cache.upsert(signal("engine.test", 1100, 100), 1000).accepted());
    poll = evaluator.poll(99, events.data(), events.size());
    EXPECT(poll.error == alarm::AlarmCacheEvaluatorError::cache_failure);
    EXPECT(poll.cache_error == telemetry::CacheError::freshness_error);
    poll = evaluator.poll(100, events.data(), events.size());
    EXPECT(poll.polled());
    EXPECT(poll.events_emitted == 1);
    EXPECT(evaluator.poll(99, events.data(), events.size()).error ==
           alarm::AlarmCacheEvaluatorError::clock_regressed);
    EXPECT(engine.state(1).last_evaluated_at_ms == 100);
}

}  // namespace

int main() {
    test_lifecycle_and_output_capacity_are_fail_closed();
    test_full_state_poll_advances_debounce_without_value_change();
    test_exact_cache_stale_boundary_asserts_without_numeric_value();
    test_full_state_poll_advances_rate_bounded_reminders();
    test_cache_epoch_change_resets_alarm_runtime_without_fake_clear();
    test_multiple_signals_aggregate_events_and_skip_unruled_state();
    test_alarm_failure_and_poll_clock_regression_are_visible();

    if (failures != 0) {
        std::cerr << failures << " alarm cache evaluator assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 alarm cache evaluator scenario groups\n";
    return EXIT_SUCCESS;
}
