#include "opengauge/diagnostics.hpp"

#include <algorithm>
#include <limits>

namespace opengauge::diagnostics {
namespace {

bool known_level(LogLevel level) {
    const auto value = static_cast<std::uint8_t>(level);
    return value >= static_cast<std::uint8_t>(LogLevel::error) &&
           value <= static_cast<std::uint8_t>(LogLevel::trace);
}

bool known_event(EventCode code) {
    const auto value = static_cast<std::uint8_t>(code);
    return value >= static_cast<std::uint8_t>(EventCode::startup) &&
           value <= static_cast<std::uint8_t>(EventCode::health_checkpoint);
}

bool known_metric(MetricCode metric) {
    return static_cast<std::uint8_t>(metric) <=
           static_cast<std::uint8_t>(MetricCode::age_ms);
}

bool known_reset_reason(ResetReason reason) {
    return static_cast<std::uint8_t>(reason) <=
           static_cast<std::uint8_t>(ResetReason::deep_sleep);
}

bool known_counter(CounterCode counter) {
    return static_cast<std::size_t>(counter) < kDiagnosticCounterCount;
}

bool level_is_enabled(LogLevel level, LogLevel threshold) {
    return static_cast<std::uint8_t>(level) <=
           static_cast<std::uint8_t>(threshold);
}

}  // namespace

DiagnosticsError DiagnosticsService::start(
    const DiagnosticsConfiguration& configuration,
    std::uint64_t now_ms) {
    if (status_.running) {
        return DiagnosticsError::invalid_state;
    }
    if (!known_level(configuration.threshold) ||
        !known_reset_reason(configuration.reset_reason)) {
        return DiagnosticsError::invalid_argument;
    }

    events_ = {};
    event_start_ = 0;
    event_count_ = 0;
    status_ = {};
    status_.running = true;
    status_.threshold = configuration.threshold;
    status_.reset_reason = configuration.reset_reason;
    status_.next_event_sequence = 1;
    increment_internal(CounterCode::reset_count, 1);

    const auto startup = record(
        LogLevel::info,
        EventCode::startup,
        MetricCode::reset_reason,
        static_cast<std::int64_t>(configuration.reset_reason),
        now_ms);
    return startup.error;
}

void DiagnosticsService::stop() {
    status_.running = false;
}

DiagnosticsError DiagnosticsService::set_threshold(LogLevel threshold) {
    if (!status_.running) {
        return DiagnosticsError::invalid_state;
    }
    if (!known_level(threshold)) {
        return DiagnosticsError::invalid_argument;
    }
    status_.threshold = threshold;
    return DiagnosticsError::none;
}

DiagnosticRecordResult DiagnosticsService::record(
    LogLevel level,
    EventCode code,
    MetricCode metric,
    std::int64_t value,
    std::uint64_t now_ms) {
    if (!status_.running) {
        return {DiagnosticsError::invalid_state};
    }
    if (!known_level(level) || !known_event(code) || !known_metric(metric) ||
        (metric == MetricCode::none && value != 0)) {
        return {DiagnosticsError::invalid_argument};
    }
    if (status_.has_timestamp && now_ms < status_.last_monotonic_ms) {
        return {DiagnosticsError::time_regression};
    }
    status_.has_timestamp = true;
    status_.last_monotonic_ms = now_ms;

    if (!level_is_enabled(level, status_.threshold)) {
        return {DiagnosticsError::none, false, 0};
    }

    std::size_t index = 0;
    if (event_count_ < events_.size()) {
        index = (event_start_ + event_count_) % events_.size();
        ++event_count_;
    } else {
        index = event_start_;
        event_start_ = (event_start_ + 1) % events_.size();
        increment_internal(CounterCode::log_records_dropped, 1);
    }

    const auto sequence = status_.next_event_sequence;
    ++status_.next_event_sequence;
    events_[index] = {sequence, now_ms, level, code, metric, value};
    status_.event_count = event_count_;
    status_.first_event_sequence = events_[event_start_].sequence;
    return {DiagnosticsError::none, true, sequence};
}

DiagnosticsError DiagnosticsService::increment(
    CounterCode counter,
    std::uint32_t amount) {
    if (!status_.running) {
        return DiagnosticsError::invalid_state;
    }
    if (!known_counter(counter) || amount == 0 ||
        counter == CounterCode::reset_count ||
        counter == CounterCode::log_records_dropped) {
        return DiagnosticsError::invalid_argument;
    }
    increment_internal(counter, amount);
    return DiagnosticsError::none;
}

DiagnosticSnapshotResult DiagnosticsService::snapshot_events(
    DiagnosticEvent* output,
    std::size_t output_capacity) const {
    if (!status_.running) {
        return {DiagnosticsError::invalid_state};
    }
    if ((output == nullptr && output_capacity != 0) ||
        output_capacity < event_count_) {
        return {output_capacity < event_count_
                    ? DiagnosticsError::insufficient_output_capacity
                    : DiagnosticsError::invalid_argument};
    }
    for (std::size_t index = 0; index < event_count_; ++index) {
        output[index] = events_[(event_start_ + index) % events_.size()];
    }
    return {DiagnosticsError::none, event_count_};
}

DiagnosticsError DiagnosticsService::clear_events() {
    if (!status_.running) {
        return DiagnosticsError::invalid_state;
    }
    events_ = {};
    event_start_ = 0;
    event_count_ = 0;
    status_.event_count = 0;
    status_.first_event_sequence = 0;
    return DiagnosticsError::none;
}

DiagnosticsStatus DiagnosticsService::status() const {
    return status_;
}

void DiagnosticsService::increment_internal(
    CounterCode counter,
    std::uint32_t amount) {
    auto& current = status_.counters[static_cast<std::size_t>(counter)];
    const auto remaining = std::numeric_limits<std::uint32_t>::max() - current;
    current += std::min(amount, remaining);
}

}  // namespace opengauge::diagnostics
