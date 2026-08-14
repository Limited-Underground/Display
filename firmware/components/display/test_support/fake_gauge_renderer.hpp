#pragma once

#include <cstdint>

#include "opengauge/gauge_renderer.hpp"

namespace opengauge::display::test_support {

struct FakeGaugeRendererStatus {
    bool running{false};
    bool has_queued_frame{false};
    bool has_presented_frame{false};
    std::uint32_t start_calls{0};
    std::uint32_t stop_calls{0};
    std::uint32_t offer_calls{0};
    std::uint32_t service_calls{0};
    std::uint32_t frames_accepted{0};
    std::uint32_t frames_presented{0};
};

class FakeGaugeRenderer final : public GaugeRenderer {
public:
    [[nodiscard]] bool is_running() const override;
    GaugeRendererError start() override;
    void stop() override;
    [[nodiscard]] GaugeRendererOfferResult offer(
        const GaugeDashboardFrame& frame) override;
    [[nodiscard]] GaugeRendererServiceResult service(
        std::uint64_t now_ms) override;

    void set_busy(bool busy);
    void set_hold_presentations(bool hold);
    void fail_next_start(
        GaugeRendererError error = GaugeRendererError::backend_failure,
        bool leaves_partial_state = false);
    void fail_next_offer(
        GaugeRendererError error = GaugeRendererError::backend_failure);
    void fail_next_service(
        GaugeRendererError error = GaugeRendererError::backend_failure);

    [[nodiscard]] bool copy_queued_frame(
        GaugeDashboardFrame& output) const;
    [[nodiscard]] bool copy_presented_frame(
        GaugeDashboardFrame& output) const;
    [[nodiscard]] FakeGaugeRendererStatus status() const;

private:
    GaugeDashboardFrame queued_frame_{};
    GaugeDashboardFrame presented_frame_{};
    FakeGaugeRendererStatus status_{};
    GaugeRendererError next_start_error_{GaugeRendererError::none};
    GaugeRendererError next_offer_error_{GaugeRendererError::none};
    GaugeRendererError next_service_error_{GaugeRendererError::none};
    std::uint64_t last_service_time_ms_{0};
    bool has_service_time_{false};
    bool busy_{false};
    bool hold_presentations_{false};
    bool next_start_leaves_partial_state_{false};
};

}  // namespace opengauge::display::test_support
