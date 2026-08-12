#pragma once

#include "opengauge/gauge_layout.hpp"

namespace opengauge::configuration {

struct GaugeLayoutChangePolicy {
    std::uint64_t confirmation_window_ms{0};
};

enum class GaugeLayoutChangeState : std::uint8_t {
    stopped = 0,
    idle,
    pending,
};

enum class GaugeLayoutChangeError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_policy,
    invalid_request,
    invalid_layout,
    change_pending,
    no_change_pending,
    request_mismatch,
    confirmation_expired,
    clock_regression,
    persistence_failed,
    persistence_uncertain,
};

struct GaugeLayoutChangeResult {
    GaugeLayoutChangeError error{GaugeLayoutChangeError::invalid_state};
    GaugeLayoutUpdateResult persistence{};

    [[nodiscard]] constexpr bool completed() const {
        return error == GaugeLayoutChangeError::none &&
               persistence.succeeded();
    }
};

struct GaugeLayoutChangeStatus {
    GaugeLayoutChangeState state{GaugeLayoutChangeState::stopped};
    std::uint32_t pending_request_id{0};
    std::uint64_t pending_opened_ms{0};
    std::uint32_t staged_count{0};
    std::uint32_t applied_count{0};
    std::uint32_t unchanged_count{0};
    std::uint32_t expired_count{0};
    std::uint32_t cancelled_count{0};
    std::uint32_t failed_count{0};
    std::uint32_t uncertain_count{0};
    std::uint32_t clock_fault_count{0};
};

// Local semantic confirmation only. The target owns physical-presence proof,
// renderer text, input debounce, task serialization, and any remote authority.
class GaugeLayoutChangeCoordinator {
public:
    explicit GaugeLayoutChangeCoordinator(GaugeLayoutStore& store);

    [[nodiscard]] GaugeLayoutChangeError start(
        const GaugeLayoutChangePolicy& policy);
    void stop();

    [[nodiscard]] GaugeLayoutChangeError stage(
        std::uint32_t request_id,
        const GaugeLayout& desired,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeResult confirm(
        std::uint32_t request_id,
        std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeError cancel(
        std::uint32_t request_id);
    [[nodiscard]] GaugeLayoutChangeError service(std::uint64_t now_ms);
    [[nodiscard]] GaugeLayoutChangeStatus status() const;

private:
    [[nodiscard]] bool observe_time(std::uint64_t now_ms);
    [[nodiscard]] bool expire_if_due(std::uint64_t now_ms);
    void clear_pending();

    GaugeLayoutStore& store_;
    GaugeLayoutChangePolicy policy_{};
    GaugeLayoutChangeStatus status_{};
    GaugeLayout pending_{};
    std::uint64_t last_now_ms_{0};
    bool has_time_{false};
};

}  // namespace opengauge::configuration
