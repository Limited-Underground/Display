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
    std::string_view label) {
    display::GaugeWidgetConfiguration result{};
    result.widget_id = id;
    result.signal_code = code;
    result.kind = display::GaugeWidgetKind::numeric;
    EXPECT(display::make_gauge_widget_label(label, result.label) ==
           display::GaugeViewModelError::none);
    result.stale_after_ms = 100;
    return result;
}

configuration::GaugeLayout layout(
    std::uint64_t generation = 1,
    std::uint32_t layout_id = 0x01300001U,
    std::uint8_t widget_count = 1) {
    configuration::GaugeLayout result{};
    result.generation = generation;
    result.layout_id = layout_id;
    result.theme = configuration::GaugeTheme::high_contrast;
    result.brightness_percent = 70;
    result.widget_count = widget_count;
    for (std::size_t index = 0; index < widget_count; ++index) {
        result.widgets[index] = widget(
            static_cast<std::uint16_t>(100 + index),
            index == 0
                ? wireless::TelemetrySignalCode::engine_speed
                : wireless::TelemetrySignalCode::vehicle_speed,
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
    std::uint64_t now_ms) {
    wireless::TelemetryBatch batch{};
    batch.gateway_id = 10;
    batch.boot_session_id = 20;
    batch.sequence = 1;
    batch.signal_count = 1;
    batch.signals[0].code =
        wireless::TelemetrySignalCode::engine_speed;
    batch.signals[0].value = {
        telemetry::SignalValueType::unsigned_integer,
        1500000,
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
        if (result == configuration::LayoutStorageError::none && armed_) {
            armed_ = false;
            reads_after_write_ = 0;
            fail_read_after_write_ = 2;
        }
        return result;
    }

    configuration::LayoutStorageError erase_slot(
        std::uint8_t slot) override {
        return delegate_.erase_slot(slot);
    }

    void fail_activation_read_after_next_write() { armed_ = true; }

    [[nodiscard]] std::uint32_t writes() const {
        return delegate_.writes(0) + delegate_.writes(1);
    }

private:
    FakeGaugeLayoutStorage delegate_{};
    std::size_t fail_read_after_write_{0};
    std::size_t reads_after_write_{0};
    bool armed_{false};
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

display::GaugeLayoutActivationWorkflowResult activate(
    Fixture& fixture,
    std::uint32_t request_id = 1,
    std::uint32_t layout_id = 2,
    std::uint64_t now_ms = 1) {
    EXPECT(fixture.workflow.stage(
               request_id, layout(0, layout_id, 2), now_ms).projected());
    return fixture.workflow.confirm_and_activate(request_id, now_ms + 1);
}

void test_changed_activation_latches_exact_generation_and_semantics() {
    Fixture fixture{};
    fixture.start();
    const auto result = activate(fixture);
    EXPECT(result.model_activated());
    EXPECT(result.completed());
    EXPECT(!result.presentation_complete());
    EXPECT(result.presentation_pending);
    EXPECT(result.expected_generation == 1);
    EXPECT(fixture.workflow.status().presentation_pending);
    EXPECT(fixture.workflow.status().presentation_generation == 1);
    EXPECT(fixture.runtime.status().presentation_pending);
    EXPECT(fixture.runtime.status().presentation_generation == 1);
}

void test_pending_gate_blocks_all_mutation_before_token_or_write() {
    Fixture fixture{};
    fixture.start();
    EXPECT(activate(fixture).model_activated());
    const auto writes_before = writes(fixture.storage);
    EXPECT(fixture.workflow.stage(100, layout(0, 3), 3).operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(fixture.workflow.stage_restore_default(
               101, layout(0, 4), 3).operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> record{};
    EXPECT(configuration::encode_gauge_layout(
               layout(9, 5), record.data(), record.size()).succeeded());
    const auto imported = fixture.workflow.stage_import_record(
        102, record.data(), record.size(), 3);
    EXPECT(!imported.decoded());
    EXPECT(imported.workflow.operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    const auto confirmed = fixture.workflow.confirm_and_activate(103, 3);
    EXPECT(confirmed.error ==
           display::GaugeLayoutActivationWorkflowError::presentation_pending);
    EXPECT(!confirmed.persistence_attempted);
    EXPECT(fixture.workflow.cancel(104, 3).operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(fixture.workflow.service(200).operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(fixture.workflow.stop(3).operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(fixture.workflow.start({100}, 3).operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(writes(fixture.storage) == writes_before);

    EXPECT(fixture.workflow.service_presentation(3)
               .presentation_completed());
    EXPECT(fixture.workflow.stage(100, layout(0, 3), 4).projected());
    EXPECT(fixture.workflow.cancel(100, 4).projected());
    EXPECT(fixture.workflow.stop(5).projected());
    fixture.runtime.stop();
    EXPECT(fixture.runtime.start(runtime_configuration()).started());
    EXPECT(fixture.workflow.start({100}, 6).projected());
    EXPECT(fixture.dashboard.status().layout_load.source !=
           configuration::GaugeLayoutSource::safe_default);
    EXPECT(fixture.view.status().widget_count == 2);
    EXPECT(!fixture.workflow.status().presentation_pending);
    EXPECT(!fixture.workflow.service_presentation(6)
                .runtime_service_attempted);
}

void test_exact_receipt_is_one_shot_and_unblocks_changes() {
    Fixture fixture{};
    fixture.start();
    EXPECT(activate(fixture).model_activated());
    const auto receipt = fixture.workflow.service_presentation(3);
    EXPECT(receipt.presentation_completed());
    EXPECT(receipt.expected_generation == 1);
    EXPECT(receipt.runtime.tracked_frame_presented);
    EXPECT(receipt.runtime.presented_generation == 1);
    EXPECT(!fixture.workflow.status().presentation_pending);
    const auto duplicate = fixture.workflow.service_presentation(4);
    EXPECT(!duplicate.runtime_service_attempted);
    EXPECT(!duplicate.presentation_completed());
    EXPECT(duplicate.error ==
           display::GaugeLayoutActivationWorkflowError::invalid_state);
}

void test_busy_offer_and_service_failures_retain_then_recover() {
    Fixture busy{};
    busy.start();
    EXPECT(activate(busy).model_activated());
    busy.renderer.set_busy(true);
    auto cycle = busy.workflow.service_presentation(3);
    EXPECT(cycle.error ==
           display::GaugeLayoutActivationWorkflowError::presentation_failure);
    EXPECT(busy.workflow.status().presentation_pending);
    busy.renderer.set_busy(false);
    EXPECT(busy.workflow.service_presentation(4).presentation_completed());

    Fixture offer{};
    offer.start();
    EXPECT(activate(offer).model_activated());
    offer.renderer.fail_next_offer();
    cycle = offer.workflow.service_presentation(3);
    EXPECT(cycle.runtime.error ==
           display::GaugeRendererRuntimeError::renderer_offer_failure);
    EXPECT(offer.workflow.status().presentation_pending);
    EXPECT(offer.workflow.service_presentation(4).presentation_completed());

    Fixture service{};
    service.start();
    EXPECT(activate(service).model_activated());
    service.renderer.fail_next_service();
    cycle = service.workflow.service_presentation(3);
    EXPECT(cycle.runtime.error ==
           display::GaugeRendererRuntimeError::renderer_service_failure);
    EXPECT(service.runtime.status().has_renderer_frame_in_flight);
    EXPECT(service.workflow.status().presentation_pending);
    EXPECT(service.workflow.service_presentation(4).presentation_completed());
}

void test_exact_receipt_wins_over_unrelated_dashboard_error() {
    Fixture fixture{};
    fixture.start();
    EXPECT(activate(fixture).model_activated());
    fixture.renderer.fail_next_service();
    EXPECT(fixture.workflow.service_presentation(3).error ==
           display::GaugeLayoutActivationWorkflowError::presentation_failure);
    EXPECT(fixture.runtime.status().has_renderer_frame_in_flight);
    deliver_rpm(fixture.sender, 100);
    const auto receipt = fixture.workflow.service_presentation(4);
    EXPECT(receipt.runtime.error ==
           display::GaugeRendererRuntimeError::dashboard_service_failure);
    EXPECT(receipt.runtime.presentation_completed);
    EXPECT(receipt.presentation_completed());
    EXPECT(!fixture.workflow.status().presentation_pending);
}

void test_wrong_generation_requires_restart_but_spurious_retains_latch() {
    Fixture wrong{};
    wrong.start();
    EXPECT(activate(wrong).model_activated());
    EXPECT(wrong.store.save(layout(2, 9, 1)).saved());
    EXPECT(wrong.dashboard.activate_persisted_layout(2).activated());
    auto cycle = wrong.workflow.service_presentation(3);
    EXPECT(cycle.runtime.tracked_frame_presented);
    EXPECT(cycle.runtime.presented_generation == 2);
    EXPECT(!cycle.runtime.presentation_completed);
    EXPECT(cycle.error ==
           display::GaugeLayoutActivationWorkflowError::restart_required);
    EXPECT(cycle.restart_required);
    EXPECT(wrong.workflow.status().restart_required);
    EXPECT(!wrong.workflow.status().presentation_pending);

    Fixture spurious{};
    spurious.start();
    EXPECT(activate(spurious).model_activated());
    spurious.renderer.set_busy(true);
    spurious.renderer.report_spurious_presentation_next_service();
    cycle = spurious.workflow.service_presentation(3);
    EXPECT(cycle.runtime.renderer_service.frame_presented);
    EXPECT(!cycle.runtime.tracked_frame_presented);
    EXPECT(!cycle.runtime.presentation_completed);
    EXPECT(spurious.workflow.status().presentation_pending);
}

void test_clock_regression_mutates_neither_runtime_nor_gate() {
    Fixture fixture{};
    fixture.start();
    EXPECT(activate(fixture).model_activated());
    fixture.renderer.set_hold_presentations(true);
    EXPECT(fixture.workflow.service_presentation(10).error ==
           display::GaugeLayoutActivationWorkflowError::presentation_pending);
    const auto runtime_before = fixture.runtime.status();
    const auto gate_before = fixture.workflow.status();
    const auto rejected = fixture.workflow.service_presentation(9);
    EXPECT(rejected.runtime.error ==
           display::GaugeRendererRuntimeError::clock_regression);
    const auto runtime_after = fixture.runtime.status();
    const auto gate_after = fixture.workflow.status();
    EXPECT(runtime_after.last_service_time_ms ==
           runtime_before.last_service_time_ms);
    EXPECT(runtime_after.cycles_serviced == runtime_before.cycles_serviced);
    EXPECT(runtime_after.presentation_generation ==
           runtime_before.presentation_generation);
    EXPECT(gate_after.presentation_pending == gate_before.presentation_pending);
    EXPECT(gate_after.presentation_generation ==
           gate_before.presentation_generation);
    EXPECT(!gate_after.restart_required);
}

void test_activation_retry_transitions_to_presentation_gate() {
    FaultFixture fixture{};
    fixture.start();
    EXPECT(fixture.workflow.stage(1, layout(0, 2, 2), 1).projected());
    fixture.storage.fail_activation_read_after_next_write();
    const auto failed = fixture.workflow.confirm_and_activate(1, 2);
    EXPECT(failed.retry_required);
    EXPECT(fixture.workflow.status().activation_pending);
    const auto writes_before = fixture.storage.writes();
    const auto retried = fixture.workflow.retry_activation();
    EXPECT(retried.model_activated());
    EXPECT(retried.presentation_pending);
    EXPECT(!fixture.workflow.status().activation_pending);
    EXPECT(fixture.workflow.status().presentation_pending);
    EXPECT(fixture.workflow.status().presentation_generation == 1);
    EXPECT(fixture.storage.writes() == writes_before);
    EXPECT(fixture.workflow.service_presentation(3)
               .presentation_completed());
}

void test_metadata_only_requires_proof_but_exact_noop_does_not() {
    Fixture metadata{};
    metadata.start();
    EXPECT(metadata.workflow.stage(1, layout(0), 1).projected());
    const auto changed = metadata.workflow.confirm_and_activate(1, 2);
    EXPECT(changed.model_activated());
    EXPECT(!changed.activation.dashboard.layout_changed);
    EXPECT(changed.activation.dashboard.frame_metadata_changed);
    EXPECT(changed.presentation_pending);
    EXPECT(metadata.workflow.status().presentation_pending);

    Fixture noop{};
    EXPECT(noop.store.save(layout()).saved());
    noop.start();
    EXPECT(noop.workflow.stage(1, layout(0), 1).projected());
    const auto unchanged = noop.workflow.confirm_and_activate(1, 2);
    EXPECT(unchanged.model_activated());
    EXPECT(unchanged.presentation_complete());
    EXPECT(!unchanged.presentation_pending);
    EXPECT(!noop.workflow.status().presentation_pending);
    EXPECT(!noop.runtime.status().presentation_pending);
}

void test_direct_runtime_cannot_overwrite_pending_generation() {
    Fixture fixture{};
    fixture.start();
    EXPECT(activate(fixture).model_activated());
    EXPECT(fixture.store.save(layout(2, 8, 1)).saved());
    EXPECT(!fixture.runtime.layout_activation_ready());
    const auto rejected = fixture.runtime.activate_persisted_layout(2);
    EXPECT(!rejected.activated());
    EXPECT(rejected.error ==
           display::GaugeRendererRuntimeError::invalid_state);
    EXPECT(fixture.runtime.status().presentation_generation == 1);
    EXPECT(fixture.workflow.status().presentation_generation == 1);
}

void test_direct_runtime_service_or_stop_requires_reconstruction() {
    Fixture serviced{};
    serviced.start();
    EXPECT(activate(serviced).model_activated());
    EXPECT(serviced.runtime.service(3).presentation_completed);
    const auto writes_before = writes(serviced.storage);
    const auto divergent = serviced.workflow.service_presentation(4);
    EXPECT(!divergent.runtime_service_attempted);
    EXPECT(divergent.error ==
           display::GaugeLayoutActivationWorkflowError::restart_required);
    EXPECT(divergent.restart_required);
    EXPECT(!serviced.workflow.status().presentation_pending);
    EXPECT(serviced.workflow.status().restart_required);
    EXPECT(serviced.workflow.stage(2, layout(0, 3), 5).operation_error ==
           configuration::GaugeLayoutChangeError::invalid_state);
    EXPECT(serviced.workflow.confirm_and_activate(2, 5).error ==
           display::GaugeLayoutActivationWorkflowError::restart_required);
    EXPECT(writes(serviced.storage) == writes_before);

    Fixture stopped{};
    stopped.start();
    EXPECT(activate(stopped).model_activated());
    stopped.runtime.stop();
    const auto stopped_result = stopped.workflow.service_presentation(3);
    EXPECT(stopped_result.error ==
           display::GaugeLayoutActivationWorkflowError::restart_required);
    EXPECT(stopped_result.restart_required);
    EXPECT(stopped.workflow.status().restart_required);
}

}  // namespace

int main() {
    test_changed_activation_latches_exact_generation_and_semantics();
    test_pending_gate_blocks_all_mutation_before_token_or_write();
    test_exact_receipt_is_one_shot_and_unblocks_changes();
    test_busy_offer_and_service_failures_retain_then_recover();
    test_exact_receipt_wins_over_unrelated_dashboard_error();
    test_wrong_generation_requires_restart_but_spurious_retains_latch();
    test_clock_regression_mutates_neither_runtime_nor_gate();
    test_activation_retry_transitions_to_presentation_gate();
    test_metadata_only_requires_proof_but_exact_noop_does_not();
    test_direct_runtime_cannot_overwrite_pending_generation();
    test_direct_runtime_service_or_stop_requires_reconstruction();

    if (failures != 0) {
        std::cerr << failures
                  << " gauge layout presentation gate check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 11 gauge layout presentation gate scenario groups\n";
    return EXIT_SUCCESS;
}
