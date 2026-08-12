#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "fake_gauge_layout_storage.hpp"
#include "opengauge/gauge_layout_change_coordinator.hpp"

namespace {

using namespace opengauge;
using configuration::test_support::FakeGaugeLayoutStorage;
using configuration::test_support::FakeWriteBehavior;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

display::GaugeWidgetConfiguration widget(
    std::uint16_t id,
    wireless::TelemetrySignalCode code,
    display::GaugeWidgetKind kind,
    std::string_view label) {
    display::GaugeWidgetConfiguration result{};
    result.widget_id = id;
    result.signal_code = code;
    result.kind = kind;
    EXPECT(display::make_gauge_widget_label(label, result.label) ==
           display::GaugeViewModelError::none);
    result.stale_after_ms = 1500;
    if (kind == display::GaugeWidgetKind::needle ||
        kind == display::GaugeWidgetKind::bar) {
        result.scale_min_raw = -100;
        result.scale_max_raw = 5000000;
    }
    return result;
}

configuration::GaugeLayout layout(std::uint8_t brightness = 65) {
    configuration::GaugeLayout result{};
    result.layout_id = 0x10203040U;
    result.brightness_percent = brightness;
    result.theme = configuration::GaugeTheme::high_contrast;
    result.widget_count = 2;
    result.widgets[0] = widget(
        7, wireless::TelemetrySignalCode::engine_speed,
        display::GaugeWidgetKind::needle, "Engine RPM");
    result.widgets[1] = widget(
        8, wireless::TelemetrySignalCode::engine_coolant_temperature,
        display::GaugeWidgetKind::numeric, "Coolant");
    return result;
}

void test_lifecycle_and_policy_are_explicit() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.stage(1, layout(), 0) ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(coordinator.start({0}) ==
           configuration::GaugeLayoutChangeError::invalid_policy);
    EXPECT(coordinator.start({100}) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.start({100}) ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(coordinator.status().state ==
           configuration::GaugeLayoutChangeState::idle);
    coordinator.stop();
    EXPECT(coordinator.status().state ==
           configuration::GaugeLayoutChangeState::stopped);
    EXPECT(coordinator.service(1) ==
           configuration::GaugeLayoutChangeError::invalid_state);
}

void test_stage_validates_and_allows_only_one_pending_change() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.start({100}) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.stage(0, layout(), 1) ==
           configuration::GaugeLayoutChangeError::invalid_request);
    auto invalid = layout();
    invalid.layout_id = 0;
    EXPECT(coordinator.stage(1, invalid, 2) ==
           configuration::GaugeLayoutChangeError::invalid_layout);
    EXPECT(coordinator.stage(10, layout(), 3) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.stage(11, layout(70), 4) ==
           configuration::GaugeLayoutChangeError::change_pending);
    const auto status = coordinator.status();
    EXPECT(status.state == configuration::GaugeLayoutChangeState::pending);
    EXPECT(status.pending_request_id == 10);
    EXPECT(status.pending_opened_ms == 3);
    EXPECT(status.staged_count == 1);
}

void test_exact_confirmation_persists_once() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.start({100}) ==
           configuration::GaugeLayoutChangeError::none);
    auto desired = layout();
    desired.generation = 999;
    EXPECT(coordinator.stage(20, desired, 10) ==
           configuration::GaugeLayoutChangeError::none);
    const auto result = coordinator.confirm(20, 109);
    EXPECT(result.completed());
    EXPECT(result.persistence.changed());
    EXPECT(result.persistence.generation == 1);
    EXPECT(storage.writes(0) == 1 && storage.writes(1) == 0);
    const auto status = coordinator.status();
    EXPECT(status.state == configuration::GaugeLayoutChangeState::idle);
    EXPECT(status.applied_count == 1);
    EXPECT(coordinator.confirm(20, 109).error ==
           configuration::GaugeLayoutChangeError::no_change_pending);
}

void test_unchanged_confirmation_suppresses_write() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    EXPECT(store.save_next_if_changed(layout()).changed());
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.start({50}) ==
           configuration::GaugeLayoutChangeError::none);
    auto same = layout();
    same.generation = 77;
    EXPECT(coordinator.stage(30, same, 1000) ==
           configuration::GaugeLayoutChangeError::none);
    const auto result = coordinator.confirm(30, 1001);
    EXPECT(result.completed());
    EXPECT(!result.persistence.changed());
    EXPECT(result.persistence.generation == 1);
    EXPECT(storage.writes(0) + storage.writes(1) == 1);
    EXPECT(coordinator.status().unchanged_count == 1);
}

void test_mismatch_and_cancel_do_not_apply() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.start({50}) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.stage(40, layout(), 20) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.confirm(41, 21).error ==
           configuration::GaugeLayoutChangeError::request_mismatch);
    EXPECT(coordinator.cancel(41) ==
           configuration::GaugeLayoutChangeError::request_mismatch);
    EXPECT(coordinator.status().state ==
           configuration::GaugeLayoutChangeState::pending);
    EXPECT(coordinator.cancel(40) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.status().cancelled_count == 1);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);
}

void test_confirmation_window_has_an_exact_boundary() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.start({10}) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.stage(50, layout(), 100) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.service(109) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.confirm(50, 110).error ==
           configuration::GaugeLayoutChangeError::confirmation_expired);
    EXPECT(coordinator.status().expired_count == 1);
    EXPECT(coordinator.status().state ==
           configuration::GaugeLayoutChangeState::idle);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);
}

void test_clock_regression_consumes_pending_change() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.start({100}) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.stage(60, layout(), 1000) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(coordinator.confirm(60, 999).error ==
           configuration::GaugeLayoutChangeError::clock_regression);
    EXPECT(coordinator.status().state ==
           configuration::GaugeLayoutChangeState::idle);
    EXPECT(coordinator.status().clock_fault_count == 1);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);
    EXPECT(coordinator.stage(61, layout(), 999) ==
           configuration::GaugeLayoutChangeError::clock_regression);
    EXPECT(coordinator.stage(61, layout(), 1000) ==
           configuration::GaugeLayoutChangeError::none);
}

void test_ordinary_failure_requires_new_confirmation() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.start({100}) ==
           configuration::GaugeLayoutChangeError::none);
    storage.set_next_write_behavior(0, FakeWriteBehavior::fail_before_write);
    EXPECT(coordinator.stage(70, layout(), 0) ==
           configuration::GaugeLayoutChangeError::none);
    const auto result = coordinator.confirm(70, 1);
    EXPECT(result.error ==
           configuration::GaugeLayoutChangeError::persistence_failed);
    EXPECT(coordinator.status().failed_count == 1);
    EXPECT(coordinator.status().state ==
           configuration::GaugeLayoutChangeState::idle);
    EXPECT(coordinator.confirm(70, 2).error ==
           configuration::GaugeLayoutChangeError::no_change_pending);
}

void test_uncertain_commit_requires_restart_and_never_replays_approval() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    EXPECT(store.save_next_if_changed(layout()).changed());
    configuration::GaugeLayoutChangeCoordinator coordinator{store};
    EXPECT(coordinator.start({100}) ==
           configuration::GaugeLayoutChangeError::none);
    storage.set_next_write_behavior(
        1, FakeWriteBehavior::fail_after_full_write);
    EXPECT(coordinator.stage(80, layout(80), 0) ==
           configuration::GaugeLayoutChangeError::none);
    const auto uncertain = coordinator.confirm(80, 1);
    EXPECT(uncertain.error ==
           configuration::GaugeLayoutChangeError::persistence_uncertain);
    EXPECT(coordinator.status().uncertain_count == 1);
    EXPECT(coordinator.status().state ==
           configuration::GaugeLayoutChangeState::idle);
    EXPECT(coordinator.confirm(80, 2).error ==
           configuration::GaugeLayoutChangeError::no_change_pending);

    configuration::GaugeLayoutStore restarted_store{storage};
    configuration::GaugeLayoutChangeCoordinator restarted{restarted_store};
    EXPECT(restarted.start({100}) ==
           configuration::GaugeLayoutChangeError::none);
    EXPECT(restarted.stage(81, layout(80), 3) ==
           configuration::GaugeLayoutChangeError::none);
    const auto reconciled = restarted.confirm(81, 4);
    EXPECT(reconciled.completed());
    EXPECT(!reconciled.persistence.changed());
    EXPECT(reconciled.persistence.generation == 2);
    EXPECT(storage.writes(0) + storage.writes(1) == 2);
}

}  // namespace

int main() {
    test_lifecycle_and_policy_are_explicit();
    test_stage_validates_and_allows_only_one_pending_change();
    test_exact_confirmation_persists_once();
    test_unchanged_confirmation_suppresses_write();
    test_mismatch_and_cancel_do_not_apply();
    test_confirmation_window_has_an_exact_boundary();
    test_clock_regression_consumes_pending_change();
    test_ordinary_failure_requires_new_confirmation();
    test_uncertain_commit_requires_restart_and_never_replays_approval();

    if (failures != 0) {
        std::cerr << failures
                  << " gauge layout change coordinator assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 gauge layout change coordinator scenario groups\n";
    return EXIT_SUCCESS;
}
