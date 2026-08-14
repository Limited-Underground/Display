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

struct GaugeLayoutImportSummary {
    std::uint64_t source_generation{0};
    std::uint32_t layout_id{0};
    GaugeTheme theme{GaugeTheme::dark};
    std::uint8_t brightness_percent{0};
    std::uint8_t widget_count{0};
};

struct GaugeLayoutImportWorkflowResult {
    GaugeLayoutCodecError codec_error{
        GaugeLayoutCodecError::invalid_argument};
    GaugeLayoutImportSummary summary{};
    GaugeLayoutChangeWorkflowResult workflow{};

    [[nodiscard]] constexpr bool decoded() const {
        return codec_error == GaugeLayoutCodecError::none;
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
    // Uses the same pending request and confirmation path as stage(). It
    // persists a validated default as a normal next generation and never
    // calls the low-level two-slot erase operation.
    [[nodiscard]] GaugeLayoutChangeWorkflowResult stage_restore_default(
        std::uint32_t request_id,
        const GaugeLayout& compiled_default,
        std::uint64_t now_ms);
    // Accepts only one exact, canonical OGL0 record. The record generation is
    // reported as source metadata but is not storage authority; confirmation
    // allocates the normal next local generation through stage().
    [[nodiscard]] GaugeLayoutImportWorkflowResult stage_import_record(
        std::uint32_t request_id,
        const std::uint8_t* record,
        std::size_t size,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeWorkflowResult confirm(
        std::uint32_t request_id,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeWorkflowResult cancel(
        std::uint32_t request_id,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeWorkflowResult service(
        std::uint64_t now_ms);
    [[nodiscard]] bool bound_to(const GaugeLayoutStore& store) const;

private:
    [[nodiscard]] GaugeLayoutChangeWorkflowResult project(
        const GaugeLayoutChangeObservation& observation,
        std::uint64_t now_ms) const;

    GaugeLayoutChangeCoordinator coordinator_;
};

}  // namespace opengauge::configuration
