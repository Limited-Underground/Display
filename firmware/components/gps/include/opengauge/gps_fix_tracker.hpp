#pragma once

#include <cstdint>

namespace opengauge::gps {

enum class GpsFixQuality : std::uint8_t {
    no_fix = 0,
    fix_2d = 1,
    fix_3d = 2,
    differential = 3,
    rtk_float = 4,
    rtk_fixed = 5,
    estimated = 6,
};

enum class GpsFixState : std::uint8_t {
    fresh = 1,
    no_fix = 2,
    stale = 3,
};

enum class GpsTrackerError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    invalid_sample,
    duplicate,
    out_of_order,
    not_found,
    clock_regression,
};

struct GpsFix {
    GpsFixQuality quality{GpsFixQuality::no_fix};
    std::uint8_t satellites_used{0};
    bool position_present{false};
    bool altitude_present{false};
    bool speed_present{false};
    bool heading_present{false};
    bool horizontal_accuracy_present{false};
    bool utc_present{false};
    std::int32_t latitude_degrees_e7{0};
    std::int32_t longitude_degrees_e7{0};
    std::int32_t altitude_mm{0};
    std::uint32_t speed_mm_per_second{0};
    std::uint32_t heading_millidegrees{0};
    std::uint32_t horizontal_accuracy_mm{0};
    std::int64_t utc_unix_ms{0};
};

struct GpsFixSample {
    std::uint32_t boot_session_id{0};
    std::uint32_t sequence{0};
    std::uint64_t source_age_ms{0};
    GpsFix fix{};
};

struct GpsTrackerConfiguration {
    std::uint64_t stale_after_ms{0};
    std::uint64_t maximum_source_age_ms{0};
};

struct GpsAcceptResult {
    GpsTrackerError error{GpsTrackerError::invalid_state};
    bool session_changed{false};
    std::uint32_t packets_missing{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == GpsTrackerError::none;
    }
};

struct GpsFixSnapshot {
    GpsTrackerError error{GpsTrackerError::invalid_state};
    GpsFixState state{GpsFixState::no_fix};
    GpsFixSample sample{};
    std::uint64_t effective_age_ms{0};

    [[nodiscard]] constexpr bool found() const {
        return error == GpsTrackerError::none;
    }
};

struct GpsTrackerStatus {
    bool running{false};
    bool has_sample{false};
    std::uint32_t current_session_id{0};
    std::uint32_t current_sequence{0};
    std::uint32_t samples_accepted{0};
    std::uint32_t duplicates_rejected{0};
    std::uint32_t out_of_order_rejected{0};
    std::uint32_t packets_missing{0};
    std::uint32_t session_changes{0};
    std::uint32_t invalid_samples{0};
};

[[nodiscard]] GpsTrackerError validate_gps_fix(const GpsFix& fix);
[[nodiscard]] GpsTrackerError validate_gps_fix_sample(
    const GpsFixSample& sample,
    std::uint64_t maximum_source_age_ms);

// Transport-neutral latest-fix tracker. Source age is combined only with
// receiver-local elapsed monotonic time; unsynchronized clocks are not compared.
class GpsFixTracker {
public:
    [[nodiscard]] GpsTrackerError start(
        const GpsTrackerConfiguration& configuration);
    void stop();
    [[nodiscard]] GpsTrackerError clear();

    [[nodiscard]] GpsAcceptResult accept(
        const GpsFixSample& sample,
        std::uint64_t received_at_ms);
    [[nodiscard]] GpsFixSnapshot read(std::uint64_t now_ms) const;
    [[nodiscard]] GpsTrackerStatus status() const;

private:
    GpsTrackerConfiguration configuration_{};
    GpsFixSample latest_{};
    std::uint64_t received_at_ms_{0};
    GpsTrackerStatus status_{};
};

}  // namespace opengauge::gps
