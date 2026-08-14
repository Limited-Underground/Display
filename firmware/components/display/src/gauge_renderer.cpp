#include "opengauge/gauge_renderer.hpp"

namespace opengauge::display {
namespace {

bool labels_equal(
    const GaugeWidgetLabel& left,
    const GaugeWidgetLabel& right) {
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

bool values_equal(
    const telemetry::SignalValue& left,
    const telemetry::SignalValue& right) {
    return left.type == right.type &&
           left.raw_value == right.raw_value &&
           left.present == right.present;
}

bool widgets_equal(
    const GaugeWidgetSnapshot& left,
    const GaugeWidgetSnapshot& right) {
    return left.widget_id == right.widget_id &&
           left.signal_code == right.signal_code &&
           left.kind == right.kind &&
           labels_equal(left.label, right.label) &&
           left.state == right.state &&
           values_equal(left.display_value, right.display_value) &&
           left.unit == right.unit &&
           left.age_ms == right.age_ms &&
           left.boot_session_id == right.boot_session_id &&
           left.packet_sequence == right.packet_sequence &&
           left.scale_min_raw == right.scale_min_raw &&
           left.scale_max_raw == right.scale_max_raw &&
           left.attention_required == right.attention_required;
}

}  // namespace

bool gauge_dashboard_frames_semantically_equal(
    const GaugeDashboardFrame& left,
    const GaugeDashboardFrame& right) {
    if (left.layout_generation != right.layout_generation ||
        left.layout_id != right.layout_id ||
        left.layout_source != right.layout_source ||
        left.theme != right.theme ||
        left.brightness_percent != right.brightness_percent ||
        left.widget_count != right.widget_count ||
        left.recovery_required != right.recovery_required ||
        left.widget_count > left.widgets.size() ||
        right.widget_count > right.widgets.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.widget_count; ++index) {
        if (!widgets_equal(left.widgets[index], right.widgets[index])) {
            return false;
        }
    }
    return true;
}

}  // namespace opengauge::display
