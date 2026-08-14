#pragma once

#include <cstddef>
#include <cstdint>

#include "opengauge/gauge_layout_change_workflow.hpp"
#include "opengauge/gauge_renderer_runtime.hpp"

namespace opengauge::display {

enum class GaugeLayoutActivationWorkflowError : std::uint8_t {
    none = 0,
    invalid_state,
    store_mismatch,
    persistence_failure,
    activation_failure,
    activation_pending,
    restart_required,
};

struct GaugeLayoutActivationWorkflowResult {
    GaugeLayoutActivationWorkflowError error{
        GaugeLayoutActivationWorkflowError::invalid_state};
    configuration::GaugeLayoutChangeWorkflowResult persistence{};
    GaugeRendererRuntimeActivationResult activation{};
    std::uint64_t expected_generation{0};
    bool persistence_attempted{false};
    bool activation_attempted{false};
    bool retry_required{false};
    bool restart_required{false};
    bool presentation_pending{false};

    [[nodiscard]] constexpr bool completed() const {
        return error == GaugeLayoutActivationWorkflowError::none &&
               activation.activated();
    }
};

struct GaugeLayoutActivationWorkflowStatus {
    bool activation_pending{false};
    bool restart_required{false};
    std::uint64_t pending_generation{0};
};

// Serialized display-side owner for local confirmation, durable selection,
// and exact-generation live activation. It owns the change workflow for the
// exact store and permits no new mutation while a committed generation still
// awaits activation. It owns no task, lock, renderer offer/service call, or
// dynamic storage; readiness may query the renderer's running state.
class GaugeLayoutActivationWorkflow {
public:
    GaugeLayoutActivationWorkflow(
        configuration::GaugeLayoutStore& store,
        GaugeRendererRuntime& renderer_runtime);

    [[nodiscard]] configuration::GaugeLayoutChangeWorkflowResult start(
        const configuration::GaugeLayoutChangePolicy& policy,
        std::uint64_t now_ms);
    [[nodiscard]] configuration::GaugeLayoutChangeWorkflowResult stop(
        std::uint64_t now_ms);
    [[nodiscard]] configuration::GaugeLayoutChangeWorkflowResult snapshot(
        std::uint64_t now_ms) const;
    [[nodiscard]] configuration::GaugeLayoutChangeWorkflowResult stage(
        std::uint32_t request_id,
        const configuration::GaugeLayout& desired,
        std::uint64_t now_ms);
    [[nodiscard]] configuration::GaugeLayoutChangeWorkflowResult
    stage_restore_default(
        std::uint32_t request_id,
        const configuration::GaugeLayout& compiled_default,
        std::uint64_t now_ms);
    [[nodiscard]] configuration::GaugeLayoutImportWorkflowResult
    stage_import_record(
        std::uint32_t request_id,
        const std::uint8_t* record,
        std::size_t size,
        std::uint64_t now_ms);
    [[nodiscard]] configuration::GaugeLayoutChangeWorkflowResult cancel(
        std::uint32_t request_id,
        std::uint64_t now_ms);
    [[nodiscard]] configuration::GaugeLayoutChangeWorkflowResult service(
        std::uint64_t now_ms);

    [[nodiscard]] GaugeLayoutActivationWorkflowResult confirm_and_activate(
        std::uint32_t request_id,
        std::uint64_t now_ms);

    // Retries only the latched committed generation using the dashboard's
    // validated start default. It performs no confirmation and no storage
    // write. Every failed attempt preserves the latch until exact activation
    // succeeds or the complete composition is reconstructed and reconciled.
    [[nodiscard]] GaugeLayoutActivationWorkflowResult retry_activation();

    [[nodiscard]] GaugeLayoutActivationWorkflowStatus status() const;

private:
    [[nodiscard]] configuration::GaugeLayoutChangeWorkflowResult
    blocked_change(std::uint64_t now_ms) const;
    [[nodiscard]] GaugeLayoutActivationWorkflowError preflight() const;
    void clear_activation_pending();

    configuration::GaugeLayoutStore& store_;
    configuration::GaugeLayoutChangeWorkflow change_workflow_;
    GaugeRendererRuntime& renderer_runtime_;
    GaugeLayoutActivationWorkflowStatus status_{};
};

}  // namespace opengauge::display
