#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "fake_esp_now_transport.hpp"
#include "fake_gauge_layout_storage.hpp"
#include "fake_gauge_renderer.hpp"
#include "opengauge/gauge_layout_activation_workflow.hpp"

namespace {

using namespace opengauge;
using configuration::test_support::FakeGaugeLayoutStorage;
using configuration::test_support::FakeWriteBehavior;
using display::test_support::FakeGaugeRenderer;
using wireless::test_support::FakeEspNowTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

wireless::PeerAddress peer(std::uint8_t suffix) {
    return {{0x02U, 0, 0, 0, 0, suffix}};
}

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
    result.stale_after_ms = 100;
    if (kind == display::GaugeWidgetKind::needle ||
        kind == display::GaugeWidgetKind::bar) {
        result.scale_min_raw = 0;
        result.scale_max_raw = 3000000;
    }
    return result;
}

configuration::GaugeLayout layout(
    std::uint64_t generation = 1,
    std::uint32_t layout_id = 0x130F0001U,
    std::uint8_t widget_count = 1) {
    configuration::GaugeLayout result{};
    result.generation = generation;
    result.layout_id = layout_id;
    result.brightness_percent = 70;
    result.theme = configuration::GaugeTheme::high_contrast;
    result.widget_count = widget_count;
    constexpr std::array<wireless::TelemetrySignalCode, 3> codes{{
        wireless::TelemetrySignalCode::engine_speed,
        wireless::TelemetrySignalCode::engine_coolant_temperature,
        wireless::TelemetrySignalCode::vehicle_speed,
    }};
    for (std::size_t index = 0; index < widget_count; ++index) {
        const auto code = codes[index % codes.size()];
        const auto kind = index % 2 == 0
                              ? display::GaugeWidgetKind::numeric
                              : display::GaugeWidgetKind::bar;
        result.widgets[index] = widget(
            static_cast<std::uint16_t>(101 + index),
            code,
            kind,
            index == 0 ? "Primary" : "Secondary");
    }
    return result;
}

display::GaugeDashboardLoopConfiguration runtime_configuration(
    const configuration::GaugeLayout& safe_default = layout()) {
    return {safe_default, {peer(1), 10, 6, 4}};
}

void start_transport_pair(
    FakeEspNowTransport& sender,
    FakeEspNowTransport& gauge) {
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(gauge.start(peer(2), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) ==
           wireless::EspNowError::none);
    EXPECT(gauge.add_peer({peer(1), 6, true}) ==
           wireless::EspNowError::none);
    sender.connect(gauge);
}

void deliver_rpm(
    FakeEspNowTransport& sender,
    std::int64_t raw_value,
    std::uint64_t now_ms,
    std::uint32_t sequence = 0) {
    wireless::TelemetryBatch batch{};
    batch.gateway_id = 10;
    batch.boot_session_id = 20;
    batch.sequence = sequence;
    batch.signal_count = 1;
    batch.signals[0].code = wireless::TelemetrySignalCode::engine_speed;
    batch.signals[0].value = {
        telemetry::SignalValueType::unsigned_integer,
        raw_value,
        true};
    batch.signals[0].unit =
        telemetry::SignalUnit::milli_revolutions_per_minute;
    batch.signals[0].quality = telemetry::SignalQuality::valid;
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               batch, encoded.data(), encoded.size()).encoded());
    EXPECT(sender.send(peer(2), {encoded.data(), encoded.size()}, now_ms)
               .accepted());
    sender.service(now_ms);
}

std::uint32_t writes(const FakeGaugeLayoutStorage& storage) {
    return storage.writes(0) + storage.writes(1);
}

void write_layout(
    FakeGaugeLayoutStorage& storage,
    std::uint8_t slot,
    const configuration::GaugeLayout& value) {
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> bytes{};
    EXPECT(configuration::encode_gauge_layout(
               value, bytes.data(), bytes.size()).succeeded());
    EXPECT(storage.write_slot(slot, bytes.data(), bytes.size()) ==
           configuration::LayoutStorageError::none);
}

class ActivationReadFailureStorage final
    : public configuration::GaugeLayoutStorage {
public:
    configuration::LayoutStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        if (fail_read_after_write_ != 0) {
            ++reads_after_write_;
            if (reads_after_write_ == fail_read_after_write_) {
                fail_read_after_write_ = 0;
                return configuration::LayoutStorageError::io_failure;
            }
        }
        return delegate_.read_slot(slot, output, size);
    }

    configuration::LayoutStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        const auto result = delegate_.write_slot(slot, data, size);
        if (result == configuration::LayoutStorageError::none &&
            arm_activation_read_failure_) {
            arm_activation_read_failure_ = false;
            reads_after_write_ = 0;
            // The store's verification read is first; the activation load's
            // first slot read is second.
            fail_read_after_write_ = 2;
        }
        return result;
    }

    configuration::LayoutStorageError erase_slot(
        std::uint8_t slot) override {
        return delegate_.erase_slot(slot);
    }

    void fail_activation_read_after_next_write() {
        arm_activation_read_failure_ = true;
    }

    std::uint32_t writes() const {
        return delegate_.writes(0) + delegate_.writes(1);
    }

private:
    FakeGaugeLayoutStorage delegate_{};
    std::size_t fail_read_after_write_{0};
    std::size_t reads_after_write_{0};
    bool arm_activation_read_failure_{false};
};

class UnappliedUncertainStorage final
    : public configuration::GaugeLayoutStorage {
public:
    configuration::LayoutStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override {
        return delegate_.read_slot(slot, output, size);
    }

    configuration::LayoutStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls_;
        if (uncertain_without_apply_) {
            uncertain_without_apply_ = false;
            return configuration::LayoutStorageError::commit_uncertain;
        }
        return delegate_.write_slot(slot, data, size);
    }

    configuration::LayoutStorageError erase_slot(
        std::uint8_t slot) override {
        return delegate_.erase_slot(slot);
    }

    void fail_next_write_uncertain_without_apply() {
        uncertain_without_apply_ = true;
    }

    std::uint32_t writes() const { return write_calls_; }

private:
    FakeGaugeLayoutStorage delegate_{};
    std::uint32_t write_calls_{0};
    bool uncertain_without_apply_{false};
};

template <typename Storage>
struct FixtureBase {
    Storage storage{};
    configuration::GaugeLayoutStore store{storage};
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    wireless::GaugeTelemetryReceiver receiver{gauge};
    display::GaugeViewModel view{receiver};
    display::GaugeDashboardLoop dashboard{store, receiver, view};
    FakeGaugeRenderer renderer{};
    display::GaugeRendererRuntime runtime{dashboard, renderer};
    display::GaugeLayoutActivationWorkflow workflow{store, runtime};

    FixtureBase() { start_transport_pair(sender, gauge); }

    void start(const configuration::GaugeLayout& safe_default = layout()) {
        EXPECT(runtime.start(runtime_configuration(safe_default)).started());
        EXPECT(workflow.start({100}, 0).projected());
    }
};

using Fixture = FixtureBase<FakeGaugeLayoutStorage>;
using FaultFixture = FixtureBase<ActivationReadFailureStorage>;
using UnappliedUncertainFixture = FixtureBase<UnappliedUncertainStorage>;

void test_exact_store_identity_and_runtime_state_preflight() {
    FakeGaugeLayoutStorage shared_storage{};
    configuration::GaugeLayoutStore change_store{shared_storage};
    // Equivalent backend access is insufficient: the runtime and workflow
    // must share this exact store owner object.
    configuration::GaugeLayoutStore runtime_store{shared_storage};
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    display::GaugeViewModel view{receiver};
    display::GaugeDashboardLoop dashboard{runtime_store, receiver, view};
    FakeGaugeRenderer renderer{};
    display::GaugeRendererRuntime runtime{dashboard, renderer};
    display::GaugeLayoutActivationWorkflow workflow{change_store, runtime};
    EXPECT(runtime.start(runtime_configuration()).started());
    EXPECT(workflow.start({100}, 0).projected());
    EXPECT(workflow.stage(1, layout(0, 2), 1).projected());
    const auto rejected = workflow.confirm_and_activate(1, 2);
    EXPECT(rejected.error ==
           display::GaugeLayoutActivationWorkflowError::store_mismatch);
    EXPECT(!rejected.persistence_attempted);
    EXPECT(writes(shared_storage) == 0);
    EXPECT(workflow.snapshot(2).status.confirmation_allowed);

    Fixture inactive{};
    EXPECT(inactive.workflow.start({100}, 0).projected());
    EXPECT(inactive.workflow.stage(1, layout(0, 3), 1).projected());
    const auto not_running = inactive.workflow.confirm_and_activate(1, 2);
    EXPECT(not_running.error ==
           display::GaugeLayoutActivationWorkflowError::invalid_state);
    EXPECT(!not_running.persistence_attempted);
    EXPECT(writes(inactive.storage) == 0);
    EXPECT(inactive.workflow.snapshot(2).status.confirmation_allowed);
}

void test_renderer_inflight_preflight_preserves_confirmation() {
    Fixture fixture{};
    fixture.start();
    fixture.renderer.set_hold_presentations(true);
    EXPECT(fixture.runtime.service(0).succeeded());
    EXPECT(fixture.runtime.status().has_renderer_frame_in_flight);
    EXPECT(fixture.workflow.stage(1, layout(0, 2), 1).projected());
    const auto renderer_before = fixture.renderer.status();
    const auto rejected = fixture.workflow.confirm_and_activate(1, 2);
    EXPECT(rejected.error ==
           display::GaugeLayoutActivationWorkflowError::invalid_state);
    EXPECT(!rejected.persistence_attempted);
    EXPECT(writes(fixture.storage) == 0);
    EXPECT(fixture.workflow.snapshot(2).status.confirmation_allowed);
    EXPECT(fixture.renderer.status().offer_calls == renderer_before.offer_calls);
    EXPECT(fixture.renderer.status().service_calls ==
           renderer_before.service_calls);
}

void test_changed_confirm_updates_model_then_presents_later() {
    Fixture fixture{};
    fixture.start();
    deliver_rpm(fixture.sender, 1500000, 10);
    EXPECT(fixture.runtime.service(10).succeeded());
    display::GaugeDashboardFrame old_dashboard{};
    display::GaugeDashboardFrame old_front{};
    EXPECT(fixture.dashboard.copy_frame(old_dashboard));
    EXPECT(fixture.renderer.copy_presented_frame(old_front));
    const auto dashboard_before = fixture.dashboard.status();
    const auto receiver_before = fixture.receiver.status();
    const auto view_before = fixture.view.status();
    const auto renderer_before = fixture.renderer.status();

    const auto desired = layout(0, 2, 2);
    EXPECT(fixture.workflow.stage(1, desired, 11).projected());
    const auto applied = fixture.workflow.confirm_and_activate(1, 12);
    EXPECT(applied.completed());
    EXPECT(applied.persistence.persistence.changed());
    EXPECT(applied.expected_generation == 1);
    EXPECT(applied.activation.dashboard.layout_changed);
    EXPECT(applied.presentation_pending);
    EXPECT(!fixture.dashboard.copy_frame(old_dashboard));
    EXPECT(fixture.renderer.copy_presented_frame(old_front));
    EXPECT(old_front.layout_id == layout().layout_id);
    EXPECT(fixture.renderer.status().offer_calls == renderer_before.offer_calls);
    EXPECT(fixture.renderer.status().service_calls ==
           renderer_before.service_calls);
    const auto dashboard_after = fixture.dashboard.status();
    const auto receiver_after = fixture.receiver.status();
    const auto view_after = fixture.view.status();
    EXPECT(dashboard_after.cycles_serviced == dashboard_before.cycles_serviced);
    EXPECT(dashboard_after.frames_published == dashboard_before.frames_published);
    EXPECT(dashboard_after.last_service_time_ms ==
           dashboard_before.last_service_time_ms);
    EXPECT(receiver_after.datagrams_received ==
           receiver_before.datagrams_received);
    EXPECT(receiver_after.packets_accepted ==
           receiver_before.packets_accepted);
    EXPECT(receiver_after.signals_updated == receiver_before.signals_updated);
    EXPECT(receiver_after.signal_count == receiver_before.signal_count);
    EXPECT(receiver_after.signal_count == 1);
    EXPECT(view_after.running && view_after.widget_count == 2);
    EXPECT(view_after.refreshes_completed == view_before.refreshes_completed);

    deliver_rpm(fixture.sender, 1600000, 100, 1);
    const auto refresh_failed = fixture.workflow.service_presentation(12);
    EXPECT(refresh_failed.error ==
           display::GaugeLayoutActivationWorkflowError::presentation_failure);
    EXPECT(refresh_failed.runtime.error ==
           display::GaugeRendererRuntimeError::dashboard_service_failure);
    EXPECT(refresh_failed.runtime.dashboard.error ==
           display::GaugeDashboardLoopError::view_refresh_failure);
    EXPECT(!fixture.dashboard.copy_frame(old_dashboard));
    EXPECT(fixture.renderer.copy_presented_frame(old_front));
    EXPECT(old_front.layout_id == layout().layout_id);
    EXPECT(fixture.runtime.status().presentation_pending);

    const auto presented = fixture.workflow.service_presentation(100);
    EXPECT(presented.presentation_completed());
    display::GaugeDashboardFrame new_front{};
    EXPECT(fixture.renderer.copy_presented_frame(new_front));
    EXPECT(new_front.layout_id == 2);
    EXPECT(new_front.layout_generation == 1);
    EXPECT(new_front.widget_count == 2);
    EXPECT(new_front.widgets[0].display_value.present);
    EXPECT(new_front.widgets[0].display_value.raw_value == 1600000);
    EXPECT(new_front.publication_sequence ==
           old_front.publication_sequence + 1);
    EXPECT(!fixture.runtime.status().presentation_pending);
}

void test_metadata_transition_discards_pending_but_exact_noop_preserves_it() {
    Fixture metadata{};
    metadata.start();
    metadata.renderer.set_busy(true);
    EXPECT(metadata.runtime.service(0).error ==
           display::GaugeRendererRuntimeError::renderer_busy);
    EXPECT(metadata.runtime.status().has_pending_frame);
    EXPECT(metadata.workflow.stage(1, layout(0), 1).projected());
    const auto metadata_applied =
        metadata.workflow.confirm_and_activate(1, 2);
    EXPECT(metadata_applied.completed());
    EXPECT(!metadata_applied.activation.dashboard.layout_changed);
    EXPECT(metadata_applied.activation.dashboard.frame_metadata_changed);
    EXPECT(metadata_applied.activation.discarded_pending_frame);
    EXPECT(!metadata.runtime.status().has_pending_frame);
    EXPECT(metadata.runtime.status().presentation_pending);
    display::GaugeDashboardFrame absent{};
    EXPECT(!metadata.dashboard.copy_frame(absent));

    Fixture noop{};
    EXPECT(noop.store.save(layout()).saved());
    noop.start();
    noop.renderer.set_busy(true);
    EXPECT(noop.runtime.service(0).error ==
           display::GaugeRendererRuntimeError::renderer_busy);
    const auto before = noop.runtime.status();
    display::GaugeDashboardFrame old{};
    EXPECT(noop.dashboard.copy_frame(old));
    EXPECT(noop.workflow.stage(1, layout(0), 1).projected());
    const auto unchanged = noop.workflow.confirm_and_activate(1, 2);
    EXPECT(unchanged.completed());
    EXPECT(!unchanged.persistence.persistence.changed());
    EXPECT(!unchanged.activation.dashboard.layout_changed);
    EXPECT(!unchanged.activation.dashboard.frame_metadata_changed);
    EXPECT(!unchanged.activation.discarded_pending_frame);
    EXPECT(!unchanged.presentation_pending);
    EXPECT(noop.runtime.status().has_pending_frame);
    EXPECT(noop.runtime.status().pending_frames_discarded_for_activation ==
           before.pending_frames_discarded_for_activation);
    display::GaugeDashboardFrame preserved{};
    EXPECT(noop.dashboard.copy_frame(preserved));
    EXPECT(preserved.publication_sequence == old.publication_sequence);
}

void test_unchanged_persistence_reconciles_lagged_running_model() {
    Fixture fixture{};
    fixture.start();
    EXPECT(fixture.runtime.service(0).succeeded());
    const auto desired = layout(0, 20, 2);
    EXPECT(fixture.store.save_next_if_changed(desired).changed());
    const auto writes_before = writes(fixture.storage);
    EXPECT(fixture.workflow.stage(1, desired, 1).projected());
    const auto reconciled = fixture.workflow.confirm_and_activate(1, 2);
    EXPECT(reconciled.completed());
    EXPECT(!reconciled.persistence.persistence.changed());
    EXPECT(reconciled.activation.dashboard.layout_changed);
    EXPECT(writes(fixture.storage) == writes_before);
    EXPECT(fixture.view.status().widget_count == 2);
    display::GaugeDashboardFrame invalidated{};
    EXPECT(!fixture.dashboard.copy_frame(invalidated));
}

void test_post_commit_load_failure_latches_and_retry_writes_nothing() {
    FaultFixture fixture{};
    fixture.start();
    EXPECT(fixture.runtime.service(0).succeeded());
    display::GaugeDashboardFrame old{};
    EXPECT(fixture.dashboard.copy_frame(old));
    EXPECT(fixture.workflow.stage(1, layout(0, 2, 2), 1).projected());
    fixture.storage.fail_activation_read_after_next_write();
    const auto failed = fixture.workflow.confirm_and_activate(1, 2);
    EXPECT(failed.error ==
           display::GaugeLayoutActivationWorkflowError::activation_failure);
    EXPECT(failed.persistence.persistence.changed());
    EXPECT(failed.expected_generation == 1);
    EXPECT(failed.retry_required && failed.restart_required);
    EXPECT(failed.activation.dashboard.error ==
           display::GaugeDashboardLoopError::layout_load_failure);
    EXPECT(fixture.workflow.status().activation_pending);
    EXPECT(fixture.workflow.status().pending_generation == 1);
    display::GaugeDashboardFrame preserved{};
    EXPECT(fixture.dashboard.copy_frame(preserved));
    EXPECT(preserved.layout_id == old.layout_id);
    const auto writes_before = fixture.storage.writes();

    const auto retried = fixture.workflow.retry_activation();
    EXPECT(retried.completed());
    EXPECT(!retried.persistence_attempted);
    EXPECT(retried.expected_generation == 1);
    EXPECT(!retried.retry_required && !retried.restart_required);
    EXPECT(fixture.storage.writes() == writes_before);
    EXPECT(!fixture.workflow.status().activation_pending);
    EXPECT(fixture.view.status().widget_count == 2);
}

void test_retry_generation_drift_fails_closed_and_keeps_latch() {
    FaultFixture fixture{};
    fixture.start();
    EXPECT(fixture.runtime.service(0).succeeded());
    EXPECT(fixture.workflow.stage(1, layout(0, 2, 2), 1).projected());
    fixture.storage.fail_activation_read_after_next_write();
    EXPECT(fixture.workflow.confirm_and_activate(1, 2).retry_required);
    EXPECT(fixture.store.save(layout(2, 3, 1)).saved());
    const auto writes_before = fixture.storage.writes();
    const auto rejected = fixture.workflow.retry_activation();
    EXPECT(rejected.error ==
           display::GaugeLayoutActivationWorkflowError::activation_failure);
    EXPECT(rejected.activation.dashboard.error ==
           display::GaugeDashboardLoopError::layout_generation_mismatch);
    EXPECT(rejected.expected_generation == 1);
    EXPECT(rejected.retry_required && rejected.restart_required);
    EXPECT(fixture.storage.writes() == writes_before);
    EXPECT(fixture.workflow.status().activation_pending);
    EXPECT(fixture.view.status().widget_count == 1);
    display::GaugeDashboardFrame old{};
    EXPECT(fixture.dashboard.copy_frame(old));
    EXPECT(old.layout_id == layout().layout_id);

    Fixture conflict{};
    conflict.start();
    EXPECT(conflict.runtime.service(0).succeeded());
    display::GaugeDashboardFrame conflict_before{};
    EXPECT(conflict.dashboard.copy_frame(conflict_before));
    write_layout(conflict.storage, 0, layout(1, 40));
    write_layout(conflict.storage, 1, layout(1, 41));
    const auto conflict_result =
        conflict.runtime.activate_persisted_layout(1);
    EXPECT(conflict_result.error ==
           display::GaugeRendererRuntimeError::layout_activation_failure);
    EXPECT(conflict_result.dashboard.error ==
           display::GaugeDashboardLoopError::layout_load_failure);
    display::GaugeDashboardFrame conflict_after{};
    EXPECT(conflict.dashboard.copy_frame(conflict_after));
    EXPECT(conflict_after.layout_id == conflict_before.layout_id);
    EXPECT(conflict.view.status().widget_count == 1);
}

void test_persistence_errors_and_clock_fault_never_activate() {
    Fixture failed{};
    failed.start();
    EXPECT(failed.workflow.stage(1, layout(0, 2), 1).projected());
    failed.storage.set_next_write_behavior(
        0, FakeWriteBehavior::fail_before_write);
    auto result = failed.workflow.confirm_and_activate(1, 2);
    EXPECT(result.error ==
           display::GaugeLayoutActivationWorkflowError::persistence_failure);
    EXPECT(result.persistence.operation_error ==
           configuration::GaugeLayoutChangeError::persistence_failed);
    EXPECT(!result.activation_attempted);
    EXPECT(!failed.workflow.status().activation_pending);
    EXPECT(!failed.workflow.status().restart_required);

    Fixture uncertain{};
    uncertain.start();
    EXPECT(uncertain.workflow.stage(1, layout(0, 2), 1).projected());
    uncertain.storage.set_next_write_behavior(
        0, FakeWriteBehavior::fail_after_full_write);
    result = uncertain.workflow.confirm_and_activate(1, 2);
    EXPECT(result.persistence.operation_error ==
           configuration::GaugeLayoutChangeError::persistence_uncertain);
    EXPECT(!result.activation_attempted);
    EXPECT(!uncertain.workflow.status().activation_pending);
    EXPECT(result.restart_required);
    EXPECT(uncertain.workflow.status().restart_required);
    const auto uncertain_writes = writes(uncertain.storage);
    EXPECT(uncertain.workflow.stage(2, layout(0, 3), 3).operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(uncertain.workflow.retry_activation().error ==
           display::GaugeLayoutActivationWorkflowError::restart_required);
    EXPECT(writes(uncertain.storage) == uncertain_writes);

    UnappliedUncertainFixture unapplied{};
    unapplied.start();
    EXPECT(unapplied.workflow.stage(1, layout(0, 2), 1).projected());
    unapplied.storage.fail_next_write_uncertain_without_apply();
    result = unapplied.workflow.confirm_and_activate(1, 2);
    EXPECT(result.persistence.operation_error ==
           configuration::GaugeLayoutChangeError::persistence_uncertain);
    EXPECT(result.restart_required);
    EXPECT(unapplied.workflow.status().restart_required);
    const auto unapplied_writes = unapplied.storage.writes();
    EXPECT(unapplied.workflow.confirm_and_activate(2, 3).error ==
           display::GaugeLayoutActivationWorkflowError::restart_required);
    EXPECT(unapplied.storage.writes() == unapplied_writes);

    Fixture clock{};
    clock.start();
    EXPECT(clock.workflow.stage(1, layout(0, 2), 100).projected());
    result = clock.workflow.confirm_and_activate(1, 99);
    EXPECT(result.persistence.operation_error ==
           configuration::GaugeLayoutChangeError::clock_regression);
    EXPECT(!result.activation_attempted);
    EXPECT(writes(clock.storage) == 0);
}

void test_pending_activation_blocks_mutation_and_survives_not_ready_retry() {
    FaultFixture fixture{};
    fixture.start();
    EXPECT(fixture.runtime.service(0).succeeded());
    EXPECT(fixture.workflow.stage(1, layout(0, 2), 1).projected());
    fixture.storage.fail_activation_read_after_next_write();
    EXPECT(fixture.workflow.confirm_and_activate(1, 2).retry_required);
    const auto writes_before = fixture.storage.writes();
    const auto blocked_stage = fixture.workflow.stage(2, layout(0, 3), 3);
    EXPECT(blocked_stage.operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    const auto blocked_confirm = fixture.workflow.confirm_and_activate(2, 3);
    EXPECT(blocked_confirm.error ==
           display::GaugeLayoutActivationWorkflowError::activation_pending);
    EXPECT(fixture.storage.writes() == writes_before);

    fixture.renderer.set_hold_presentations(true);
    EXPECT(fixture.runtime.service(3).succeeded());
    EXPECT(fixture.runtime.status().has_renderer_frame_in_flight);
    const auto not_ready = fixture.workflow.retry_activation();
    EXPECT(not_ready.error ==
           display::GaugeLayoutActivationWorkflowError::invalid_state);
    EXPECT(not_ready.retry_required && not_ready.restart_required);
    EXPECT(fixture.workflow.status().activation_pending);
    EXPECT(fixture.storage.writes() == writes_before);
}

void test_maximum_widgets_and_restart_reload_exact_persisted_layout() {
    Fixture fixture{};
    fixture.start();
    const auto maximum = layout(0, 8, 8);
    EXPECT(fixture.workflow.stage(1, maximum, 1).projected());
    EXPECT(fixture.workflow.confirm_and_activate(1, 2).completed());
    EXPECT(fixture.view.status().widget_count ==
           display::kMaximumGaugeWidgets);
    EXPECT(fixture.workflow.service_presentation(2)
               .presentation_completed());
    display::GaugeDashboardFrame frame{};
    EXPECT(fixture.renderer.copy_presented_frame(frame));
    EXPECT(frame.widget_count == display::kMaximumGaugeWidgets);
    EXPECT(frame.publication_sequence == 1);

    EXPECT(fixture.workflow.stop(3).projected());
    fixture.runtime.stop();
    EXPECT(fixture.runtime.start(runtime_configuration()).started());
    EXPECT(fixture.workflow.start({100}, 4).projected());
    EXPECT(fixture.view.status().widget_count ==
           display::kMaximumGaugeWidgets);
    EXPECT(fixture.runtime.service(4).succeeded());
    EXPECT(fixture.renderer.copy_presented_frame(frame));
    EXPECT(frame.layout_id == 8);
    EXPECT(frame.layout_generation == 1);
    EXPECT(frame.publication_sequence == 1);
}

}  // namespace

int main() {
    test_exact_store_identity_and_runtime_state_preflight();
    test_renderer_inflight_preflight_preserves_confirmation();
    test_changed_confirm_updates_model_then_presents_later();
    test_metadata_transition_discards_pending_but_exact_noop_preserves_it();
    test_unchanged_persistence_reconciles_lagged_running_model();
    test_post_commit_load_failure_latches_and_retry_writes_nothing();
    test_retry_generation_drift_fails_closed_and_keeps_latch();
    test_persistence_errors_and_clock_fault_never_activate();
    test_pending_activation_blocks_mutation_and_survives_not_ready_retry();
    test_maximum_widgets_and_restart_reload_exact_persisted_layout();

    if (failures != 0) {
        std::cerr << failures
                  << " gauge layout activation workflow check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout <<
        "PASS: 10 gauge layout activation workflow scenario groups\n";
    return EXIT_SUCCESS;
}
