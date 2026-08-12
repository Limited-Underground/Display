#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

#include "fake_gauge_layout_storage.hpp"
#include "opengauge/gauge_layout_change_workflow.hpp"

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

void test_lifecycle_always_returns_a_coherent_projection() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeWorkflow workflow{store};

    auto result = workflow.snapshot(0);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::unavailable);

    result = workflow.start({0}, 0);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::invalid_policy);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::unavailable);

    result = workflow.start({100}, 0);
    EXPECT(result.operation_error == configuration::GaugeLayoutChangeError::none);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::ready);

    result = workflow.start({100}, 0);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::ready);

    result = workflow.stop(1);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::unavailable);
}

void test_stage_immediately_returns_the_exact_live_prompt() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeWorkflow workflow{store};
    EXPECT(workflow.start({100}, 0).projected());

    const auto result = workflow.stage(10, layout(), 5);
    EXPECT(result.operation_error == configuration::GaugeLayoutChangeError::none);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::
               confirmation_required);
    EXPECT(result.status.pending_request_id == 10);
    EXPECT(result.status.confirmation_remaining_ms == 100);
    EXPECT(result.status.confirmation_allowed);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);
}

void test_mismatch_flags_rejection_without_hiding_prompt() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeWorkflow workflow{store};
    EXPECT(workflow.start({100}, 0).projected());
    EXPECT(workflow.stage(20, layout(), 10).projected());

    const auto result = workflow.confirm(21, 11);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::request_mismatch);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::
               confirmation_required);
    EXPECT(result.status.pending_request_id == 20);
    EXPECT(result.status.confirmation_allowed);
    EXPECT(result.status.last_operation_rejected);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);
}

void test_confirm_changed_and_unchanged_are_atomic_results() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeWorkflow workflow{store};
    EXPECT(workflow.start({100}, 0).projected());
    EXPECT(workflow.stage(30, layout(), 1).projected());
    auto result = workflow.confirm(30, 2);
    EXPECT(result.projected());
    EXPECT(result.operation_error == configuration::GaugeLayoutChangeError::none);
    EXPECT(result.persistence.changed());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::applied);
    EXPECT(result.status.generation == 1);

    EXPECT(workflow.stage(31, layout(), 3).projected());
    result = workflow.confirm(31, 4);
    EXPECT(result.projected());
    EXPECT(result.persistence.succeeded());
    EXPECT(!result.persistence.changed());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::unchanged);
    EXPECT(result.status.generation == 1);
    EXPECT(storage.writes(0) + storage.writes(1) == 1);
}

void test_cancel_and_expiry_return_terminal_semantics() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeWorkflow workflow{store};
    EXPECT(workflow.start({10}, 0).projected());
    EXPECT(workflow.stage(40, layout(), 100).projected());
    auto result = workflow.cancel(40, 101);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::cancelled);

    EXPECT(workflow.stage(41, layout(), 102).projected());
    result = workflow.snapshot(112);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::expired);
    EXPECT(result.status.action ==
           configuration::GaugeLayoutChangeOperatorAction::service_expiry);
    result = workflow.service(112);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::confirmation_expired);
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::expired);
    EXPECT(result.status.action ==
           configuration::GaugeLayoutChangeOperatorAction::stage_new_request);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);
}

void test_storage_failure_and_uncertainty_have_distinct_actions() {
    FakeGaugeLayoutStorage failed_storage{};
    configuration::GaugeLayoutStore failed_store{failed_storage};
    configuration::GaugeLayoutChangeWorkflow failed{failed_store};
    EXPECT(failed.start({100}, 0).projected());
    failed_storage.set_next_write_behavior(
        0, FakeWriteBehavior::fail_before_write);
    EXPECT(failed.stage(50, layout(), 1).projected());
    auto result = failed.confirm(50, 2);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::persistence_failed);
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::persistence_failed);
    EXPECT(result.status.action ==
           configuration::GaugeLayoutChangeOperatorAction::stage_new_request);

    FakeGaugeLayoutStorage uncertain_storage{};
    configuration::GaugeLayoutStore uncertain_store{uncertain_storage};
    EXPECT(uncertain_store.save_next_if_changed(layout()).changed());
    configuration::GaugeLayoutChangeWorkflow uncertain{uncertain_store};
    EXPECT(uncertain.start({100}, 0).projected());
    uncertain_storage.set_next_write_behavior(
        1, FakeWriteBehavior::fail_after_full_write);
    EXPECT(uncertain.stage(60, layout(80), 1).projected());
    result = uncertain.confirm(60, 2);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::persistence_uncertain);
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::restart_required);
    EXPECT(result.status.action ==
           configuration::GaugeLayoutChangeOperatorAction::
               restart_and_reconcile);
}

void test_clock_regression_consumes_prompt_and_projects_fault() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeWorkflow workflow{store};
    EXPECT(workflow.start({100}, 0).projected());
    EXPECT(workflow.stage(70, layout(), 1000).projected());
    const auto result = workflow.confirm(70, 999);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::clock_regression);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::clock_fault);
    EXPECT(!result.status.confirmation_allowed);
    EXPECT(result.status.pending_request_id == 0);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);
}

void test_same_boot_request_replay_is_projected_as_rejection() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeWorkflow workflow{store};
    EXPECT(workflow.start({100}, 0).projected());
    EXPECT(workflow.stage(80, layout(), 1).projected());
    EXPECT(workflow.cancel(80, 2).projected());
    const auto result = workflow.stage(80, layout(), 3);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::request_not_newer);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::rejected);
    EXPECT(result.status.last_operation_rejected);
    EXPECT(result.status.action ==
           configuration::GaugeLayoutChangeOperatorAction::stage_new_request);
}

void test_restore_default_uses_confirmed_generation_not_slot_erase() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    auto custom = layout(80);
    EXPECT(store.save_next_if_changed(custom).changed());
    configuration::GaugeLayoutChangeWorkflow workflow{store};
    EXPECT(workflow.start({100}, 0).projected());

    auto compiled_default = layout(65);
    compiled_default.generation = 999;
    auto result = workflow.stage_restore_default(90, compiled_default, 1);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::
               confirmation_required);
    EXPECT(storage.writes(0) + storage.writes(1) == 1);
    EXPECT(storage.erases(0) + storage.erases(1) == 0);

    result = workflow.confirm(90, 2);
    EXPECT(result.projected());
    EXPECT(result.persistence.changed());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::applied);
    EXPECT(result.status.generation == 2);
    EXPECT(storage.writes(0) + storage.writes(1) == 2);
    EXPECT(storage.erases(0) + storage.erases(1) == 0);

    configuration::GaugeLayoutStore restarted_store{storage};
    configuration::GaugeLayout loaded{};
    auto safe_default = layout(65);
    safe_default.generation = 1;
    const auto loaded_result = restarted_store.load(safe_default, loaded);
    EXPECT(loaded_result.source == configuration::GaugeLayoutSource::slot_b);
    EXPECT(!loaded_result.recovery_required);
    EXPECT(loaded.generation == 2);
    EXPECT(loaded.brightness_percent == 65);

    EXPECT(workflow.stage_restore_default(91, compiled_default, 3).projected());
    result = workflow.confirm(91, 4);
    EXPECT(result.projected());
    EXPECT(result.persistence.succeeded() && !result.persistence.changed());
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::unchanged);
    EXPECT(storage.writes(0) + storage.writes(1) == 2);
    EXPECT(storage.erases(0) + storage.erases(1) == 0);

    auto invalid = compiled_default;
    invalid.layout_id = 0;
    result = workflow.stage_restore_default(92, invalid, 5);
    EXPECT(result.operation_error ==
           configuration::GaugeLayoutChangeError::invalid_layout);
    EXPECT(result.status.state ==
           configuration::GaugeLayoutChangeOperatorState::rejected);
    EXPECT(storage.writes(0) + storage.writes(1) == 2);
}

void test_import_record_validates_before_staging_and_ignores_generation() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayoutChangeWorkflow workflow{store};
    EXPECT(workflow.start({100}, 0).projected());

    auto imported = layout(72);
    imported.generation = std::numeric_limits<std::uint64_t>::max();
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> record{};
    EXPECT(configuration::encode_gauge_layout(
               imported, record.data(), record.size()).succeeded());

    auto result = workflow.stage_import_record(
        100, record.data(), record.size() - 1, 1);
    EXPECT(!result.decoded());
    EXPECT(result.codec_error ==
           configuration::GaugeLayoutCodecError::invalid_argument);
    EXPECT(result.workflow.projected());
    EXPECT(result.workflow.status.state ==
           configuration::GaugeLayoutChangeOperatorState::ready);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);

    result = workflow.stage_import_record(100, record.data(), record.size(), 2);
    EXPECT(result.decoded());
    EXPECT(result.summary.source_generation ==
           std::numeric_limits<std::uint64_t>::max());
    EXPECT(result.summary.layout_id == imported.layout_id);
    EXPECT(result.summary.theme == imported.theme);
    EXPECT(result.summary.brightness_percent == 72);
    EXPECT(result.summary.widget_count == imported.widget_count);
    EXPECT(result.workflow.projected());
    EXPECT(result.workflow.status.state ==
           configuration::GaugeLayoutChangeOperatorState::
               confirmation_required);
    EXPECT(result.workflow.status.pending_request_id == 100);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);

    auto corrupt = record;
    corrupt[100] ^= 0x40U;
    result = workflow.stage_import_record(
        101, corrupt.data(), corrupt.size(), 3);
    EXPECT(!result.decoded());
    EXPECT(result.codec_error ==
           configuration::GaugeLayoutCodecError::checksum_mismatch);
    EXPECT(result.workflow.projected());
    EXPECT(result.workflow.status.pending_request_id == 100);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);

    const auto confirmed = workflow.confirm(100, 4);
    EXPECT(confirmed.projected());
    EXPECT(confirmed.persistence.changed());
    EXPECT(confirmed.persistence.generation == 1);
    EXPECT(storage.writes(0) + storage.writes(1) == 1);
    EXPECT(storage.erases(0) + storage.erases(1) == 0);

    configuration::GaugeLayoutStore restarted{storage};
    configuration::GaugeLayout loaded{};
    auto safe_default = layout();
    safe_default.generation = 1;
    const auto load_result = restarted.load(safe_default, loaded);
    EXPECT(load_result.source == configuration::GaugeLayoutSource::slot_a);
    EXPECT(loaded.generation == 1);
    EXPECT(loaded.brightness_percent == 72);
}

}  // namespace

int main() {
    test_lifecycle_always_returns_a_coherent_projection();
    test_stage_immediately_returns_the_exact_live_prompt();
    test_mismatch_flags_rejection_without_hiding_prompt();
    test_confirm_changed_and_unchanged_are_atomic_results();
    test_cancel_and_expiry_return_terminal_semantics();
    test_storage_failure_and_uncertainty_have_distinct_actions();
    test_clock_regression_consumes_prompt_and_projects_fault();
    test_same_boot_request_replay_is_projected_as_rejection();
    test_restore_default_uses_confirmed_generation_not_slot_erase();
    test_import_record_validates_before_staging_and_ignores_generation();

    if (failures != 0) {
        std::cerr << failures
                  << " layout-change workflow assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 layout-change workflow groups\n";
    return EXIT_SUCCESS;
}
