#pragma once

#include <cstdint>

#include "opengauge/gauge_dashboard_loop.hpp"

namespace opengauge::display {

enum class GaugeRendererError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_frame,
    busy,
    clock_regression,
    backend_failure,
};

struct GaugeRendererOfferResult {
    GaugeRendererError error{GaugeRendererError::invalid_state};

    [[nodiscard]] constexpr bool accepted() const {
        return error == GaugeRendererError::none;
    }
};

struct GaugeRendererServiceResult {
    GaugeRendererError error{GaugeRendererError::invalid_state};
    bool frame_presented{false};

    [[nodiscard]] constexpr bool serviced() const {
        return error == GaugeRendererError::none;
    }
};

// Compares renderer-visible content field by field. Publication sequence and
// publication time are diagnostic metadata, not presentation state. Unused
// widget capacity is likewise outside the visible frame.
[[nodiscard]] bool gauge_dashboard_frames_semantically_equal(
    const GaugeDashboardFrame& left,
    const GaugeDashboardFrame& right);

// Nonblocking renderer boundary. start/stop own only renderer lifecycle.
// offer() must synchronously copy the complete frame before returning accepted;
// every other result means that it consumed nothing. service() advances at
// most the renderer's already accepted work and never calls back into the
// dashboard. A failed service must retain the exact accepted in-flight frame
// for retry and preserve the prior complete presented/front frame. A failed
// start may leave partial renderer state; stop() must be idempotent and restore
// a stopped state. The caller serializes all calls.
class GaugeRenderer {
public:
    virtual ~GaugeRenderer() = default;

    [[nodiscard]] virtual bool is_running() const = 0;
    virtual GaugeRendererError start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual GaugeRendererOfferResult offer(
        const GaugeDashboardFrame& frame) = 0;
    [[nodiscard]] virtual GaugeRendererServiceResult service(
        std::uint64_t now_ms) = 0;
};

}  // namespace opengauge::display
