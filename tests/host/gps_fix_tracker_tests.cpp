#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opengauge/gps_fix_tracker.hpp"

namespace {

using namespace opengauge::gps;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

GpsFix fix_3d() {
    GpsFix fix{};
    fix.quality = GpsFixQuality::fix_3d;
    fix.satellites_used = 8;
    fix.position_present = true;
    fix.altitude_present = true;
    fix.speed_present = true;
    fix.heading_present = true;
    fix.horizontal_accuracy_present = true;
    fix.utc_present = true;
    fix.latitude_degrees_e7 = 361234567;
    fix.longitude_degrees_e7 = -865432100;
    fix.altitude_mm = 250000;
    fix.speed_mm_per_second = 12345;
    fix.heading_millidegrees = 359999;
    fix.horizontal_accuracy_mm = 1500;
    fix.utc_unix_ms = 1786250000000LL;
    return fix;
}

GpsFixSample sample(
    std::uint32_t session,
    std::uint32_t sequence,
    std::uint64_t source_age = 0) {
    return {session, sequence, source_age, fix_3d()};
}

void test_quality_presence_and_coordinate_validation() {
    GpsFix no_fix{};
    EXPECT(validate_gps_fix(no_fix) == GpsTrackerError::none);
    no_fix.position_present = true;
    EXPECT(validate_gps_fix(no_fix) == GpsTrackerError::invalid_sample);

    auto fix = fix_3d();
    fix.latitude_degrees_e7 = 900000000;
    fix.longitude_degrees_e7 = -1800000000;
    EXPECT(validate_gps_fix(fix) == GpsTrackerError::none);
    fix.latitude_degrees_e7 = 900000001;
    EXPECT(validate_gps_fix(fix) == GpsTrackerError::invalid_sample);
    fix = fix_3d();
    fix.quality = GpsFixQuality::fix_2d;
    fix.altitude_present = false;
    fix.altitude_mm = 0;
    EXPECT(validate_gps_fix(fix) == GpsTrackerError::none);
    fix.quality = GpsFixQuality::fix_3d;
    EXPECT(validate_gps_fix(fix) == GpsTrackerError::invalid_sample);
}

void test_measurement_and_utc_boundaries() {
    auto fix = fix_3d();
    fix.speed_mm_per_second = 200000;
    fix.heading_millidegrees = 0;
    fix.horizontal_accuracy_mm = 100000000;
    fix.utc_unix_ms = 946684800000LL;
    EXPECT(validate_gps_fix(fix) == GpsTrackerError::none);
    fix.heading_millidegrees = 360000;
    EXPECT(validate_gps_fix(fix) == GpsTrackerError::invalid_sample);
    fix = fix_3d();
    fix.horizontal_accuracy_mm = 0;
    EXPECT(validate_gps_fix(fix) == GpsTrackerError::invalid_sample);
    fix = fix_3d();
    fix.utc_present = false;
    EXPECT(validate_gps_fix(fix) == GpsTrackerError::invalid_sample);
}

void test_lifecycle_configuration_and_clear() {
    GpsFixTracker tracker{};
    EXPECT(tracker.accept(sample(1, 0), 0).error ==
           GpsTrackerError::invalid_state);
    EXPECT(tracker.start({0, 1000}) ==
           GpsTrackerError::invalid_configuration);
    EXPECT(tracker.start({1000, 999}) ==
           GpsTrackerError::invalid_configuration);
    EXPECT(tracker.start({1000, 5000}) == GpsTrackerError::none);
    EXPECT(tracker.start({1000, 5000}) == GpsTrackerError::invalid_state);
    EXPECT(tracker.read(0).error == GpsTrackerError::not_found);
    EXPECT(tracker.accept(sample(1, 0), 10).accepted());
    EXPECT(tracker.clear() == GpsTrackerError::none);
    EXPECT(!tracker.status().has_sample);
    EXPECT(tracker.status().samples_accepted == 0);
    tracker.stop();
    EXPECT(tracker.clear() == GpsTrackerError::invalid_state);
}

void test_sequence_gap_duplicate_out_of_order_and_wrap() {
    GpsFixTracker tracker{};
    EXPECT(tracker.start({1000, 5000}) == GpsTrackerError::none);
    EXPECT(tracker.accept(sample(1, 10), 10).accepted());
    const auto gap = tracker.accept(sample(1, 13), 20);
    EXPECT(gap.accepted());
    EXPECT(gap.packets_missing == 2);
    EXPECT(tracker.accept(sample(1, 13), 21).error ==
           GpsTrackerError::duplicate);
    EXPECT(tracker.accept(sample(1, 12), 22).error ==
           GpsTrackerError::out_of_order);
    EXPECT(tracker.status().packets_missing == 2);
    EXPECT(tracker.status().duplicates_rejected == 1);
    EXPECT(tracker.status().out_of_order_rejected == 1);

    GpsFixTracker wrapping{};
    EXPECT(wrapping.start({1000, 5000}) == GpsTrackerError::none);
    EXPECT(wrapping.accept(
               sample(2, std::numeric_limits<std::uint32_t>::max()), 1)
               .accepted());
    EXPECT(wrapping.accept(sample(2, 0), 2).accepted());
}

void test_session_restart_and_receive_clock_regression() {
    GpsFixTracker tracker{};
    EXPECT(tracker.start({1000, 5000}) == GpsTrackerError::none);
    EXPECT(tracker.accept(sample(7, 100), 100).accepted());
    EXPECT(tracker.accept(sample(7, 101), 99).error ==
           GpsTrackerError::clock_regression);
    const auto restarted = tracker.accept(sample(8, 0), 101);
    EXPECT(restarted.accepted());
    EXPECT(restarted.session_changed);
    EXPECT(tracker.status().session_changes == 1);
    EXPECT(tracker.status().current_session_id == 8);
    EXPECT(tracker.status().current_sequence == 0);
}

void test_exact_stale_boundary_combines_source_and_local_age() {
    GpsFixTracker tracker{};
    EXPECT(tracker.start({1000, 5000}) == GpsTrackerError::none);
    EXPECT(tracker.accept(sample(1, 0, 400), 100).accepted());
    auto snapshot = tracker.read(699);
    EXPECT(snapshot.found());
    EXPECT(snapshot.state == GpsFixState::fresh);
    EXPECT(snapshot.effective_age_ms == 999);
    EXPECT(snapshot.sample.fix.position_present);
    snapshot = tracker.read(700);
    EXPECT(snapshot.state == GpsFixState::stale);
    EXPECT(snapshot.effective_age_ms == 1000);
    EXPECT(!snapshot.sample.fix.position_present);
    EXPECT(!snapshot.sample.fix.utc_present);
    EXPECT(snapshot.sample.fix.latitude_degrees_e7 == 0);
    EXPECT(tracker.read(99).error == GpsTrackerError::clock_regression);
}

void test_no_fix_is_explicit_and_never_invents_position() {
    GpsFixTracker tracker{};
    EXPECT(tracker.start({1000, 5000}) == GpsTrackerError::none);
    auto no_fix = sample(1, 0);
    no_fix.fix = {};
    no_fix.fix.utc_present = true;
    no_fix.fix.utc_unix_ms = 1786250000000LL;
    EXPECT(tracker.accept(no_fix, 50).accepted());
    const auto snapshot = tracker.read(50);
    EXPECT(snapshot.state == GpsFixState::no_fix);
    EXPECT(!snapshot.sample.fix.position_present);
    EXPECT(snapshot.sample.fix.utc_present);
}

void test_source_age_limit_invalid_counts_and_age_saturation() {
    GpsFixTracker tracker{};
    EXPECT(tracker.start({1000, 2000}) == GpsTrackerError::none);
    EXPECT(tracker.accept(sample(1, 0, 2001), 0).error ==
           GpsTrackerError::invalid_sample);
    auto invalid = sample(0, 0);
    EXPECT(tracker.accept(invalid, 0).error ==
           GpsTrackerError::invalid_sample);
    EXPECT(tracker.status().invalid_samples == 2);

    GpsFixTracker saturation{};
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    EXPECT(saturation.start({1000, maximum}) == GpsTrackerError::none);
    EXPECT(saturation.accept(sample(1, 0, maximum - 5), 1).accepted());
    const auto snapshot = saturation.read(10);
    EXPECT(snapshot.effective_age_ms == maximum);
    EXPECT(snapshot.state == GpsFixState::stale);
}

}  // namespace

int main() {
    test_quality_presence_and_coordinate_validation();
    test_measurement_and_utc_boundaries();
    test_lifecycle_configuration_and_clear();
    test_sequence_gap_duplicate_out_of_order_and_wrap();
    test_session_restart_and_receive_clock_regression();
    test_exact_stale_boundary_combines_source_and_local_age();
    test_no_fix_is_explicit_and_never_invents_position();
    test_source_age_limit_invalid_counts_and_age_saturation();

    if (failures != 0) {
        std::cerr << failures << " GPS fix tracker assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 GPS fix tracker scenario groups\n";
    return EXIT_SUCCESS;
}
