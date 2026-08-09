#include "opengauge/alarm_cache_evaluator.hpp"

#include <array>

namespace opengauge::alarm {

AlarmCacheEvaluator::AlarmCacheEvaluator(
    telemetry::TelemetryCache& cache,
    AlarmEngine& engine)
    : cache_(cache), engine_(engine) {}

AlarmError AlarmCacheEvaluator::start() {
    if (status_.running) {
        return AlarmError::invalid_state;
    }
    const auto error = engine_.start();
    if (error != AlarmError::none) {
        return error;
    }
    status_ = {};
    status_.running = true;
    last_poll_at_ms_ = 0;
    has_polled_ = false;
    epoch_initialized_ = false;
    return AlarmError::none;
}

void AlarmCacheEvaluator::stop() {
    if (status_.running) {
        engine_.stop();
    }
    status_.running = false;
    status_.cache_epoch = 0;
    last_poll_at_ms_ = 0;
    has_polled_ = false;
    epoch_initialized_ = false;
}

AlarmCachePollResult AlarmCacheEvaluator::poll(
    std::uint64_t now_ms,
    AlarmEvent* events,
    std::size_t event_capacity) {
    if (!status_.running) {
        return {AlarmCacheEvaluatorError::invalid_state};
    }
    if (events == nullptr && event_capacity != 0) {
        return {AlarmCacheEvaluatorError::invalid_argument};
    }
    const auto rule_count = engine_.status().rule_count;
    if (event_capacity < rule_count) {
        return {
            AlarmCacheEvaluatorError::insufficient_output_capacity};
    }
    if (has_polled_ && now_ms < last_poll_at_ms_) {
        return {AlarmCacheEvaluatorError::clock_regressed};
    }

    AlarmCachePollResult result{AlarmCacheEvaluatorError::none};
    auto current = cache_.current_cursor();
    if (epoch_initialized_ && current.epoch != status_.cache_epoch) {
        const auto reset = reset_alarm_runtime();
        if (reset != AlarmError::none) {
            return {
                AlarmCacheEvaluatorError::alarm_failure,
                telemetry::CacheError::none,
                reset};
        }
        result.cache_epoch_changed = true;
        result.alarm_runtime_reset = true;
        ++status_.epoch_resets;
    }
    status_.cache_epoch = current.epoch;
    epoch_initialized_ = true;

    std::array<telemetry::CachedSignalSnapshot,
               telemetry::kTelemetryCacheCapacity>
        snapshots{};
    auto collected = cache_.collect_changes(
        {status_.cache_epoch, 0},
        now_ms,
        snapshots.data(),
        snapshots.size());
    if (collected.error == telemetry::CacheError::cursor_epoch_mismatch) {
        const auto reset = reset_alarm_runtime();
        if (reset != AlarmError::none) {
            return {
                AlarmCacheEvaluatorError::alarm_failure,
                collected.error,
                reset};
        }
        current = cache_.current_cursor();
        status_.cache_epoch = current.epoch;
        collected = cache_.collect_changes(
            {status_.cache_epoch, 0},
            now_ms,
            snapshots.data(),
            snapshots.size());
        result.cache_epoch_changed = true;
        result.alarm_runtime_reset = true;
        ++status_.epoch_resets;
    }
    if (!collected.collected()) {
        return {
            AlarmCacheEvaluatorError::cache_failure,
            collected.error};
    }

    result.snapshots_collected = collected.snapshot_count;
    for (std::size_t index = 0; index < collected.snapshot_count; ++index) {
        const auto validation = engine_.validate_snapshot(snapshots[index]);
        if (validation.error == AlarmError::no_matching_rule) {
            continue;
        }
        if (!validation.accepted()) {
            return {
                AlarmCacheEvaluatorError::alarm_failure,
                telemetry::CacheError::none,
                validation.error,
                result.snapshots_collected,
                0,
                0,
                0,
                0,
                result.cache_epoch_changed,
                result.alarm_runtime_reset};
        }
    }
    for (std::size_t index = 0; index < collected.snapshot_count; ++index) {
        auto evaluated = engine_.evaluate(
            snapshots[index],
            now_ms,
            events + result.events_emitted,
            event_capacity - result.events_emitted);
        if (evaluated.error == AlarmError::no_matching_rule) {
            ++result.snapshots_without_rules;
            continue;
        }
        if (!evaluated.evaluated()) {
            result.alarm_error = evaluated.error;
            return {
                AlarmCacheEvaluatorError::alarm_failure,
                telemetry::CacheError::none,
                evaluated.error,
                result.snapshots_collected,
                result.snapshots_without_rules,
                result.rules_matched,
                result.state_changes,
                result.events_emitted,
                result.cache_epoch_changed,
                result.alarm_runtime_reset};
        }
        result.rules_matched += evaluated.rules_matched;
        result.state_changes += evaluated.state_changes;
        result.events_emitted += evaluated.events_emitted;
        ++status_.snapshots_evaluated;
    }

    last_poll_at_ms_ = now_ms;
    has_polled_ = true;
    ++status_.polls_completed;
    status_.events_emitted +=
        static_cast<std::uint32_t>(result.events_emitted);
    return result;
}

AlarmCacheEvaluatorStatus AlarmCacheEvaluator::status() const {
    return status_;
}

AlarmError AlarmCacheEvaluator::reset_alarm_runtime() {
    engine_.stop();
    return engine_.start();
}

}  // namespace opengauge::alarm
