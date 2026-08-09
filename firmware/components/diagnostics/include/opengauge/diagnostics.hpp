#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opengauge::diagnostics {

inline constexpr std::size_t kDiagnosticEventCapacity = 32;

enum class LogLevel : std::uint8_t {
    error = 1,
    warning = 2,
    info = 3,
    debug = 4,
    trace = 5,
};

enum class EventCode : std::uint8_t {
    startup = 1,
    can_state_changed = 2,
    decoder_rejected_frame = 3,
    cache_state_changed = 4,
    wireless_state_changed = 5,
    render_timing = 6,
    update_state_changed = 7,
    configuration_recovery = 8,
    health_checkpoint = 9,
};

enum class MetricCode : std::uint8_t {
    none = 0,
    reset_reason = 1,
    count = 2,
    duration_us = 3,
    queue_depth = 4,
    error_code = 5,
    state_code = 6,
    age_ms = 7,
};

enum class ResetReason : std::uint8_t {
    unknown = 0,
    power_on = 1,
    software = 2,
    watchdog = 3,
    brownout = 4,
    panic = 5,
    deep_sleep = 6,
};

enum class CounterCode : std::uint8_t {
    can_frames_received = 0,
    can_errors = 1,
    can_overflows = 2,
    decoder_unknown = 3,
    decoder_invalid = 4,
    cache_stale_transitions = 5,
    wireless_send_accepted = 6,
    wireless_send_failed = 7,
    wireless_receive_accepted = 8,
    wireless_receive_rejected = 9,
    wireless_sequence_gaps = 10,
    render_updates = 11,
    render_deadline_misses = 12,
    update_failures = 13,
    reset_count = 14,
    log_records_dropped = 15,
};

inline constexpr std::size_t kDiagnosticCounterCount = 16;

enum class DiagnosticsError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_argument,
    time_regression,
    insufficient_output_capacity,
};

struct DiagnosticsConfiguration {
    LogLevel threshold{LogLevel::info};
    ResetReason reset_reason{ResetReason::unknown};
};

struct DiagnosticEvent {
    std::uint64_t sequence{0};
    std::uint64_t monotonic_ms{0};
    LogLevel level{LogLevel::info};
    EventCode code{EventCode::startup};
    MetricCode metric{MetricCode::none};
    std::int64_t value{0};
};

struct DiagnosticRecordResult {
    DiagnosticsError error{DiagnosticsError::invalid_state};
    bool stored{false};
    std::uint64_t sequence{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == DiagnosticsError::none;
    }
};

struct DiagnosticSnapshotResult {
    DiagnosticsError error{DiagnosticsError::invalid_state};
    std::size_t event_count{0};

    [[nodiscard]] constexpr bool copied() const {
        return error == DiagnosticsError::none;
    }
};

struct DiagnosticsStatus {
    bool running{false};
    LogLevel threshold{LogLevel::info};
    ResetReason reset_reason{ResetReason::unknown};
    std::size_t event_count{0};
    std::uint64_t first_event_sequence{0};
    std::uint64_t next_event_sequence{1};
    std::array<std::uint32_t, kDiagnosticCounterCount> counters{};
    bool has_timestamp{false};
    std::uint64_t last_monotonic_ms{0};
};

// A bounded structured diagnostics core. It accepts no strings, byte buffers,
// peer addresses, credentials, or vehicle identifiers.
class DiagnosticsService {
public:
    [[nodiscard]] DiagnosticsError start(
        const DiagnosticsConfiguration& configuration,
        std::uint64_t now_ms);
    void stop();

    [[nodiscard]] DiagnosticsError set_threshold(LogLevel threshold);
    [[nodiscard]] DiagnosticRecordResult record(
        LogLevel level,
        EventCode code,
        MetricCode metric,
        std::int64_t value,
        std::uint64_t now_ms);
    [[nodiscard]] DiagnosticsError increment(
        CounterCode counter,
        std::uint32_t amount = 1);

    [[nodiscard]] DiagnosticSnapshotResult snapshot_events(
        DiagnosticEvent* output,
        std::size_t output_capacity) const;
    [[nodiscard]] DiagnosticsError clear_events();
    [[nodiscard]] DiagnosticsStatus status() const;

private:
    void increment_internal(CounterCode counter, std::uint32_t amount);

    std::array<DiagnosticEvent, kDiagnosticEventCapacity> events_{};
    std::size_t event_start_{0};
    std::size_t event_count_{0};
    DiagnosticsStatus status_{};
};

}  // namespace opengauge::diagnostics
