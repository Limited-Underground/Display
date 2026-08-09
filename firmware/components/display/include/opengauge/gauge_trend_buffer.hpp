#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/gauge_view_model.hpp"

namespace opengauge::display {

inline constexpr std::size_t kMaximumGaugeTrends = 4;
inline constexpr std::size_t kMaximumTrendPoints = 120;

enum class GaugeTrendError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    duplicate_trend,
    trend_capacity_full,
    invalid_snapshot,
    trend_not_found,
    insufficient_output_capacity,
    clock_regression,
};

struct GaugeTrendConfiguration {
    std::uint16_t trend_id{0};
    wireless::TelemetrySignalCode signal_code{
        wireless::TelemetrySignalCode::engine_speed};
    std::uint16_t point_capacity{0};
    std::uint64_t minimum_sample_interval_ms{0};
};

struct GaugeTrendPoint {
    std::uint64_t captured_at_ms{0};
    GaugeValueState state{GaugeValueState::missing};
    telemetry::SignalValue value{};
    telemetry::SignalUnit unit{telemetry::SignalUnit::none};
    std::uint32_t boot_session_id{0};
    std::uint32_t packet_sequence{0};
    bool gap{true};
};

struct GaugeTrendAppendResult {
    GaugeTrendError error{GaugeTrendError::invalid_state};
    std::size_t trends_appended{0};
    std::size_t trends_interval_skipped{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == GaugeTrendError::none;
    }
};

struct GaugeTrendStatus {
    bool running{false};
    std::size_t trend_count{0};
    std::uint32_t points_appended{0};
    std::uint32_t interval_skips{0};
    std::uint32_t points_overwritten{0};
};

class GaugeTrendBuffer {
public:
    [[nodiscard]] GaugeTrendError add_trend(
        const GaugeTrendConfiguration& configuration);
    [[nodiscard]] GaugeTrendError clear_trends();
    [[nodiscard]] GaugeTrendError start();
    void stop();

    [[nodiscard]] GaugeTrendAppendResult append(
        const GaugeWidgetSnapshot& snapshot,
        std::uint64_t captured_at_ms);
    [[nodiscard]] GaugeTrendError read(
        std::uint16_t trend_id,
        GaugeTrendPoint* output,
        std::size_t output_capacity,
        std::size_t& output_count) const;
    [[nodiscard]] GaugeTrendError clear_points(std::uint16_t trend_id);
    [[nodiscard]] GaugeTrendStatus status() const;

private:
    struct Series {
        GaugeTrendConfiguration configuration{};
        std::array<GaugeTrendPoint, kMaximumTrendPoints> points{};
        std::size_t start{0};
        std::size_t count{0};
        bool has_last_sample{false};
        std::uint64_t last_sample_ms{0};
    };

    [[nodiscard]] std::size_t find_trend(std::uint16_t trend_id) const;

    std::array<Series, kMaximumGaugeTrends> series_{};
    std::size_t trend_count_{0};
    GaugeTrendStatus status_{};
};

}  // namespace opengauge::display
