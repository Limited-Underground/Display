#include "opengauge/gps_fix_tracker.hpp"

#include <algorithm>
#include <limits>

namespace opengauge::gps {
namespace {

constexpr std::int32_t kMaximumLatitudeE7 = 900000000;
constexpr std::int32_t kMaximumLongitudeE7 = 1800000000;
constexpr std::int32_t kMinimumAltitudeMm = -1000000;
constexpr std::int32_t kMaximumAltitudeMm = 100000000;
constexpr std::uint32_t kMaximumSpeedMmPerSecond = 200000;
constexpr std::uint32_t kMaximumHorizontalAccuracyMm = 100000000;
constexpr std::int64_t kMinimumUtcUnixMs = 946684800000LL;   // 2000-01-01
constexpr std::int64_t kMaximumUtcUnixMs = 4133980800000LL;  // 2101-01-01

bool known_quality(GpsFixQuality quality) {
    return static_cast<std::uint8_t>(quality) <=
           static_cast<std::uint8_t>(GpsFixQuality::estimated);
}

bool no_spatial_values(const GpsFix& fix) {
    return !fix.position_present && !fix.altitude_present &&
           !fix.speed_present && !fix.heading_present &&
           !fix.horizontal_accuracy_present &&
           fix.latitude_degrees_e7 == 0 &&
           fix.longitude_degrees_e7 == 0 && fix.altitude_mm == 0 &&
           fix.speed_mm_per_second == 0 &&
           fix.heading_millidegrees == 0 &&
           fix.horizontal_accuracy_mm == 0;
}

bool valid_or_zero(bool present, std::int64_t value) {
    return present || value == 0;
}

void saturating_increment(std::uint32_t& value, std::uint32_t amount = 1) {
    const auto remaining = std::numeric_limits<std::uint32_t>::max() - value;
    value += std::min(amount, remaining);
}

void clear_spatial_values(GpsFix& fix) {
    fix.position_present = false;
    fix.altitude_present = false;
    fix.speed_present = false;
    fix.heading_present = false;
    fix.horizontal_accuracy_present = false;
    fix.utc_present = false;
    fix.latitude_degrees_e7 = 0;
    fix.longitude_degrees_e7 = 0;
    fix.altitude_mm = 0;
    fix.speed_mm_per_second = 0;
    fix.heading_millidegrees = 0;
    fix.horizontal_accuracy_mm = 0;
    fix.utc_unix_ms = 0;
}

}  // namespace

GpsTrackerError validate_gps_fix(const GpsFix& fix) {
    if (!known_quality(fix.quality) || fix.satellites_used > 64) {
        return GpsTrackerError::invalid_sample;
    }
    if (fix.utc_present) {
        if (fix.utc_unix_ms < kMinimumUtcUnixMs ||
            fix.utc_unix_ms >= kMaximumUtcUnixMs) {
            return GpsTrackerError::invalid_sample;
        }
    } else if (fix.utc_unix_ms != 0) {
        return GpsTrackerError::invalid_sample;
    }

    if (fix.quality == GpsFixQuality::no_fix) {
        return no_spatial_values(fix)
                   ? GpsTrackerError::none
                   : GpsTrackerError::invalid_sample;
    }
    if (!fix.position_present || fix.satellites_used == 0 ||
        fix.latitude_degrees_e7 < -kMaximumLatitudeE7 ||
        fix.latitude_degrees_e7 > kMaximumLatitudeE7 ||
        fix.longitude_degrees_e7 < -kMaximumLongitudeE7 ||
        fix.longitude_degrees_e7 > kMaximumLongitudeE7) {
        return GpsTrackerError::invalid_sample;
    }
    if (fix.quality != GpsFixQuality::fix_2d && !fix.altitude_present) {
        return GpsTrackerError::invalid_sample;
    }
    if (!valid_or_zero(fix.altitude_present, fix.altitude_mm) ||
        (fix.altitude_present &&
         (fix.altitude_mm < kMinimumAltitudeMm ||
          fix.altitude_mm > kMaximumAltitudeMm)) ||
        !valid_or_zero(fix.speed_present, fix.speed_mm_per_second) ||
        (fix.speed_present &&
         fix.speed_mm_per_second > kMaximumSpeedMmPerSecond) ||
        !valid_or_zero(fix.heading_present, fix.heading_millidegrees) ||
        (fix.heading_present && fix.heading_millidegrees >= 360000) ||
        !valid_or_zero(fix.horizontal_accuracy_present,
                       fix.horizontal_accuracy_mm) ||
        (fix.horizontal_accuracy_present &&
         (fix.horizontal_accuracy_mm == 0 ||
          fix.horizontal_accuracy_mm > kMaximumHorizontalAccuracyMm))) {
        return GpsTrackerError::invalid_sample;
    }
    return GpsTrackerError::none;
}

GpsTrackerError validate_gps_fix_sample(
    const GpsFixSample& sample,
    std::uint64_t maximum_source_age_ms) {
    if (sample.boot_session_id == 0 ||
        sample.source_age_ms > maximum_source_age_ms) {
        return GpsTrackerError::invalid_sample;
    }
    return validate_gps_fix(sample.fix);
}

GpsTrackerError GpsFixTracker::start(
    const GpsTrackerConfiguration& configuration) {
    if (status_.running) {
        return GpsTrackerError::invalid_state;
    }
    if (configuration.stale_after_ms == 0 ||
        configuration.maximum_source_age_ms < configuration.stale_after_ms) {
        return GpsTrackerError::invalid_configuration;
    }
    configuration_ = configuration;
    latest_ = {};
    received_at_ms_ = 0;
    status_ = {};
    status_.running = true;
    return GpsTrackerError::none;
}

void GpsFixTracker::stop() {
    status_.running = false;
}

GpsTrackerError GpsFixTracker::clear() {
    if (!status_.running) {
        return GpsTrackerError::invalid_state;
    }
    latest_ = {};
    received_at_ms_ = 0;
    const auto running = status_.running;
    status_ = {};
    status_.running = running;
    return GpsTrackerError::none;
}

GpsAcceptResult GpsFixTracker::accept(
    const GpsFixSample& sample,
    std::uint64_t received_at_ms) {
    if (!status_.running) {
        return {GpsTrackerError::invalid_state};
    }
    if (validate_gps_fix_sample(
            sample, configuration_.maximum_source_age_ms) !=
        GpsTrackerError::none) {
        saturating_increment(status_.invalid_samples);
        return {GpsTrackerError::invalid_sample};
    }
    if (status_.has_sample && received_at_ms < received_at_ms_) {
        return {GpsTrackerError::clock_regression};
    }

    GpsAcceptResult result{GpsTrackerError::none, false, 0};
    if (status_.has_sample &&
        sample.boot_session_id == latest_.boot_session_id) {
        const auto distance = sample.sequence - latest_.sequence;
        if (distance == 0) {
            saturating_increment(status_.duplicates_rejected);
            return {GpsTrackerError::duplicate};
        }
        if (distance >= 0x80000000U) {
            saturating_increment(status_.out_of_order_rejected);
            return {GpsTrackerError::out_of_order};
        }
        result.packets_missing = distance - 1U;
        saturating_increment(
            status_.packets_missing, result.packets_missing);
    } else if (status_.has_sample) {
        result.session_changed = true;
        saturating_increment(status_.session_changes);
    }

    latest_ = sample;
    received_at_ms_ = received_at_ms;
    status_.has_sample = true;
    status_.current_session_id = sample.boot_session_id;
    status_.current_sequence = sample.sequence;
    saturating_increment(status_.samples_accepted);
    return result;
}

GpsFixSnapshot GpsFixTracker::read(std::uint64_t now_ms) const {
    if (!status_.running) {
        return {GpsTrackerError::invalid_state};
    }
    if (!status_.has_sample) {
        return {GpsTrackerError::not_found};
    }
    if (now_ms < received_at_ms_) {
        return {GpsTrackerError::clock_regression};
    }
    const auto local_age = now_ms - received_at_ms_;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto age = local_age > maximum - latest_.source_age_ms
                         ? maximum
                         : latest_.source_age_ms + local_age;
    GpsFixSnapshot snapshot{
        GpsTrackerError::none,
        latest_.fix.quality == GpsFixQuality::no_fix
            ? GpsFixState::no_fix
            : GpsFixState::fresh,
        latest_,
        age};
    if (age >= configuration_.stale_after_ms) {
        snapshot.state = GpsFixState::stale;
        clear_spatial_values(snapshot.sample.fix);
    }
    return snapshot;
}

GpsTrackerStatus GpsFixTracker::status() const {
    return status_;
}

}  // namespace opengauge::gps
