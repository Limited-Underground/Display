#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "opengauge/gauge_telemetry_receiver.hpp"

namespace opengauge::display {

inline constexpr std::size_t kMaximumGaugeWidgets = 8;
inline constexpr std::size_t kMaximumGaugeWidgetLabelBytes = 24;

enum class GaugeWidgetKind : std::uint8_t {
    numeric = 1,
    needle = 2,
    bar = 3,
    status = 4,
};

enum class GaugeValueState : std::uint8_t {
    valid = 1,
    suspect = 2,
    missing = 3,
    stale = 4,
    unavailable = 5,
    error = 6,
    out_of_range = 7,
    unknown = 8,
};

enum class GaugeViewModelError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    duplicate_widget,
    widget_capacity_full,
    insufficient_output_capacity,
    receiver_failure,
};

struct GaugeWidgetLabel {
    std::array<char, kMaximumGaugeWidgetLabelBytes + 1> bytes{};
    std::uint8_t length{0};
};

struct GaugeWidgetConfiguration {
    std::uint16_t widget_id{0};
    wireless::TelemetrySignalCode signal_code{
        wireless::TelemetrySignalCode::engine_speed};
    GaugeWidgetKind kind{GaugeWidgetKind::numeric};
    GaugeWidgetLabel label{};
    std::uint64_t stale_after_ms{0};
    std::int64_t scale_min_raw{0};
    std::int64_t scale_max_raw{0};
};

struct GaugeWidgetSnapshot {
    std::uint16_t widget_id{0};
    wireless::TelemetrySignalCode signal_code{
        wireless::TelemetrySignalCode::engine_speed};
    GaugeWidgetKind kind{GaugeWidgetKind::numeric};
    GaugeWidgetLabel label{};
    GaugeValueState state{GaugeValueState::missing};
    telemetry::SignalValue display_value{};
    telemetry::SignalUnit unit{telemetry::SignalUnit::none};
    std::uint64_t age_ms{0};
    std::uint32_t boot_session_id{0};
    std::uint32_t packet_sequence{0};
    std::int64_t scale_min_raw{0};
    std::int64_t scale_max_raw{0};
    bool attention_required{true};
};

struct GaugeDashboardRefreshResult {
    GaugeViewModelError error{GaugeViewModelError::invalid_state};
    wireless::GaugeReceiverError receiver_error{
        wireless::GaugeReceiverError::none};
    std::size_t widget_count{0};
    std::size_t numeric_value_count{0};
    std::size_t attention_count{0};
    std::uint64_t refreshed_at_ms{0};

    [[nodiscard]] constexpr bool refreshed() const {
        return error == GaugeViewModelError::none;
    }
};

struct GaugeViewModelStatus {
    bool running{false};
    std::size_t widget_count{0};
    std::uint32_t refreshes_completed{0};
    std::uint32_t receiver_failures{0};
    std::uint32_t last_attention_count{0};
};

[[nodiscard]] GaugeViewModelError make_gauge_widget_label(
    std::string_view text,
    GaugeWidgetLabel& output);
[[nodiscard]] GaugeViewModelError validate_gauge_widget_configuration(
    const GaugeWidgetConfiguration& configuration);

// Display-framework-neutral projection. It never renders, formats localized
// text, persists layouts, or substitutes a number for nonvalid receiver state.
class GaugeViewModel {
public:
    explicit GaugeViewModel(
        const wireless::GaugeTelemetryReceiver& receiver);

    [[nodiscard]] GaugeViewModelError add_widget(
        const GaugeWidgetConfiguration& configuration);
    [[nodiscard]] GaugeViewModelError clear_widgets();
    [[nodiscard]] GaugeViewModelError start();
    void stop();

    [[nodiscard]] GaugeDashboardRefreshResult refresh(
        std::uint64_t now_ms,
        GaugeWidgetSnapshot* output,
        std::size_t output_capacity);

    [[nodiscard]] GaugeViewModelStatus status() const;

private:
    [[nodiscard]] std::size_t find_widget(std::uint16_t widget_id) const;

    const wireless::GaugeTelemetryReceiver& receiver_;
    std::array<GaugeWidgetConfiguration, kMaximumGaugeWidgets> widgets_{};
    std::size_t widget_count_{0};
    GaugeViewModelStatus status_{};
};

}  // namespace opengauge::display
