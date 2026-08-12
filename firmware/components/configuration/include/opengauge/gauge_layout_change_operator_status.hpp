#pragma once

#include <type_traits>

#include "opengauge/gauge_layout_change_coordinator.hpp"

namespace opengauge::configuration {

enum class GaugeLayoutChangeOperation : std::uint8_t {
    snapshot = 0,
    stage,
    confirm,
    cancel,
    service,
};

enum class GaugeLayoutChangeOperatorState : std::uint8_t {
    unavailable = 0,
    ready,
    confirmation_required,
    applied,
    unchanged,
    cancelled,
    expired,
    rejected,
    persistence_failed,
    restart_required,
    clock_fault,
};

enum class GaugeLayoutChangeOperatorAction : std::uint8_t {
    none = 0,
    confirm_or_cancel,
    stage_new_request,
    service_expiry,
    restart_and_reconcile,
    service_clock,
};

enum class GaugeLayoutChangeProjectionError : std::uint8_t {
    none = 0,
    invalid_status,
    invalid_observation,
};

struct GaugeLayoutChangeObservation {
    GaugeLayoutChangeOperation operation{GaugeLayoutChangeOperation::snapshot};
    GaugeLayoutChangeError error{GaugeLayoutChangeError::none};
    GaugeLayoutUpdateResult persistence{};
};

struct GaugeLayoutChangeOperatorStatus {
    GaugeLayoutChangeOperatorState state{
        GaugeLayoutChangeOperatorState::unavailable};
    GaugeLayoutChangeOperatorAction action{
        GaugeLayoutChangeOperatorAction::none};
    std::uint32_t pending_request_id{0};
    std::uint64_t confirmation_remaining_ms{0};
    std::uint64_t generation{0};
    bool attention_required{false};
    bool confirmation_allowed{false};
    bool last_operation_rejected{false};
};

struct GaugeLayoutChangeProjectionResult {
    GaugeLayoutChangeProjectionError error{
        GaugeLayoutChangeProjectionError::invalid_status};
    GaugeLayoutChangeOperatorStatus status{};

    [[nodiscard]] constexpr bool projected() const {
        return error == GaugeLayoutChangeProjectionError::none;
    }
};

static_assert(std::is_trivially_copyable_v<GaugeLayoutChangeOperatorStatus>);

// Produces semantic local UI state only. It does not format text, authorize a
// source, prove physical presence, record diagnostics, or mutate coordinator
// state. pending_request_id must be used as an opaque local action token.
[[nodiscard]] GaugeLayoutChangeProjectionResult
project_gauge_layout_change_operator_status(
    const GaugeLayoutChangeStatus& coordinator,
    const GaugeLayoutChangeObservation& observation,
    std::uint64_t now_ms);

}  // namespace opengauge::configuration
