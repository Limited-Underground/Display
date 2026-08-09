#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_can_receiver.hpp"
#include "fake_esp_now_transport.hpp"
#include "opengauge/gateway_telemetry_loop.hpp"

namespace {

using namespace opengauge;
using can::test_support::FakeCanReceiver;
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

can::CanFrame eec1_frame(
    std::uint16_t engine_speed_raw,
    std::uint64_t received_at_ms) {
    can::CanFrame frame{};
    frame.identifier = 0x0CF0042AU;
    frame.format = can::CanFrameFormat::extended;
    frame.kind = can::CanFrameKind::data;
    frame.data.fill(0xFFU);
    frame.data[3] = static_cast<std::uint8_t>(
        engine_speed_raw & 0xFFU);
    frame.data[4] = static_cast<std::uint8_t>(
        engine_speed_raw >> 8U);
    frame.data_length = 8;
    frame.received_at_ms = received_at_ms;
    return frame;
}

gateway::GatewayLoopConfiguration configuration(
    std::uint8_t frame_budget = 4,
    std::uint64_t stale_after_ms = 1000) {
    return {
        {250000, false, true, false},
        {1, 1, 0},
        stale_after_ms,
        frame_budget};
}

wireless::TelemetrySubscription engine_speed_subscription(
    std::uint32_t stale_after_ms = 1000) {
    return {
        wireless::TelemetrySignalCode::engine_speed,
        0,
        1000,
        stale_after_ms,
        0};
}

void register_eec1(can::J1939DecoderRegistry& registry) {
    EXPECT(registry.register_decoder(
               can::kEec1Pgn,
               can::decode_eec1_engine_speed) ==
           can::J1939DecodeError::none);
}

void start_transport_pair(
    FakeEspNowTransport& sender,
    FakeEspNowTransport& receiver) {
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(receiver.start(peer(2), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) ==
           wireless::EspNowError::none);
    EXPECT(receiver.add_peer({peer(1), 6, true}) ==
           wireless::EspNowError::none);
    sender.connect(receiver);
}

wireless::TelemetryDecodeResult receive_packet(
    FakeEspNowTransport& receiver) {
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> bytes{};
    const auto received = receiver.receive({bytes.data(), bytes.size()});
    EXPECT(received.has_frame());
    return wireless::decode_telemetry_packet(
        bytes.data(), received.received_bytes);
}

void test_start_validation_lifecycle_and_rollback() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry empty_registry{};
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport transport{};
    gateway::GatewayTelemetryLoop empty_loop{
        receiver, empty_registry, cache, publisher, transport};
    EXPECT(empty_loop.start(configuration()).error ==
           gateway::GatewayLoopError::invalid_configuration);

    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, transport};
    auto invalid = configuration();
    invalid.maximum_can_frames_per_cycle = 0;
    EXPECT(loop.start(invalid).error ==
           gateway::GatewayLoopError::invalid_configuration);
    invalid = configuration();
    invalid.can_policy.bitrate = 123456;
    const auto failed = loop.start(invalid);
    EXPECT(failed.error == gateway::GatewayLoopError::can_start_failure);
    EXPECT(failed.can_error == can::CanReceiverError::invalid_policy);
    EXPECT(receiver.status().mode == can::CanReceiverMode::offline);
    EXPECT(!publisher.scheduler_status().running);

    EXPECT(publisher.start({9, 9, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto publisher_failed = loop.start(configuration());
    EXPECT(publisher_failed.error ==
           gateway::GatewayLoopError::publisher_start_failure);
    EXPECT(publisher_failed.publisher_error ==
           wireless::PublishSchedulerError::invalid_state);
    EXPECT(receiver.status().mode == can::CanReceiverMode::offline);
    publisher.stop();

    EXPECT(loop.start(configuration()).started());
    EXPECT(loop.start(configuration()).error ==
           gateway::GatewayLoopError::invalid_state);
    EXPECT(loop.status().running);
    loop.stop();
    EXPECT(!loop.status().running);
    EXPECT(receiver.status().mode == can::CanReceiverMode::offline);
    EXPECT(!publisher.scheduler_status().running);
    EXPECT(loop.service(0).error == gateway::GatewayLoopError::invalid_state);
}

void test_bounded_fifo_drain_and_latest_cache_state() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport transport{};
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, transport};
    EXPECT(loop.start(configuration(2)).started());
    EXPECT(receiver.inject(eec1_frame(8000, 0)) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(eec1_frame(8008, 1)) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(eec1_frame(8016, 2)) ==
           can::CanReceiverError::none);

    auto cycle = loop.service(1);
    EXPECT(cycle.healthy());
    EXPECT(cycle.can_frames_received == 2);
    EXPECT(cycle.can_frames_decoded == 2);
    EXPECT(cycle.signals_decoded == 2);
    EXPECT(cycle.cache_writes_accepted == 2);
    EXPECT(cycle.can_frame_budget_exhausted);
    EXPECT(receiver.status().queue_depth == 1);
    auto cached = cache.read("engine.speed", 1);
    EXPECT(cached.found());
    EXPECT(cached.snapshot.signal.value.raw_value == 1001000);

    cycle = loop.service(2);
    EXPECT(cycle.can_frames_received == 1);
    EXPECT(!cycle.can_frame_budget_exhausted);
    cached = cache.read("engine.speed", 2);
    EXPECT(cached.snapshot.signal.value.raw_value == 1002000);
    EXPECT(loop.status().can_frames_received == 3);
}

void test_unsupported_and_invalid_frames_are_visible_but_bounded() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport transport{};
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, transport};
    auto config = configuration();
    config.can_policy.accept_standard_frames = true;
    EXPECT(loop.start(config).started());
    auto unknown = eec1_frame(8000, 0);
    unknown.identifier = 0x18FF992AU;
    EXPECT(receiver.inject(unknown) == can::CanReceiverError::none);
    auto standard = eec1_frame(8000, 1);
    standard.identifier = 0x700U;
    standard.format = can::CanFrameFormat::standard;
    EXPECT(receiver.inject(standard) == can::CanReceiverError::none);

    const auto cycle = loop.service(1);
    EXPECT(cycle.error == gateway::GatewayLoopError::decode_failure);
    EXPECT(cycle.can_frames_received == 2);
    EXPECT(cycle.unsupported_can_frames == 1);
    EXPECT(cycle.decode_failures == 1);
    EXPECT(cycle.decode_error == can::J1939DecodeError::invalid_identifier);
    EXPECT(cache.size() == 0);
}

void test_eec1_receive_cache_publish_and_radio_delivery() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, sender};
    EXPECT(loop.start(configuration()).started());
    const auto subscription = engine_speed_subscription();
    EXPECT(loop.add_peer(peer(2), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(receiver.inject(eec1_frame(8000, 0)) ==
           can::CanReceiverError::none);

    const auto cycle = loop.service(0);
    EXPECT(cycle.healthy());
    EXPECT(cycle.can_frames_received == 1);
    EXPECT(cycle.cache_snapshots_collected == 1);
    EXPECT(cycle.peers_serviced == 1);
    EXPECT(cycle.packets_queued == 1);
    const auto wire = receive_packet(gauge);
    EXPECT(wire.decoded());
    EXPECT(wire.batch.signal_count == 1);
    EXPECT(wire.batch.signals[0].code ==
           wireless::TelemetrySignalCode::engine_speed);
    EXPECT(wire.batch.signals[0].quality ==
           telemetry::SignalQuality::valid);
    EXPECT(wire.batch.signals[0].value.raw_value == 1000000);
}

void test_unavailable_value_replaces_numeric_value_on_wire() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, sender};
    EXPECT(loop.start(configuration()).started());
    const auto subscription = engine_speed_subscription();
    EXPECT(loop.add_peer(peer(2), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(receiver.inject(eec1_frame(8000, 0)) ==
           can::CanReceiverError::none);
    EXPECT(loop.service(0).packets_queued == 1);
    EXPECT(receive_packet(gauge).decoded());

    EXPECT(receiver.inject(eec1_frame(0xFF00U, 50)) ==
           can::CanReceiverError::none);
    const auto cycle = loop.service(50);
    EXPECT(cycle.healthy());
    EXPECT(cycle.packets_queued == 1);
    const auto wire = receive_packet(gauge);
    EXPECT(wire.decoded());
    EXPECT(wire.batch.signals[0].quality ==
           telemetry::SignalQuality::unavailable);
    EXPECT(!wire.batch.signals[0].value.present);
    EXPECT(wire.batch.signals[0].value.raw_value == 0);
}

void test_bus_off_does_not_block_exact_stale_publication() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, sender};
    EXPECT(loop.start(configuration(4, 100)).started());
    const auto subscription = engine_speed_subscription(100);
    EXPECT(loop.add_peer(peer(2), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(receiver.inject(eec1_frame(8000, 0)) ==
           can::CanReceiverError::none);
    EXPECT(loop.service(0).packets_queued == 1);
    EXPECT(receive_packet(gauge).decoded());
    EXPECT(receiver.set_bus_state(can::CanBusState::bus_off) ==
           can::CanReceiverError::none);

    auto cycle = loop.service(99);
    EXPECT(cycle.bus_state == can::CanBusState::bus_off);
    EXPECT(cycle.packets_queued == 0);
    cycle = loop.service(100);
    EXPECT(cycle.bus_state == can::CanBusState::bus_off);
    EXPECT(cycle.packets_queued == 1);
    const auto stale = receive_packet(gauge);
    EXPECT(stale.decoded());
    EXPECT(stale.batch.signals[0].quality ==
           telemetry::SignalQuality::stale);
    EXPECT(!stale.batch.signals[0].value.present);
    EXPECT(stale.batch.signals[0].source_age_ms == 100);
}

void test_local_transport_failure_retries_without_sequence_gap() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport sender{};
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, sender};
    EXPECT(loop.start(configuration()).started());
    const auto subscription = engine_speed_subscription();
    EXPECT(loop.add_peer(peer(2), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(receiver.inject(eec1_frame(8000, 0)) ==
           can::CanReceiverError::none);

    auto cycle = loop.service(0);
    EXPECT(cycle.error == gateway::GatewayLoopError::peer_service_failure);
    EXPECT(cycle.peer_service_failures == 1);
    EXPECT(cycle.packets_queued == 0);
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) ==
           wireless::EspNowError::none);
    cycle = loop.service(0);
    EXPECT(cycle.healthy());
    EXPECT(cycle.packets_queued == 1);
    EXPECT(sender.poll_delivery().receipt.token != 0);
    EXPECT(loop.status().peer_service_failures == 1);
}

void test_receiver_overflow_is_propagated_to_cycle_and_status() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport transport{};
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, transport};
    EXPECT(loop.start(configuration(16)).started());
    for (std::size_t index = 0; index < FakeCanReceiver::kQueueCapacity;
         ++index) {
        EXPECT(receiver.inject(eec1_frame(
                   static_cast<std::uint16_t>(8000U + index), index)) ==
               can::CanReceiverError::none);
    }
    EXPECT(receiver.inject(eec1_frame(9000, 16)) ==
           can::CanReceiverError::queue_full);
    const auto cycle = loop.service(16);
    EXPECT(cycle.can_frames_received == FakeCanReceiver::kQueueCapacity);
    EXPECT(cycle.receiver_overflow_count == 1);
    EXPECT(!cycle.can_frame_budget_exhausted);
    EXPECT(loop.status().last_receiver_overflow_count == 1);
}

void test_stop_restart_clears_cache_peers_and_counters() {
    FakeCanReceiver receiver{};
    can::J1939DecoderRegistry registry{};
    register_eec1(registry);
    telemetry::TelemetryCache cache{};
    wireless::TelemetryGatewayPublisher publisher{};
    FakeEspNowTransport transport{};
    gateway::GatewayTelemetryLoop loop{
        receiver, registry, cache, publisher, transport};
    EXPECT(loop.start(configuration()).started());
    const auto subscription = engine_speed_subscription();
    EXPECT(loop.add_peer(peer(2), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(receiver.inject(eec1_frame(8000, 0)) ==
           can::CanReceiverError::none);
    EXPECT(loop.service(0).can_frames_received == 1);
    EXPECT(cache.size() == 1);
    EXPECT(loop.status().peer_count == 1);
    loop.stop();

    EXPECT(loop.start(configuration()).started());
    EXPECT(cache.size() == 0);
    EXPECT(loop.status().peer_count == 0);
    EXPECT(loop.status().cycles_serviced == 0);
    EXPECT(loop.status().can_frames_received == 0);
    const auto cycle = loop.service(1);
    EXPECT(cycle.healthy());
    EXPECT(cycle.peers_serviced == 0);
    EXPECT(cycle.packets_queued == 0);
}

}  // namespace

int main() {
    test_start_validation_lifecycle_and_rollback();
    test_bounded_fifo_drain_and_latest_cache_state();
    test_unsupported_and_invalid_frames_are_visible_but_bounded();
    test_eec1_receive_cache_publish_and_radio_delivery();
    test_unavailable_value_replaces_numeric_value_on_wire();
    test_bus_off_does_not_block_exact_stale_publication();
    test_local_transport_failure_retries_without_sequence_gap();
    test_receiver_overflow_is_propagated_to_cycle_and_status();
    test_stop_restart_clears_cache_peers_and_counters();

    if (failures != 0) {
        std::cerr << failures << " gateway loop assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 gateway telemetry loop scenario groups\n";
    return EXIT_SUCCESS;
}
