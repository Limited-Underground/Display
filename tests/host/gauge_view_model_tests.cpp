#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "fake_esp_now_transport.hpp"
#include "opengauge/gauge_view_model.hpp"

namespace {

using namespace opengauge;
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
    wireless::TelemetrySignalCode code =
        wireless::TelemetrySignalCode::engine_speed,
    display::GaugeWidgetKind kind = display::GaugeWidgetKind::numeric,
    std::string_view label = "Engine") {
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

wireless::WireTelemetrySignal signal(
    telemetry::SignalQuality quality,
    std::int64_t value = 1000000) {
    wireless::WireTelemetrySignal result{};
    const auto* descriptor = wireless::telemetry_signal_descriptor(
        wireless::TelemetrySignalCode::engine_speed);
    EXPECT(descriptor != nullptr);
    result.code = wireless::TelemetrySignalCode::engine_speed;
    result.value = {descriptor->value_type, value, true};
    result.unit = descriptor->unit;
    result.quality = quality;
    if (quality != telemetry::SignalQuality::valid &&
        quality != telemetry::SignalQuality::suspect) {
        result.value.raw_value = 0;
        result.value.present = false;
    }
    return result;
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

void deliver(
    FakeEspNowTransport& sender,
    const wireless::WireTelemetrySignal& state,
    std::uint32_t session,
    std::uint32_t sequence,
    std::uint64_t now_ms) {
    wireless::TelemetryBatch batch{};
    batch.gateway_id = 10;
    batch.boot_session_id = session;
    batch.sequence = sequence;
    batch.signal_count = 1;
    batch.signals[0] = state;
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               batch, encoded.data(), encoded.size()).encoded());
    EXPECT(sender.send(peer(2), {encoded.data(), encoded.size()}, now_ms)
               .accepted());
    sender.service(now_ms);
}

wireless::GaugeReceiverConfiguration receiver_configuration() {
    return {peer(1), 10, 6, 4};
}

void test_widget_validation_capacity_and_lifecycle() {
    FakeEspNowTransport transport{};
    wireless::GaugeTelemetryReceiver receiver{transport};
    display::GaugeViewModel view{receiver};
    EXPECT(view.start() == display::GaugeViewModelError::invalid_state);
    auto invalid = widget(0);
    EXPECT(view.add_widget(invalid) ==
           display::GaugeViewModelError::invalid_configuration);
    invalid = widget(1, wireless::TelemetrySignalCode::engine_speed,
                     display::GaugeWidgetKind::bar);
    invalid.scale_max_raw = 0;
    EXPECT(view.add_widget(invalid) ==
           display::GaugeViewModelError::invalid_configuration);
    invalid = widget(1);
    invalid.kind = display::GaugeWidgetKind::status;
    EXPECT(view.add_widget(invalid) ==
           display::GaugeViewModelError::invalid_configuration);
    EXPECT(display::make_gauge_widget_label("", invalid.label) ==
           display::GaugeViewModelError::invalid_configuration);

    for (std::uint16_t id = 1; id <= display::kMaximumGaugeWidgets; ++id) {
        EXPECT(view.add_widget(widget(id)) ==
               display::GaugeViewModelError::none);
    }
    EXPECT(view.add_widget(widget(1)) ==
           display::GaugeViewModelError::duplicate_widget);
    EXPECT(view.add_widget(widget(9)) ==
           display::GaugeViewModelError::widget_capacity_full);
    EXPECT(receiver.start(receiver_configuration()) ==
           wireless::GaugeReceiverError::none);
    EXPECT(view.start() == display::GaugeViewModelError::none);
    EXPECT(view.add_widget(widget(9)) ==
           display::GaugeViewModelError::invalid_state);
    EXPECT(view.clear_widgets() == display::GaugeViewModelError::invalid_state);
    view.stop();
    EXPECT(view.clear_widgets() == display::GaugeViewModelError::none);
}

void test_missing_signal_is_explicit_and_has_no_number() {
    FakeEspNowTransport transport{};
    wireless::GaugeTelemetryReceiver receiver{transport};
    EXPECT(receiver.start(receiver_configuration()) ==
           wireless::GaugeReceiverError::none);
    display::GaugeViewModel view{receiver};
    EXPECT(view.add_widget(widget(1)) == display::GaugeViewModelError::none);
    EXPECT(view.start() == display::GaugeViewModelError::none);
    std::array<display::GaugeWidgetSnapshot, 1> output{};
    const auto refresh = view.refresh(0, output.data(), output.size());
    EXPECT(refresh.refreshed());
    EXPECT(refresh.widget_count == 1);
    EXPECT(refresh.numeric_value_count == 0);
    EXPECT(refresh.attention_count == 1);
    EXPECT(output[0].state == display::GaugeValueState::missing);
    EXPECT(!output[0].display_value.present);
    EXPECT(output[0].display_value.type ==
           telemetry::SignalValueType::unsigned_integer);
    EXPECT(output[0].unit ==
           telemetry::SignalUnit::milli_revolutions_per_minute);
    EXPECT(output[0].attention_required);
}

void test_valid_and_suspect_values_preserve_numeric_state() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(receiver_configuration()) ==
           wireless::GaugeReceiverError::none);
    display::GaugeViewModel view{receiver};
    EXPECT(view.add_widget(widget(1)) == display::GaugeViewModelError::none);
    EXPECT(view.start() == display::GaugeViewModelError::none);
    std::array<display::GaugeWidgetSnapshot, 1> output{};

    deliver(sender, signal(telemetry::SignalQuality::valid), 1, 0, 0);
    EXPECT(receiver.service(0).packets_accepted == 1);
    auto refresh = view.refresh(0, output.data(), output.size());
    EXPECT(refresh.numeric_value_count == 1);
    EXPECT(refresh.attention_count == 0);
    EXPECT(output[0].state == display::GaugeValueState::valid);
    EXPECT(output[0].display_value.raw_value == 1000000);
    EXPECT(!output[0].attention_required);

    deliver(sender, signal(telemetry::SignalQuality::suspect, 900000),
            1, 1, 50);
    EXPECT(receiver.service(50).packets_accepted == 1);
    refresh = view.refresh(50, output.data(), output.size());
    EXPECT(refresh.numeric_value_count == 1);
    EXPECT(refresh.attention_count == 1);
    EXPECT(output[0].state == display::GaugeValueState::suspect);
    EXPECT(output[0].display_value.raw_value == 900000);
    EXPECT(output[0].attention_required);
}

void test_exact_stale_boundary_removes_numeric_value() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(receiver_configuration()) ==
           wireless::GaugeReceiverError::none);
    display::GaugeViewModel view{receiver};
    EXPECT(view.add_widget(widget(1)) == display::GaugeViewModelError::none);
    EXPECT(view.start() == display::GaugeViewModelError::none);
    std::array<display::GaugeWidgetSnapshot, 1> output{};
    deliver(sender, signal(telemetry::SignalQuality::valid), 1, 0, 0);
    EXPECT(receiver.service(0).packets_accepted == 1);
    EXPECT(view.refresh(99, output.data(), output.size()).refreshed());
    EXPECT(output[0].state == display::GaugeValueState::valid);
    EXPECT(output[0].display_value.present);
    EXPECT(view.refresh(100, output.data(), output.size()).refreshed());
    EXPECT(output[0].state == display::GaugeValueState::stale);
    EXPECT(!output[0].display_value.present);
    EXPECT(output[0].display_value.raw_value == 0);
    EXPECT(output[0].age_ms == 100);
}

void test_unavailable_error_and_session_metadata_are_projected() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(receiver_configuration()) ==
           wireless::GaugeReceiverError::none);
    display::GaugeViewModel view{receiver};
    EXPECT(view.add_widget(widget(1)) == display::GaugeViewModelError::none);
    EXPECT(view.start() == display::GaugeViewModelError::none);
    std::array<display::GaugeWidgetSnapshot, 1> output{};

    deliver(sender, signal(telemetry::SignalQuality::unavailable), 2, 0, 0);
    EXPECT(receiver.service(0).packets_accepted == 1);
    EXPECT(view.refresh(0, output.data(), output.size()).refreshed());
    EXPECT(output[0].state == display::GaugeValueState::unavailable);
    EXPECT(!output[0].display_value.present);
    EXPECT(output[0].boot_session_id == 2);
    EXPECT(output[0].packet_sequence == 0);

    deliver(sender, signal(telemetry::SignalQuality::error), 2, 1, 50);
    EXPECT(receiver.service(50).packets_accepted == 1);
    EXPECT(view.refresh(50, output.data(), output.size()).refreshed());
    EXPECT(output[0].state == display::GaugeValueState::error);
    EXPECT(!output[0].display_value.present);
    EXPECT(output[0].packet_sequence == 1);
}

void test_multiple_widgets_share_signal_without_aliasing_configuration() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(receiver_configuration()) ==
           wireless::GaugeReceiverError::none);
    display::GaugeViewModel view{receiver};
    EXPECT(view.add_widget(widget(1)) == display::GaugeViewModelError::none);
    EXPECT(view.add_widget(widget(
               2,
               wireless::TelemetrySignalCode::engine_speed,
               display::GaugeWidgetKind::bar,
               "RPM Bar")) == display::GaugeViewModelError::none);
    EXPECT(view.start() == display::GaugeViewModelError::none);
    std::array<display::GaugeWidgetSnapshot, 2> output{};
    deliver(sender, signal(telemetry::SignalQuality::valid), 1, 0, 0);
    EXPECT(receiver.service(0).packets_accepted == 1);
    const auto refresh = view.refresh(0, output.data(), output.size());
    EXPECT(refresh.refreshed());
    EXPECT(refresh.numeric_value_count == 2);
    EXPECT(output[0].widget_id == 1);
    EXPECT(output[1].widget_id == 2);
    EXPECT(output[0].kind == display::GaugeWidgetKind::numeric);
    EXPECT(output[1].kind == display::GaugeWidgetKind::bar);
    EXPECT(output[1].scale_max_raw == 3000000);
    EXPECT(output[0].display_value.raw_value ==
           output[1].display_value.raw_value);
}

void test_output_capacity_and_receiver_failure_leave_output_unchanged() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(receiver_configuration()) ==
           wireless::GaugeReceiverError::none);
    display::GaugeViewModel view{receiver};
    EXPECT(view.add_widget(widget(1)) == display::GaugeViewModelError::none);
    EXPECT(view.start() == display::GaugeViewModelError::none);
    std::array<display::GaugeWidgetSnapshot, 1> output{};
    output[0].widget_id = 99;
    EXPECT(view.refresh(0, output.data(), 0).error ==
           display::GaugeViewModelError::insufficient_output_capacity);
    EXPECT(output[0].widget_id == 99);

    deliver(sender, signal(telemetry::SignalQuality::valid), 1, 0, 100);
    EXPECT(receiver.service(100).packets_accepted == 1);
    const auto failed = view.refresh(99, output.data(), output.size());
    EXPECT(failed.error == display::GaugeViewModelError::receiver_failure);
    EXPECT(failed.receiver_error ==
           wireless::GaugeReceiverError::freshness_failure);
    EXPECT(output[0].widget_id == 99);
    EXPECT(view.status().receiver_failures == 1);
}

void test_running_widget_replacement_is_atomic_and_preserves_counters() {
    FakeEspNowTransport transport{};
    wireless::GaugeTelemetryReceiver receiver{transport};
    EXPECT(receiver.start(receiver_configuration()) ==
           wireless::GaugeReceiverError::none);
    display::GaugeViewModel view{receiver};
    EXPECT(view.add_widget(widget(1)) == display::GaugeViewModelError::none);
    EXPECT(view.start() == display::GaugeViewModelError::none);
    std::array<display::GaugeWidgetSnapshot, 2> output{};
    EXPECT(view.refresh(0, output.data(), output.size()).refreshed());
    const auto before = view.status();

    std::array<display::GaugeWidgetConfiguration, 2> invalid{{
        widget(2), widget(2)}};
    EXPECT(view.replace_widgets(invalid.data(), invalid.size()) ==
           display::GaugeViewModelError::duplicate_widget);
    auto after = view.status();
    EXPECT(after.running);
    EXPECT(after.widget_count == 1);
    EXPECT(after.refreshes_completed == before.refreshes_completed);
    EXPECT(view.refresh(1, output.data(), output.size()).refreshed());
    EXPECT(output[0].widget_id == 1);

    invalid[1].widget_id = 3;
    invalid[1].stale_after_ms = 0;
    const auto before_invalid = view.status();
    EXPECT(view.replace_widgets(invalid.data(), invalid.size()) ==
           display::GaugeViewModelError::invalid_configuration);
    after = view.status();
    EXPECT(after.widget_count == 1);
    EXPECT(after.refreshes_completed == before_invalid.refreshes_completed);

    std::array<display::GaugeWidgetConfiguration,
               display::kMaximumGaugeWidgets + 1> oversized{};
    EXPECT(view.replace_widgets(oversized.data(), oversized.size()) ==
           display::GaugeViewModelError::widget_capacity_full);
    EXPECT(view.replace_widgets(nullptr, 1) ==
           display::GaugeViewModelError::invalid_configuration);
    EXPECT(view.replace_widgets(invalid.data(), 0) ==
           display::GaugeViewModelError::invalid_configuration);

    std::array<display::GaugeWidgetConfiguration, 2> replacement{{
        widget(10),
        widget(
            11,
            wireless::TelemetrySignalCode::engine_speed,
            display::GaugeWidgetKind::bar,
            "RPM Bar")}};
    const auto before_success = view.status();
    EXPECT(view.replace_widgets(replacement.data(), replacement.size()) ==
           display::GaugeViewModelError::none);
    after = view.status();
    EXPECT(after.running);
    EXPECT(after.widget_count == 2);
    EXPECT(after.refreshes_completed == before_success.refreshes_completed);
    EXPECT(after.receiver_failures == before_success.receiver_failures);
    EXPECT(view.refresh(2, output.data(), output.size()).refreshed());
    EXPECT(output[0].widget_id == 10);
    EXPECT(output[1].widget_id == 11);

    const auto single = widget(12);
    EXPECT(view.replace_widgets(&single, 1) ==
           display::GaugeViewModelError::none);
    EXPECT(view.refresh(3, output.data(), 1).refreshed());
    EXPECT(output[0].widget_id == 12);
}

}  // namespace

int main() {
    test_widget_validation_capacity_and_lifecycle();
    test_missing_signal_is_explicit_and_has_no_number();
    test_valid_and_suspect_values_preserve_numeric_state();
    test_exact_stale_boundary_removes_numeric_value();
    test_unavailable_error_and_session_metadata_are_projected();
    test_multiple_widgets_share_signal_without_aliasing_configuration();
    test_output_capacity_and_receiver_failure_leave_output_unchanged();
    test_running_widget_replacement_is_atomic_and_preserves_counters();

    if (failures != 0) {
        std::cerr << failures << " gauge view-model assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 gauge view-model scenario groups\n";
    return EXIT_SUCCESS;
}
