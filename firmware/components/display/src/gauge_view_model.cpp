#include "opengauge/gauge_view_model.hpp"

#include <array>
#include <limits>

namespace opengauge::display {
namespace {

constexpr std::size_t kNotFound =
    std::numeric_limits<std::size_t>::max();

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

bool canonical_label(const GaugeWidgetLabel& label) {
    if (label.length == 0 ||
        label.length > kMaximumGaugeWidgetLabelBytes ||
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

GaugeValueState map_state(telemetry::SignalQuality quality) {
    switch (quality) {
        case telemetry::SignalQuality::valid:
            return GaugeValueState::valid;
        case telemetry::SignalQuality::suspect:
            return GaugeValueState::suspect;
        case telemetry::SignalQuality::stale:
            return GaugeValueState::stale;
        case telemetry::SignalQuality::unavailable:
            return GaugeValueState::unavailable;
        case telemetry::SignalQuality::error:
            return GaugeValueState::error;
        case telemetry::SignalQuality::out_of_range:
            return GaugeValueState::out_of_range;
        case telemetry::SignalQuality::unknown:
            return GaugeValueState::unknown;
    }
    return GaugeValueState::unknown;
}

bool state_allows_value(GaugeValueState state) {
    return state == GaugeValueState::valid ||
           state == GaugeValueState::suspect;
}

}  // namespace

GaugeViewModelError make_gauge_widget_label(
    std::string_view text,
    GaugeWidgetLabel& output) {
    if (text.empty() || text.size() > kMaximumGaugeWidgetLabelBytes) {
        return GaugeViewModelError::invalid_configuration;
    }
    GaugeWidgetLabel candidate{};
    for (std::size_t index = 0; index < text.size(); ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if (byte < 0x20U || byte > 0x7EU) {
            return GaugeViewModelError::invalid_configuration;
        }
        candidate.bytes[index] = text[index];
    }
    candidate.length = static_cast<std::uint8_t>(text.size());
    output = candidate;
    return GaugeViewModelError::none;
}

GaugeViewModelError validate_gauge_widget_configuration(
    const GaugeWidgetConfiguration& configuration) {
    const auto* descriptor = wireless::telemetry_signal_descriptor(
        configuration.signal_code);
    if (configuration.widget_id == 0 || descriptor == nullptr ||
        !known_kind(configuration.kind) ||
        !canonical_label(configuration.label) ||
        configuration.stale_after_ms == 0) {
        return GaugeViewModelError::invalid_configuration;
    }
    if (configuration.kind == GaugeWidgetKind::needle ||
        configuration.kind == GaugeWidgetKind::bar) {
        if (descriptor->value_type == telemetry::SignalValueType::boolean ||
            configuration.scale_min_raw >= configuration.scale_max_raw) {
            return GaugeViewModelError::invalid_configuration;
        }
    } else if (configuration.scale_min_raw != 0 ||
               configuration.scale_max_raw != 0) {
        return GaugeViewModelError::invalid_configuration;
    }
    if (configuration.kind == GaugeWidgetKind::status &&
        descriptor->value_type != telemetry::SignalValueType::boolean) {
        return GaugeViewModelError::invalid_configuration;
    }
    return GaugeViewModelError::none;
}

GaugeViewModel::GaugeViewModel(
    const wireless::GaugeTelemetryReceiver& receiver)
    : receiver_(receiver) {}

GaugeViewModelError GaugeViewModel::add_widget(
    const GaugeWidgetConfiguration& configuration) {
    if (status_.running) {
        return GaugeViewModelError::invalid_state;
    }
    const auto validation =
        validate_gauge_widget_configuration(configuration);
    if (validation != GaugeViewModelError::none) {
        return validation;
    }
    if (find_widget(configuration.widget_id) != kNotFound) {
        return GaugeViewModelError::duplicate_widget;
    }
    if (widget_count_ == widgets_.size()) {
        return GaugeViewModelError::widget_capacity_full;
    }
    widgets_[widget_count_] = configuration;
    ++widget_count_;
    status_.widget_count = widget_count_;
    return GaugeViewModelError::none;
}

GaugeViewModelError GaugeViewModel::clear_widgets() {
    if (status_.running) {
        return GaugeViewModelError::invalid_state;
    }
    widgets_ = {};
    widget_count_ = 0;
    status_ = {};
    return GaugeViewModelError::none;
}

GaugeViewModelError GaugeViewModel::replace_widgets(
    const GaugeWidgetConfiguration* configurations,
    std::size_t configuration_count) {
    if (configurations == nullptr || configuration_count == 0) {
        return GaugeViewModelError::invalid_configuration;
    }
    if (configuration_count > widgets_.size()) {
        return GaugeViewModelError::widget_capacity_full;
    }

    std::array<GaugeWidgetConfiguration, kMaximumGaugeWidgets> candidate{};
    for (std::size_t index = 0; index < configuration_count; ++index) {
        const auto validation =
            validate_gauge_widget_configuration(configurations[index]);
        if (validation != GaugeViewModelError::none) {
            return validation;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (candidate[prior].widget_id ==
                configurations[index].widget_id) {
                return GaugeViewModelError::duplicate_widget;
            }
        }
        candidate[index] = configurations[index];
    }

    widgets_ = candidate;
    widget_count_ = configuration_count;
    status_.widget_count = widget_count_;
    return GaugeViewModelError::none;
}

GaugeViewModelError GaugeViewModel::start() {
    if (status_.running || !receiver_.status().running ||
        widget_count_ == 0) {
        return GaugeViewModelError::invalid_state;
    }
    status_ = {};
    status_.running = true;
    status_.widget_count = widget_count_;
    return GaugeViewModelError::none;
}

void GaugeViewModel::stop() {
    status_.running = false;
    status_.last_attention_count = 0;
}

GaugeDashboardRefreshResult GaugeViewModel::refresh(
    std::uint64_t now_ms,
    GaugeWidgetSnapshot* output,
    std::size_t output_capacity) {
    if (!status_.running) {
        return {GaugeViewModelError::invalid_state};
    }
    if (output == nullptr && output_capacity != 0) {
        return {GaugeViewModelError::invalid_configuration};
    }
    if (output_capacity < widget_count_) {
        return {GaugeViewModelError::insufficient_output_capacity};
    }

    std::array<GaugeWidgetSnapshot, kMaximumGaugeWidgets> candidate{};
    GaugeDashboardRefreshResult result{
        GaugeViewModelError::none,
        wireless::GaugeReceiverError::none,
        widget_count_,
        0,
        0,
        now_ms};
    for (std::size_t index = 0; index < widget_count_; ++index) {
        const auto& configuration = widgets_[index];
        auto& snapshot = candidate[index];
        snapshot.widget_id = configuration.widget_id;
        snapshot.signal_code = configuration.signal_code;
        snapshot.kind = configuration.kind;
        snapshot.label = configuration.label;
        snapshot.scale_min_raw = configuration.scale_min_raw;
        snapshot.scale_max_raw = configuration.scale_max_raw;
        snapshot.state = GaugeValueState::missing;
        snapshot.attention_required = true;
        const auto* descriptor = wireless::telemetry_signal_descriptor(
            configuration.signal_code);
        snapshot.display_value.type = descriptor->value_type;
        snapshot.unit = descriptor->unit;

        const auto received = receiver_.read(
            configuration.signal_code,
            now_ms,
            configuration.stale_after_ms);
        if (received.error ==
            wireless::GaugeReceiverError::signal_not_found) {
            ++result.attention_count;
            continue;
        }
        if (!received.found()) {
            ++status_.receiver_failures;
            return {
                GaugeViewModelError::receiver_failure,
                received.error,
                widget_count_,
                0,
                0,
                now_ms};
        }

        snapshot.state = map_state(received.effective_quality);
        snapshot.unit = received.unit;
        snapshot.age_ms = received.age_ms;
        snapshot.boot_session_id = received.boot_session_id;
        snapshot.packet_sequence = received.packet_sequence;
        snapshot.display_value = received.display_value;
        snapshot.attention_required =
            snapshot.state != GaugeValueState::valid;
        if (!state_allows_value(snapshot.state)) {
            snapshot.display_value.raw_value = 0;
            snapshot.display_value.present = false;
        } else {
            ++result.numeric_value_count;
        }
        if (snapshot.attention_required) {
            ++result.attention_count;
        }
    }

    for (std::size_t index = 0; index < widget_count_; ++index) {
        output[index] = candidate[index];
    }
    ++status_.refreshes_completed;
    status_.last_attention_count =
        static_cast<std::uint32_t>(result.attention_count);
    return result;
}

GaugeViewModelStatus GaugeViewModel::status() const {
    return status_;
}

bool GaugeViewModel::bound_to(
    const wireless::GaugeTelemetryReceiver& receiver) const {
    return &receiver_ == &receiver;
}

std::size_t GaugeViewModel::find_widget(std::uint16_t widget_id) const {
    for (std::size_t index = 0; index < widget_count_; ++index) {
        if (widgets_[index].widget_id == widget_id) {
            return index;
        }
    }
    return kNotFound;
}

}  // namespace opengauge::display
