#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_esp_now_transport.hpp"
#include "opengauge/telemetry_gateway_publisher.hpp"

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

telemetry::NormalizedSignal signal(
    wireless::TelemetrySignalCode code,
    std::int64_t value,
    std::uint64_t received_at_ms) {
    telemetry::NormalizedSignal result{};
    const auto* descriptor = wireless::telemetry_signal_descriptor(code);
    EXPECT(descriptor != nullptr);
    EXPECT(telemetry::make_signal_id(
               descriptor->normalized_id, result.id) ==
           telemetry::SignalModelError::none);
    result.value = {descriptor->value_type, value, true};
    result.unit = descriptor->unit;
    result.quality = telemetry::SignalQuality::valid;
    result.source.protocol = telemetry::SignalSourceProtocol::synthetic;
    result.received_at_ms = received_at_ms;
    return result;
}

telemetry::NormalizedSignal unregistered_signal(
    std::uint64_t received_at_ms) {
    telemetry::NormalizedSignal result{};
    EXPECT(telemetry::make_signal_id("auxiliary.test_value", result.id) ==
           telemetry::SignalModelError::none);
    result.value = {
        telemetry::SignalValueType::signed_integer, 42, true};
    result.unit = telemetry::SignalUnit::count;
    result.quality = telemetry::SignalQuality::valid;
    result.source.protocol = telemetry::SignalSourceProtocol::synthetic;
    result.received_at_ms = received_at_ms;
    return result;
}

wireless::TelemetrySubscription subscription(
    wireless::TelemetrySignalCode code,
    std::uint32_t stale_after_ms = 1000) {
    return {code, 0, 1000, stale_after_ms, 0};
}

void start_transport_pair(
    FakeEspNowTransport& sender,
    FakeEspNowTransport& receiver) {
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(receiver.start(peer(2), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) == wireless::EspNowError::none);
    EXPECT(receiver.add_peer({peer(1), 6, true}) == wireless::EspNowError::none);
    sender.connect(receiver);
}

wireless::TelemetryDecodeResult receive_packet(
    FakeEspNowTransport& receiver) {
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    const auto received = receiver.receive({encoded.data(), encoded.size()});
    EXPECT(received.has_frame());
    return wireless::decode_telemetry_packet(
        encoded.data(), received.received_bytes);
}

void test_lifecycle_cache_mapping_and_unregistered_skip() {
    wireless::TelemetryGatewayPublisher publisher{};
    telemetry::TelemetryCache cache{};
    EXPECT(publisher.poll_cache(cache, 0).error ==
           wireless::GatewayPublisherError::invalid_state);
    EXPECT(publisher.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    EXPECT(publisher.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::invalid_state);
    const auto speed = subscription(
        wireless::TelemetrySignalCode::engine_speed);
    EXPECT(publisher.add_peer(peer(2), &speed, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(cache.upsert(
               signal(speed.code, 1000, 0), 1000).accepted());
    EXPECT(cache.upsert(unregistered_signal(0), 1000).accepted());
    const auto poll = publisher.poll_cache(cache, 0);
    EXPECT(poll.polled());
    EXPECT(poll.snapshots_collected == 2);
    EXPECT(poll.registered_signals_updated == 1);
    EXPECT(poll.unregistered_signals_skipped == 1);
    EXPECT(publisher.scheduler_status().signal_count == 1);
    const auto unchanged = publisher.poll_cache(cache, 0);
    EXPECT(unchanged.polled());
    EXPECT(unchanged.snapshots_collected == 0);
    publisher.stop();
    EXPECT(!publisher.scheduler_status().running);
}

void test_cache_epoch_clear_drops_source_state_then_reloads() {
    wireless::TelemetryGatewayPublisher publisher{};
    telemetry::TelemetryCache cache{};
    FakeEspNowTransport sender{};
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) == wireless::EspNowError::none);
    EXPECT(publisher.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto speed = subscription(
        wireless::TelemetrySignalCode::engine_speed);
    EXPECT(publisher.add_peer(peer(2), &speed, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(cache.upsert(signal(speed.code, 1000, 0), 1000).accepted());
    EXPECT(publisher.poll_cache(cache, 0).polled());
    EXPECT(publisher.service_peer(sender, peer(2), 0).queued());

    cache.clear();
    const auto cleared = publisher.poll_cache(cache, 10);
    EXPECT(cleared.polled());
    EXPECT(cleared.cache_epoch_changed);
    EXPECT(cleared.snapshots_collected == 0);
    EXPECT(publisher.scheduler_status().signal_count == 0);
    EXPECT(publisher.service_peer(sender, peer(2), 50).error ==
           wireless::GatewayPublisherError::no_data);

    // The same numeric value must still publish promptly in the new epoch.
    EXPECT(cache.upsert(signal(speed.code, 1000, 60), 1000).accepted());
    EXPECT(publisher.poll_cache(cache, 60).registered_signals_updated == 1);
    const auto service = publisher.service_peer(sender, peer(2), 60);
    EXPECT(service.queued());
    EXPECT(service.packet_sequence == 1);
}

void test_local_transport_rejection_retries_same_sequence() {
    wireless::TelemetryGatewayPublisher publisher{};
    telemetry::TelemetryCache cache{};
    FakeEspNowTransport sender{};
    EXPECT(publisher.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto speed = subscription(
        wireless::TelemetrySignalCode::engine_speed);
    EXPECT(publisher.add_peer(peer(2), &speed, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(cache.upsert(signal(speed.code, 1000, 0), 1000).accepted());
    EXPECT(publisher.poll_cache(cache, 0).polled());

    auto result = publisher.service_peer(sender, peer(2), 0);
    EXPECT(result.error == wireless::GatewayPublisherError::transport_failure);
    EXPECT(result.transport_error == wireless::EspNowError::not_ready);
    EXPECT(result.packet_sequence == 0);
    EXPECT(!result.local_queue_accepted);
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) == wireless::EspNowError::none);
    result = publisher.service_peer(sender, peer(2), 0);
    EXPECT(result.queued());
    EXPECT(result.packet_sequence == 0);
    EXPECT(result.transport_token != 0);
    EXPECT(result.encoded_bytes == wireless::kTelemetryPacketBytes);
}

void test_initial_batch_is_paced_three_then_one() {
    wireless::TelemetryGatewayPublisher publisher{};
    telemetry::TelemetryCache cache{};
    FakeEspNowTransport sender{};
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) == wireless::EspNowError::none);
    EXPECT(publisher.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const std::array<wireless::TelemetrySubscription, 4> subscriptions{{
        subscription(wireless::TelemetrySignalCode::engine_speed),
        subscription(wireless::TelemetrySignalCode::engine_coolant_temperature),
        subscription(wireless::TelemetrySignalCode::vehicle_speed),
        subscription(wireless::TelemetrySignalCode::electrical_voltage),
    }};
    EXPECT(publisher.add_peer(
               peer(2), subscriptions.data(), subscriptions.size()) ==
           wireless::PublishSchedulerError::none);
    EXPECT(cache.upsert(signal(subscriptions[0].code, 1000, 0), 1000).accepted());
    EXPECT(cache.upsert(signal(subscriptions[1].code, 85000, 0), 1000).accepted());
    EXPECT(cache.upsert(signal(subscriptions[2].code, 12000, 0), 1000).accepted());
    EXPECT(cache.upsert(signal(subscriptions[3].code, 13800, 0), 1000).accepted());
    EXPECT(publisher.poll_cache(cache, 0).registered_signals_updated == 4);
    auto result = publisher.service_peer(sender, peer(2), 0);
    EXPECT(result.queued());
    EXPECT(result.packet_sequence == 0);
    EXPECT(publisher.service_peer(sender, peer(2), 49).error ==
           wireless::GatewayPublisherError::no_data);
    result = publisher.service_peer(sender, peer(2), 50);
    EXPECT(result.queued());
    EXPECT(result.packet_sequence == 1);
    EXPECT(sender.status().transmit_queue_depth == 2);
}

void test_scheduler_derives_stale_packet_without_another_cache_poll() {
    wireless::TelemetryGatewayPublisher publisher{};
    telemetry::TelemetryCache cache{};
    FakeEspNowTransport sender{};
    FakeEspNowTransport receiver{};
    start_transport_pair(sender, receiver);
    EXPECT(publisher.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto speed = subscription(
        wireless::TelemetrySignalCode::engine_speed, 150);
    EXPECT(publisher.add_peer(peer(2), &speed, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(cache.upsert(signal(speed.code, 1000, 0), 1000).accepted());
    EXPECT(publisher.poll_cache(cache, 0).polled());
    EXPECT(publisher.service_peer(sender, peer(2), 0).queued());
    sender.service(0);
    auto decoded = receive_packet(receiver);
    EXPECT(decoded.decoded());
    EXPECT(decoded.batch.signals[0].quality == telemetry::SignalQuality::valid);
    EXPECT(decoded.batch.signals[0].value.present);

    EXPECT(publisher.service_peer(sender, peer(2), 149).error ==
           wireless::GatewayPublisherError::no_data);
    EXPECT(publisher.service_peer(sender, peer(2), 150).queued());
    sender.service(150);
    decoded = receive_packet(receiver);
    EXPECT(decoded.decoded());
    EXPECT(decoded.batch.signals[0].quality == telemetry::SignalQuality::stale);
    EXPECT(!decoded.batch.signals[0].value.present);
    EXPECT(decoded.batch.signals[0].source_age_ms == 150);
}

void test_end_to_end_local_acceptance_radio_loss_and_gap() {
    wireless::TelemetryGatewayPublisher publisher{};
    telemetry::TelemetryCache cache{};
    FakeEspNowTransport sender{};
    FakeEspNowTransport receiver{};
    start_transport_pair(sender, receiver);
    EXPECT(publisher.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto speed = subscription(
        wireless::TelemetrySignalCode::engine_speed, 5000);
    EXPECT(publisher.add_peer(peer(2), &speed, 1) ==
           wireless::PublishSchedulerError::none);
    wireless::TelemetryStreamTracker tracker{};

    EXPECT(cache.upsert(signal(speed.code, 1000, 0), 5000).accepted());
    EXPECT(publisher.poll_cache(cache, 0).polled());
    EXPECT(publisher.service_peer(sender, peer(2), 0).packet_sequence == 0);
    sender.service(0);
    auto decoded = receive_packet(receiver);
    EXPECT(decoded.decoded());
    EXPECT(tracker.ingest(decoded.batch, 0).accepted());
    EXPECT(sender.poll_delivery().receipt.radio_delivered());

    EXPECT(cache.upsert(signal(speed.code, 1010, 50), 5000).accepted());
    EXPECT(publisher.poll_cache(cache, 50).registered_signals_updated == 1);
    sender.drop_next_transmissions(1);
    auto service = publisher.service_peer(sender, peer(2), 50);
    EXPECT(service.queued());
    EXPECT(service.packet_sequence == 1);
    sender.service(50);
    EXPECT(receiver.receive({nullptr, 0}).error ==
           wireless::EspNowError::no_data);
    EXPECT(sender.poll_delivery().receipt.outcome ==
           wireless::DeliveryOutcome::injected_loss);

    EXPECT(cache.upsert(signal(speed.code, 1020, 100), 5000).accepted());
    EXPECT(publisher.poll_cache(cache, 100).registered_signals_updated == 1);
    service = publisher.service_peer(sender, peer(2), 100);
    EXPECT(service.queued());
    EXPECT(service.packet_sequence == 2);
    sender.service(100);
    decoded = receive_packet(receiver);
    const auto gap = tracker.ingest(decoded.batch, 100);
    EXPECT(gap.accepted());
    EXPECT(gap.disposition == wireless::TelemetrySequenceDisposition::gap);
    EXPECT(gap.missing_packets == 1);
}

}  // namespace

int main() {
    test_lifecycle_cache_mapping_and_unregistered_skip();
    test_cache_epoch_clear_drops_source_state_then_reloads();
    test_local_transport_rejection_retries_same_sequence();
    test_initial_batch_is_paced_three_then_one();
    test_scheduler_derives_stale_packet_without_another_cache_poll();
    test_end_to_end_local_acceptance_radio_loss_and_gap();

    if (failures != 0) {
        std::cerr << failures << " gateway publisher assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 telemetry gateway publisher scenario groups\n";
    return EXIT_SUCCESS;
}
