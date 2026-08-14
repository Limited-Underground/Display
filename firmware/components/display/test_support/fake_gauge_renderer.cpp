#include "fake_gauge_renderer.hpp"

#include <limits>

namespace opengauge::display::test_support {
namespace {

void increment_saturated(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

bool valid_frame(const GaugeDashboardFrame& frame) {
    if (frame.publication_sequence == 0 || frame.widget_count < 1 ||
        frame.widget_count > frame.widgets.size()) {
        return false;
    }
    for (std::size_t index = 0; index < frame.widget_count; ++index) {
        if (frame.widgets[index].label.length >
            kMaximumGaugeWidgetLabelBytes) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool FakeGaugeRenderer::is_running() const {
    return status_.running;
}

GaugeRendererError FakeGaugeRenderer::start() {
    increment_saturated(status_.start_calls);
    if (status_.running) {
        return GaugeRendererError::invalid_state;
    }
    if (next_start_error_ != GaugeRendererError::none) {
        const auto error = next_start_error_;
        next_start_error_ = GaugeRendererError::none;
        status_.running = next_start_leaves_partial_state_;
        next_start_leaves_partial_state_ = false;
        return error;
    }
    queued_frame_ = {};
    presented_frame_ = {};
    status_.running = true;
    status_.has_queued_frame = false;
    status_.has_presented_frame = false;
    has_service_time_ = false;
    last_service_time_ms_ = 0;
    return GaugeRendererError::none;
}

void FakeGaugeRenderer::stop() {
    increment_saturated(status_.stop_calls);
    status_.running = false;
    status_.has_queued_frame = false;
    status_.has_presented_frame = false;
    queued_frame_ = {};
    presented_frame_ = {};
    has_service_time_ = false;
    last_service_time_ms_ = 0;
}

GaugeRendererOfferResult FakeGaugeRenderer::offer(
    const GaugeDashboardFrame& frame) {
    increment_saturated(status_.offer_calls);
    if (!status_.running) {
        return {GaugeRendererError::invalid_state};
    }
    if (!valid_frame(frame)) {
        return {GaugeRendererError::invalid_frame};
    }
    if (next_offer_error_ != GaugeRendererError::none) {
        const auto error = next_offer_error_;
        next_offer_error_ = GaugeRendererError::none;
        return {error};
    }
    if (busy_ || status_.has_queued_frame) {
        return {GaugeRendererError::busy};
    }
    queued_frame_ = frame;
    status_.has_queued_frame = true;
    increment_saturated(status_.frames_accepted);
    return {GaugeRendererError::none};
}

GaugeRendererServiceResult FakeGaugeRenderer::service(
    std::uint64_t now_ms) {
    increment_saturated(status_.service_calls);
    if (!status_.running) {
        return {GaugeRendererError::invalid_state, false};
    }
    if (has_service_time_ && now_ms < last_service_time_ms_) {
        return {GaugeRendererError::clock_regression, false};
    }
    if (next_service_error_ != GaugeRendererError::none) {
        const auto error = next_service_error_;
        next_service_error_ = GaugeRendererError::none;
        return {error, false};
    }
    has_service_time_ = true;
    last_service_time_ms_ = now_ms;
    if (hold_presentations_ || !status_.has_queued_frame) {
        return {GaugeRendererError::none, false};
    }
    presented_frame_ = queued_frame_;
    queued_frame_ = {};
    status_.has_queued_frame = false;
    status_.has_presented_frame = true;
    increment_saturated(status_.frames_presented);
    return {GaugeRendererError::none, true};
}

void FakeGaugeRenderer::set_busy(bool busy) {
    busy_ = busy;
}

void FakeGaugeRenderer::set_hold_presentations(bool hold) {
    hold_presentations_ = hold;
}

void FakeGaugeRenderer::fail_next_start(
    GaugeRendererError error,
    bool leaves_partial_state) {
    next_start_error_ = error;
    next_start_leaves_partial_state_ = leaves_partial_state;
}

void FakeGaugeRenderer::fail_next_offer(GaugeRendererError error) {
    next_offer_error_ = error;
}

void FakeGaugeRenderer::fail_next_service(GaugeRendererError error) {
    next_service_error_ = error;
}

bool FakeGaugeRenderer::copy_queued_frame(
    GaugeDashboardFrame& output) const {
    if (!status_.has_queued_frame) {
        return false;
    }
    output = queued_frame_;
    return true;
}

bool FakeGaugeRenderer::copy_presented_frame(
    GaugeDashboardFrame& output) const {
    if (!status_.has_presented_frame) {
        return false;
    }
    output = presented_frame_;
    return true;
}

FakeGaugeRendererStatus FakeGaugeRenderer::status() const {
    return status_;
}

}  // namespace opengauge::display::test_support
