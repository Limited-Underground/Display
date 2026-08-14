#pragma once

#include <cstdint>

#include "opengauge/gauge_renderer.hpp"

namespace opengauge::display {

enum class GaugeRendererRuntimeError : std::uint8_t {
    none = 0,
    invalid_state,
    dashboard_start_failure,
    renderer_start_failure,
    dashboard_service_failure,
    dashboard_frame_unavailable,
    renderer_busy,
    renderer_offer_failure,
    renderer_service_failure,
    clock_regression,
};

struct GaugeRendererRuntimeStartResult {
    GaugeRendererRuntimeError error{
        GaugeRendererRuntimeError::invalid_state};
    GaugeDashboardLoopStartResult dashboard{};
    GaugeRendererError renderer_error{GaugeRendererError::none};

    [[nodiscard]] constexpr bool started() const {
        return error == GaugeRendererRuntimeError::none;
    }
};

struct GaugeRendererRuntimeCycleResult {
    GaugeRendererRuntimeError error{GaugeRendererRuntimeError::none};
    GaugeDashboardLoopCycleResult dashboard{};
    GaugeRendererOfferResult renderer_offer{};
    GaugeRendererServiceResult renderer_service{};
    bool dashboard_serviced{false};
    bool frame_observed{false};
    bool pending_replaced{false};
    bool frame_semantically_equal{false};
    bool offer_attempted{false};
    bool pending_after_cycle{false};

    [[nodiscard]] constexpr bool succeeded() const {
        return error == GaugeRendererRuntimeError::none;
    }
};

struct GaugeRendererRuntimeStatus {
    bool running{false};
    bool has_pending_frame{false};
    bool has_last_accepted_frame{false};
    bool has_service_time{false};
    std::uint64_t last_service_time_ms{0};
    std::uint32_t cycles_serviced{0};
    std::uint32_t dashboard_cycle_failures{0};
    std::uint64_t frames_observed{0};
    std::uint64_t frames_coalesced{0};
    std::uint64_t equivalent_frames_observed{0};
    std::uint64_t offers_attempted{0};
    std::uint64_t offers_accepted{0};
    std::uint32_t renderer_busy_cycles{0};
    std::uint32_t renderer_offer_failures{0};
    std::uint32_t renderer_service_failures{0};
    GaugeRendererRuntimeError last_error{GaugeRendererRuntimeError::none};
};

// Single-owner cooperative binding for one exact dashboard loop and renderer.
// Its direct state is fixed; it allocates no memory. Every accepted monotonic
// cycle services the dashboard exactly once, attempts at most one pending
// offer, then services the renderer exactly once. New frames replace older
// pending work, while every nonaccepted offer retains the latest frame.
class GaugeRendererRuntime {
public:
    GaugeRendererRuntime(
        GaugeDashboardLoop& dashboard,
        GaugeRenderer& renderer);

    [[nodiscard]] GaugeRendererRuntimeStartResult start(
        const GaugeDashboardLoopConfiguration& configuration);
    void stop();

    [[nodiscard]] GaugeRendererRuntimeCycleResult service(
        std::uint64_t now_ms);
    [[nodiscard]] GaugeRendererRuntimeStatus status() const;

private:
    void observe_frame(
        const GaugeDashboardFrame& frame,
        GaugeRendererRuntimeCycleResult& result);

    GaugeDashboardLoop& dashboard_;
    GaugeRenderer& renderer_;
    GaugeDashboardFrame pending_frame_{};
    GaugeDashboardFrame accepted_frame_{};
    GaugeRendererRuntimeStatus status_{};
};

}  // namespace opengauge::display
