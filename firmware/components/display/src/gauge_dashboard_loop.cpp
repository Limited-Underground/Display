#include "opengauge/gauge_dashboard_loop.hpp"

#include <limits>

namespace opengauge::display {
namespace {

bool valid_receiver_configuration(
    const wireless::GaugeReceiverConfiguration& configuration) {
    return wireless::is_valid_unicast_address(
               configuration.expected_gateway_address) &&
           configuration.expected_gateway_id != 0 &&
           configuration.expected_channel >= 1 &&
           configuration.expected_channel <= 14 &&
           configuration.maximum_packets_per_cycle >= 1 &&
           configuration.maximum_packets_per_cycle <=
               wireless::kMaximumGaugePacketsPerCycle;
}

bool usable_layout_selection(
    const configuration::GaugeLayoutLoadResult& load) {
    if (load.error != configuration::GaugeLayoutStoreError::none ||
        !load.has_usable_layout()) {
        return false;
    }
    if (load.source != configuration::GaugeLayoutSource::safe_default) {
        return true;
    }
    // A safe default is authoritative only for known-empty storage. Corrupt
    // media is not silently converted into a plausible dashboard.
    return load.slot_a == configuration::LayoutSlotState::empty &&
           load.slot_b == configuration::LayoutSlotState::empty;
}

void increment_saturated(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

void increment_saturated(std::uint64_t& value) {
    if (value != std::numeric_limits<std::uint64_t>::max()) {
        ++value;
    }
}

}  // namespace

GaugeDashboardLoop::GaugeDashboardLoop(
    configuration::GaugeLayoutStore& layout_store,
    wireless::GaugeTelemetryReceiver& receiver,
    GaugeViewModel& view_model)
    : layout_store_(layout_store),
      receiver_(receiver),
      view_model_(view_model) {}

GaugeDashboardLoopStartResult GaugeDashboardLoop::start(
    const GaugeDashboardLoopConfiguration& loop_configuration) {
    GaugeDashboardLoopStartResult result{};
    if (status_.running || receiver_.status().running ||
        view_model_.status().running) {
        result.error = GaugeDashboardLoopError::invalid_state;
        return result;
    }
    if (!view_model_.bound_to(receiver_)) {
        result.error = GaugeDashboardLoopError::receiver_view_mismatch;
        return result;
    }
    if (configuration::validate_gauge_layout(
            loop_configuration.safe_default) !=
            configuration::GaugeLayoutCodecError::none ||
        !valid_receiver_configuration(loop_configuration.receiver)) {
        return result;
    }

    configuration::GaugeLayout selected{};
    result.layout_load = layout_store_.load(
        loop_configuration.safe_default, selected);
    if (result.layout_load.error !=
        configuration::GaugeLayoutStoreError::none) {
        result.error = GaugeDashboardLoopError::layout_load_failure;
        return result;
    }
    if (!usable_layout_selection(result.layout_load)) {
        result.error = GaugeDashboardLoopError::no_usable_layout;
        return result;
    }

    if (view_model_.clear_widgets() != GaugeViewModelError::none) {
        result.error = GaugeDashboardLoopError::view_configuration_failure;
        result.view_error = GaugeViewModelError::invalid_state;
        return result;
    }
    for (std::size_t index = 0; index < selected.widget_count; ++index) {
        result.view_error = view_model_.add_widget(selected.widgets[index]);
        if (result.view_error != GaugeViewModelError::none) {
            const auto ignored = view_model_.clear_widgets();
            static_cast<void>(ignored);
            result.error =
                GaugeDashboardLoopError::view_configuration_failure;
            return result;
        }
    }

    result.receiver_error = receiver_.start(loop_configuration.receiver);
    if (result.receiver_error != wireless::GaugeReceiverError::none) {
        const auto ignored = view_model_.clear_widgets();
        static_cast<void>(ignored);
        result.error = GaugeDashboardLoopError::receiver_start_failure;
        return result;
    }
    result.view_error = view_model_.start();
    if (result.view_error != GaugeViewModelError::none) {
        receiver_.stop();
        view_model_.stop();
        const auto ignored = view_model_.clear_widgets();
        static_cast<void>(ignored);
        result.error = GaugeDashboardLoopError::view_start_failure;
        return result;
    }

    active_layout_ = selected;
    frame_ = {};
    status_ = {};
    status_.running = true;
    status_.layout_load = result.layout_load;
    status_.receiver = receiver_.status();
    status_.view = view_model_.status();
    result.error = GaugeDashboardLoopError::none;
    return result;
}

void GaugeDashboardLoop::stop() {
    if (!status_.running) {
        return;
    }
    view_model_.stop();
    receiver_.stop();
    const auto ignored = view_model_.clear_widgets();
    static_cast<void>(ignored);
    active_layout_ = {};
    frame_ = {};
    status_ = {};
}

GaugeDashboardLoopCycleResult GaugeDashboardLoop::service(
    std::uint64_t now_ms) {
    GaugeDashboardLoopCycleResult result{};
    if (!status_.running) {
        result.error = GaugeDashboardLoopError::invalid_state;
        return result;
    }
    if (status_.has_service_time && now_ms < status_.last_service_time_ms) {
        result.error = GaugeDashboardLoopError::clock_regression;
        increment_saturated(status_.clock_regressions);
        status_.last_error = result.error;
        return result;
    }
    status_.has_service_time = true;
    status_.last_service_time_ms = now_ms;

    result.receiver = receiver_.service(now_ms);
    increment_saturated(status_.cycles_serviced);
    if (!result.receiver.serviced()) {
        increment_saturated(status_.receiver_cycle_failures);
    }
    GaugeDashboardFrame candidate{};
    result.refresh = view_model_.refresh(
        now_ms, candidate.widgets.data(), candidate.widgets.size());
    if (!result.refresh.refreshed()) {
        increment_saturated(status_.view_refresh_failures);
        result.error = GaugeDashboardLoopError::view_refresh_failure;
        status_.last_error = result.error;
        status_.receiver = receiver_.status();
        status_.view = view_model_.status();
        return result;
    }

    candidate.publication_sequence =
        status_.frames_published ==
                std::numeric_limits<std::uint64_t>::max()
            ? status_.frames_published
            : status_.frames_published + 1;
    candidate.published_at_ms = now_ms;
    candidate.layout_generation = active_layout_.generation;
    candidate.layout_id = active_layout_.layout_id;
    candidate.layout_source = status_.layout_load.source;
    candidate.theme = active_layout_.theme;
    candidate.brightness_percent = active_layout_.brightness_percent;
    candidate.widget_count = active_layout_.widget_count;
    candidate.recovery_required = status_.layout_load.recovery_required;

    frame_ = candidate;
    increment_saturated(status_.frames_published);
    status_.has_frame = true;
    status_.receiver = receiver_.status();
    status_.view = view_model_.status();
    result.frame_published = true;
    result.publication_sequence = candidate.publication_sequence;
    if (!result.receiver.serviced()) {
        result.error = GaugeDashboardLoopError::receiver_service_failure;
    }
    status_.last_error = result.error;
    return result;
}

bool GaugeDashboardLoop::copy_frame(GaugeDashboardFrame& output) const {
    if (!status_.has_frame) {
        return false;
    }
    output = frame_;
    return true;
}

GaugeDashboardLoopStatus GaugeDashboardLoop::status() const {
    auto result = status_;
    result.receiver = receiver_.status();
    result.view = view_model_.status();
    return result;
}

}  // namespace opengauge::display
