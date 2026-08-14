#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/gauge_dashboard_loop.hpp"

namespace opengauge::display {

inline constexpr std::uint16_t kRoundGaugeViewportPixels = 466;
inline constexpr std::uint16_t kRoundGaugeClipCenterPixels = 233;
inline constexpr std::uint16_t kRoundGaugeClipRadiusPixels = 233;
inline constexpr std::uint32_t kRoundGaugeNormalizedMaximum = 1000000U;
inline constexpr std::size_t kRoundGaugePrimitivesPerWidget = 3;
inline constexpr std::size_t kMaximumRoundGaugeRenderPrimitives =
    kMaximumGaugeWidgets * kRoundGaugePrimitivesPerWidget + 1;

enum class GaugeRoundRenderPlanError : std::uint8_t {
    none = 0,
    invalid_frame,
    insufficient_output_capacity,
    invalid_geometry,
};

enum class GaugeRoundRenderPrimitiveKind : std::uint8_t {
    widget_panel = 1,
    label,
    numeric_value,
    needle,
    bar,
    status_indicator,
    state_badge,
    recovery_badge,
};

// Integer pixel-edge box in the half-open 466 x 466 viewport:
// [x, x + width) x [y, y + height). Backends must also apply the circular
// clip centered at (233, 233) with radius 233 pixel-edge units.
struct GaugeRoundRenderBox {
    std::uint16_t x{0};
    std::uint16_t y{0};
    std::uint16_t width{0};
    std::uint16_t height{0};
};

struct GaugeRoundRenderPrimitive {
    GaugeRoundRenderPrimitiveKind kind{
        GaugeRoundRenderPrimitiveKind::widget_panel};
    GaugeRoundRenderBox bounds{};
    std::uint16_t widget_id{0};
    GaugeWidgetKind widget_kind{GaugeWidgetKind::numeric};
    GaugeValueState state{GaugeValueState::missing};
    GaugeWidgetLabel label{};
    telemetry::SignalValue value{};
    telemetry::SignalUnit unit{telemetry::SignalUnit::none};
    std::uint32_t normalized_position{0};
    bool attention_required{false};
};

struct GaugeRoundRenderPlan {
    // Diagnostics only. Semantic equality intentionally ignores these fields.
    std::uint64_t source_publication_sequence{0};
    std::uint64_t source_published_at_ms{0};
    std::uint64_t layout_generation{0};
    std::uint32_t layout_id{0};
    configuration::GaugeLayoutSource layout_source{
        configuration::GaugeLayoutSource::none};
    configuration::GaugeTheme theme{configuration::GaugeTheme::dark};
    std::uint8_t brightness_percent{0};
    std::uint8_t widget_count{0};
    bool recovery_required{false};
    std::uint8_t primitive_count{0};
    std::array<GaugeRoundRenderPrimitive,
               kMaximumRoundGaugeRenderPrimitives> primitives{};
};

struct GaugeRoundRenderPlanCompileResult {
    GaugeRoundRenderPlanError error{GaugeRoundRenderPlanError::invalid_frame};
    std::size_t required_primitive_capacity{0};
    std::size_t primitive_count{0};

    [[nodiscard]] constexpr bool compiled() const {
        return error == GaugeRoundRenderPlanError::none;
    }
};

// Compiles into a local fixed-capacity candidate and commits output once.
// primitive_capacity lets a target enforce a smaller bounded backend budget.
// Status widgets fail closed until the registered telemetry model contains a
// boolean signal that can produce a validated dashboard snapshot.
[[nodiscard]] GaugeRoundRenderPlanCompileResult
compile_gauge_round_render_plan(
    const GaugeDashboardFrame& frame,
    std::size_t primitive_capacity,
    GaugeRoundRenderPlan& output);

// Compares only renderer-visible active content field by field. It excludes
// diagnostic publication sequence/time and unused fixed-capacity tails.
[[nodiscard]] bool gauge_round_render_plans_semantically_equal(
    const GaugeRoundRenderPlan& left,
    const GaugeRoundRenderPlan& right);

}  // namespace opengauge::display
