#include "opengauge/gauge_round_render_plan.hpp"

#include <array>
#include <cstdint>

namespace opengauge::display {
namespace {

struct SlotGeometry {
    std::uint16_t x;
    std::uint16_t y;
    std::uint16_t width;
    std::uint16_t height;
};

using SlotSet = std::array<SlotGeometry, kMaximumGaugeWidgets>;

constexpr std::array<SlotSet, kMaximumGaugeWidgets> kSlotSets{{
    {{{93, 153, 280, 160}}},
    {{{108, 82, 250, 112}, {108, 272, 250, 112}}},
    {{{175, 58, 116, 84}, {72, 244, 116, 84}, {278, 244, 116, 84}}},
    {{{90, 103, 110, 84}, {266, 103, 110, 84},
      {90, 279, 110, 84}, {266, 279, 110, 84}}},
    {{{185, 62, 96, 72}, {313, 155, 96, 72}, {264, 306, 96, 72},
      {106, 306, 96, 72}, {57, 155, 96, 72}}},
    {{{187, 63, 92, 70}, {304, 131, 92, 70}, {304, 266, 92, 70},
      {187, 333, 92, 70}, {70, 266, 92, 70}, {70, 131, 92, 70}}},
    {{{191, 58, 84, 64}, {315, 130, 84, 64}, {315, 273, 84, 64},
      {191, 344, 84, 64}, {67, 273, 84, 64}, {67, 130, 84, 64},
      {191, 201, 84, 64}}},
    {{{194, 59, 78, 58}, {297, 101, 78, 58}, {339, 204, 78, 58},
      {297, 307, 78, 58}, {194, 349, 78, 58}, {91, 307, 78, 58},
      {49, 204, 78, 58}, {91, 101, 78, 58}}},
}};

constexpr GaugeRoundRenderBox kRecoveryBounds{173, 16, 120, 32};

bool known_state(GaugeValueState state) {
    switch (state) {
        case GaugeValueState::valid:
        case GaugeValueState::suspect:
        case GaugeValueState::missing:
        case GaugeValueState::stale:
        case GaugeValueState::unavailable:
        case GaugeValueState::error:
        case GaugeValueState::out_of_range:
        case GaugeValueState::unknown:
            return true;
    }
    return false;
}

bool known_kind(GaugeWidgetKind kind) {
    switch (kind) {
        case GaugeWidgetKind::numeric:
        case GaugeWidgetKind::needle:
        case GaugeWidgetKind::bar:
        case GaugeWidgetKind::status:
            return true;
    }
    return false;
}

bool known_theme(configuration::GaugeTheme theme) {
    switch (theme) {
        case configuration::GaugeTheme::dark:
        case configuration::GaugeTheme::light:
        case configuration::GaugeTheme::high_contrast:
            return true;
    }
    return false;
}

bool known_source(configuration::GaugeLayoutSource source) {
    switch (source) {
        case configuration::GaugeLayoutSource::slot_a:
        case configuration::GaugeLayoutSource::slot_b:
        case configuration::GaugeLayoutSource::safe_default:
            return true;
        case configuration::GaugeLayoutSource::none:
            return false;
    }
    return false;
}

bool canonical_label(const GaugeWidgetLabel& label) {
    if (label.length == 0 || label.length > kMaximumGaugeWidgetLabelBytes ||
        label.bytes[label.length] != '\0') {
        return false;
    }
    for (std::size_t index = 0; index < label.bytes.size(); ++index) {
        if (index < label.length) {
            const auto byte = static_cast<unsigned char>(label.bytes[index]);
            if (byte < 0x20U || byte > 0x7EU) {
                return false;
            }
        } else if (label.bytes[index] != '\0') {
            return false;
        }
    }
    return true;
}

bool valid_box(const GaugeRoundRenderBox& box) {
    return box.width != 0 && box.height != 0 &&
           static_cast<std::uint32_t>(box.x) + box.width <=
               kRoundGaugeViewportPixels &&
           static_cast<std::uint32_t>(box.y) + box.height <=
               kRoundGaugeViewportPixels;
}

bool within_safe_circle(const SlotGeometry& slot) {
    constexpr std::int32_t kSafeRadius = 218;
    constexpr std::int32_t kSafeRadiusSquared =
        kSafeRadius * kSafeRadius;
    const std::array<std::int32_t, 2> xs{{
        static_cast<std::int32_t>(slot.x),
        static_cast<std::int32_t>(slot.x + slot.width)}};
    const std::array<std::int32_t, 2> ys{{
        static_cast<std::int32_t>(slot.y),
        static_cast<std::int32_t>(slot.y + slot.height)}};
    for (const auto x : xs) {
        for (const auto y : ys) {
            const auto dx = x - kRoundGaugeClipCenterPixels;
            const auto dy = y - kRoundGaugeClipCenterPixels;
            if (dx * dx + dy * dy > kSafeRadiusSquared) {
                return false;
            }
        }
    }
    return true;
}

bool valid_value_shape(const telemetry::SignalValue& value) {
    if (!value.present) {
        return value.raw_value == 0;
    }
    switch (value.type) {
        case telemetry::SignalValueType::boolean:
            return value.raw_value == 0 || value.raw_value == 1;
        case telemetry::SignalValueType::signed_integer:
            return true;
        case telemetry::SignalValueType::unsigned_integer:
            return value.raw_value >= 0;
    }
    return false;
}

bool valid_snapshot(const GaugeWidgetSnapshot& widget) {
    const auto* descriptor =
        wireless::telemetry_signal_descriptor(widget.signal_code);
    if (widget.widget_id == 0 || descriptor == nullptr ||
        !canonical_label(widget.label) || !known_kind(widget.kind) ||
        !known_state(widget.state) ||
        widget.display_value.type != descriptor->value_type ||
        widget.unit != descriptor->unit ||
        !valid_value_shape(widget.display_value) ||
        widget.attention_required != (widget.state != GaugeValueState::valid)) {
        return false;
    }

    // The current registry contains no boolean signal, so a dashboard-loop
    // status widget is not currently constructible and must fail closed here.
    switch (widget.kind) {
        case GaugeWidgetKind::numeric:
            if (widget.scale_min_raw != 0 || widget.scale_max_raw != 0) {
                return false;
            }
            break;
        case GaugeWidgetKind::needle:
        case GaugeWidgetKind::bar:
            if (descriptor->value_type == telemetry::SignalValueType::boolean ||
                widget.scale_min_raw >= widget.scale_max_raw) {
                return false;
            }
            break;
        case GaugeWidgetKind::status:
            return false;
    }

    if (widget.state == GaugeValueState::valid ||
        widget.state == GaugeValueState::suspect) {
        return widget.display_value.present;
    }
    return !widget.display_value.present && widget.display_value.raw_value == 0;
}

// Exact overflow-free fraction comparison. Returns -1, 0, or 1 for a/b
// versus c/d using continued-fraction quotients, never cross multiplication.
int compare_fractions(
    std::uint64_t a,
    std::uint64_t b,
    std::uint64_t c,
    std::uint64_t d) {
    bool inverted = false;
    for (;;) {
        const auto left_quotient = a / b;
        const auto right_quotient = c / d;
        if (left_quotient != right_quotient) {
            const int ordinary = left_quotient < right_quotient ? -1 : 1;
            return inverted ? -ordinary : ordinary;
        }
        const auto left_remainder = a % b;
        const auto right_remainder = c % d;
        if (left_remainder == 0 || right_remainder == 0) {
            int ordinary = 0;
            if (left_remainder == 0 && right_remainder != 0) {
                ordinary = -1;
            } else if (left_remainder != 0 && right_remainder == 0) {
                ordinary = 1;
            }
            return inverted ? -ordinary : ordinary;
        }
        a = b;
        b = left_remainder;
        c = d;
        d = right_remainder;
        inverted = !inverted;
    }
}

std::uint64_t ordered_signed(std::int64_t value) {
    return static_cast<std::uint64_t>(value) ^
           (std::uint64_t{1} << 63U);
}

std::uint32_t normalized_position(
    std::int64_t value,
    std::int64_t minimum,
    std::int64_t maximum) {
    if (value <= minimum) {
        return 0;
    }
    if (value >= maximum) {
        return kRoundGaugeNormalizedMaximum;
    }
    const auto offset = ordered_signed(value) - ordered_signed(minimum);
    const auto span = ordered_signed(maximum) - ordered_signed(minimum);
    std::uint32_t low = 0;
    std::uint32_t high = kRoundGaugeNormalizedMaximum;
    while (low < high) {
        const auto middle = low + (high - low + 1U) / 2U;
        if (compare_fractions(
                middle,
                kRoundGaugeNormalizedMaximum,
                offset,
                span) <= 0) {
            low = middle;
        } else {
            high = middle - 1U;
        }
    }
    return low;
}

GaugeRoundRenderPrimitive make_base_primitive(
    GaugeRoundRenderPrimitiveKind kind,
    const GaugeRoundRenderBox& bounds,
    const GaugeWidgetSnapshot& widget) {
    GaugeRoundRenderPrimitive primitive{};
    primitive.kind = kind;
    primitive.bounds = bounds;
    primitive.widget_id = widget.widget_id;
    primitive.widget_kind = widget.kind;
    primitive.state = widget.state;
    primitive.attention_required = widget.attention_required;
    return primitive;
}

bool labels_equal(const GaugeWidgetLabel& left, const GaugeWidgetLabel& right) {
    if (left.length != right.length ||
        left.length > kMaximumGaugeWidgetLabelBytes ||
        right.length > kMaximumGaugeWidgetLabelBytes) {
        return false;
    }
    for (std::size_t index = 0; index < left.length; ++index) {
        if (left.bytes[index] != right.bytes[index]) {
            return false;
        }
    }
    return true;
}

bool primitives_equal(
    const GaugeRoundRenderPrimitive& left,
    const GaugeRoundRenderPrimitive& right) {
    return left.kind == right.kind && left.bounds.x == right.bounds.x &&
           left.bounds.y == right.bounds.y &&
           left.bounds.width == right.bounds.width &&
           left.bounds.height == right.bounds.height &&
           left.widget_id == right.widget_id &&
           left.widget_kind == right.widget_kind &&
           left.state == right.state && labels_equal(left.label, right.label) &&
           left.value.type == right.value.type &&
           left.value.raw_value == right.value.raw_value &&
           left.value.present == right.value.present &&
           left.unit == right.unit &&
           left.normalized_position == right.normalized_position &&
           left.attention_required == right.attention_required;
}

}  // namespace

GaugeRoundRenderPlanCompileResult compile_gauge_round_render_plan(
    const GaugeDashboardFrame& frame,
    std::size_t primitive_capacity,
    GaugeRoundRenderPlan& output) {
    if (frame.layout_generation == 0 || frame.layout_id == 0 ||
        !known_source(frame.layout_source) || !known_theme(frame.theme) ||
        frame.brightness_percent == 0 || frame.brightness_percent > 100 ||
        frame.widget_count == 0 || frame.widget_count > kMaximumGaugeWidgets) {
        return {GaugeRoundRenderPlanError::invalid_frame, 0, 0};
    }

    const auto required =
        static_cast<std::size_t>(frame.widget_count) *
            kRoundGaugePrimitivesPerWidget +
        (frame.recovery_required ? 1U : 0U);
    if (primitive_capacity < required ||
        required > kMaximumRoundGaugeRenderPrimitives) {
        return {
            GaugeRoundRenderPlanError::insufficient_output_capacity,
            required,
            0};
    }

    for (std::size_t index = 0; index < frame.widget_count; ++index) {
        if (!valid_snapshot(frame.widgets[index])) {
            return {GaugeRoundRenderPlanError::invalid_frame, required, 0};
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (frame.widgets[prior].widget_id ==
                frame.widgets[index].widget_id) {
                return {GaugeRoundRenderPlanError::invalid_frame, required, 0};
            }
        }
    }

    const auto& slots = kSlotSets[frame.widget_count - 1U];
    for (std::size_t index = 0; index < frame.widget_count; ++index) {
        const auto& slot = slots[index];
        const GaugeRoundRenderBox box{
            slot.x, slot.y, slot.width, slot.height};
        if (!valid_box(box) || !within_safe_circle(slot)) {
            return {GaugeRoundRenderPlanError::invalid_geometry, required, 0};
        }
    }
    if (frame.recovery_required && !valid_box(kRecoveryBounds)) {
        return {GaugeRoundRenderPlanError::invalid_geometry, required, 0};
    }

    GaugeRoundRenderPlan candidate{};
    candidate.source_publication_sequence = frame.publication_sequence;
    candidate.source_published_at_ms = frame.published_at_ms;
    candidate.layout_generation = frame.layout_generation;
    candidate.layout_id = frame.layout_id;
    candidate.layout_source = frame.layout_source;
    candidate.theme = frame.theme;
    candidate.brightness_percent = frame.brightness_percent;
    candidate.widget_count = frame.widget_count;
    candidate.recovery_required = frame.recovery_required;

    std::size_t destination = 0;
    for (std::size_t index = 0; index < frame.widget_count; ++index) {
        const auto& widget = frame.widgets[index];
        const auto& slot = slots[index];
        const GaugeRoundRenderBox panel{
            slot.x, slot.y, slot.width, slot.height};
        const auto label_height = static_cast<std::uint16_t>(slot.height / 3U);
        const GaugeRoundRenderBox label{
            static_cast<std::uint16_t>(slot.x + 4U),
            static_cast<std::uint16_t>(slot.y + 4U),
            static_cast<std::uint16_t>(slot.width - 8U),
            static_cast<std::uint16_t>(label_height - 4U)};
        const GaugeRoundRenderBox primary{
            static_cast<std::uint16_t>(slot.x + 4U),
            static_cast<std::uint16_t>(slot.y + label_height),
            static_cast<std::uint16_t>(slot.width - 8U),
            static_cast<std::uint16_t>(slot.height - label_height - 4U)};

        candidate.primitives[destination++] = make_base_primitive(
            GaugeRoundRenderPrimitiveKind::widget_panel, panel, widget);
        auto label_primitive = make_base_primitive(
            GaugeRoundRenderPrimitiveKind::label, label, widget);
        label_primitive.label = widget.label;
        candidate.primitives[destination++] = label_primitive;

        if (widget.state != GaugeValueState::valid) {
            candidate.primitives[destination++] = make_base_primitive(
                GaugeRoundRenderPrimitiveKind::state_badge, primary, widget);
            continue;
        }

        auto primary_primitive = make_base_primitive(
            GaugeRoundRenderPrimitiveKind::numeric_value, primary, widget);
        primary_primitive.value = widget.display_value;
        primary_primitive.unit = widget.unit;
        switch (widget.kind) {
            case GaugeWidgetKind::numeric:
                primary_primitive.kind =
                    GaugeRoundRenderPrimitiveKind::numeric_value;
                break;
            case GaugeWidgetKind::needle:
                primary_primitive.kind = GaugeRoundRenderPrimitiveKind::needle;
                primary_primitive.normalized_position = normalized_position(
                    widget.display_value.raw_value,
                    widget.scale_min_raw,
                    widget.scale_max_raw);
                break;
            case GaugeWidgetKind::bar:
                primary_primitive.kind = GaugeRoundRenderPrimitiveKind::bar;
                primary_primitive.normalized_position = normalized_position(
                    widget.display_value.raw_value,
                    widget.scale_min_raw,
                    widget.scale_max_raw);
                break;
            case GaugeWidgetKind::status:
                return {GaugeRoundRenderPlanError::invalid_frame, required, 0};
        }
        candidate.primitives[destination++] = primary_primitive;
    }

    if (frame.recovery_required) {
        GaugeRoundRenderPrimitive recovery{};
        recovery.kind = GaugeRoundRenderPrimitiveKind::recovery_badge;
        recovery.bounds = kRecoveryBounds;
        recovery.attention_required = true;
        candidate.primitives[destination++] = recovery;
    }
    candidate.primitive_count = static_cast<std::uint8_t>(destination);
    output = candidate;
    return {GaugeRoundRenderPlanError::none, required, destination};
}

bool gauge_round_render_plans_semantically_equal(
    const GaugeRoundRenderPlan& left,
    const GaugeRoundRenderPlan& right) {
    if (left.layout_generation != right.layout_generation ||
        left.layout_id != right.layout_id ||
        left.layout_source != right.layout_source ||
        left.theme != right.theme ||
        left.brightness_percent != right.brightness_percent ||
        left.widget_count != right.widget_count ||
        left.recovery_required != right.recovery_required ||
        left.primitive_count != right.primitive_count ||
        left.primitive_count > left.primitives.size() ||
        right.primitive_count > right.primitives.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.primitive_count; ++index) {
        if (!primitives_equal(left.primitives[index], right.primitives[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace opengauge::display
