#include "opengauge/gauge_trend_buffer.hpp"

#include <limits>

namespace opengauge::display {
namespace {

constexpr std::size_t kNotFound =
    std::numeric_limits<std::size_t>::max();

bool state_allows_value(GaugeValueState state) {
    return state == GaugeValueState::valid ||
           state == GaugeValueState::suspect;
}

bool known_state(GaugeValueState state) {
    const auto value = static_cast<std::uint8_t>(state);
    return value >= static_cast<std::uint8_t>(GaugeValueState::valid) &&
           value <= static_cast<std::uint8_t>(GaugeValueState::unknown);
}

bool valid_snapshot(const GaugeWidgetSnapshot& snapshot) {
    const auto* descriptor = wireless::telemetry_signal_descriptor(
        snapshot.signal_code);
    if (snapshot.widget_id == 0 || descriptor == nullptr ||
        !known_state(snapshot.state) ||
        snapshot.unit != descriptor->unit ||
        snapshot.display_value.type != descriptor->value_type) {
        return false;
    }
    const bool allows = state_allows_value(snapshot.state);
    if (snapshot.display_value.present != allows ||
        (allows && snapshot.boot_session_id == 0)) {
        return false;
    }
    if (!allows && snapshot.display_value.raw_value != 0) {
        return false;
    }
    return true;
}

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

GaugeTrendError GaugeTrendBuffer::add_trend(
    const GaugeTrendConfiguration& configuration) {
    if (status_.running) {
        return GaugeTrendError::invalid_state;
    }
    if (configuration.trend_id == 0 ||
        wireless::telemetry_signal_descriptor(
            configuration.signal_code) == nullptr ||
        configuration.point_capacity < 2 ||
        configuration.point_capacity > kMaximumTrendPoints ||
        configuration.minimum_sample_interval_ms == 0) {
        return GaugeTrendError::invalid_configuration;
    }
    if (find_trend(configuration.trend_id) != kNotFound) {
        return GaugeTrendError::duplicate_trend;
    }
    if (trend_count_ == series_.size()) {
        return GaugeTrendError::trend_capacity_full;
    }
    series_[trend_count_].configuration = configuration;
    ++trend_count_;
    status_.trend_count = trend_count_;
    return GaugeTrendError::none;
}

GaugeTrendError GaugeTrendBuffer::clear_trends() {
    if (status_.running) {
        return GaugeTrendError::invalid_state;
    }
    series_ = {};
    trend_count_ = 0;
    status_ = {};
    return GaugeTrendError::none;
}

GaugeTrendError GaugeTrendBuffer::start() {
    if (status_.running || trend_count_ == 0) {
        return GaugeTrendError::invalid_state;
    }
    status_ = {};
    status_.running = true;
    status_.trend_count = trend_count_;
    for (std::size_t index = 0; index < trend_count_; ++index) {
        const auto configuration = series_[index].configuration;
        series_[index] = {};
        series_[index].configuration = configuration;
    }
    return GaugeTrendError::none;
}

void GaugeTrendBuffer::stop() {
    status_.running = false;
}

GaugeTrendAppendResult GaugeTrendBuffer::append(
    const GaugeWidgetSnapshot& snapshot,
    std::uint64_t captured_at_ms) {
    if (!status_.running) {
        return {GaugeTrendError::invalid_state};
    }
    if (!valid_snapshot(snapshot)) {
        return {GaugeTrendError::invalid_snapshot};
    }

    for (std::size_t index = 0; index < trend_count_; ++index) {
        const auto& series = series_[index];
        if (series.configuration.signal_code == snapshot.signal_code &&
            series.has_last_sample &&
            captured_at_ms < series.last_sample_ms) {
            return {GaugeTrendError::clock_regression};
        }
    }

    GaugeTrendAppendResult result{GaugeTrendError::none, 0, 0};
    for (std::size_t index = 0; index < trend_count_; ++index) {
        auto& series = series_[index];
        if (series.configuration.signal_code != snapshot.signal_code) {
            continue;
        }
        if (series.has_last_sample &&
            captured_at_ms - series.last_sample_ms <
                series.configuration.minimum_sample_interval_ms) {
            ++result.trends_interval_skipped;
            saturating_increment(status_.interval_skips);
            continue;
        }

        std::size_t destination = 0;
        if (series.count < series.configuration.point_capacity) {
            destination =
                (series.start + series.count) % series.configuration.point_capacity;
            ++series.count;
        } else {
            destination = series.start;
            series.start =
                (series.start + 1) % series.configuration.point_capacity;
            saturating_increment(status_.points_overwritten);
        }
        GaugeTrendPoint point{};
        point.captured_at_ms = captured_at_ms;
        point.state = snapshot.state;
        point.value = snapshot.display_value;
        point.unit = snapshot.unit;
        point.boot_session_id = snapshot.boot_session_id;
        point.packet_sequence = snapshot.packet_sequence;
        point.gap = !state_allows_value(snapshot.state);
        if (point.gap) {
            point.value.present = false;
            point.value.raw_value = 0;
        }
        series.points[destination] = point;
        series.has_last_sample = true;
        series.last_sample_ms = captured_at_ms;
        ++result.trends_appended;
        saturating_increment(status_.points_appended);
    }
    return result;
}

GaugeTrendError GaugeTrendBuffer::read(
    std::uint16_t trend_id,
    GaugeTrendPoint* output,
    std::size_t output_capacity,
    std::size_t& output_count) const {
    if (!status_.running) {
        return GaugeTrendError::invalid_state;
    }
    const auto index = find_trend(trend_id);
    if (index == kNotFound) {
        return GaugeTrendError::trend_not_found;
    }
    const auto& series = series_[index];
    if ((output == nullptr && output_capacity != 0) ||
        output_capacity < series.count) {
        return output_capacity < series.count
                   ? GaugeTrendError::insufficient_output_capacity
                   : GaugeTrendError::invalid_configuration;
    }
    for (std::size_t point = 0; point < series.count; ++point) {
        output[point] = series.points[
            (series.start + point) % series.configuration.point_capacity];
    }
    output_count = series.count;
    return GaugeTrendError::none;
}

GaugeTrendError GaugeTrendBuffer::clear_points(std::uint16_t trend_id) {
    if (!status_.running) {
        return GaugeTrendError::invalid_state;
    }
    const auto index = find_trend(trend_id);
    if (index == kNotFound) {
        return GaugeTrendError::trend_not_found;
    }
    const auto configuration = series_[index].configuration;
    series_[index] = {};
    series_[index].configuration = configuration;
    return GaugeTrendError::none;
}

GaugeTrendStatus GaugeTrendBuffer::status() const {
    return status_;
}

std::size_t GaugeTrendBuffer::find_trend(std::uint16_t trend_id) const {
    for (std::size_t index = 0; index < trend_count_; ++index) {
        if (series_[index].configuration.trend_id == trend_id) {
            return index;
        }
    }
    return kNotFound;
}

}  // namespace opengauge::display
