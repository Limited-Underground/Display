#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "fake_esp_now_transport.hpp"
#include "fake_gauge_layout_storage.hpp"
#include "opengauge/gauge_dashboard_loop.hpp"

namespace {

using namespace opengauge;
using configuration::test_support::FakeGaugeLayoutStorage;
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
    std::uint64_t stale_after_ms = 100) {
    display::GaugeWidgetConfiguration result{};
    result.widget_id = id;
    result.signal_code = code;
    result.kind = kind;
    EXPECT(display::make_gauge_widget_label("Gauge", result.label) ==
           display::GaugeViewModelError::none);
    result.stale_after_ms = stale_after_ms;
    if (kind == display::GaugeWidgetKind::needle ||
        kind == display::GaugeWidgetKind::bar) {
        result.scale_min_raw = -100000;
        result.scale_max_raw = 5000000;
    }
    return result;
}

configuration::GaugeLayout layout(
    std::uint64_t generation,
    std::uint32_t layout_id = 0x12340001U,
    std::uint8_t widget_count = 3,
    std::uint64_t stale_after_ms = 100) {
    constexpr std::array<wireless::TelemetrySignalCode, 4> codes{{
        wireless::TelemetrySignalCode::engine_speed,
        wireless::TelemetrySignalCode::engine_coolant_temperature,
        wireless::TelemetrySignalCode::vehicle_speed,
        wireless::TelemetrySignalCode::electrical_voltage,
    }};
    constexpr std::array<display::GaugeWidgetKind, 3> kinds{{
        display::GaugeWidgetKind::numeric,
        display::GaugeWidgetKind::needle,
        display::GaugeWidgetKind::bar,
    }};
    configuration::GaugeLayout result{};
    result.generation = generation;
    result.layout_id = layout_id;
    result.brightness_percent = 72;
    result.theme = configuration::GaugeTheme::high_contrast;
    result.widget_count = widget_count;
    for (std::size_t index = 0; index < widget_count; ++index) {
        result.widgets[index] = widget(
            static_cast<std::uint16_t>(101 + index),
            codes[index % codes.size()],
            kinds[index % kinds.size()],
            stale_after_ms);
    }
    return result;
}

wireless::WireTelemetrySignal signal(
    wireless::TelemetrySignalCode code,
    std::int64_t raw_value,
    std::uint32_t source_age_ms = 0) {
    wireless::WireTelemetrySignal result{};
    const auto* descriptor = wireless::telemetry_signal_descriptor(code);
    EXPECT(descriptor != nullptr);
    result.code = code;
    result.value = {descriptor->value_type, raw_value, true};
    result.unit = descriptor->unit;
    result.quality = telemetry::SignalQuality::valid;
    result.source_age_ms = source_age_ms;
    return result;
}

wireless::TelemetryBatch batch(
    std::uint32_t session,
    std::uint32_t sequence,
    const wireless::WireTelemetrySignal* signals,
    std::size_t signal_count) {
    wireless::TelemetryBatch result{};
    result.gateway_id = 10;
    result.boot_session_id = session;
    result.sequence = sequence;
    result.signal_count = static_cast<std::uint8_t>(signal_count);
    for (std::size_t index = 0; index < signal_count; ++index) {
        result.signals[index] = signals[index];
    }
    return result;
}

std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encode(
    const wireless::TelemetryBatch& telemetry) {
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               telemetry, encoded.data(), encoded.size()).encoded());
    return encoded;
}

class ObservingTransport final : public wireless::EspNowTransport {
public:
    explicit ObservingTransport(FakeEspNowTransport& delegate)
        : delegate_(delegate) {}

    [[nodiscard]] std::size_t mtu() const override {
        return delegate_.mtu();
    }
    [[nodiscard]] wireless::EspNowStatus status() const override {
        return delegate_.status();
    }
    wireless::EspNowError start(
        const wireless::PeerAddress& local,
        wireless::EspNowPolicy policy) override {
        return delegate_.start(local, policy);
    }
    void stop() override { delegate_.stop(); }
    wireless::EspNowError add_peer(
        const wireless::PeerConfiguration& peer_configuration) override {
        return delegate_.add_peer(peer_configuration);
    }
    wireless::EspNowError remove_peer(
        const wireless::PeerAddress& address) override {
        return delegate_.remove_peer(address);
    }
    wireless::SendResult send(
        const wireless::PeerAddress& destination,
        wireless::ByteView payload,
        std::uint64_t now_ms) override {
        return delegate_.send(destination, payload, now_ms);
    }
    wireless::ReceiveResult receive(
        wireless::MutableByteView destination) override {
        if (next_receive_error_ != wireless::EspNowError::none) {
            const auto error = next_receive_error_;
            next_receive_error_ = wireless::EspNowError::none;
            return {error};
        }
        if (injected_) {
            if (destination.data == nullptr || destination.size < injected_size_) {
                return {wireless::EspNowError::buffer_too_small};
            }
            std::copy(
                injected_bytes_.begin(),
                injected_bytes_.begin() + injected_size_,
                destination.data);
            injected_ = false;
            return {
                wireless::EspNowError::none,
                injected_size_,
                injected_metadata_};
        }
        return delegate_.receive(destination);
    }
    wireless::DeliveryResult poll_delivery() override {
        return delegate_.poll_delivery();
    }
    void service(std::uint64_t now_ms) override {
        ++service_calls_;
        delegate_.service(now_ms);
    }

    void inject(
        const std::uint8_t* bytes,
        std::size_t size,
        wireless::ReceiveMetadata metadata) {
        EXPECT(bytes != nullptr);
        EXPECT(size <= injected_bytes_.size());
        if (bytes == nullptr || size > injected_bytes_.size()) {
            return;
        }
        std::copy(bytes, bytes + size, injected_bytes_.begin());
        injected_size_ = size;
        injected_metadata_ = metadata;
        injected_ = true;
    }

    void fail_next_receive(
        wireless::EspNowError error = wireless::EspNowError::io_failure) {
        next_receive_error_ = error;
    }

    [[nodiscard]] std::uint32_t service_calls() const {
        return service_calls_;
    }

private:
    FakeEspNowTransport& delegate_;
    std::array<std::uint8_t, wireless::kMaximumEspNowPayloadBytes>
        injected_bytes_{};
    std::size_t injected_size_{0};
    wireless::ReceiveMetadata injected_metadata_{};
    wireless::EspNowError next_receive_error_{wireless::EspNowError::none};
    std::uint32_t service_calls_{0};
    bool injected_{false};
};

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
    const wireless::TelemetryBatch& telemetry,
    std::uint64_t now_ms) {
    const auto encoded = encode(telemetry);
    EXPECT(sender.send(peer(2), {encoded.data(), encoded.size()}, now_ms)
               .accepted());
    sender.service(now_ms);
}

display::GaugeDashboardLoopConfiguration loop_configuration(
    const configuration::GaugeLayout& safe_default,
    std::uint8_t budget = 4) {
    return {safe_default, {peer(1), 10, 6, budget}};
}

struct LoopFixture {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    ObservingTransport transport{gauge};
    wireless::GaugeTelemetryReceiver receiver{transport};
    display::GaugeViewModel view{receiver};
    display::GaugeDashboardLoop loop{store, receiver, view};

    LoopFixture() { start_transport_pair(sender, gauge); }
};

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

wireless::ReceiveMetadata metadata(
    wireless::PeerAddress source,
    std::uint64_t received_at_ms,
    bool encrypted = true) {
    wireless::ReceiveMetadata result{};
    result.source = source;
    result.received_at_ms = received_at_ms;
    result.channel = 6;
    result.encrypted = encrypted;
    return result;
}

bool same_widget(
    const display::GaugeWidgetSnapshot& left,
    const display::GaugeWidgetSnapshot& right) {
    return left.widget_id == right.widget_id &&
           left.signal_code == right.signal_code &&
           left.kind == right.kind &&
           left.label.length == right.label.length &&
           left.label.bytes == right.label.bytes &&
           left.state == right.state &&
           left.display_value.type == right.display_value.type &&
           left.display_value.raw_value == right.display_value.raw_value &&
           left.display_value.present == right.display_value.present &&
           left.unit == right.unit &&
           left.age_ms == right.age_ms &&
           left.boot_session_id == right.boot_session_id &&
           left.packet_sequence == right.packet_sequence &&
           left.scale_min_raw == right.scale_min_raw &&
           left.scale_max_raw == right.scale_max_raw &&
           left.attention_required == right.attention_required;
}

bool same_frame(
    const display::GaugeDashboardFrame& left,
    const display::GaugeDashboardFrame& right) {
    if (left.publication_sequence != right.publication_sequence ||
        left.published_at_ms != right.published_at_ms ||
        left.layout_generation != right.layout_generation ||
        left.layout_id != right.layout_id ||
        left.layout_source != right.layout_source ||
        left.theme != right.theme ||
        left.brightness_percent != right.brightness_percent ||
        left.widget_count != right.widget_count ||
        left.recovery_required != right.recovery_required) {
        return false;
    }
    for (std::size_t index = 0; index < left.widgets.size(); ++index) {
        if (!same_widget(left.widgets[index], right.widgets[index])) {
            return false;
        }
    }
    return true;
}

void test_lifecycle_validation_and_failed_start_rollback() {
    LoopFixture fixture{};
    display::GaugeDashboardFrame untouched{};
    untouched.layout_id = 99;
    EXPECT(fixture.loop.service(0).error ==
           display::GaugeDashboardLoopError::invalid_state);
    EXPECT(!fixture.loop.copy_frame(untouched));
    EXPECT(untouched.layout_id == 99);

    auto invalid = loop_configuration(layout(1));
    invalid.receiver.maximum_packets_per_cycle = 0;
    EXPECT(fixture.loop.start(invalid).error ==
           display::GaugeDashboardLoopError::invalid_configuration);
    EXPECT(!fixture.receiver.status().running);
    EXPECT(!fixture.view.status().running);

    const auto started = fixture.loop.start(loop_configuration(layout(1)));
    EXPECT(started.started());
    EXPECT(fixture.loop.start(loop_configuration(layout(1))).error ==
           display::GaugeDashboardLoopError::invalid_state);
    EXPECT(fixture.loop.service(0).published());
    EXPECT(fixture.transport.service_calls() == 1);
    fixture.loop.stop();
    EXPECT(!fixture.loop.status().running);
    EXPECT(!fixture.receiver.status().running);
    EXPECT(!fixture.view.status().running);
    EXPECT(!fixture.loop.copy_frame(untouched));

    fixture.storage.fail_next_read(0);
    const auto failed = fixture.loop.start(loop_configuration(layout(1)));
    EXPECT(failed.error ==
           display::GaugeDashboardLoopError::layout_load_failure);
    EXPECT(!fixture.receiver.status().running);
    EXPECT(!fixture.view.status().running);
    EXPECT(!fixture.loop.copy_frame(untouched));
}

void test_mismatched_receiver_view_binding_fails_before_storage_or_start() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    FakeEspNowTransport transport{};
    wireless::GaugeTelemetryReceiver receiver{transport};
    wireless::GaugeTelemetryReceiver other_receiver{transport};
    display::GaugeViewModel mismatched_view{other_receiver};
    display::GaugeDashboardLoop mismatched{
        store, receiver, mismatched_view};

    storage.fail_next_read(0);
    const auto rejected = mismatched.start(loop_configuration(layout(1)));
    EXPECT(rejected.error ==
           display::GaugeDashboardLoopError::receiver_view_mismatch);
    EXPECT(!receiver.status().running);
    EXPECT(!other_receiver.status().running);
    EXPECT(!mismatched_view.status().running);

    display::GaugeViewModel correct_view{receiver};
    display::GaugeDashboardLoop correct{store, receiver, correct_view};
    const auto load_failed = correct.start(loop_configuration(layout(1)));
    EXPECT(load_failed.error ==
           display::GaugeDashboardLoopError::layout_load_failure);
    EXPECT(!receiver.status().running);
    EXPECT(!correct_view.status().running);
}

void test_layout_selection_safe_newest_degraded_and_fail_closed() {
    LoopFixture empty{};
    auto result = empty.loop.start(loop_configuration(layout(1)));
    EXPECT(result.started());
    EXPECT(result.layout_load.source ==
           configuration::GaugeLayoutSource::safe_default);
    EXPECT(result.layout_load.recovery_required);
    empty.loop.stop();

    LoopFixture newest{};
    EXPECT(newest.store.save(layout(2, 2)).saved());
    EXPECT(newest.store.save(layout(3, 3)).saved());
    result = newest.loop.start(loop_configuration(layout(1)));
    EXPECT(result.started());
    EXPECT(result.layout_load.source == configuration::GaugeLayoutSource::slot_b);
    EXPECT(!result.layout_load.recovery_required);
    EXPECT(newest.loop.service(0).published());
    display::GaugeDashboardFrame frame{};
    EXPECT(newest.loop.copy_frame(frame));
    EXPECT(frame.layout_generation == 3 && frame.layout_id == 3);

    LoopFixture degraded{};
    EXPECT(degraded.store.save(layout(4, 4)).saved());
    result = degraded.loop.start(loop_configuration(layout(1)));
    EXPECT(result.started());
    EXPECT(result.layout_load.recovery_required);
    EXPECT(degraded.loop.service(0).published());
    EXPECT(degraded.loop.copy_frame(frame));
    EXPECT(frame.recovery_required && frame.layout_generation == 4);

    LoopFixture conflict{};
    auto first = layout(5, 5);
    auto second = layout(5, 6);
    write_layout(conflict.storage, 0, first);
    write_layout(conflict.storage, 1, second);
    result = conflict.loop.start(loop_configuration(layout(1)));
    EXPECT(result.error ==
           display::GaugeDashboardLoopError::layout_load_failure);
    EXPECT(!conflict.receiver.status().running);

    LoopFixture corrupt{};
    write_layout(corrupt.storage, 0, layout(2));
    corrupt.storage.corrupt(0, 100, 0x44U);
    result = corrupt.loop.start(loop_configuration(layout(1)));
    EXPECT(result.error ==
           display::GaugeDashboardLoopError::no_usable_layout);
    EXPECT(!corrupt.receiver.status().running);
}

void test_fixed_frame_metadata_and_eight_widget_order() {
    LoopFixture fixture{};
    const auto selected = layout(1, 0xAABBCCDDU, 8);
    EXPECT(fixture.loop.start(loop_configuration(selected, 1)).started());
    const auto cycle = fixture.loop.service(10);
    EXPECT(cycle.published());
    EXPECT(cycle.receiver.error == wireless::GaugeReceiverError::no_data);
    EXPECT(fixture.transport.service_calls() == 1);

    display::GaugeDashboardFrame frame{};
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.publication_sequence == 1);
    EXPECT(frame.published_at_ms == 10);
    EXPECT(frame.layout_generation == 1);
    EXPECT(frame.layout_id == 0xAABBCCDDU);
    EXPECT(frame.theme == configuration::GaugeTheme::high_contrast);
    EXPECT(frame.brightness_percent == 72);
    EXPECT(frame.widget_count == 8);
    for (std::size_t index = 0; index < frame.widget_count; ++index) {
        EXPECT(frame.widgets[index].widget_id == 101 + index);
        EXPECT(frame.widgets[index].state == display::GaugeValueState::missing);
    }
}

void test_real_decoder_receiver_flow_to_numeric_needle_and_bar() {
    LoopFixture fixture{};
    const auto selected = layout(1, 1, 3);
    EXPECT(fixture.loop.start(loop_configuration(selected)).started());
    const std::array<wireless::WireTelemetrySignal, 3> signals{{
        signal(wireless::TelemetrySignalCode::engine_speed, 1500000),
        signal(wireless::TelemetrySignalCode::engine_coolant_temperature, 85000),
        signal(wireless::TelemetrySignalCode::vehicle_speed, 55000),
    }};
    deliver(fixture.sender, batch(1, 0, signals.data(), signals.size()), 100);
    const auto cycle = fixture.loop.service(100);
    EXPECT(cycle.published());
    EXPECT(cycle.receiver.packets_accepted == 1);
    EXPECT(cycle.receiver.signals_updated == 3);

    display::GaugeDashboardFrame frame{};
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].kind == display::GaugeWidgetKind::numeric);
    EXPECT(frame.widgets[1].kind == display::GaugeWidgetKind::needle);
    EXPECT(frame.widgets[2].kind == display::GaugeWidgetKind::bar);
    EXPECT(frame.widgets[0].display_value.raw_value == 1500000);
    EXPECT(frame.widgets[1].display_value.raw_value == 85000);
    EXPECT(frame.widgets[2].display_value.raw_value == 55000);
    EXPECT(frame.widgets[0].state == display::GaugeValueState::valid);
}

void test_exact_stale_boundary_removes_value() {
    LoopFixture fixture{};
    const auto selected = layout(1, 1, 1, 100);
    EXPECT(fixture.loop.start(loop_configuration(selected)).started());
    const auto speed = signal(
        wireless::TelemetrySignalCode::engine_speed, 900000, 20);
    deliver(fixture.sender, batch(1, 0, &speed, 1), 100);
    EXPECT(fixture.loop.service(100).published());
    EXPECT(fixture.loop.service(179).published());
    display::GaugeDashboardFrame frame{};
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].age_ms == 99);
    EXPECT(frame.widgets[0].state == display::GaugeValueState::valid);
    EXPECT(frame.widgets[0].display_value.present);

    EXPECT(fixture.loop.service(180).published());
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].age_ms == 100);
    EXPECT(frame.widgets[0].state == display::GaugeValueState::stale);
    EXPECT(!frame.widgets[0].display_value.present);
    EXPECT(frame.widgets[0].display_value.raw_value == 0);
}

void test_rejections_are_typed_counted_and_do_not_overwrite_good_state() {
    LoopFixture fixture{};
    EXPECT(fixture.loop.start(loop_configuration(layout(1, 1, 1))).started());
    const auto original = signal(
        wireless::TelemetrySignalCode::engine_speed, 1000000);
    deliver(fixture.sender, batch(1, 0, &original, 1), 100);
    EXPECT(fixture.loop.service(100).published());

    const auto replacement = signal(
        wireless::TelemetrySignalCode::engine_speed, 2000000);
    const auto replacement_bytes = encode(batch(1, 1, &replacement, 1));
    fixture.transport.inject(
        replacement_bytes.data(), replacement_bytes.size(),
        metadata(peer(3), 101));
    auto cycle = fixture.loop.service(101);
    EXPECT(cycle.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);
    EXPECT(cycle.receiver.error ==
           wireless::GaugeReceiverError::unauthorized_source);
    EXPECT(cycle.receiver.unauthorized_datagrams == 1);
    EXPECT(cycle.published());

    fixture.transport.inject(
        replacement_bytes.data(), replacement_bytes.size(),
        metadata(peer(1), 102, false));
    cycle = fixture.loop.service(102);
    EXPECT(cycle.receiver.error ==
           wireless::GaugeReceiverError::encryption_required);
    EXPECT(cycle.receiver.unencrypted_datagrams == 1);
    EXPECT(cycle.published());

    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> malformed{};
    malformed.fill(0xA5U);
    fixture.transport.inject(
        malformed.data(), malformed.size(), metadata(peer(1), 103));
    cycle = fixture.loop.service(103);
    EXPECT(cycle.receiver.error == wireless::GaugeReceiverError::packet_failure);
    EXPECT(cycle.receiver.malformed_packets == 1);
    EXPECT(cycle.published());

    fixture.transport.fail_next_receive();
    cycle = fixture.loop.service(104);
    EXPECT(cycle.receiver.error ==
           wireless::GaugeReceiverError::transport_failure);
    EXPECT(cycle.receiver.transport_error == wireless::EspNowError::io_failure);
    EXPECT(cycle.published());

    display::GaugeDashboardFrame frame{};
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].display_value.raw_value == 1000000);
    EXPECT(frame.widgets[0].packet_sequence == 0);
    const auto status = fixture.loop.status();
    EXPECT(status.receiver.unauthorized_datagrams == 1);
    EXPECT(status.receiver.unencrypted_datagrams == 1);
    EXPECT(status.receiver.malformed_packets == 1);
    EXPECT(status.receiver_cycle_failures == 4);

    EXPECT(fixture.loop.service(200).published());
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].state == display::GaugeValueState::stale);
    EXPECT(!frame.widgets[0].display_value.present);
}

void test_receiver_regressions_refresh_and_good_packet_in_cycle_wins() {
    LoopFixture fixture{};
    EXPECT(fixture.loop.start(loop_configuration(layout(1, 1, 1))).started());
    auto speed = signal(wireless::TelemetrySignalCode::engine_speed, 1000000);
    deliver(fixture.sender, batch(1, 0, &speed, 1), 100);
    EXPECT(fixture.loop.service(100).published());

    speed.value.raw_value = 1100000;
    const auto regressed = encode(batch(1, 1, &speed, 1));
    fixture.transport.inject(
        regressed.data(), regressed.size(), metadata(peer(1), 99));
    speed.value.raw_value = 1200000;
    deliver(fixture.sender, batch(1, 1, &speed, 1), 110);
    auto cycle = fixture.loop.service(110);
    EXPECT(cycle.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);
    EXPECT(cycle.receiver.stream_error ==
           wireless::TelemetryStreamError::clock_regressed);
    EXPECT(cycle.receiver.stream_failures == 1);
    EXPECT(cycle.receiver.packets_accepted == 1);
    EXPECT(cycle.published());
    display::GaugeDashboardFrame frame{};
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].display_value.raw_value == 1200000);
    EXPECT(frame.widgets[0].packet_sequence == 1);

    speed.value.raw_value = 1300000;
    const auto repeatedly_regressed = encode(batch(1, 2, &speed, 1));
    fixture.transport.inject(
        repeatedly_regressed.data(), repeatedly_regressed.size(),
        metadata(peer(1), 109));
    cycle = fixture.loop.service(160);
    EXPECT(cycle.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);
    EXPECT(cycle.published());
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].state == display::GaugeValueState::valid);
    EXPECT(frame.widgets[0].age_ms == 50);
    EXPECT(frame.widgets[0].display_value.raw_value == 1200000);

    fixture.transport.inject(
        repeatedly_regressed.data(), repeatedly_regressed.size(),
        metadata(peer(1), 109));
    cycle = fixture.loop.service(210);
    EXPECT(cycle.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);
    EXPECT(cycle.published());
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].state == display::GaugeValueState::stale);
    EXPECT(frame.widgets[0].age_ms == 100);
    EXPECT(!frame.widgets[0].display_value.present);
    EXPECT(frame.widgets[0].packet_sequence == 1);
}

void test_refresh_failure_preserves_frame_semantically() {
    LoopFixture fixture{};
    EXPECT(fixture.loop.start(loop_configuration(layout(1, 1, 1))).started());
    const auto speed = signal(
        wireless::TelemetrySignalCode::engine_speed, 1000000);
    deliver(fixture.sender, batch(1, 0, &speed, 1), 100);
    EXPECT(fixture.loop.service(100).published());
    display::GaugeDashboardFrame before{};
    EXPECT(fixture.loop.copy_frame(before));

    const auto future = signal(
        wireless::TelemetrySignalCode::engine_speed, 2000000);
    const auto future_bytes = encode(batch(1, 1, &future, 1));
    fixture.transport.inject(
        future_bytes.data(), future_bytes.size(),
        metadata(peer(1), 200));
    auto cycle = fixture.loop.service(101);
    EXPECT(cycle.error ==
           display::GaugeDashboardLoopError::view_refresh_failure);
    EXPECT(cycle.refresh.receiver_error ==
           wireless::GaugeReceiverError::freshness_failure);
    EXPECT(cycle.receiver.packets_accepted == 1);
    EXPECT(!cycle.published());
    display::GaugeDashboardFrame after{};
    EXPECT(fixture.loop.copy_frame(after));
    EXPECT(same_frame(before, after));

    cycle = fixture.loop.service(200);
    EXPECT(cycle.published());
    EXPECT(fixture.loop.copy_frame(after));
    EXPECT(after.widgets[0].display_value.raw_value == 2000000);
    EXPECT(after.widgets[0].packet_sequence == 1);
}

void test_caller_time_regression_fails_before_receiver_with_no_signals() {
    LoopFixture fixture{};
    EXPECT(fixture.loop.start(loop_configuration(layout(1, 1, 1))).started());
    EXPECT(fixture.loop.service(100).published());
    EXPECT(fixture.transport.service_calls() == 1);
    display::GaugeDashboardFrame before{};
    EXPECT(fixture.loop.copy_frame(before));

    const auto cycle = fixture.loop.service(99);
    EXPECT(cycle.error == display::GaugeDashboardLoopError::clock_regression);
    EXPECT(!cycle.published());
    EXPECT(fixture.transport.service_calls() == 1);
    display::GaugeDashboardFrame after{};
    EXPECT(fixture.loop.copy_frame(after));
    EXPECT(same_frame(before, after));
    EXPECT(fixture.loop.status().clock_regressions == 1);
    EXPECT(fixture.loop.status().cycles_serviced == 1);
}

void test_gateway_session_change_removes_absent_prior_values() {
    LoopFixture fixture{};
    auto selected = layout(1, 1, 2);
    selected.widgets[1] = widget(
        102, wireless::TelemetrySignalCode::electrical_voltage,
        display::GaugeWidgetKind::numeric);
    EXPECT(fixture.loop.start(loop_configuration(selected)).started());
    const std::array<wireless::WireTelemetrySignal, 2> first{{
        signal(wireless::TelemetrySignalCode::engine_speed, 1000000),
        signal(wireless::TelemetrySignalCode::electrical_voltage, 13800),
    }};
    deliver(fixture.sender, batch(1, 0, first.data(), first.size()), 10);
    EXPECT(fixture.loop.service(10).published());

    const auto speed = signal(
        wireless::TelemetrySignalCode::engine_speed, 900000);
    deliver(fixture.sender, batch(2, 0, &speed, 1), 20);
    const auto cycle = fixture.loop.service(20);
    EXPECT(cycle.receiver.session_resets == 1);
    EXPECT(cycle.published());
    display::GaugeDashboardFrame frame{};
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.widgets[0].boot_session_id == 2);
    EXPECT(frame.widgets[0].display_value.raw_value == 900000);
    EXPECT(frame.widgets[1].state == display::GaugeValueState::missing);
    EXPECT(!frame.widgets[1].display_value.present);
}

void test_stop_restart_clears_runtime_and_reloads_persisted_layout() {
    LoopFixture fixture{};
    EXPECT(fixture.store.save(layout(2, 20, 2)).saved());
    EXPECT(fixture.loop.start(loop_configuration(layout(1))).started());
    EXPECT(fixture.loop.service(0).published());
    display::GaugeDashboardFrame frame{};
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.layout_generation == 2 && frame.layout_id == 20);

    fixture.loop.stop();
    EXPECT(!fixture.loop.copy_frame(frame));
    auto changed = layout(3, 30, 1);
    changed.theme = configuration::GaugeTheme::light;
    EXPECT(fixture.store.save(changed).saved());
    EXPECT(fixture.loop.start(loop_configuration(layout(1))).started());
    EXPECT(!fixture.loop.copy_frame(frame));
    EXPECT(fixture.loop.service(1).published());
    EXPECT(fixture.loop.copy_frame(frame));
    EXPECT(frame.publication_sequence == 1);
    EXPECT(frame.layout_generation == 3 && frame.layout_id == 30);
    EXPECT(frame.theme == configuration::GaugeTheme::light);
    EXPECT(frame.widget_count == 1);
    EXPECT(fixture.receiver.status().signal_count == 0);
}

}  // namespace

int main() {
    test_lifecycle_validation_and_failed_start_rollback();
    test_mismatched_receiver_view_binding_fails_before_storage_or_start();
    test_layout_selection_safe_newest_degraded_and_fail_closed();
    test_fixed_frame_metadata_and_eight_widget_order();
    test_real_decoder_receiver_flow_to_numeric_needle_and_bar();
    test_exact_stale_boundary_removes_value();
    test_rejections_are_typed_counted_and_do_not_overwrite_good_state();
    test_receiver_regressions_refresh_and_good_packet_in_cycle_wins();
    test_refresh_failure_preserves_frame_semantically();
    test_caller_time_regression_fails_before_receiver_with_no_signals();
    test_gateway_session_change_removes_absent_prior_values();
    test_stop_restart_clears_runtime_and_reloads_persisted_layout();

    if (failures != 0) {
        std::cerr << failures
                  << " gauge dashboard loop assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 gauge dashboard loop scenario groups\n";
    return EXIT_SUCCESS;
}
