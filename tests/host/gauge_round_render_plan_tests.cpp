#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opengauge/gauge_round_render_plan.hpp"

namespace {

using namespace opengauge;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

display::GaugeWidgetSnapshot widget(
    std::uint16_t id,
    display::GaugeWidgetKind kind,
    display::GaugeValueState state = display::GaugeValueState::valid,
    wireless::TelemetrySignalCode code =
        wireless::TelemetrySignalCode::engine_speed,
    std::int64_t raw_value = 1500000) {
    display::GaugeWidgetSnapshot result{};
    result.widget_id = id;
    result.signal_code = code;
    result.kind = kind;
    constexpr char kLabel[] = "Gauge";
    for (std::size_t index = 0; index < 5; ++index) {
        result.label.bytes[index] = kLabel[index];
    }
    result.label.length = 5;
    result.state = state;
    const auto* descriptor = wireless::telemetry_signal_descriptor(code);
    EXPECT(descriptor != nullptr);
    result.display_value.type = descriptor->value_type;
    result.unit = descriptor->unit;
    result.age_ms = 22;
    result.boot_session_id = 33;
    result.packet_sequence = 44;
    result.attention_required = state != display::GaugeValueState::valid;
    if (state == display::GaugeValueState::valid ||
        state == display::GaugeValueState::suspect) {
        result.display_value.present = true;
        result.display_value.raw_value = raw_value;
    }
    if (kind == display::GaugeWidgetKind::needle ||
        kind == display::GaugeWidgetKind::bar) {
        result.scale_min_raw = 0;
        result.scale_max_raw = 3000000;
    }
    return result;
}

display::GaugeDashboardFrame frame(std::uint8_t count = 3) {
    display::GaugeDashboardFrame result{};
    result.publication_sequence = 10;
    result.published_at_ms = 20;
    result.layout_generation = 30;
    result.layout_id = 40;
    result.layout_source = configuration::GaugeLayoutSource::slot_a;
    result.theme = configuration::GaugeTheme::high_contrast;
    result.brightness_percent = 70;
    result.widget_count = count;
    constexpr std::array<display::GaugeWidgetKind, 3> kinds{{
        display::GaugeWidgetKind::numeric,
        display::GaugeWidgetKind::needle,
        display::GaugeWidgetKind::bar,
    }};
    for (std::size_t index = 0; index < count; ++index) {
        result.widgets[index] = widget(
            static_cast<std::uint16_t>(100 + index),
            kinds[index % kinds.size()]);
    }
    return result;
}

const display::GaugeRoundRenderPrimitive& primary(
    const display::GaugeRoundRenderPlan& plan,
    std::size_t widget_index) {
    return plan.primitives[
        widget_index * display::kRoundGaugePrimitivesPerWidget + 2U];
}

void seed_sentinel(display::GaugeRoundRenderPlan& plan) {
    plan.source_publication_sequence = 0xAAAAAAAAAAAAAAAAULL;
    plan.source_published_at_ms = 0xBBBBBBBBBBBBBBBBULL;
    plan.layout_generation = 0xCCCCCCCCCCCCCCCCULL;
    plan.layout_id = 0xDDDDDDDDU;
    plan.layout_source = configuration::GaugeLayoutSource::slot_b;
    plan.theme = configuration::GaugeTheme::light;
    plan.brightness_percent = 99;
    plan.widget_count = 7;
    plan.recovery_required = true;
    plan.primitive_count = 1;
    plan.primitives[0].kind =
        display::GaugeRoundRenderPrimitiveKind::recovery_badge;
    plan.primitives[0].widget_id = 0xEEU;
    plan.primitives[0].value = {
        telemetry::SignalValueType::signed_integer, -77, true};
}

bool sentinel_preserved(const display::GaugeRoundRenderPlan& plan) {
    return plan.source_publication_sequence == 0xAAAAAAAAAAAAAAAAULL &&
           plan.source_published_at_ms == 0xBBBBBBBBBBBBBBBBULL &&
           plan.layout_generation == 0xCCCCCCCCCCCCCCCCULL &&
           plan.layout_id == 0xDDDDDDDDU &&
           plan.layout_source == configuration::GaugeLayoutSource::slot_b &&
           plan.theme == configuration::GaugeTheme::light &&
           plan.brightness_percent == 99 && plan.widget_count == 7 &&
           plan.recovery_required && plan.primitive_count == 1 &&
           plan.primitives[0].kind ==
               display::GaugeRoundRenderPrimitiveKind::recovery_badge &&
           plan.primitives[0].widget_id == 0xEEU &&
           plan.primitives[0].value.present &&
           plan.primitives[0].value.raw_value == -77;
}

void expect_compile_failure_preserves(
    const display::GaugeDashboardFrame& input,
    std::size_t capacity,
    display::GaugeRoundRenderPlanError expected) {
    display::GaugeRoundRenderPlan output{};
    seed_sentinel(output);
    const auto result = display::compile_gauge_round_render_plan(
        input, capacity, output);
    EXPECT(result.error == expected);
    EXPECT(sentinel_preserved(output));
}

bool box_within_clip(const display::GaugeRoundRenderBox& box) {
    constexpr std::int32_t center =
        display::kRoundGaugeClipCenterPixels;
    constexpr std::int32_t radius =
        display::kRoundGaugeClipRadiusPixels;
    const std::array<std::int32_t, 2> xs{{
        static_cast<std::int32_t>(box.x),
        static_cast<std::int32_t>(box.x) + box.width}};
    const std::array<std::int32_t, 2> ys{{
        static_cast<std::int32_t>(box.y),
        static_cast<std::int32_t>(box.y) + box.height}};
    for (const auto x : xs) {
        for (const auto y : ys) {
            const auto dx = x - center;
            const auto dy = y - center;
            if (dx * dx + dy * dy > radius * radius) {
                return false;
            }
        }
    }
    return true;
}

bool boxes_intersect(
    const display::GaugeRoundRenderBox& left,
    const display::GaugeRoundRenderBox& right) {
    const auto left_right = static_cast<std::uint32_t>(left.x) + left.width;
    const auto right_right = static_cast<std::uint32_t>(right.x) + right.width;
    const auto left_bottom = static_cast<std::uint32_t>(left.y) + left.height;
    const auto right_bottom = static_cast<std::uint32_t>(right.y) + right.height;
    return left.x < right_right && right.x < left_right &&
           left.y < right_bottom && right.y < left_bottom;
}

void test_all_widget_counts_have_bounded_circular_geometry() {
    for (std::uint8_t count = 1; count <= display::kMaximumGaugeWidgets;
         ++count) {
        auto input = frame(count);
        input.recovery_required = true;
        display::GaugeRoundRenderPlan plan{};
        const auto result = display::compile_gauge_round_render_plan(
            input, display::kMaximumRoundGaugeRenderPrimitives, plan);
        EXPECT(result.compiled());
        EXPECT(plan.widget_count == count);
        EXPECT(plan.primitive_count == count * 3U + 1U);
        for (std::size_t index = 0; index < plan.primitive_count; ++index) {
            const auto& box = plan.primitives[index].bounds;
            EXPECT(box.width != 0 && box.height != 0);
            EXPECT(static_cast<std::uint32_t>(box.x) + box.width <= 466U);
            EXPECT(static_cast<std::uint32_t>(box.y) + box.height <= 466U);
            EXPECT(box_within_clip(box));
        }
        const auto& recovery = plan.primitives[plan.primitive_count - 1U];
        EXPECT(recovery.kind ==
               display::GaugeRoundRenderPrimitiveKind::recovery_badge);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& panel =
                plan.primitives[index *
                                display::kRoundGaugePrimitivesPerWidget];
            EXPECT(panel.kind ==
                   display::GaugeRoundRenderPrimitiveKind::widget_panel);
            EXPECT(!boxes_intersect(recovery.bounds, panel.bounds));
        }
    }
}

void test_valid_representations_and_diagnostic_metadata() {
    const auto input = frame();
    display::GaugeRoundRenderPlan plan{};
    const auto result = display::compile_gauge_round_render_plan(
        input, display::kMaximumRoundGaugeRenderPrimitives, plan);
    EXPECT(result.compiled() && result.primitive_count == 9);
    EXPECT(plan.source_publication_sequence == input.publication_sequence);
    EXPECT(plan.source_published_at_ms == input.published_at_ms);
    EXPECT(plan.layout_generation == input.layout_generation);
    EXPECT(plan.layout_id == input.layout_id);
    EXPECT(primary(plan, 0).kind ==
           display::GaugeRoundRenderPrimitiveKind::numeric_value);
    EXPECT(primary(plan, 1).kind ==
           display::GaugeRoundRenderPrimitiveKind::needle);
    EXPECT(primary(plan, 2).kind ==
           display::GaugeRoundRenderPrimitiveKind::bar);
    for (std::size_t index = 0; index < 3; ++index) {
        EXPECT(primary(plan, index).value.present);
        EXPECT(primary(plan, index).value.raw_value == 1500000);
        EXPECT(!plan.primitives[index * 3U].value.present);
        EXPECT(plan.primitives[index * 3U].value.raw_value == 0);
        EXPECT(!plan.primitives[index * 3U + 1U].value.present);
        EXPECT(plan.primitives[index * 3U + 1U].value.raw_value == 0);
    }
    EXPECT(primary(plan, 1).normalized_position == 500000U);
    EXPECT(primary(plan, 2).normalized_position == 500000U);
}

void test_suspect_and_every_nonvalid_state_are_no_value_badges() {
    constexpr std::array<display::GaugeValueState, 7> states{{
        display::GaugeValueState::suspect,
        display::GaugeValueState::missing,
        display::GaugeValueState::stale,
        display::GaugeValueState::unavailable,
        display::GaugeValueState::error,
        display::GaugeValueState::out_of_range,
        display::GaugeValueState::unknown,
    }};
    for (const auto state : states) {
        for (const auto kind : {display::GaugeWidgetKind::numeric,
                                display::GaugeWidgetKind::needle,
                                display::GaugeWidgetKind::bar}) {
            auto input = frame(1);
            input.widgets[0] = widget(101, kind, state);
            display::GaugeRoundRenderPlan plan{};
            const auto result = display::compile_gauge_round_render_plan(
                input, display::kMaximumRoundGaugeRenderPrimitives, plan);
            EXPECT(result.compiled());
            EXPECT(primary(plan, 0).kind ==
                   display::GaugeRoundRenderPrimitiveKind::state_badge);
            EXPECT(!primary(plan, 0).value.present);
            EXPECT(primary(plan, 0).value.raw_value == 0);
            EXPECT(primary(plan, 0).unit == telemetry::SignalUnit::none);
            EXPECT(primary(plan, 0).normalized_position == 0);
        }
    }
}

void test_status_fails_closed_until_boolean_signal_is_registered() {
    auto input = frame(1);
    input.widgets[0].kind = display::GaugeWidgetKind::status;
    input.widgets[0].scale_min_raw = 0;
    input.widgets[0].scale_max_raw = 0;
    expect_compile_failure_preserves(
        input,
        display::kMaximumRoundGaugeRenderPrimitives,
        display::GaugeRoundRenderPlanError::invalid_frame);
}

void test_capacity_and_recovery_overlay_are_atomic() {
    auto input = frame(8);
    input.recovery_required = true;
    constexpr std::size_t required =
        display::kMaximumGaugeWidgets * 3U + 1U;
    expect_compile_failure_preserves(
        input,
        required - 1U,
        display::GaugeRoundRenderPlanError::insufficient_output_capacity);
    display::GaugeRoundRenderPlan plan{};
    const auto result = display::compile_gauge_round_render_plan(
        input, required, plan);
    EXPECT(result.compiled());
    EXPECT(result.required_primitive_capacity == required);
    EXPECT(plan.primitive_count == required);
    EXPECT(plan.primitives[required - 1U].kind ==
           display::GaugeRoundRenderPrimitiveKind::recovery_badge);
    EXPECT(plan.primitives[required - 1U].attention_required);
    EXPECT(!plan.primitives[required - 1U].value.present);
}

void test_invalid_frame_variants_preserve_output() {
    auto invalid = frame();
    invalid.widget_count = 0;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame(1);
    invalid.widget_count = 9;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.layout_generation = 0;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.layout_source = configuration::GaugeLayoutSource::none;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.theme = static_cast<configuration::GaugeTheme>(0xFFU);
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[1].widget_id = invalid.widgets[0].widget_id;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[0].label.bytes[0] = '\x01';
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[0].signal_code =
        static_cast<wireless::TelemetrySignalCode>(0xFFFFU);
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[0].kind = static_cast<display::GaugeWidgetKind>(0xFFU);
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[0].state = static_cast<display::GaugeValueState>(0xFFU);
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[0].unit = telemetry::SignalUnit::millivolt;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[0].display_value.type =
        telemetry::SignalValueType::signed_integer;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[0].display_value.present = false;
    invalid.widgets[0].display_value.raw_value = 0;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[0].attention_required = true;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
    invalid = frame();
    invalid.widgets[1].scale_max_raw = invalid.widgets[1].scale_min_raw;
    expect_compile_failure_preserves(
        invalid, 25, display::GaugeRoundRenderPlanError::invalid_frame);
}

void test_full_signed_range_normalization_is_exact_and_overflow_free() {
    auto input = frame(1);
    input.widgets[0] = widget(
        101,
        display::GaugeWidgetKind::needle,
        display::GaugeValueState::valid,
        wireless::TelemetrySignalCode::engine_coolant_temperature,
        0);
    input.widgets[0].scale_min_raw = std::numeric_limits<std::int64_t>::min();
    input.widgets[0].scale_max_raw = std::numeric_limits<std::int64_t>::max();
    display::GaugeRoundRenderPlan plan{};
    EXPECT(display::compile_gauge_round_render_plan(input, 3, plan).compiled());
    EXPECT(primary(plan, 0).normalized_position == 500000U);
    input.widgets[0].display_value.raw_value =
        std::numeric_limits<std::int64_t>::min();
    EXPECT(display::compile_gauge_round_render_plan(input, 3, plan).compiled());
    EXPECT(primary(plan, 0).normalized_position == 0);
    input.widgets[0].display_value.raw_value =
        std::numeric_limits<std::int64_t>::max();
    EXPECT(display::compile_gauge_round_render_plan(input, 3, plan).compiled());
    EXPECT(primary(plan, 0).normalized_position == 1000000U);
}

void test_smaller_recompile_clears_inactive_tail_and_private_metadata() {
    display::GaugeRoundRenderPlan plan{};
    auto large = frame(8);
    EXPECT(display::compile_gauge_round_render_plan(large, 24, plan).compiled());
    EXPECT(plan.primitives[23].widget_id != 0);
    auto small = frame(1);
    EXPECT(display::compile_gauge_round_render_plan(small, 3, plan).compiled());
    EXPECT(plan.primitive_count == 3);
    EXPECT(plan.primitives[3].widget_id == 0);
    EXPECT(plan.primitives[3].value.raw_value == 0);
    EXPECT(plan.primitives[0].widget_id == small.widgets[0].widget_id);
    // Receiver age/session/packet metadata has no render-plan field and thus
    // cannot be copied into a primitive.
}

void test_semantic_equality_ignores_only_diagnostics_and_inactive_tail() {
    display::GaugeRoundRenderPlan left{};
    EXPECT(display::compile_gauge_round_render_plan(frame(), 9, left).compiled());
    auto right = left;
    right.source_publication_sequence += 100;
    right.source_published_at_ms += 100;
    right.primitives[20].widget_id = 999;
    EXPECT(display::gauge_round_render_plans_semantically_equal(left, right));
    right.primitives[2].normalized_position += 1;
    EXPECT(!display::gauge_round_render_plans_semantically_equal(left, right));
    right = left;
    right.layout_generation += 1;
    EXPECT(!display::gauge_round_render_plans_semantically_equal(left, right));
    right = left;
    right.primitives[1].label.bytes[0] = 'X';
    EXPECT(!display::gauge_round_render_plans_semantically_equal(left, right));
}

}  // namespace

int main() {
    test_all_widget_counts_have_bounded_circular_geometry();
    test_valid_representations_and_diagnostic_metadata();
    test_suspect_and_every_nonvalid_state_are_no_value_badges();
    test_status_fails_closed_until_boolean_signal_is_registered();
    test_capacity_and_recovery_overlay_are_atomic();
    test_invalid_frame_variants_preserve_output();
    test_full_signed_range_normalization_is_exact_and_overflow_free();
    test_smaller_recompile_clears_inactive_tail_and_private_metadata();
    test_semantic_equality_ignores_only_diagnostics_and_inactive_tail();

    if (failures != 0) {
        std::cerr << failures << " gauge round render-plan test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 gauge round render-plan scenario groups\n";
    return EXIT_SUCCESS;
}
