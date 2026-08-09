#pragma once

#include <cstddef>
#include <cstdint>

#include "opengauge/alarm_engine.hpp"

namespace opengauge::alarm {

enum class AlarmCacheEvaluatorError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_argument,
    insufficient_output_capacity,
    clock_regressed,
    cache_failure,
    alarm_failure,
};

struct AlarmCachePollResult {
    AlarmCacheEvaluatorError error{
        AlarmCacheEvaluatorError::invalid_state};
    telemetry::CacheError cache_error{telemetry::CacheError::none};
    AlarmError alarm_error{AlarmError::none};
    std::size_t snapshots_collected{0};
    std::size_t snapshots_without_rules{0};
    std::size_t rules_matched{0};
    std::size_t state_changes{0};
    std::size_t events_emitted{0};
    bool cache_epoch_changed{false};
    bool alarm_runtime_reset{false};

    [[nodiscard]] constexpr bool polled() const {
        return error == AlarmCacheEvaluatorError::none;
    }
};

struct AlarmCacheEvaluatorStatus {
    bool running{false};
    std::uint32_t cache_epoch{0};
    std::uint32_t polls_completed{0};
    std::uint32_t epoch_resets{0};
    std::uint32_t snapshots_evaluated{0};
    std::uint32_t events_emitted{0};
};

// Bounded single-owner bridge. Every poll collects the cache's complete latest
// state (maximum 16 snapshots), not only changed generations, so debounce,
// staleness, and reminder time can advance while a numeric value is unchanged.
class AlarmCacheEvaluator {
public:
    AlarmCacheEvaluator(
        telemetry::TelemetryCache& cache,
        AlarmEngine& engine);

    [[nodiscard]] AlarmError start();
    void stop();

    [[nodiscard]] AlarmCachePollResult poll(
        std::uint64_t now_ms,
        AlarmEvent* events,
        std::size_t event_capacity);

    [[nodiscard]] AlarmCacheEvaluatorStatus status() const;

private:
    [[nodiscard]] AlarmError reset_alarm_runtime();

    telemetry::TelemetryCache& cache_;
    AlarmEngine& engine_;
    AlarmCacheEvaluatorStatus status_{};
    std::uint64_t last_poll_at_ms_{0};
    bool has_polled_{false};
    bool epoch_initialized_{false};
};

}  // namespace opengauge::alarm
