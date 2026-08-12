#include "opengauge/gauge_layout_change_operator_status.hpp"

namespace opengauge::configuration {
namespace {

using OperatorState = GaugeLayoutChangeOperatorState;
using OperatorAction = GaugeLayoutChangeOperatorAction;
using ProjectionError = GaugeLayoutChangeProjectionError;

bool coherent_status(const GaugeLayoutChangeStatus& status) {
    switch (status.state) {
        case GaugeLayoutChangeState::stopped:
            return status.pending_request_id == 0 &&
                   status.pending_opened_ms == 0 &&
                   status.confirmation_window_ms == 0;
        case GaugeLayoutChangeState::idle:
            return status.pending_request_id == 0 &&
                   status.pending_opened_ms == 0 &&
                   status.confirmation_window_ms != 0;
        case GaugeLayoutChangeState::pending:
            return status.pending_request_id != 0 &&
                   status.pending_request_id == status.last_request_id &&
                   status.confirmation_window_ms != 0;
    }
    return false;
}

GaugeLayoutChangeProjectionResult unavailable() {
    GaugeLayoutChangeProjectionResult result{};
    result.error = ProjectionError::none;
    result.status.state = OperatorState::unavailable;
    return result;
}

GaugeLayoutChangeProjectionResult ready() {
    GaugeLayoutChangeProjectionResult result{};
    result.error = ProjectionError::none;
    result.status.state = OperatorState::ready;
    result.status.action = OperatorAction::stage_new_request;
    return result;
}

GaugeLayoutChangeProjectionResult terminal(
    OperatorState state,
    OperatorAction action,
    bool attention,
    std::uint64_t generation = 0) {
    GaugeLayoutChangeProjectionResult result{};
    result.error = ProjectionError::none;
    result.status.state = state;
    result.status.action = action;
    result.status.attention_required = attention;
    result.status.generation = generation;
    return result;
}

GaugeLayoutChangeProjectionResult live_status(
    const GaugeLayoutChangeStatus& status,
    std::uint64_t now_ms) {
    if (status.state == GaugeLayoutChangeState::stopped) {
        return unavailable();
    }
    if (status.state == GaugeLayoutChangeState::idle) {
        return ready();
    }
    if (now_ms < status.pending_opened_ms) {
        return terminal(
            OperatorState::clock_fault,
            OperatorAction::service_clock,
            true);
    }
    const auto elapsed = now_ms - status.pending_opened_ms;
    if (elapsed >= status.confirmation_window_ms) {
        return terminal(
            OperatorState::expired,
            OperatorAction::service_expiry,
            true);
    }
    GaugeLayoutChangeProjectionResult result{};
    result.error = ProjectionError::none;
    result.status.state = OperatorState::confirmation_required;
    result.status.action = OperatorAction::confirm_or_cancel;
    result.status.pending_request_id = status.pending_request_id;
    result.status.confirmation_remaining_ms =
        status.confirmation_window_ms - elapsed;
    result.status.attention_required = true;
    result.status.confirmation_allowed = true;
    return result;
}

bool store_result_absent(const GaugeLayoutUpdateResult& result) {
    return result.error == GaugeLayoutStoreError::storage_failure &&
           result.state == GaugeLayoutUpdateState::unchanged &&
           result.active_slot == GaugeLayoutSource::none &&
           result.generation == 0;
}

GaugeLayoutChangeProjectionResult rejected_from_live(
    const GaugeLayoutChangeStatus& status,
    std::uint64_t now_ms) {
    const auto live = live_status(status, now_ms);
    if (!live.projected()) {
        return live;
    }
    if (live.status.state == OperatorState::confirmation_required) {
        auto rejected = live;
        rejected.status.attention_required = true;
        rejected.status.last_operation_rejected = true;
        return rejected;
    }
    if (status.state == GaugeLayoutChangeState::pending) {
        return {ProjectionError::invalid_observation};
    }
    if (status.state == GaugeLayoutChangeState::stopped) {
        return unavailable();
    }
    auto rejected = terminal(
        OperatorState::rejected,
        OperatorAction::stage_new_request,
        true);
    rejected.status.last_operation_rejected = true;
    return rejected;
}

GaugeLayoutChangeProjectionResult project_snapshot(
    const GaugeLayoutChangeStatus& status,
    const GaugeLayoutChangeObservation& observation,
    std::uint64_t now_ms) {
    if (observation.error != GaugeLayoutChangeError::none ||
        !store_result_absent(observation.persistence)) {
        return {ProjectionError::invalid_observation};
    }
    return live_status(status, now_ms);
}

GaugeLayoutChangeProjectionResult project_stage(
    const GaugeLayoutChangeStatus& status,
    const GaugeLayoutChangeObservation& observation,
    std::uint64_t now_ms) {
    if (!store_result_absent(observation.persistence)) {
        return {ProjectionError::invalid_observation};
    }
    switch (observation.error) {
        case GaugeLayoutChangeError::none:
            if (status.state != GaugeLayoutChangeState::pending) {
                return {ProjectionError::invalid_observation};
            }
            return live_status(status, now_ms);
        case GaugeLayoutChangeError::clock_regression:
            if (status.state != GaugeLayoutChangeState::idle) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                OperatorState::clock_fault,
                OperatorAction::service_clock,
                true);
        case GaugeLayoutChangeError::invalid_request:
        case GaugeLayoutChangeError::invalid_layout:
            return rejected_from_live(status, now_ms);
        case GaugeLayoutChangeError::invalid_state:
            return status.state == GaugeLayoutChangeState::stopped
                       ? unavailable()
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::request_not_newer:
            return status.state == GaugeLayoutChangeState::idle
                       ? rejected_from_live(status, now_ms)
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::change_pending:
            return status.state == GaugeLayoutChangeState::pending
                       ? rejected_from_live(status, now_ms)
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::invalid_policy:
        case GaugeLayoutChangeError::no_change_pending:
        case GaugeLayoutChangeError::request_mismatch:
        case GaugeLayoutChangeError::confirmation_expired:
        case GaugeLayoutChangeError::persistence_failed:
        case GaugeLayoutChangeError::persistence_uncertain:
            return {ProjectionError::invalid_observation};
    }
    return {ProjectionError::invalid_observation};
}

GaugeLayoutChangeProjectionResult project_confirm(
    const GaugeLayoutChangeStatus& status,
    const GaugeLayoutChangeObservation& observation,
    std::uint64_t now_ms) {
    switch (observation.error) {
        case GaugeLayoutChangeError::none:
            if (status.state != GaugeLayoutChangeState::idle ||
                !observation.persistence.succeeded() ||
                observation.persistence.generation == 0 ||
                observation.persistence.active_slot == GaugeLayoutSource::none) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                observation.persistence.changed()
                    ? OperatorState::applied
                    : OperatorState::unchanged,
                OperatorAction::none,
                false,
                observation.persistence.generation);
        case GaugeLayoutChangeError::persistence_failed:
            if (status.state != GaugeLayoutChangeState::idle ||
                observation.persistence.succeeded() ||
                observation.persistence.error ==
                    GaugeLayoutStoreError::commit_uncertain) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                OperatorState::persistence_failed,
                OperatorAction::stage_new_request,
                true);
        case GaugeLayoutChangeError::persistence_uncertain:
            if (status.state != GaugeLayoutChangeState::idle ||
                observation.persistence.error !=
                    GaugeLayoutStoreError::commit_uncertain) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                OperatorState::restart_required,
                OperatorAction::restart_and_reconcile,
                true);
        case GaugeLayoutChangeError::confirmation_expired:
            if (status.state != GaugeLayoutChangeState::idle ||
                !store_result_absent(observation.persistence)) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                OperatorState::expired,
                OperatorAction::stage_new_request,
                true);
        case GaugeLayoutChangeError::clock_regression:
            if (status.state != GaugeLayoutChangeState::idle ||
                !store_result_absent(observation.persistence)) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                OperatorState::clock_fault,
                OperatorAction::service_clock,
                true);
        case GaugeLayoutChangeError::invalid_state:
            if (status.state != GaugeLayoutChangeState::stopped ||
                !store_result_absent(observation.persistence)) {
                return {ProjectionError::invalid_observation};
            }
            return unavailable();
        case GaugeLayoutChangeError::no_change_pending:
            if (!store_result_absent(observation.persistence)) {
                return {ProjectionError::invalid_observation};
            }
            return status.state == GaugeLayoutChangeState::idle
                       ? rejected_from_live(status, now_ms)
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::request_mismatch:
            if (!store_result_absent(observation.persistence)) {
                return {ProjectionError::invalid_observation};
            }
            return status.state == GaugeLayoutChangeState::pending
                       ? rejected_from_live(status, now_ms)
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::invalid_policy:
        case GaugeLayoutChangeError::invalid_request:
        case GaugeLayoutChangeError::request_not_newer:
        case GaugeLayoutChangeError::invalid_layout:
        case GaugeLayoutChangeError::change_pending:
            return {ProjectionError::invalid_observation};
    }
    return {ProjectionError::invalid_observation};
}

GaugeLayoutChangeProjectionResult project_cancel(
    const GaugeLayoutChangeStatus& status,
    const GaugeLayoutChangeObservation& observation,
    std::uint64_t now_ms) {
    if (!store_result_absent(observation.persistence)) {
        return {ProjectionError::invalid_observation};
    }
    switch (observation.error) {
        case GaugeLayoutChangeError::none:
            if (status.state != GaugeLayoutChangeState::idle) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                OperatorState::cancelled,
                OperatorAction::stage_new_request,
                false);
        case GaugeLayoutChangeError::invalid_state:
            return status.state == GaugeLayoutChangeState::stopped
                       ? unavailable()
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::no_change_pending:
            return status.state == GaugeLayoutChangeState::idle
                       ? rejected_from_live(status, now_ms)
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::request_mismatch:
            return status.state == GaugeLayoutChangeState::pending
                       ? rejected_from_live(status, now_ms)
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::invalid_policy:
        case GaugeLayoutChangeError::invalid_request:
        case GaugeLayoutChangeError::request_not_newer:
        case GaugeLayoutChangeError::invalid_layout:
        case GaugeLayoutChangeError::change_pending:
        case GaugeLayoutChangeError::confirmation_expired:
        case GaugeLayoutChangeError::clock_regression:
        case GaugeLayoutChangeError::persistence_failed:
        case GaugeLayoutChangeError::persistence_uncertain:
            return {ProjectionError::invalid_observation};
    }
    return {ProjectionError::invalid_observation};
}

GaugeLayoutChangeProjectionResult project_service(
    const GaugeLayoutChangeStatus& status,
    const GaugeLayoutChangeObservation& observation,
    std::uint64_t now_ms) {
    if (!store_result_absent(observation.persistence)) {
        return {ProjectionError::invalid_observation};
    }
    switch (observation.error) {
        case GaugeLayoutChangeError::none:
            return live_status(status, now_ms);
        case GaugeLayoutChangeError::invalid_state:
            return status.state == GaugeLayoutChangeState::stopped
                       ? unavailable()
                       : GaugeLayoutChangeProjectionResult{
                             ProjectionError::invalid_observation};
        case GaugeLayoutChangeError::confirmation_expired:
            if (status.state != GaugeLayoutChangeState::idle) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                OperatorState::expired,
                OperatorAction::stage_new_request,
                true);
        case GaugeLayoutChangeError::clock_regression:
            if (status.state != GaugeLayoutChangeState::idle) {
                return {ProjectionError::invalid_observation};
            }
            return terminal(
                OperatorState::clock_fault,
                OperatorAction::service_clock,
                true);
        case GaugeLayoutChangeError::invalid_policy:
        case GaugeLayoutChangeError::invalid_request:
        case GaugeLayoutChangeError::request_not_newer:
        case GaugeLayoutChangeError::invalid_layout:
        case GaugeLayoutChangeError::change_pending:
        case GaugeLayoutChangeError::no_change_pending:
        case GaugeLayoutChangeError::request_mismatch:
        case GaugeLayoutChangeError::persistence_failed:
        case GaugeLayoutChangeError::persistence_uncertain:
            return {ProjectionError::invalid_observation};
    }
    return {ProjectionError::invalid_observation};
}

}  // namespace

GaugeLayoutChangeProjectionResult project_gauge_layout_change_operator_status(
    const GaugeLayoutChangeStatus& coordinator,
    const GaugeLayoutChangeObservation& observation,
    std::uint64_t now_ms) {
    if (!coherent_status(coordinator)) {
        return {ProjectionError::invalid_status};
    }
    switch (observation.operation) {
        case GaugeLayoutChangeOperation::snapshot:
            return project_snapshot(coordinator, observation, now_ms);
        case GaugeLayoutChangeOperation::stage:
            return project_stage(coordinator, observation, now_ms);
        case GaugeLayoutChangeOperation::confirm:
            return project_confirm(coordinator, observation, now_ms);
        case GaugeLayoutChangeOperation::cancel:
            return project_cancel(coordinator, observation, now_ms);
        case GaugeLayoutChangeOperation::service:
            return project_service(coordinator, observation, now_ms);
    }
    return {ProjectionError::invalid_observation};
}

}  // namespace opengauge::configuration
