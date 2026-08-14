#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/gauge_layout.hpp"
#include "opengauge/gauge_view_model.hpp"
#include "opengauge/gauge_telemetry_receiver.hpp"

namespace opengauge::display {

enum class GaugeDashboardLoopError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    receiver_view_mismatch,
    layout_load_failure,
    no_usable_layout,
    view_configuration_failure,
    receiver_start_failure,
    view_start_failure,
    receiver_service_failure,
    view_refresh_failure,
    clock_regression,
};

struct GaugeDashboardLoopConfiguration {
    configuration::GaugeLayout safe_default{};
    wireless::GaugeReceiverConfiguration receiver{};
};

struct GaugeDashboardLoopStartResult {
    GaugeDashboardLoopError error{
        GaugeDashboardLoopError::invalid_configuration};
    configuration::GaugeLayoutLoadResult layout_load{};
    wireless::GaugeReceiverError receiver_error{
        wireless::GaugeReceiverError::none};
    GaugeViewModelError view_error{GaugeViewModelError::none};

    [[nodiscard]] constexpr bool started() const {
        return error == GaugeDashboardLoopError::none;
    }
};

// Fixed-capacity renderer input. The runtime loop publishes this record only
// after every widget has refreshed successfully, so a consumer never observes
// a partially updated dashboard.
struct GaugeDashboardFrame {
    std::uint64_t publication_sequence{0};
    std::uint64_t published_at_ms{0};
    std::uint64_t layout_generation{0};
    std::uint32_t layout_id{0};
    configuration::GaugeLayoutSource layout_source{
        configuration::GaugeLayoutSource::none};
    configuration::GaugeTheme theme{configuration::GaugeTheme::dark};
    std::uint8_t brightness_percent{0};
    std::uint8_t widget_count{0};
    bool recovery_required{false};
    std::array<GaugeWidgetSnapshot, kMaximumGaugeWidgets> widgets{};
};

struct GaugeDashboardLoopCycleResult {
    GaugeDashboardLoopError error{GaugeDashboardLoopError::none};
    wireless::GaugeReceiverCycleResult receiver{};
    GaugeDashboardRefreshResult refresh{};
    bool frame_published{false};
    std::uint64_t publication_sequence{0};

    [[nodiscard]] constexpr bool published() const {
        return frame_published;
    }
};

struct GaugeDashboardLoopStatus {
    bool running{false};
    bool has_frame{false};
    configuration::GaugeLayoutLoadResult layout_load{};
    wireless::GaugeReceiverStatus receiver{};
    GaugeViewModelStatus view{};
    std::uint32_t cycles_serviced{0};
    std::uint64_t frames_published{0};
    std::uint32_t receiver_cycle_failures{0};
    std::uint32_t view_refresh_failures{0};
    std::uint32_t clock_regressions{0};
    std::uint64_t last_service_time_ms{0};
    bool has_service_time{false};
    GaugeDashboardLoopError last_error{GaugeDashboardLoopError::none};
};

// Single-owner, cooperative host-testable composition. The loop's direct state
// and service code introduce no dynamic allocation; dependency allocation
// behavior remains outside this contract. It performs no rendering, storage
// mutation, radio provisioning, or hardware initialization. The caller owns
// transport setup and serializes all calls. Each accepted nondecreasing-time
// cycle services the receiver once; a regressed caller time fails before it.
class GaugeDashboardLoop {
public:
    GaugeDashboardLoop(
        configuration::GaugeLayoutStore& layout_store,
        wireless::GaugeTelemetryReceiver& receiver,
        GaugeViewModel& view_model);

    [[nodiscard]] GaugeDashboardLoopStartResult start(
        const GaugeDashboardLoopConfiguration& configuration);
    void stop();

    [[nodiscard]] GaugeDashboardLoopCycleResult service(
        std::uint64_t now_ms);

    // Returns false and preserves output until one complete frame exists.
    [[nodiscard]] bool copy_frame(GaugeDashboardFrame& output) const;
    [[nodiscard]] GaugeDashboardLoopStatus status() const;

private:
    configuration::GaugeLayoutStore& layout_store_;
    wireless::GaugeTelemetryReceiver& receiver_;
    GaugeViewModel& view_model_;
    configuration::GaugeLayout active_layout_{};
    GaugeDashboardFrame frame_{};
    GaugeDashboardLoopStatus status_{};
};

}  // namespace opengauge::display
