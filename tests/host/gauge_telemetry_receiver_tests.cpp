#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_esp_now_transport.hpp"
#include "opengauge/gauge_telemetry_receiver.hpp"

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

wireless::WireTelemetrySignal wire_signal(
    wireless::TelemetrySignalCode code,
    std::int64_t value,
    std::uint32_t source_age_ms = 0,
    telemetry::SignalQuality quality = telemetry::SignalQuality::valid) {
    wireless::WireTelemetrySignal result{};
    const auto* descriptor = wireless::telemetry_signal_descriptor(code);
    EXPECT(descriptor != nullptr);
    result.code = code;
    result.value = {descriptor->value_type, value, true};
    result.unit = descriptor->unit;
    result.quality = quality;
    result.source_age_ms = source_age_ms;
    if (quality != telemetry::SignalQuality::valid &&
        quality != telemetry::SignalQuality::suspect) {
        result.value.raw_value = 0;
        result.value.present = false;
    }
    return result;
}

wireless::TelemetryBatch batch(
    std::uint32_t session,
    std::uint32_t sequence,
    const wireless::WireTelemetrySignal* signals,
    std::size_t signal_count,
    std::uint64_t gateway_id = 10) {
    wireless::TelemetryBatch result{};
    result.gateway_id = gateway_id;
    result.boot_session_id = session;
    result.sequence = sequence;
    result.signal_count = static_cast<std::uint8_t>(signal_count);
    for (std::size_t index = 0; index < signal_count; ++index) {
        result.signals[index] = signals[index];
    }
    return result;
}

wireless::GaugeReceiverConfiguration configuration(
    std::uint8_t budget = 4,
    std::uint8_t channel = 6,
    wireless::PeerAddress gateway_address = peer(1)) {
    return {gateway_address, 10, channel, budget};
}

void start_transport_pair(
    FakeEspNowTransport& sender,
    FakeEspNowTransport& gauge,
    bool encrypted = true,
    std::uint8_t channel = 6) {
    EXPECT(sender.start(peer(1), {channel, encrypted}) ==
           wireless::EspNowError::none);
    EXPECT(gauge.start(peer(2), {channel, encrypted}) ==
           wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), channel, encrypted}) ==
           wireless::EspNowError::none);
    EXPECT(gauge.add_peer({peer(1), channel, encrypted}) ==
           wireless::EspNowError::none);
    sender.connect(gauge);
}

void deliver(
    FakeEspNowTransport& sender,
    const wireless::TelemetryBatch& telemetry,
    std::uint64_t now_ms) {
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               telemetry, encoded.data(), encoded.size()).encoded());
    EXPECT(sender.send(peer(2), {encoded.data(), encoded.size()}, now_ms)
               .accepted());
    sender.service(now_ms);
}

void test_configuration_lifecycle_and_empty_reads() {
    FakeEspNowTransport transport{};
    wireless::GaugeTelemetryReceiver receiver{transport};
    EXPECT(receiver.service(0).error ==
           wireless::GaugeReceiverError::invalid_state);
    auto invalid = configuration();
    invalid.expected_gateway_id = 0;
    EXPECT(receiver.start(invalid) ==
           wireless::GaugeReceiverError::invalid_configuration);
    invalid = configuration(0);
    EXPECT(receiver.start(invalid) ==
           wireless::GaugeReceiverError::invalid_configuration);
    invalid = configuration(5);
    EXPECT(receiver.start(invalid) ==
           wireless::GaugeReceiverError::invalid_configuration);
    EXPECT(receiver.start(configuration()) ==
           wireless::GaugeReceiverError::none);
    EXPECT(receiver.start(configuration()) ==
           wireless::GaugeReceiverError::invalid_state);
    EXPECT(receiver.service(0).error == wireless::GaugeReceiverError::no_data);
    EXPECT(receiver.read(
               wireless::TelemetrySignalCode::engine_speed, 0, 100).error ==
           wireless::GaugeReceiverError::signal_not_found);
    receiver.stop();
    EXPECT(receiver.read(
               wireless::TelemetrySignalCode::engine_speed, 0, 100).error ==
           wireless::GaugeReceiverError::invalid_state);
}

void test_authorized_packet_delivery_and_receiver_local_freshness() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(configuration()) ==
           wireless::GaugeReceiverError::none);
    const auto speed = wire_signal(
        wireless::TelemetrySignalCode::engine_speed, 1000000, 25);
    deliver(sender, batch(1, 0, &speed, 1), 100);

    const auto cycle = receiver.service(100);
    EXPECT(cycle.serviced());
    EXPECT(cycle.datagrams_received == 1);
    EXPECT(cycle.packets_accepted == 1);
    EXPECT(cycle.signals_updated == 1);
    auto read = receiver.read(
        wireless::TelemetrySignalCode::engine_speed, 125, 100);
    EXPECT(read.found());
    EXPECT(read.effective_quality == telemetry::SignalQuality::valid);
    EXPECT(read.display_value.raw_value == 1000000);
    EXPECT(read.age_ms == 50);
    EXPECT(read.boot_session_id == 1);
    EXPECT(read.packet_sequence == 0);
    read = receiver.read(
        wireless::TelemetrySignalCode::engine_speed, 175, 100);
    EXPECT(read.found());
    EXPECT(read.effective_quality == telemetry::SignalQuality::stale);
    EXPECT(!read.display_value.present);
    EXPECT(read.display_value.raw_value == 0);
    EXPECT(read.age_ms == 100);
}

void test_source_encryption_channel_and_gateway_identity_checks() {
    const auto speed = wire_signal(
        wireless::TelemetrySignalCode::engine_speed, 1000000);

    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver unauthorized{gauge};
    EXPECT(unauthorized.start(configuration(4, 6, peer(3))) ==
           wireless::GaugeReceiverError::none);
    deliver(sender, batch(1, 0, &speed, 1), 0);
    auto cycle = unauthorized.service(0);
    EXPECT(cycle.error == wireless::GaugeReceiverError::unauthorized_source);
    EXPECT(cycle.unauthorized_datagrams == 1);

    unauthorized.stop();
    EXPECT(unauthorized.start(configuration()) ==
           wireless::GaugeReceiverError::none);
    deliver(sender, batch(1, 0, &speed, 1, 11), 1);
    cycle = unauthorized.service(1);
    EXPECT(cycle.error == wireless::GaugeReceiverError::stream_failure);
    EXPECT(cycle.stream_error == wireless::TelemetryStreamError::gateway_mismatch);
    EXPECT(cycle.gateway_identity_mismatches == 1);
    EXPECT(cycle.stream_failures == 1);

    FakeEspNowTransport open_sender{};
    FakeEspNowTransport open_gauge{};
    start_transport_pair(open_sender, open_gauge, false);
    wireless::GaugeTelemetryReceiver unencrypted{open_gauge};
    EXPECT(unencrypted.start(configuration()) ==
           wireless::GaugeReceiverError::none);
    deliver(open_sender, batch(1, 0, &speed, 1), 0);
    cycle = unencrypted.service(0);
    EXPECT(cycle.error == wireless::GaugeReceiverError::encryption_required);
    EXPECT(cycle.unencrypted_datagrams == 1);

    FakeEspNowTransport channel_sender{};
    FakeEspNowTransport channel_gauge{};
    start_transport_pair(channel_sender, channel_gauge, true, 6);
    wireless::GaugeTelemetryReceiver wrong_channel{channel_gauge};
    EXPECT(wrong_channel.start(configuration(4, 7)) ==
           wireless::GaugeReceiverError::none);
    deliver(channel_sender, batch(1, 0, &speed, 1), 0);
    cycle = wrong_channel.service(0);
    EXPECT(cycle.error == wireless::GaugeReceiverError::channel_mismatch);
    EXPECT(cycle.channel_mismatches == 1);
}

void test_malformed_packet_is_rejected_without_store_mutation() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(configuration()) ==
           wireless::GaugeReceiverError::none);
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> malformed{};
    malformed.fill(0xA5U);
    EXPECT(sender.send(peer(2), {malformed.data(), malformed.size()}, 0)
               .accepted());
    sender.service(0);
    const auto cycle = receiver.service(0);
    EXPECT(cycle.error == wireless::GaugeReceiverError::packet_failure);
    EXPECT(cycle.malformed_packets == 1);
    EXPECT(receiver.status().signal_count == 0);
}

void test_duplicate_gap_and_out_of_order_sequence_handling() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(configuration()) ==
           wireless::GaugeReceiverError::none);
    auto speed = wire_signal(
        wireless::TelemetrySignalCode::engine_speed, 1000000);
    deliver(sender, batch(1, 0, &speed, 1), 0);
    EXPECT(receiver.service(0).packets_accepted == 1);

    speed.value.raw_value = 1001000;
    deliver(sender, batch(1, 0, &speed, 1), 1);
    auto cycle = receiver.service(1);
    EXPECT(cycle.duplicate_packets == 1);
    EXPECT(cycle.packets_accepted == 0);

    speed.value.raw_value = 1002000;
    deliver(sender, batch(1, 2, &speed, 1), 2);
    cycle = receiver.service(2);
    EXPECT(cycle.gap_packets == 1);
    EXPECT(cycle.missing_packets == 1);
    EXPECT(cycle.packets_accepted == 1);

    speed.value.raw_value = 1001500;
    deliver(sender, batch(1, 1, &speed, 1), 3);
    cycle = receiver.service(3);
    EXPECT(cycle.out_of_order_packets == 1);
    EXPECT(cycle.packets_accepted == 0);
    const auto read = receiver.read(
        wireless::TelemetrySignalCode::engine_speed, 3, 1000);
    EXPECT(read.display_value.raw_value == 1002000);
    EXPECT(read.packet_sequence == 2);
}

void test_session_change_clears_signals_absent_from_new_session() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(configuration()) ==
           wireless::GaugeReceiverError::none);
    const std::array<wireless::WireTelemetrySignal, 2> first{{
        wire_signal(wireless::TelemetrySignalCode::engine_speed, 1000000),
        wire_signal(wireless::TelemetrySignalCode::electrical_voltage, 13800),
    }};
    deliver(sender, batch(1, 0, first.data(), first.size()), 0);
    EXPECT(receiver.service(0).signals_updated == 2);
    EXPECT(receiver.status().signal_count == 2);

    const auto speed = wire_signal(
        wireless::TelemetrySignalCode::engine_speed, 900000);
    deliver(sender, batch(2, 0, &speed, 1), 1);
    const auto cycle = receiver.service(1);
    EXPECT(cycle.session_resets == 1);
    EXPECT(cycle.packets_accepted == 1);
    EXPECT(receiver.status().signal_count == 1);
    EXPECT(receiver.read(
               wireless::TelemetrySignalCode::electrical_voltage,
               1, 1000).error ==
           wireless::GaugeReceiverError::signal_not_found);
    EXPECT(receiver.read(
               wireless::TelemetrySignalCode::engine_speed,
               1, 1000).boot_session_id == 2);
}

void test_packet_budget_preserves_remaining_transport_queue() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(configuration(2)) ==
           wireless::GaugeReceiverError::none);
    auto speed = wire_signal(
        wireless::TelemetrySignalCode::engine_speed, 1000000);
    for (std::uint32_t sequence = 0; sequence < 3; ++sequence) {
        speed.value.raw_value += 1000;
        std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
        EXPECT(wireless::encode_telemetry_packet(
                   batch(1, sequence, &speed, 1),
                   encoded.data(), encoded.size()).encoded());
        EXPECT(sender.send(peer(2), {encoded.data(), encoded.size()}, 0)
                   .accepted());
    }
    sender.service(0);
    auto cycle = receiver.service(0);
    EXPECT(cycle.datagrams_received == 2);
    EXPECT(cycle.packets_accepted == 2);
    EXPECT(cycle.packet_budget_exhausted);
    EXPECT(gauge.status().receive_queue_depth == 1);
    cycle = receiver.service(1);
    EXPECT(cycle.datagrams_received == 1);
    EXPECT(cycle.packets_accepted == 1);
    EXPECT(!cycle.packet_budget_exhausted);
}

void test_read_freshness_errors_are_explicit() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    EXPECT(receiver.start(configuration()) ==
           wireless::GaugeReceiverError::none);
    const auto speed = wire_signal(
        wireless::TelemetrySignalCode::engine_speed, 1000000);
    deliver(sender, batch(1, 0, &speed, 1), 100);
    EXPECT(receiver.service(100).packets_accepted == 1);
    auto read = receiver.read(
        wireless::TelemetrySignalCode::engine_speed, 100, 0);
    EXPECT(read.error == wireless::GaugeReceiverError::freshness_failure);
    EXPECT(read.freshness_error ==
           wireless::TelemetryFreshnessError::invalid_stale_threshold);
    read = receiver.read(
        wireless::TelemetrySignalCode::engine_speed, 99, 1000);
    EXPECT(read.error == wireless::GaugeReceiverError::freshness_failure);
    EXPECT(read.freshness_error ==
           wireless::TelemetryFreshnessError::clock_regressed);
}

}  // namespace

int main() {
    test_configuration_lifecycle_and_empty_reads();
    test_authorized_packet_delivery_and_receiver_local_freshness();
    test_source_encryption_channel_and_gateway_identity_checks();
    test_malformed_packet_is_rejected_without_store_mutation();
    test_duplicate_gap_and_out_of_order_sequence_handling();
    test_session_change_clears_signals_absent_from_new_session();
    test_packet_budget_preserves_remaining_transport_queue();
    test_read_freshness_errors_are_explicit();

    if (failures != 0) {
        std::cerr << failures << " gauge receiver assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 gauge telemetry receiver scenario groups\n";
    return EXIT_SUCCESS;
}
