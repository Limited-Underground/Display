#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/gauge_layout_change_operator_status.hpp"

namespace {

using namespace opengauge::configuration;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

GaugeLayoutChangeStatus stopped_status() {
    return {};
}

GaugeLayoutChangeStatus idle_status() {
    GaugeLayoutChangeStatus result{};
    result.state = GaugeLayoutChangeState::idle;
    result.confirmation_window_ms = 20;
    return result;
}

GaugeLayoutChangeStatus pending_status() {
    auto result = idle_status();
    result.state = GaugeLayoutChangeState::pending;
    result.pending_request_id = 7;
    result.last_request_id = 7;
    result.pending_opened_ms = 100;
    return result;
}

GaugeLayoutChangeObservation observation(
    GaugeLayoutChangeOperation operation,
    GaugeLayoutChangeError error = GaugeLayoutChangeError::none) {
    GaugeLayoutChangeObservation result{};
    result.operation = operation;
    result.error = error;
    return result;
}

void test_snapshot_projects_stopped_and_ready_state() {
    const auto stopped = project_gauge_layout_change_operator_status(
        stopped_status(), observation(GaugeLayoutChangeOperation::snapshot), 0);
    EXPECT(stopped.projected());
    EXPECT(stopped.status.state ==
           GaugeLayoutChangeOperatorState::unavailable);
    EXPECT(stopped.status.action == GaugeLayoutChangeOperatorAction::none);
    EXPECT(!stopped.status.attention_required);

    const auto ready = project_gauge_layout_change_operator_status(
        idle_status(), observation(GaugeLayoutChangeOperation::snapshot), 0);
    EXPECT(ready.projected());
    EXPECT(ready.status.state == GaugeLayoutChangeOperatorState::ready);
    EXPECT(ready.status.action ==
           GaugeLayoutChangeOperatorAction::stage_new_request);
    EXPECT(!ready.status.confirmation_allowed);
}

void test_pending_projection_uses_exact_time_boundary() {
    const auto pending = pending_status();
    auto result = project_gauge_layout_change_operator_status(
        pending, observation(GaugeLayoutChangeOperation::snapshot), 100);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::confirmation_required);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::confirm_or_cancel);
    EXPECT(result.status.pending_request_id == 7);
    EXPECT(result.status.confirmation_remaining_ms == 20);
    EXPECT(result.status.confirmation_allowed);
    EXPECT(result.status.attention_required);

    result = project_gauge_layout_change_operator_status(
        pending, observation(GaugeLayoutChangeOperation::snapshot), 119);
    EXPECT(result.status.confirmation_remaining_ms == 1);
    result = project_gauge_layout_change_operator_status(
        pending, observation(GaugeLayoutChangeOperation::snapshot), 120);
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::expired);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::service_expiry);
    EXPECT(!result.status.confirmation_allowed);
    EXPECT(result.status.pending_request_id == 0);

    result = project_gauge_layout_change_operator_status(
        pending, observation(GaugeLayoutChangeOperation::snapshot), 99);
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::clock_fault);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::service_clock);
}

void test_stage_results_preserve_or_reject_the_live_prompt() {
    auto result = project_gauge_layout_change_operator_status(
        pending_status(), observation(GaugeLayoutChangeOperation::stage), 101);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::confirmation_required);

    result = project_gauge_layout_change_operator_status(
        idle_status(),
        observation(
            GaugeLayoutChangeOperation::stage,
            GaugeLayoutChangeError::invalid_layout),
        0);
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::rejected);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::stage_new_request);

    result = project_gauge_layout_change_operator_status(
        pending_status(),
        observation(
            GaugeLayoutChangeOperation::stage,
            GaugeLayoutChangeError::change_pending),
        101);
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::confirmation_required);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::confirm_or_cancel);
    EXPECT(result.status.pending_request_id == 7);
    EXPECT(result.status.confirmation_allowed);
    EXPECT(result.status.last_operation_rejected);

    result = project_gauge_layout_change_operator_status(
        idle_status(),
        observation(
            GaugeLayoutChangeOperation::stage,
            GaugeLayoutChangeError::clock_regression),
        0);
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::clock_fault);

    result = project_gauge_layout_change_operator_status(
        stopped_status(),
        observation(
            GaugeLayoutChangeOperation::stage,
            GaugeLayoutChangeError::invalid_state),
        0);
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::unavailable);
}

void test_confirm_success_distinguishes_applied_and_unchanged() {
    auto applied_observation = observation(GaugeLayoutChangeOperation::confirm);
    applied_observation.persistence = {
        GaugeLayoutStoreError::none,
        GaugeLayoutUpdateState::updated,
        GaugeLayoutSource::slot_a,
        5};
    auto result = project_gauge_layout_change_operator_status(
        idle_status(), applied_observation, 0);
    EXPECT(result.projected());
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::applied);
    EXPECT(result.status.generation == 5);
    EXPECT(!result.status.attention_required);

    auto unchanged_observation =
        observation(GaugeLayoutChangeOperation::confirm);
    unchanged_observation.persistence = {
        GaugeLayoutStoreError::none,
        GaugeLayoutUpdateState::unchanged,
        GaugeLayoutSource::slot_b,
        4};
    result = project_gauge_layout_change_operator_status(
        idle_status(), unchanged_observation, 0);
    EXPECT(result.projected());
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::unchanged);
    EXPECT(result.status.generation == 4);
}

void test_confirm_storage_failures_have_distinct_actions() {
    auto failed = observation(
        GaugeLayoutChangeOperation::confirm,
        GaugeLayoutChangeError::persistence_failed);
    failed.persistence.error = GaugeLayoutStoreError::storage_failure;
    auto result = project_gauge_layout_change_operator_status(
        idle_status(), failed, 0);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::persistence_failed);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::stage_new_request);
    EXPECT(result.status.attention_required);

    auto uncertain = observation(
        GaugeLayoutChangeOperation::confirm,
        GaugeLayoutChangeError::persistence_uncertain);
    uncertain.persistence.error = GaugeLayoutStoreError::commit_uncertain;
    result = project_gauge_layout_change_operator_status(
        idle_status(), uncertain, 0);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::restart_required);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::restart_and_reconcile);
}

void test_confirm_rejections_keep_fail_closed_actions() {
    auto result = project_gauge_layout_change_operator_status(
        pending_status(),
        observation(
            GaugeLayoutChangeOperation::confirm,
            GaugeLayoutChangeError::request_mismatch),
        101);
    EXPECT(result.projected());
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::confirmation_required);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::confirm_or_cancel);
    EXPECT(result.status.last_operation_rejected);

    result = project_gauge_layout_change_operator_status(
        pending_status(),
        observation(
            GaugeLayoutChangeOperation::confirm,
            GaugeLayoutChangeError::request_mismatch),
        120);
    EXPECT(result.error ==
           GaugeLayoutChangeProjectionError::invalid_observation);

    result = project_gauge_layout_change_operator_status(
        idle_status(),
        observation(
            GaugeLayoutChangeOperation::confirm,
            GaugeLayoutChangeError::confirmation_expired),
        0);
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::expired);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::stage_new_request);

    result = project_gauge_layout_change_operator_status(
        idle_status(),
        observation(
            GaugeLayoutChangeOperation::confirm,
            GaugeLayoutChangeError::clock_regression),
        0);
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::clock_fault);

    result = project_gauge_layout_change_operator_status(
        stopped_status(),
        observation(
            GaugeLayoutChangeOperation::confirm,
            GaugeLayoutChangeError::invalid_state),
        0);
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::unavailable);
}

void test_cancel_and_service_results_are_unambiguous() {
    auto result = project_gauge_layout_change_operator_status(
        idle_status(), observation(GaugeLayoutChangeOperation::cancel), 0);
    EXPECT(result.projected());
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::cancelled);
    EXPECT(result.status.action ==
           GaugeLayoutChangeOperatorAction::stage_new_request);

    result = project_gauge_layout_change_operator_status(
        pending_status(),
        observation(
            GaugeLayoutChangeOperation::cancel,
            GaugeLayoutChangeError::request_mismatch),
        101);
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::confirmation_required);
    EXPECT(result.status.confirmation_allowed);
    EXPECT(result.status.last_operation_rejected);

    result = project_gauge_layout_change_operator_status(
        pending_status(), observation(GaugeLayoutChangeOperation::service), 101);
    EXPECT(result.status.state ==
           GaugeLayoutChangeOperatorState::confirmation_required);
    result = project_gauge_layout_change_operator_status(
        idle_status(),
        observation(
            GaugeLayoutChangeOperation::service,
            GaugeLayoutChangeError::confirmation_expired),
        0);
    EXPECT(result.status.state == GaugeLayoutChangeOperatorState::expired);
}

void test_incoherent_status_and_observation_fail_closed() {
    auto incoherent = idle_status();
    incoherent.confirmation_window_ms = 0;
    auto result = project_gauge_layout_change_operator_status(
        incoherent, observation(GaugeLayoutChangeOperation::snapshot), 0);
    EXPECT(result.error == GaugeLayoutChangeProjectionError::invalid_status);

    incoherent = pending_status();
    incoherent.last_request_id = 6;
    result = project_gauge_layout_change_operator_status(
        incoherent, observation(GaugeLayoutChangeOperation::snapshot), 101);
    EXPECT(result.error == GaugeLayoutChangeProjectionError::invalid_status);

    auto invalid = observation(GaugeLayoutChangeOperation::snapshot);
    invalid.error = GaugeLayoutChangeError::invalid_request;
    result = project_gauge_layout_change_operator_status(
        idle_status(), invalid, 0);
    EXPECT(result.error ==
           GaugeLayoutChangeProjectionError::invalid_observation);

    auto malformed_success = observation(GaugeLayoutChangeOperation::confirm);
    malformed_success.persistence.error = GaugeLayoutStoreError::none;
    result = project_gauge_layout_change_operator_status(
        idle_status(), malformed_success, 0);
    EXPECT(result.error ==
           GaugeLayoutChangeProjectionError::invalid_observation);

    auto impossible = observation(GaugeLayoutChangeOperation::cancel);
    impossible.persistence = {
        GaugeLayoutStoreError::none,
        GaugeLayoutUpdateState::updated,
        GaugeLayoutSource::slot_a,
        1};
    result = project_gauge_layout_change_operator_status(
        idle_status(), impossible, 0);
    EXPECT(result.error ==
           GaugeLayoutChangeProjectionError::invalid_observation);

    result = project_gauge_layout_change_operator_status(
        idle_status(),
        observation(
            GaugeLayoutChangeOperation::stage,
            GaugeLayoutChangeError::change_pending),
        0);
    EXPECT(result.error ==
           GaugeLayoutChangeProjectionError::invalid_observation);

    result = project_gauge_layout_change_operator_status(
        pending_status(),
        observation(
            GaugeLayoutChangeOperation::confirm,
            GaugeLayoutChangeError::no_change_pending),
        101);
    EXPECT(result.error ==
           GaugeLayoutChangeProjectionError::invalid_observation);

    result = project_gauge_layout_change_operator_status(
        idle_status(),
        observation(
            GaugeLayoutChangeOperation::cancel,
            GaugeLayoutChangeError::request_mismatch),
        0);
    EXPECT(result.error ==
           GaugeLayoutChangeProjectionError::invalid_observation);

    auto unknown = observation(GaugeLayoutChangeOperation::snapshot);
    unknown.operation = static_cast<GaugeLayoutChangeOperation>(255);
    result = project_gauge_layout_change_operator_status(
        idle_status(), unknown, 0);
    EXPECT(result.error ==
           GaugeLayoutChangeProjectionError::invalid_observation);
}

}  // namespace

int main() {
    test_snapshot_projects_stopped_and_ready_state();
    test_pending_projection_uses_exact_time_boundary();
    test_stage_results_preserve_or_reject_the_live_prompt();
    test_confirm_success_distinguishes_applied_and_unchanged();
    test_confirm_storage_failures_have_distinct_actions();
    test_confirm_rejections_keep_fail_closed_actions();
    test_cancel_and_service_results_are_unambiguous();
    test_incoherent_status_and_observation_fail_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " layout change operator projection assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 layout change operator projection groups\n";
    return EXIT_SUCCESS;
}
