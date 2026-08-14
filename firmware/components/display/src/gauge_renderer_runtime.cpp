#include "opengauge/gauge_renderer_runtime.hpp"

#include <limits>

namespace opengauge::display {
namespace {

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

void record_error(
    GaugeRendererRuntimeCycleResult& result,
    GaugeRendererRuntimeError error) {
    if (result.error == GaugeRendererRuntimeError::none) {
        result.error = error;
    }
}

}  // namespace

GaugeRendererRuntime::GaugeRendererRuntime(
    GaugeDashboardLoop& dashboard,
    GaugeRenderer& renderer)
    : dashboard_(dashboard), renderer_(renderer) {}

GaugeRendererRuntimeStartResult GaugeRendererRuntime::start(
    const GaugeDashboardLoopConfiguration& configuration) {
    GaugeRendererRuntimeStartResult result{};
    if (status_.running || dashboard_.status().running ||
        renderer_.is_running()) {
        return result;
    }

    result.dashboard = dashboard_.start(configuration);
    if (!result.dashboard.started()) {
        result.error = GaugeRendererRuntimeError::dashboard_start_failure;
        return result;
    }

    result.renderer_error = renderer_.start();
    if (result.renderer_error != GaugeRendererError::none) {
        renderer_.stop();
        dashboard_.stop();
        result.error = GaugeRendererRuntimeError::renderer_start_failure;
        return result;
    }

    pending_frame_ = {};
    accepted_frame_ = {};
    status_ = {};
    status_.running = true;
    result.error = GaugeRendererRuntimeError::none;
    return result;
}

void GaugeRendererRuntime::stop() {
    if (status_.running) {
        renderer_.stop();
        dashboard_.stop();
    }
    pending_frame_ = {};
    accepted_frame_ = {};
    status_ = {};
}

GaugeRendererRuntimeCycleResult GaugeRendererRuntime::service(
    std::uint64_t now_ms) {
    GaugeRendererRuntimeCycleResult result{};
    if (!status_.running) {
        result.error = GaugeRendererRuntimeError::invalid_state;
        return result;
    }
    if (status_.has_service_time && now_ms < status_.last_service_time_ms) {
        result.error = GaugeRendererRuntimeError::clock_regression;
        return result;
    }

    status_.has_service_time = true;
    status_.last_service_time_ms = now_ms;
    increment_saturated(status_.cycles_serviced);

    result.dashboard = dashboard_.service(now_ms);
    result.dashboard_serviced = true;
    if (result.dashboard.error != GaugeDashboardLoopError::none) {
        increment_saturated(status_.dashboard_cycle_failures);
        record_error(
            result, GaugeRendererRuntimeError::dashboard_service_failure);
    }

    if (result.dashboard.frame_published) {
        GaugeDashboardFrame observed{};
        if (!dashboard_.copy_frame(observed)) {
            record_error(
                result,
                GaugeRendererRuntimeError::dashboard_frame_unavailable);
        } else {
            result.frame_observed = true;
            increment_saturated(status_.frames_observed);
            observe_frame(observed, result);
        }
    }

    if (status_.has_pending_frame) {
        result.offer_attempted = true;
        increment_saturated(status_.offers_attempted);
        result.renderer_offer = renderer_.offer(pending_frame_);
        if (result.renderer_offer.accepted()) {
            accepted_frame_ = pending_frame_;
            status_.has_last_accepted_frame = true;
            status_.has_pending_frame = false;
            status_.has_renderer_frame_in_flight = true;
            increment_saturated(status_.offers_accepted);
        } else if (result.renderer_offer.error == GaugeRendererError::busy) {
            increment_saturated(status_.renderer_busy_cycles);
            record_error(result, GaugeRendererRuntimeError::renderer_busy);
        } else {
            increment_saturated(status_.renderer_offer_failures);
            record_error(
                result, GaugeRendererRuntimeError::renderer_offer_failure);
        }
    }

    result.renderer_service = renderer_.service(now_ms);
    if (!result.renderer_service.serviced()) {
        increment_saturated(status_.renderer_service_failures);
        record_error(
            result, GaugeRendererRuntimeError::renderer_service_failure);
    }
    if (result.renderer_service.serviced() &&
        result.renderer_service.frame_presented) {
        status_.has_renderer_frame_in_flight = false;
        if (status_.presentation_pending &&
            accepted_frame_.layout_generation ==
                status_.presentation_generation) {
            status_.presentation_pending = false;
            status_.presentation_generation = 0;
        }
    }

    result.pending_after_cycle = status_.has_pending_frame;
    status_.last_error = result.error;
    return result;
}

GaugeRendererRuntimeActivationResult
GaugeRendererRuntime::activate_persisted_layout(
    std::uint64_t expected_generation) {
    GaugeRendererRuntimeActivationResult result{};
    if (!status_.running || !dashboard_.status().running ||
        !renderer_.is_running()) {
        return result;
    }
    if (status_.has_renderer_frame_in_flight) {
        result.error = GaugeRendererRuntimeError::renderer_frame_in_flight;
        return result;
    }

    result.dashboard = dashboard_.activate_persisted_layout(
        expected_generation);
    if (!result.dashboard.activated()) {
        result.error = GaugeRendererRuntimeError::layout_activation_failure;
        return result;
    }

    if ((result.dashboard.layout_changed ||
         result.dashboard.frame_metadata_changed) &&
        status_.has_pending_frame) {
        pending_frame_ = {};
        status_.has_pending_frame = false;
        result.discarded_pending_frame = true;
        increment_saturated(
            status_.pending_frames_discarded_for_activation);
    }
    if (result.dashboard.layout_changed ||
        result.dashboard.frame_metadata_changed) {
        increment_saturated(status_.layout_activations);
        status_.presentation_pending = true;
        status_.presentation_generation = expected_generation;
    }
    result.error = GaugeRendererRuntimeError::none;
    result.presentation_pending = status_.presentation_pending;
    return result;
}

bool GaugeRendererRuntime::layout_activation_ready() const {
    return status_.running && dashboard_.status().running &&
           renderer_.is_running() &&
           !status_.has_renderer_frame_in_flight;
}

bool GaugeRendererRuntime::bound_to(
    const configuration::GaugeLayoutStore& store) const {
    return dashboard_.bound_to(store);
}

GaugeRendererRuntimeStatus GaugeRendererRuntime::status() const {
    return status_;
}

void GaugeRendererRuntime::observe_frame(
    const GaugeDashboardFrame& frame,
    GaugeRendererRuntimeCycleResult& result) {
    if (status_.has_pending_frame) {
        result.frame_semantically_equal =
            gauge_dashboard_frames_semantically_equal(
                frame, pending_frame_);
        pending_frame_ = frame;
        result.pending_replaced = true;
        increment_saturated(status_.frames_coalesced);
        if (result.frame_semantically_equal) {
            increment_saturated(status_.equivalent_frames_observed);
        }
        return;
    }

    if (status_.has_last_accepted_frame) {
        result.frame_semantically_equal =
            gauge_dashboard_frames_semantically_equal(
                frame, accepted_frame_);
        if (result.frame_semantically_equal) {
            increment_saturated(status_.equivalent_frames_observed);
        }
    }

    pending_frame_ = frame;
    status_.has_pending_frame = true;
}

}  // namespace opengauge::display
