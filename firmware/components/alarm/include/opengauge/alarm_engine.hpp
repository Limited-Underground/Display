#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/telemetry_cache.hpp"

namespace opengauge::alarm {

inline constexpr std::size_t kMaximumAlarmRules = 16;
inline constexpr std::uint32_t kMaximumAlarmDurationMs = 86400000U;

enum class AlarmComparison : std::uint8_t {
    above_or_equal = 1,
    below_or_equal = 2,
    outside_inclusive_range = 3,
};

enum class AlarmSeverity : std::uint8_t {
    notice = 1,
    caution = 2,
    warning = 3,
    critical = 4,
};

enum class NonvalidSignalBehavior : std::uint8_t {
    clear_condition = 1,
    hold_state = 2,
    assert_alarm = 3,
};

enum class AlarmLifecycle : std::uint8_t {
    inactive = 0,
    pending_assert,
    active,
    pending_clear,
    latched,
};

enum class AlarmEventKind : std::uint8_t {
    asserted = 1,
    condition_cleared_latched = 2,
    cleared = 3,
    acknowledged = 4,
    reminder = 5,
};

enum class AlarmError : std::uint8_t {
    none = 0,
    no_event,
    invalid_argument,
    invalid_state,
    invalid_rule,
    duplicate_rule,
    capacity_full,
    no_matching_rule,
    insufficient_output_capacity,
    invalid_signal,
    incompatible_signal,
    clock_regressed,
    rule_not_found,
    not_active,
    already_acknowledged,
};

struct AlarmRule {
    std::uint16_t id{0};
    telemetry::SignalId signal_id{};
    telemetry::SignalValueType value_type{
        telemetry::SignalValueType::signed_integer};
    telemetry::SignalUnit unit{telemetry::SignalUnit::none};
    AlarmComparison comparison{AlarmComparison::above_or_equal};
    std::int64_t threshold_low{0};
    std::int64_t threshold_high{0};
    std::uint64_t hysteresis_raw{0};
    std::uint32_t assert_debounce_ms{0};
    std::uint32_t clear_debounce_ms{0};
    std::uint32_t reminder_interval_ms{0};
    AlarmSeverity severity{AlarmSeverity::warning};
    NonvalidSignalBehavior nonvalid_behavior{
        NonvalidSignalBehavior::clear_condition};
    bool latching{false};
};

struct AlarmEvent {
    std::uint16_t rule_id{0};
    AlarmEventKind kind{AlarmEventKind::asserted};
    AlarmSeverity severity{AlarmSeverity::warning};
    AlarmLifecycle lifecycle_after{AlarmLifecycle::inactive};
    telemetry::SignalQuality signal_quality{
        telemetry::SignalQuality::unknown};
    telemetry::SignalValue value{};
    telemetry::SignalUnit unit{telemetry::SignalUnit::none};
    std::uint64_t occurred_at_ms{0};
    std::uint64_t active_duration_ms{0};
    bool condition_present{false};
    bool acknowledged{false};
};

struct AlarmEvaluationResult {
    AlarmError error{AlarmError::invalid_state};
    std::size_t rules_matched{0};
    std::size_t state_changes{0};
    std::size_t events_emitted{0};

    [[nodiscard]] constexpr bool evaluated() const {
        return error == AlarmError::none;
    }
};

struct AlarmSnapshotValidationResult {
    AlarmError error{AlarmError::invalid_state};
    std::size_t rules_matched{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == AlarmError::none;
    }
};

struct AlarmAcknowledgeResult {
    AlarmError error{AlarmError::rule_not_found};
    AlarmEvent event{};
    bool event_emitted{false};

    [[nodiscard]] constexpr bool acknowledged() const {
        return error == AlarmError::none;
    }
};

struct AlarmRuleState {
    AlarmError error{AlarmError::rule_not_found};
    AlarmLifecycle lifecycle{AlarmLifecycle::inactive};
    bool condition_present{false};
    bool acknowledged{false};
    telemetry::SignalQuality last_quality{
        telemetry::SignalQuality::unknown};
    std::uint64_t active_since_ms{0};
    std::uint64_t last_evaluated_at_ms{0};
    bool has_evaluated{false};

    [[nodiscard]] constexpr bool found() const {
        return error == AlarmError::none;
    }
};

struct AlarmEngineStatus {
    bool running{false};
    std::size_t rule_count{0};
    std::size_t active_or_latched_count{0};
    std::uint32_t evaluations{0};
    std::uint32_t assertions{0};
    std::uint32_t clears{0};
    std::uint32_t acknowledgements{0};
    std::uint32_t reminders{0};
};

[[nodiscard]] AlarmError validate_alarm_rule(const AlarmRule& rule);

// Fixed-capacity, single-owner alarm state machine. It consumes normalized
// cache snapshots and never reads raw vehicle frames. Each evaluation can emit
// at most one event per matching rule; the caller supplies bounded storage.
class AlarmEngine {
public:
    [[nodiscard]] AlarmError add_rule(const AlarmRule& rule);
    [[nodiscard]] AlarmError clear_rules();
    [[nodiscard]] AlarmError start();
    void stop();

    [[nodiscard]] AlarmEvaluationResult evaluate(
        const telemetry::CachedSignalSnapshot& snapshot,
        std::uint64_t now_ms,
        AlarmEvent* events,
        std::size_t event_capacity);

    [[nodiscard]] AlarmSnapshotValidationResult validate_snapshot(
        const telemetry::CachedSignalSnapshot& snapshot) const;

    [[nodiscard]] AlarmAcknowledgeResult acknowledge(
        std::uint16_t rule_id,
        std::uint64_t now_ms);

    [[nodiscard]] AlarmRuleState state(std::uint16_t rule_id) const;
    [[nodiscard]] AlarmEngineStatus status() const;

private:
    enum class TriggerSide : std::uint8_t {
        none = 0,
        low,
        high,
        nonvalid,
    };

    struct RuleSlot {
        AlarmRule rule{};
        AlarmLifecycle lifecycle{AlarmLifecycle::inactive};
        TriggerSide trigger_side{TriggerSide::none};
        telemetry::SignalQuality last_quality{
            telemetry::SignalQuality::unknown};
        telemetry::SignalValue last_value{};
        std::uint64_t pending_since_ms{0};
        std::uint64_t active_since_ms{0};
        std::uint64_t last_evaluated_at_ms{0};
        std::uint64_t last_event_at_ms{0};
        bool condition_present{false};
        bool acknowledged{false};
        bool has_evaluated{false};
        bool has_event{false};
        bool occupied{false};
    };

    [[nodiscard]] std::size_t find_rule(std::uint16_t id) const;
    [[nodiscard]] AlarmEvent make_event(
        const RuleSlot& slot,
        AlarmEventKind kind,
        std::uint64_t now_ms) const;
    void reset_runtime(RuleSlot& slot);

    std::array<RuleSlot, kMaximumAlarmRules> rules_{};
    std::size_t rule_count_{0};
    AlarmEngineStatus status_{};
};

}  // namespace opengauge::alarm
