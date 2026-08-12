#pragma once

#include "opengauge/gauge_layout_change_operator_status.hpp"

namespace opengauge::configuration {

struct GaugeLayoutChangeWorkflowResult {
    GaugeLayoutChangeError operation_error{GaugeLayoutChangeError::none};
    GaugeLayoutUpdateResult persistence{};
    GaugeLayoutChangeProjectionError projection_error{
        GaugeLayoutChangeProjectionError::invalid_status};
    GaugeLayoutChangeOperatorStatus status{};

    [[nodiscard]] constexpr bool projected() const {
        return projection_error == GaugeLayoutChangeProjectionError::none;
    }
};

// One serialized application boundary for coordinator operation plus immediate
// operator projection. It owns no mutex/task and performs no rendering,
// diagnostics, source authorization, physical-presence proof, or target I/O.
class GaugeLayoutChangeWorkflow {
public:
    explicit GaugeLayoutChangeWorkflow(GaugeLayoutStore& store);

    [[nodiscard]] GaugeLayoutChangeWorkflowResult start(
        const GaugeLayoutChangePolicy& policy,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeWorkflowResult stop(std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeWorkflowResult snapshot(
        std::uint64_t now_ms) const;
    [[nodiscard]] GaugeLayoutChangeWorkflowResult stage(
        std::uint32_t request_id,
        const GaugeLayout& desired,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeWorkflowResult confirm(
        std::uint32_t request_id,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeWorkflowResult cancel(
        std::uint32_t request_id,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeWorkflowResult service(
        std::uint64_t now_ms);

private:
    [[nodiscard]] GaugeLayoutChangeWorkflowResult project(
        const GaugeLayoutChangeObservation& observation,
        std::uint64_t now_ms) const;

    GaugeLayoutChangeCoordinator coordinator_;
};

}  // namespace opengauge::configuration
