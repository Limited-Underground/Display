#include "opengauge/alarm_engine.hpp"

#include <limits>
#include <string_view>

namespace opengauge::alarm {
namespace {

constexpr std::size_t kNotFound =
    std::numeric_limits<std::size_t>::max();

bool known_value_type(telemetry::SignalValueType type) {
    switch (type) {
        case telemetry::SignalValueType::boolean:
        case telemetry::SignalValueType::signed_integer:
        case telemetry::SignalValueType::unsigned_integer:
            return true;
    }
    return false;
}

bool known_unit(telemetry::SignalUnit unit) {
    switch (unit) {
        case telemetry::SignalUnit::none:
        case telemetry::SignalUnit::count:
        case telemetry::SignalUnit::milli_celsius:
        case telemetry::SignalUnit::pascal:
        case telemetry::SignalUnit::millivolt:
        case telemetry::SignalUnit::milliampere:
        case telemetry::SignalUnit::milli_percent:
        case telemetry::SignalUnit::milli_revolutions_per_minute:
        case telemetry::SignalUnit::millimetres_per_second:
            return true;
    }
    return false;
}

bool known_quality(telemetry::SignalQuality quality) {
    switch (quality) {
        case telemetry::SignalQuality::valid:
        case telemetry::SignalQuality::suspect:
        case telemetry::SignalQuality::unavailable:
        case telemetry::SignalQuality::error:
        case telemetry::SignalQuality::out_of_range:
        case telemetry::SignalQuality::stale:
        case telemetry::SignalQuality::unknown:
            return true;
    }
    return false;
}

bool consistent_effective_quality(
    const telemetry::CachedSignalSnapshot& snapshot) {
    if (snapshot.effective_quality == snapshot.signal.quality) {
        return true;
    }
    return snapshot.effective_quality == telemetry::SignalQuality::stale &&
           (snapshot.signal.quality == telemetry::SignalQuality::valid ||
            snapshot.signal.quality == telemetry::SignalQuality::suspect);
}

bool known_comparison(AlarmComparison comparison) {
    switch (comparison) {
        case AlarmComparison::above_or_equal:
        case AlarmComparison::below_or_equal:
        case AlarmComparison::outside_inclusive_range:
            return true;
    }
    return false;
}

bool known_severity(AlarmSeverity severity) {
    switch (severity) {
        case AlarmSeverity::notice:
        case AlarmSeverity::caution:
        case AlarmSeverity::warning:
        case AlarmSeverity::critical:
            return true;
    }
    return false;
}

bool known_nonvalid_behavior(NonvalidSignalBehavior behavior) {
    switch (behavior) {
        case NonvalidSignalBehavior::clear_condition:
        case NonvalidSignalBehavior::hold_state:
        case NonvalidSignalBehavior::assert_alarm:
            return true;
    }
    return false;
}

bool canonical_signal_id(const telemetry::SignalId& id) {
    if (id.length == 0 || id.length > telemetry::kMaximumSignalIdBytes ||
        id.bytes[id.length] != '\0') {
        return false;
    }
    telemetry::SignalId canonical{};
    const auto error = telemetry::make_signal_id(
        std::string_view(id.bytes.data(), id.length), canonical);
    if (error != telemetry::SignalModelError::none ||
        canonical.length != id.length) {
        return false;
    }
    for (std::size_t index = 0; index < id.bytes.size(); ++index) {
        if (canonical.bytes[index] != id.bytes[index]) {
            return false;
        }
    }
    return true;
}

bool active_lifecycle(AlarmLifecycle lifecycle) {
    return lifecycle == AlarmLifecycle::active ||
           lifecycle == AlarmLifecycle::pending_clear ||
           lifecycle == AlarmLifecycle::latched;
}

std::uint64_t ordered_difference(
    std::int64_t high,
    std::int64_t low) {
    return static_cast<std::uint64_t>(high) -
           static_cast<std::uint64_t>(low);
}

}  // namespace

AlarmError validate_alarm_rule(const AlarmRule& rule) {
    if (rule.id == 0 || !canonical_signal_id(rule.signal_id) ||
        !known_value_type(rule.value_type) || !known_unit(rule.unit) ||
        !known_comparison(rule.comparison) ||
        !known_severity(rule.severity) ||
        !known_nonvalid_behavior(rule.nonvalid_behavior) ||
        rule.hysteresis_raw >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max()) ||
        rule.assert_debounce_ms > kMaximumAlarmDurationMs ||
        rule.clear_debounce_ms > kMaximumAlarmDurationMs ||
        rule.reminder_interval_ms > kMaximumAlarmDurationMs) {
        return AlarmError::invalid_rule;
    }
    if (rule.comparison == AlarmComparison::outside_inclusive_range) {
        if (rule.threshold_low >= rule.threshold_high) {
            return AlarmError::invalid_rule;
        }
        const auto width = ordered_difference(
            rule.threshold_high, rule.threshold_low);
        if (rule.hysteresis_raw > width / 2U) {
            return AlarmError::invalid_rule;
        }
    } else if (rule.threshold_high != 0) {
        return AlarmError::invalid_rule;
    }
    if (rule.value_type == telemetry::SignalValueType::boolean) {
        if (rule.comparison ==
                AlarmComparison::outside_inclusive_range ||
            (rule.threshold_low != 0 && rule.threshold_low != 1) ||
            rule.hysteresis_raw > 1) {
            return AlarmError::invalid_rule;
        }
    } else if (rule.value_type ==
                   telemetry::SignalValueType::unsigned_integer &&
               (rule.threshold_low < 0 ||
                (rule.comparison ==
                     AlarmComparison::outside_inclusive_range &&
                 rule.threshold_high < 0))) {
        return AlarmError::invalid_rule;
    }
    return AlarmError::none;
}

AlarmError AlarmEngine::add_rule(const AlarmRule& rule) {
    if (status_.running) {
        return AlarmError::invalid_state;
    }
    const auto validation = validate_alarm_rule(rule);
    if (validation != AlarmError::none) {
        return validation;
    }
    if (find_rule(rule.id) != kNotFound) {
        return AlarmError::duplicate_rule;
    }
    if (rule_count_ == rules_.size()) {
        return AlarmError::capacity_full;
    }
    for (auto& slot : rules_) {
        if (!slot.occupied) {
            slot = {};
            slot.rule = rule;
            slot.occupied = true;
            ++rule_count_;
            status_.rule_count = rule_count_;
            return AlarmError::none;
        }
    }
    return AlarmError::capacity_full;
}

AlarmError AlarmEngine::clear_rules() {
    if (status_.running) {
        return AlarmError::invalid_state;
    }
    rules_ = {};
    rule_count_ = 0;
    status_ = {};
    return AlarmError::none;
}

AlarmError AlarmEngine::start() {
    if (status_.running) {
        return AlarmError::invalid_state;
    }
    if (rule_count_ == 0) {
        return AlarmError::invalid_state;
    }
    for (auto& slot : rules_) {
        if (slot.occupied) {
            reset_runtime(slot);
        }
    }
    status_ = {};
    status_.running = true;
    status_.rule_count = rule_count_;
    return AlarmError::none;
}

void AlarmEngine::stop() {
    for (auto& slot : rules_) {
        if (slot.occupied) {
            reset_runtime(slot);
        }
    }
    status_.running = false;
    status_.active_or_latched_count = 0;
}

AlarmEvaluationResult AlarmEngine::evaluate(
    const telemetry::CachedSignalSnapshot& snapshot,
    std::uint64_t now_ms,
    AlarmEvent* events,
    std::size_t event_capacity) {
    if (!status_.running) {
        return {AlarmError::invalid_state};
    }
    if (events == nullptr && event_capacity != 0) {
        return {AlarmError::invalid_argument};
    }
    const auto validation = validate_snapshot(snapshot);
    if (validation.error != AlarmError::none) {
        return {validation.error, validation.rules_matched};
    }
    const auto matching = validation.rules_matched;
    for (const auto& slot : rules_) {
        if (slot.occupied && telemetry::signal_id_equals(
                                 snapshot.signal.id,
                                 std::string_view(
                                     slot.rule.signal_id.bytes.data(),
                                     slot.rule.signal_id.length))) {
            if (slot.has_evaluated &&
                now_ms < slot.last_evaluated_at_ms) {
                return {AlarmError::clock_regressed, matching};
            }
        }
    }
    if (event_capacity < matching) {
        return {
            AlarmError::insufficient_output_capacity,
            matching};
    }

    AlarmEvaluationResult result{AlarmError::none, matching};
    for (auto& slot : rules_) {
        if (!slot.occupied || !telemetry::signal_id_equals(
                                  snapshot.signal.id,
                                  std::string_view(
                                      slot.rule.signal_id.bytes.data(),
                                      slot.rule.signal_id.length))) {
            continue;
        }

        enum class Decision : std::uint8_t {
            clear,
            trigger,
            hold,
        };
        Decision decision = Decision::clear;
        TriggerSide side = TriggerSide::none;
        const auto quality = snapshot.effective_quality;
        if (quality != telemetry::SignalQuality::valid) {
            switch (slot.rule.nonvalid_behavior) {
                case NonvalidSignalBehavior::clear_condition:
                    decision = Decision::clear;
                    break;
                case NonvalidSignalBehavior::hold_state:
                    decision = Decision::hold;
                    break;
                case NonvalidSignalBehavior::assert_alarm:
                    decision = Decision::trigger;
                    side = TriggerSide::nonvalid;
                    break;
            }
        } else {
            const auto value = snapshot.signal.value.raw_value;
            const bool applying_hysteresis =
                slot.lifecycle == AlarmLifecycle::active ||
                slot.lifecycle == AlarmLifecycle::pending_clear;
            switch (slot.rule.comparison) {
                case AlarmComparison::above_or_equal:
                    if (value >= slot.rule.threshold_low) {
                        decision = Decision::trigger;
                        side = TriggerSide::high;
                    } else if (!applying_hysteresis ||
                               slot.rule.hysteresis_raw == 0) {
                        decision = Decision::clear;
                    } else {
                        const auto difference = ordered_difference(
                            slot.rule.threshold_low, value);
                        decision =
                            difference >= slot.rule.hysteresis_raw
                                ? Decision::clear
                                : Decision::trigger;
                        side = TriggerSide::high;
                    }
                    break;
                case AlarmComparison::below_or_equal:
                    if (value <= slot.rule.threshold_low) {
                        decision = Decision::trigger;
                        side = TriggerSide::low;
                    } else if (!applying_hysteresis ||
                               slot.rule.hysteresis_raw == 0) {
                        decision = Decision::clear;
                    } else {
                        const auto difference = ordered_difference(
                            value, slot.rule.threshold_low);
                        decision =
                            difference >= slot.rule.hysteresis_raw
                                ? Decision::clear
                                : Decision::trigger;
                        side = TriggerSide::low;
                    }
                    break;
                case AlarmComparison::outside_inclusive_range:
                    if (value <= slot.rule.threshold_low) {
                        decision = Decision::trigger;
                        side = TriggerSide::low;
                    } else if (value >= slot.rule.threshold_high) {
                        decision = Decision::trigger;
                        side = TriggerSide::high;
                    } else if (!applying_hysteresis ||
                               slot.trigger_side == TriggerSide::nonvalid ||
                               slot.rule.hysteresis_raw == 0) {
                        decision = Decision::clear;
                    } else if (slot.trigger_side == TriggerSide::low) {
                        const auto difference = ordered_difference(
                            value, slot.rule.threshold_low);
                        decision =
                            difference >= slot.rule.hysteresis_raw
                                ? Decision::clear
                                : Decision::trigger;
                        side = TriggerSide::low;
                    } else {
                        const auto difference = ordered_difference(
                            slot.rule.threshold_high, value);
                        decision =
                            difference >= slot.rule.hysteresis_raw
                                ? Decision::clear
                                : Decision::trigger;
                        side = TriggerSide::high;
                    }
                    break;
            }
        }

        slot.last_quality = quality;
        slot.last_value = {};
        slot.last_value.type = slot.rule.value_type;
        if (quality == telemetry::SignalQuality::valid) {
            slot.last_value = snapshot.signal.value;
        }
        const auto lifecycle_before = slot.lifecycle;

        auto emit = [&](AlarmEventKind kind) {
            slot.last_event_at_ms = now_ms;
            slot.has_event = true;
            events[result.events_emitted] = make_event(slot, kind, now_ms);
            ++result.events_emitted;
            switch (kind) {
                case AlarmEventKind::asserted:
                    ++status_.assertions;
                    break;
                case AlarmEventKind::cleared:
                    ++status_.clears;
                    break;
                case AlarmEventKind::acknowledged:
                    ++status_.acknowledgements;
                    break;
                case AlarmEventKind::reminder:
                    ++status_.reminders;
                    break;
                case AlarmEventKind::condition_cleared_latched:
                    break;
            }
        };

        auto assert_now = [&]() {
            slot.lifecycle = AlarmLifecycle::active;
            slot.condition_present = true;
            slot.acknowledged = false;
            slot.trigger_side = side;
            slot.active_since_ms = now_ms;
            emit(AlarmEventKind::asserted);
        };

        auto clear_now = [&]() {
            slot.lifecycle = AlarmLifecycle::inactive;
            slot.condition_present = false;
            emit(AlarmEventKind::cleared);
            slot.acknowledged = false;
            slot.trigger_side = TriggerSide::none;
            slot.pending_since_ms = 0;
            slot.active_since_ms = 0;
        };

        if (decision == Decision::hold) {
            if (slot.lifecycle == AlarmLifecycle::pending_assert) {
                slot.lifecycle = AlarmLifecycle::inactive;
                slot.pending_since_ms = 0;
                slot.condition_present = false;
            } else if (slot.lifecycle == AlarmLifecycle::pending_clear) {
                slot.lifecycle = AlarmLifecycle::active;
                slot.pending_since_ms = 0;
                slot.condition_present = true;
            }
        } else {
            const bool trigger = decision == Decision::trigger;
            switch (slot.lifecycle) {
                case AlarmLifecycle::inactive:
                    if (trigger) {
                        if (slot.rule.assert_debounce_ms == 0) {
                            assert_now();
                        } else {
                            slot.lifecycle =
                                AlarmLifecycle::pending_assert;
                            slot.condition_present = true;
                            slot.trigger_side = side;
                            slot.pending_since_ms = now_ms;
                        }
                    }
                    break;
                case AlarmLifecycle::pending_assert:
                    if (!trigger) {
                        slot.lifecycle = AlarmLifecycle::inactive;
                        slot.condition_present = false;
                        slot.trigger_side = TriggerSide::none;
                        slot.pending_since_ms = 0;
                    } else {
                        slot.condition_present = true;
                        slot.trigger_side = side;
                        if (now_ms - slot.pending_since_ms >=
                            slot.rule.assert_debounce_ms) {
                            assert_now();
                        }
                    }
                    break;
                case AlarmLifecycle::active:
                    if (trigger) {
                        slot.condition_present = true;
                        slot.trigger_side = side;
                    } else if (slot.rule.clear_debounce_ms == 0) {
                        if (slot.rule.latching &&
                            !slot.acknowledged) {
                            slot.lifecycle = AlarmLifecycle::latched;
                            slot.condition_present = false;
                            emit(AlarmEventKind::condition_cleared_latched);
                        } else {
                            clear_now();
                        }
                    } else {
                        slot.lifecycle = AlarmLifecycle::pending_clear;
                        slot.condition_present = false;
                        slot.pending_since_ms = now_ms;
                    }
                    break;
                case AlarmLifecycle::pending_clear:
                    if (trigger) {
                        slot.lifecycle = AlarmLifecycle::active;
                        slot.condition_present = true;
                        slot.trigger_side = side;
                        slot.pending_since_ms = 0;
                    } else if (now_ms - slot.pending_since_ms >=
                               slot.rule.clear_debounce_ms) {
                        if (slot.rule.latching &&
                            !slot.acknowledged) {
                            slot.lifecycle = AlarmLifecycle::latched;
                            slot.condition_present = false;
                            emit(AlarmEventKind::condition_cleared_latched);
                        } else {
                            clear_now();
                        }
                    }
                    break;
                case AlarmLifecycle::latched:
                    if (trigger) {
                        slot.lifecycle = AlarmLifecycle::active;
                        slot.condition_present = true;
                        slot.trigger_side = side;
                        slot.acknowledged = false;
                    }
                    break;
            }
        }

        if (result.events_emitted == 0 ||
            events[result.events_emitted - 1].rule_id != slot.rule.id) {
            if ((slot.lifecycle == AlarmLifecycle::active ||
                 slot.lifecycle == AlarmLifecycle::latched) &&
                slot.rule.reminder_interval_ms != 0 &&
                slot.has_event &&
                now_ms - slot.last_event_at_ms >=
                    slot.rule.reminder_interval_ms) {
                emit(AlarmEventKind::reminder);
            }
        }

        slot.last_evaluated_at_ms = now_ms;
        slot.has_evaluated = true;
        if (slot.lifecycle != lifecycle_before) {
            ++result.state_changes;
        }
    }
    ++status_.evaluations;
    return result;
}

AlarmSnapshotValidationResult AlarmEngine::validate_snapshot(
    const telemetry::CachedSignalSnapshot& snapshot) const {
    if (!status_.running) {
        return {AlarmError::invalid_state};
    }
    if (telemetry::validate_normalized_signal(snapshot.signal) !=
            telemetry::SignalModelError::none ||
        !known_quality(snapshot.effective_quality) ||
        !consistent_effective_quality(snapshot)) {
        return {AlarmError::invalid_signal};
    }

    std::size_t matching = 0;
    for (const auto& slot : rules_) {
        if (!slot.occupied || !telemetry::signal_id_equals(
                                  snapshot.signal.id,
                                  std::string_view(
                                      slot.rule.signal_id.bytes.data(),
                                      slot.rule.signal_id.length))) {
            continue;
        }
        ++matching;
        if (snapshot.signal.value.type != slot.rule.value_type ||
            snapshot.signal.unit != slot.rule.unit) {
            return {AlarmError::incompatible_signal, matching};
        }
    }
    return {
        matching == 0 ? AlarmError::no_matching_rule : AlarmError::none,
        matching};
}

AlarmAcknowledgeResult AlarmEngine::acknowledge(
    std::uint16_t rule_id,
    std::uint64_t now_ms) {
    if (!status_.running) {
        return {AlarmError::invalid_state};
    }
    const auto index = find_rule(rule_id);
    if (index == kNotFound) {
        return {AlarmError::rule_not_found};
    }
    auto& slot = rules_[index];
    if (slot.has_evaluated && now_ms < slot.last_evaluated_at_ms) {
        return {AlarmError::clock_regressed};
    }
    if (!active_lifecycle(slot.lifecycle)) {
        return {AlarmError::not_active};
    }
    if (slot.acknowledged) {
        return {AlarmError::already_acknowledged};
    }

    slot.acknowledged = true;
    AlarmEventKind kind = AlarmEventKind::acknowledged;
    if (slot.lifecycle == AlarmLifecycle::latched) {
        slot.lifecycle = AlarmLifecycle::inactive;
        slot.condition_present = false;
        kind = AlarmEventKind::cleared;
    }
    slot.last_event_at_ms = now_ms;
    slot.has_event = true;
    auto event = make_event(slot, kind, now_ms);
    ++status_.acknowledgements;
    if (kind == AlarmEventKind::cleared) {
        ++status_.clears;
        slot.trigger_side = TriggerSide::none;
        slot.pending_since_ms = 0;
        slot.active_since_ms = 0;
    }
    slot.last_evaluated_at_ms = now_ms;
    slot.has_evaluated = true;
    return {AlarmError::none, event, true};
}

AlarmRuleState AlarmEngine::state(std::uint16_t rule_id) const {
    const auto index = find_rule(rule_id);
    if (index == kNotFound) {
        return {AlarmError::rule_not_found};
    }
    const auto& slot = rules_[index];
    return {
        AlarmError::none,
        slot.lifecycle,
        slot.condition_present,
        slot.acknowledged,
        slot.last_quality,
        slot.active_since_ms,
        slot.last_evaluated_at_ms,
        slot.has_evaluated};
}

AlarmEngineStatus AlarmEngine::status() const {
    auto result = status_;
    result.active_or_latched_count = 0;
    for (const auto& slot : rules_) {
        if (slot.occupied && active_lifecycle(slot.lifecycle)) {
            ++result.active_or_latched_count;
        }
    }
    return result;
}

std::size_t AlarmEngine::find_rule(std::uint16_t id) const {
    for (std::size_t index = 0; index < rules_.size(); ++index) {
        if (rules_[index].occupied && rules_[index].rule.id == id) {
            return index;
        }
    }
    return kNotFound;
}

AlarmEvent AlarmEngine::make_event(
    const RuleSlot& slot,
    AlarmEventKind kind,
    std::uint64_t now_ms) const {
    const auto duration =
        active_lifecycle(slot.lifecycle) ||
                kind == AlarmEventKind::cleared
            ? now_ms - slot.active_since_ms
            : 0;
    return {
        slot.rule.id,
        kind,
        slot.rule.severity,
        slot.lifecycle,
        slot.last_quality,
        slot.last_value,
        slot.rule.unit,
        now_ms,
        duration,
        slot.condition_present,
        slot.acknowledged};
}

void AlarmEngine::reset_runtime(RuleSlot& slot) {
    const auto rule = slot.rule;
    slot = {};
    slot.rule = rule;
    slot.occupied = true;
}

}  // namespace opengauge::alarm
