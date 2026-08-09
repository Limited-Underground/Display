#include "opengauge/critical_alert_outbox.hpp"

#include <algorithm>
#include <limits>

namespace opengauge::integration {
namespace {

constexpr std::size_t kNotFound =
    std::numeric_limits<std::size_t>::max();

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) {
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    return right > maximum - left ? maximum : left + right;
}

}  // namespace

CriticalOutboxError CriticalAlertOutbox::start(
    const CriticalAlertOutboxConfiguration& configuration) {
    if (status_.running) {
        return CriticalOutboxError::invalid_state;
    }
    if (configuration.local_commit_timeout_ms == 0 ||
        configuration.acknowledgement_timeout_ms == 0 ||
        configuration.retry_backoff_ms == 0 ||
        configuration.maximum_lifetime_ms <=
            configuration.acknowledgement_timeout_ms ||
        configuration.maximum_attempts == 0 ||
        configuration.emergency_reserve == 0 ||
        configuration.emergency_reserve >= kCriticalAlertOutboxCapacity) {
        return CriticalOutboxError::invalid_configuration;
    }
    configuration_ = configuration;
    entries_ = {};
    has_clock_ = false;
    last_monotonic_ms_ = 0;
    status_ = {};
    status_.running = true;
    status_.next_token = 1;
    return CriticalOutboxError::none;
}

void CriticalAlertOutbox::stop() {
    status_.running = false;
}

CriticalOutboxError CriticalAlertOutbox::enqueue(
    const std::array<std::uint8_t, kCriticalAlertFrameBytes>& frame,
    std::uint64_t now_ms) {
    if (!status_.running) {
        return CriticalOutboxError::invalid_state;
    }
    const auto clock = advance_clock(now_ms);
    if (clock != CriticalOutboxError::none) {
        return clock;
    }
    const auto decoded = decode_critical_alert(frame.data(), frame.size());
    if (!decoded.decoded()) {
        return CriticalOutboxError::invalid_frame;
    }
    if (find_event(decoded.alert.event_id) != kNotFound) {
        return CriticalOutboxError::duplicate_event;
    }
    const auto occupied = status_.queued_count + status_.in_flight_count;
    if (decoded.alert.severity != AlertSeverity::emergency &&
        occupied >= entries_.size() - configuration_.emergency_reserve) {
        return CriticalOutboxError::capacity_full;
    }
    auto slot = kNotFound;
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].state == EntryState::empty) {
            slot = index;
            break;
        }
    }
    if (slot == kNotFound) {
        return CriticalOutboxError::capacity_full;
    }
    entries_[slot].state = EntryState::queued;
    entries_[slot].alert = decoded.alert;
    entries_[slot].frame = frame;
    entries_[slot].enqueued_ms = now_ms;
    entries_[slot].state_changed_ms = now_ms;
    entries_[slot].next_attempt_ms = now_ms;
    saturating_increment(status_.enqueued);
    refresh_counts();
    return CriticalOutboxError::none;
}

PreparedCriticalAlert CriticalAlertOutbox::prepare(std::uint64_t now_ms) {
    if (!status_.running || status_.send_prepared) {
        return {CriticalOutboxError::invalid_state};
    }
    const auto clock = advance_clock(now_ms);
    if (clock != CriticalOutboxError::none) {
        return {clock};
    }
    auto selected = kNotFound;
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        const auto& entry = entries_[index];
        if (entry.state == EntryState::queued &&
            now_ms >= entry.next_attempt_ms &&
            (selected == kNotFound ||
             static_cast<std::uint8_t>(entry.alert.severity) >
                 static_cast<std::uint8_t>(entries_[selected].alert.severity) ||
             (entry.alert.severity == entries_[selected].alert.severity &&
              entry.enqueued_ms < entries_[selected].enqueued_ms))) {
            selected = index;
        }
    }
    if (selected == kNotFound) {
        return {CriticalOutboxError::no_frame_ready};
    }
    auto& entry = entries_[selected];
    auto token = status_.next_token;
    ++status_.next_token;
    if (status_.next_token == 0) {
        status_.next_token = 1;
    }
    entry.state = EntryState::prepared;
    entry.state_changed_ms = now_ms;
    entry.token = token;
    status_.send_prepared = true;
    refresh_counts();
    return {
        CriticalOutboxError::none,
        token,
        entry.alert.event_id,
        entry.frame};
}

CriticalOutboxError CriticalAlertOutbox::commit_local_send(
    std::uint32_t token,
    bool locally_accepted,
    std::uint64_t now_ms) {
    if (!status_.running || !status_.send_prepared) {
        return CriticalOutboxError::invalid_state;
    }
    const auto clock = advance_clock(now_ms);
    if (clock != CriticalOutboxError::none) {
        return clock;
    }
    const auto index = find_token(token);
    if (index == kNotFound) {
        return CriticalOutboxError::token_mismatch;
    }
    auto& entry = entries_[index];
    entry.token = 0;
    entry.state_changed_ms = now_ms;
    status_.send_prepared = false;
    if (locally_accepted) {
        entry.state = EntryState::in_flight;
        ++entry.attempts;
        saturating_increment(status_.local_acceptances);
    } else {
        entry.state = EntryState::queued;
        entry.next_attempt_ms = saturating_add(
            now_ms, configuration_.retry_backoff_ms);
        saturating_increment(status_.local_rejections);
    }
    refresh_counts();
    return CriticalOutboxError::none;
}

CriticalOutboxError CriticalAlertOutbox::acknowledge(
    const CriticalAlertAcknowledgement& acknowledgement,
    std::uint64_t now_ms) {
    if (!status_.running) {
        return CriticalOutboxError::invalid_state;
    }
    const auto clock = advance_clock(now_ms);
    if (clock != CriticalOutboxError::none) {
        return clock;
    }
    const auto validation = validate_acknowledgement(acknowledgement);
    if (validation != CriticalOutboxError::none) {
        return validation;
    }
    const auto index = find_event(acknowledgement.event_id);
    if (entries_[index].state == EntryState::prepared) {
        status_.send_prepared = false;
    }
    remove_entry(index);
    saturating_increment(status_.acknowledgements);
    refresh_counts();
    return CriticalOutboxError::none;
}

CriticalOutboxError CriticalAlertOutbox::validate_acknowledgement(
    const CriticalAlertAcknowledgement& acknowledgement) const {
    if (!status_.running) {
        return CriticalOutboxError::invalid_state;
    }
    const auto index = find_event(acknowledgement.event_id);
    if (index == kNotFound || entries_[index].attempts == 0 ||
        entries_[index].alert.condition_id != acknowledgement.condition_id ||
        entries_[index].alert.state != acknowledgement.state) {
        return CriticalOutboxError::acknowledgement_mismatch;
    }
    return CriticalOutboxError::none;
}

CriticalOutboxAdvanceResult CriticalAlertOutbox::advance(
    std::uint64_t now_ms) {
    if (!status_.running) {
        return {CriticalOutboxError::invalid_state};
    }
    const auto clock = advance_clock(now_ms);
    if (clock != CriticalOutboxError::none) {
        return {clock};
    }
    CriticalOutboxAdvanceResult result{CriticalOutboxError::none};
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        auto& entry = entries_[index];
        if (entry.state == EntryState::empty) {
            continue;
        }
        const auto lifetime = now_ms - entry.enqueued_ms;
        if (lifetime >= configuration_.maximum_lifetime_ms) {
            result.failures[result.failure_count++] = {
                entry.alert.event_id,
                entry.alert.condition_id,
                CriticalDeliveryFailure::maximum_lifetime,
                entry.attempts};
            if (entry.state == EntryState::prepared) {
                status_.send_prepared = false;
            }
            remove_entry(index);
            saturating_increment(status_.terminal_failures);
            continue;
        }
        if (entry.state == EntryState::prepared &&
            now_ms - entry.state_changed_ms >=
                configuration_.local_commit_timeout_ms) {
            entry.state = EntryState::queued;
            entry.token = 0;
            entry.state_changed_ms = now_ms;
            entry.next_attempt_ms = saturating_add(
                now_ms, configuration_.retry_backoff_ms);
            status_.send_prepared = false;
            ++result.prepared_released;
            continue;
        }
        if (entry.state == EntryState::in_flight &&
            now_ms - entry.state_changed_ms >=
                configuration_.acknowledgement_timeout_ms) {
            if (entry.attempts >= configuration_.maximum_attempts) {
                result.failures[result.failure_count++] = {
                    entry.alert.event_id,
                    entry.alert.condition_id,
                    CriticalDeliveryFailure::acknowledgement_timeout,
                    entry.attempts};
                remove_entry(index);
                saturating_increment(status_.terminal_failures);
            } else {
                entry.state = EntryState::queued;
                entry.state_changed_ms = now_ms;
                entry.next_attempt_ms = saturating_add(
                    now_ms, configuration_.retry_backoff_ms);
                ++result.retries_released;
                saturating_increment(status_.retry_timeouts);
            }
        }
    }
    refresh_counts();
    return result;
}

CriticalAlertOutboxStatus CriticalAlertOutbox::status() const {
    return status_;
}

CriticalOutboxError CriticalAlertOutbox::advance_clock(std::uint64_t now_ms) {
    if (has_clock_ && now_ms < last_monotonic_ms_) {
        return CriticalOutboxError::clock_regression;
    }
    has_clock_ = true;
    last_monotonic_ms_ = now_ms;
    return CriticalOutboxError::none;
}

std::size_t CriticalAlertOutbox::find_event(std::uint64_t event_id) const {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].state != EntryState::empty &&
            entries_[index].alert.event_id == event_id) {
            return index;
        }
    }
    return kNotFound;
}

std::size_t CriticalAlertOutbox::find_token(std::uint32_t token) const {
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].state == EntryState::prepared &&
            entries_[index].token == token) {
            return index;
        }
    }
    return kNotFound;
}

void CriticalAlertOutbox::remove_entry(std::size_t index) {
    entries_[index] = {};
}

void CriticalAlertOutbox::refresh_counts() {
    status_.queued_count = 0;
    status_.in_flight_count = 0;
    for (const auto& entry : entries_) {
        if (entry.state == EntryState::queued ||
            entry.state == EntryState::prepared) {
            ++status_.queued_count;
        } else if (entry.state == EntryState::in_flight) {
            ++status_.in_flight_count;
        }
    }
}

}  // namespace opengauge::integration
