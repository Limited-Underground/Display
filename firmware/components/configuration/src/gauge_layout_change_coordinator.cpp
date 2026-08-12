#include "opengauge/gauge_layout_change_coordinator.hpp"

#include <limits>

namespace opengauge::configuration {
namespace {

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

GaugeLayoutChangeCoordinator::GaugeLayoutChangeCoordinator(
    GaugeLayoutStore& store)
    : store_(store) {}

GaugeLayoutChangeError GaugeLayoutChangeCoordinator::start(
    const GaugeLayoutChangePolicy& policy) {
    if (status_.state != GaugeLayoutChangeState::stopped) {
        return GaugeLayoutChangeError::invalid_state;
    }
    if (policy.confirmation_window_ms == 0) {
        return GaugeLayoutChangeError::invalid_policy;
    }
    policy_ = policy;
    status_ = {};
    status_.state = GaugeLayoutChangeState::idle;
    status_.confirmation_window_ms = policy.confirmation_window_ms;
    pending_ = {};
    last_now_ms_ = 0;
    has_time_ = false;
    return GaugeLayoutChangeError::none;
}

void GaugeLayoutChangeCoordinator::stop() {
    clear_pending();
    policy_ = {};
    status_.state = GaugeLayoutChangeState::stopped;
    status_.confirmation_window_ms = 0;
    has_time_ = false;
    last_now_ms_ = 0;
}

GaugeLayoutChangeError GaugeLayoutChangeCoordinator::stage(
    std::uint32_t request_id,
    const GaugeLayout& desired,
    std::uint64_t now_ms) {
    if (status_.state == GaugeLayoutChangeState::stopped) {
        return GaugeLayoutChangeError::invalid_state;
    }
    if (!observe_time(now_ms)) {
        return GaugeLayoutChangeError::clock_regression;
    }
    (void)expire_if_due(now_ms);
    if (request_id == 0) {
        return GaugeLayoutChangeError::invalid_request;
    }
    GaugeLayout validated = desired;
    validated.generation = 1;
    if (validate_gauge_layout(validated) != GaugeLayoutCodecError::none) {
        return GaugeLayoutChangeError::invalid_layout;
    }
    if (status_.state == GaugeLayoutChangeState::pending) {
        return GaugeLayoutChangeError::change_pending;
    }
    if (request_id <= status_.last_request_id) {
        return GaugeLayoutChangeError::request_not_newer;
    }

    pending_ = desired;
    pending_.generation = 0;
    status_.state = GaugeLayoutChangeState::pending;
    status_.pending_request_id = request_id;
    status_.last_request_id = request_id;
    status_.pending_opened_ms = now_ms;
    saturating_increment(status_.staged_count);
    return GaugeLayoutChangeError::none;
}

GaugeLayoutChangeResult GaugeLayoutChangeCoordinator::confirm(
    std::uint32_t request_id,
    std::uint64_t now_ms) {
    GaugeLayoutChangeResult result{};
    if (status_.state == GaugeLayoutChangeState::stopped) {
        return result;
    }
    if (!observe_time(now_ms)) {
        result.error = GaugeLayoutChangeError::clock_regression;
        return result;
    }
    if (expire_if_due(now_ms)) {
        result.error = GaugeLayoutChangeError::confirmation_expired;
        return result;
    }
    if (status_.state != GaugeLayoutChangeState::pending) {
        result.error = GaugeLayoutChangeError::no_change_pending;
        return result;
    }
    if (request_id == 0 || request_id != status_.pending_request_id) {
        result.error = GaugeLayoutChangeError::request_mismatch;
        return result;
    }

    const GaugeLayout desired = pending_;
    clear_pending();
    result.persistence = store_.save_next_if_changed(desired);
    if (result.persistence.error == GaugeLayoutStoreError::commit_uncertain) {
        saturating_increment(status_.uncertain_count);
        result.error = GaugeLayoutChangeError::persistence_uncertain;
        return result;
    }
    if (!result.persistence.succeeded()) {
        saturating_increment(status_.failed_count);
        result.error = GaugeLayoutChangeError::persistence_failed;
        return result;
    }
    if (result.persistence.changed()) {
        saturating_increment(status_.applied_count);
    } else {
        saturating_increment(status_.unchanged_count);
    }
    result.error = GaugeLayoutChangeError::none;
    return result;
}

GaugeLayoutChangeError GaugeLayoutChangeCoordinator::cancel(
    std::uint32_t request_id) {
    if (status_.state == GaugeLayoutChangeState::stopped) {
        return GaugeLayoutChangeError::invalid_state;
    }
    if (status_.state != GaugeLayoutChangeState::pending) {
        return GaugeLayoutChangeError::no_change_pending;
    }
    if (request_id == 0 || request_id != status_.pending_request_id) {
        return GaugeLayoutChangeError::request_mismatch;
    }
    clear_pending();
    saturating_increment(status_.cancelled_count);
    return GaugeLayoutChangeError::none;
}

GaugeLayoutChangeError GaugeLayoutChangeCoordinator::service(
    std::uint64_t now_ms) {
    if (status_.state == GaugeLayoutChangeState::stopped) {
        return GaugeLayoutChangeError::invalid_state;
    }
    if (!observe_time(now_ms)) {
        return GaugeLayoutChangeError::clock_regression;
    }
    return expire_if_due(now_ms)
               ? GaugeLayoutChangeError::confirmation_expired
               : GaugeLayoutChangeError::none;
}

GaugeLayoutChangeStatus GaugeLayoutChangeCoordinator::status() const {
    return status_;
}

bool GaugeLayoutChangeCoordinator::observe_time(std::uint64_t now_ms) {
    if (has_time_ && now_ms < last_now_ms_) {
        clear_pending();
        saturating_increment(status_.clock_fault_count);
        return false;
    }
    last_now_ms_ = now_ms;
    has_time_ = true;
    return true;
}

bool GaugeLayoutChangeCoordinator::expire_if_due(std::uint64_t now_ms) {
    if (status_.state != GaugeLayoutChangeState::pending) {
        return false;
    }
    if (now_ms - status_.pending_opened_ms < policy_.confirmation_window_ms) {
        return false;
    }
    clear_pending();
    saturating_increment(status_.expired_count);
    return true;
}

void GaugeLayoutChangeCoordinator::clear_pending() {
    pending_ = {};
    status_.pending_request_id = 0;
    status_.pending_opened_ms = 0;
    if (status_.state != GaugeLayoutChangeState::stopped) {
        status_.state = GaugeLayoutChangeState::idle;
    }
}

}  // namespace opengauge::configuration
