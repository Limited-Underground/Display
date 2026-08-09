#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_esp_now_transport.hpp"
#include "opengauge/telemetry_publish_scheduler.hpp"

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

telemetry::CachedSignalSnapshot snapshot(
    wireless::TelemetrySignalCode code,
    std::int64_t value,
    telemetry::SignalQuality effective_quality =
        telemetry::SignalQuality::valid,
    std::uint64_t age_ms = 0) {
    telemetry::CachedSignalSnapshot result{};
    const auto* descriptor = wireless::telemetry_signal_descriptor(code);
    EXPECT(descriptor != nullptr);
    EXPECT(telemetry::make_signal_id(
               descriptor->normalized_id, result.signal.id) ==
           telemetry::SignalModelError::none);
    result.signal.value = {descriptor->value_type, value, true};
    result.signal.unit = descriptor->unit;
    result.signal.quality = telemetry::SignalQuality::valid;
    result.signal.source.protocol = telemetry::SignalSourceProtocol::synthetic;
    result.effective_quality = effective_quality;
    result.age_ms = age_ms;
    return result;
}

wireless::TelemetrySubscription policy(
    wireless::TelemetrySignalCode code,
    std::uint32_t minimum_interval_ms = 100,
    std::uint32_t maximum_interval_ms = 1000,
    std::uint32_t stale_after_ms = 500,
    std::uint64_t deadband_raw = 10) {
    return {
        code,
        minimum_interval_ms,
        maximum_interval_ms,
        stale_after_ms,
        deadband_raw};
}

void update_four_signals(
    wireless::TelemetryPublishScheduler& scheduler,
    std::uint64_t observed_at_ms = 0) {
    EXPECT(scheduler.update_signal(
               wireless::TelemetrySignalCode::engine_speed,
               snapshot(wireless::TelemetrySignalCode::engine_speed, 1000000),
               observed_at_ms).accepted());
    EXPECT(scheduler.update_signal(
               wireless::TelemetrySignalCode::engine_coolant_temperature,
               snapshot(
                   wireless::TelemetrySignalCode::engine_coolant_temperature,
                   85000),
               observed_at_ms).accepted());
    EXPECT(scheduler.update_signal(
               wireless::TelemetrySignalCode::vehicle_speed,
               snapshot(wireless::TelemetrySignalCode::vehicle_speed, 12000),
               observed_at_ms).accepted());
    EXPECT(scheduler.update_signal(
               wireless::TelemetrySignalCode::electrical_voltage,
               snapshot(
                   wireless::TelemetrySignalCode::electrical_voltage, 13800),
               observed_at_ms).accepted());
}

void test_lifecycle_peer_policy_and_capacity() {
    wireless::TelemetryPublishScheduler scheduler{};
    EXPECT(scheduler.add_peer(peer(1), nullptr, 0) ==
           wireless::PublishSchedulerError::invalid_state);
    EXPECT(scheduler.start({0, 1, 0}) ==
           wireless::PublishSchedulerError::invalid_identity);
    EXPECT(scheduler.start({1, 0, 0}) ==
           wireless::PublishSchedulerError::invalid_identity);
    EXPECT(scheduler.start({1, 2, 100}) ==
           wireless::PublishSchedulerError::none);
    EXPECT(scheduler.start({1, 2, 100}) ==
           wireless::PublishSchedulerError::invalid_state);

    auto subscription = policy(
        wireless::TelemetrySignalCode::engine_speed);
    EXPECT(scheduler.add_peer({}, &subscription, 1) ==
           wireless::PublishSchedulerError::invalid_argument);
    EXPECT(scheduler.add_peer(peer(1), nullptr, 1) ==
           wireless::PublishSchedulerError::invalid_argument);
    EXPECT(scheduler.add_peer(peer(1), &subscription, 1, {0}) ==
           wireless::PublishSchedulerError::invalid_argument);
    EXPECT(scheduler.add_peer(peer(1), &subscription, 1, {49}) ==
           wireless::PublishSchedulerError::invalid_argument);
    auto invalid = subscription;
    invalid.maximum_interval_ms = 0;
    EXPECT(scheduler.add_peer(peer(1), &invalid, 1) ==
           wireless::PublishSchedulerError::invalid_subscription);
    invalid = subscription;
    invalid.maximum_interval_ms = 50;
    EXPECT(scheduler.add_peer(peer(1), &invalid, 1) ==
           wireless::PublishSchedulerError::invalid_subscription);
    const std::array<wireless::TelemetrySubscription, 2> duplicate{{
        subscription, subscription}};
    EXPECT(scheduler.add_peer(peer(1), duplicate.data(), duplicate.size()) ==
           wireless::PublishSchedulerError::duplicate_subscription);

    for (std::uint8_t suffix = 1;
         suffix <= wireless::kMaximumScheduledGaugePeers;
         ++suffix) {
        EXPECT(scheduler.add_peer(peer(suffix), &subscription, 1) ==
               wireless::PublishSchedulerError::none);
    }
    EXPECT(scheduler.add_peer(peer(20), &subscription, 1) ==
           wireless::PublishSchedulerError::peer_capacity_full);
    EXPECT(scheduler.add_peer(peer(1), &subscription, 1) ==
           wireless::PublishSchedulerError::peer_already_exists);
    EXPECT(scheduler.status().peer_count ==
           wireless::kMaximumScheduledGaugePeers);
    EXPECT(scheduler.remove_peer(peer(4)) ==
           wireless::PublishSchedulerError::none);
    EXPECT(scheduler.remove_peer(peer(4)) ==
           wireless::PublishSchedulerError::peer_not_found);
    scheduler.stop();
    EXPECT(!scheduler.status().running);
    EXPECT(scheduler.status().peer_count == 0);
}

void test_signal_updates_validate_and_coalesce() {
    wireless::TelemetryPublishScheduler scheduler{};
    auto speed = snapshot(wireless::TelemetrySignalCode::engine_speed, 1000000);
    EXPECT(scheduler.update_signal(
               wireless::TelemetrySignalCode::engine_speed, speed, 100).error ==
           wireless::PublishSchedulerError::invalid_state);
    EXPECT(scheduler.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    auto result = scheduler.update_signal(
        wireless::TelemetrySignalCode::engine_speed, speed, 100);
    EXPECT(result.accepted());
    EXPECT(result.inserted);
    EXPECT(result.semantic_change);
    result = scheduler.update_signal(
        wireless::TelemetrySignalCode::engine_speed, speed, 110);
    EXPECT(result.accepted());
    EXPECT(!result.inserted);
    EXPECT(!result.semantic_change);
    speed.signal.value.raw_value += 1;
    result = scheduler.update_signal(
        wireless::TelemetrySignalCode::engine_speed, speed, 120);
    EXPECT(result.accepted());
    EXPECT(result.semantic_change);
    EXPECT(scheduler.update_signal(
               wireless::TelemetrySignalCode::engine_speed, speed, 119).error ==
           wireless::PublishSchedulerError::clock_regressed);
    EXPECT(scheduler.update_signal(
               wireless::TelemetrySignalCode::vehicle_speed, speed, 130).error ==
           wireless::PublishSchedulerError::signal_rejected);
    EXPECT(scheduler.status().signal_count == 1);
}

void test_initial_three_signal_batch_and_per_peer_sequence() {
    wireless::TelemetryPublishScheduler scheduler{};
    EXPECT(scheduler.start({0xAAU, 0xBBU, 100}) ==
           wireless::PublishSchedulerError::none);
    const std::array<wireless::TelemetrySubscription, 4> subscriptions{{
        policy(wireless::TelemetrySignalCode::engine_speed),
        policy(wireless::TelemetrySignalCode::engine_coolant_temperature),
        policy(wireless::TelemetrySignalCode::vehicle_speed),
        policy(wireless::TelemetrySignalCode::electrical_voltage),
    }};
    EXPECT(scheduler.add_peer(
               peer(1), subscriptions.data(), subscriptions.size()) ==
           wireless::PublishSchedulerError::none);
    update_four_signals(scheduler, 100);
    auto plan = scheduler.prepare(peer(1), 200);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.signal_count == 3);
    EXPECT(plan.batch.sequence == 0);
    EXPECT(plan.batch.gateway_uptime_ms == 100);
    EXPECT(scheduler.prepare(peer(1), 200).error ==
           wireless::PublishSchedulerError::plan_pending);
    auto commit = scheduler.commit(peer(1), plan.token, true, 200);
    EXPECT(commit.committed());
    EXPECT(commit.transport_accepted);
    EXPECT(commit.next_sequence == 1);
    EXPECT(scheduler.prepare(peer(1), 249).error ==
           wireless::PublishSchedulerError::no_data);
    plan = scheduler.prepare(peer(1), 250);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.signal_count == 1);
    EXPECT(plan.batch.signals[0].code ==
           wireless::TelemetrySignalCode::electrical_voltage);
    EXPECT(plan.batch.sequence == 1);
    EXPECT(scheduler.commit(peer(1), plan.token, true, 250).next_sequence == 2);
    EXPECT(scheduler.prepare(peer(1), 250).error ==
           wireless::PublishSchedulerError::no_data);
}

void test_deadband_minimum_interval_and_periodic_refresh() {
    wireless::TelemetryPublishScheduler scheduler{};
    EXPECT(scheduler.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto subscription = policy(
        wireless::TelemetrySignalCode::engine_speed, 100, 1000, 5000, 10);
    EXPECT(scheduler.add_peer(peer(1), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(scheduler.update_signal(
               subscription.code, snapshot(subscription.code, 1000), 0).accepted());
    auto plan = scheduler.prepare(peer(1), 0);
    EXPECT(plan.prepared());
    EXPECT(scheduler.commit(peer(1), plan.token, true, 0).committed());

    EXPECT(scheduler.update_signal(
               subscription.code, snapshot(subscription.code, 1005), 10).accepted());
    EXPECT(scheduler.prepare(peer(1), 100).error ==
           wireless::PublishSchedulerError::no_data);
    EXPECT(scheduler.update_signal(
               subscription.code, snapshot(subscription.code, 1010), 20).accepted());
    plan = scheduler.prepare(peer(1), 99);
    EXPECT(plan.error == wireless::PublishSchedulerError::no_data);
    plan = scheduler.prepare(peer(1), 100);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.signals[0].value.raw_value == 1010);
    EXPECT(scheduler.commit(peer(1), plan.token, true, 100).committed());
    EXPECT(scheduler.prepare(peer(1), 1099).error ==
           wireless::PublishSchedulerError::no_data);
    plan = scheduler.prepare(peer(1), 1100);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.signal_count == 1);
}

void test_quality_and_exact_stale_transitions_bypass_minimum_interval() {
    wireless::TelemetryPublishScheduler scheduler{};
    EXPECT(scheduler.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto subscription = policy(
        wireless::TelemetrySignalCode::engine_speed, 1000, 5000, 150, 100);
    EXPECT(scheduler.add_peer(peer(1), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(scheduler.update_signal(
               subscription.code,
               snapshot(subscription.code, 1000,
                        telemetry::SignalQuality::valid, 0),
               0).accepted());
    auto plan = scheduler.prepare(peer(1), 0);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.signals[0].source_age_ms == 0);
    EXPECT(plan.batch.signals[0].quality == telemetry::SignalQuality::valid);
    EXPECT(scheduler.commit(peer(1), plan.token, true, 0).committed());
    EXPECT(scheduler.prepare(peer(1), 149).error ==
           wireless::PublishSchedulerError::no_data);
    plan = scheduler.prepare(peer(1), 150);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.signals[0].quality == telemetry::SignalQuality::stale);
    EXPECT(!plan.batch.signals[0].value.present);
    EXPECT(plan.batch.signals[0].value.raw_value == 0);
    EXPECT(scheduler.commit(peer(1), plan.token, true, 150).committed());

    EXPECT(scheduler.update_signal(
               subscription.code, snapshot(subscription.code, 1000), 200).accepted());
    plan = scheduler.prepare(peer(1), 200);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.signals[0].quality == telemetry::SignalQuality::valid);
    EXPECT(plan.batch.signals[0].value.present);
    EXPECT(scheduler.commit(peer(1), plan.token, true, 200).committed());

    EXPECT(scheduler.update_signal(
               subscription.code,
               snapshot(subscription.code, 1000,
                        telemetry::SignalQuality::unavailable),
               250).accepted());
    plan = scheduler.prepare(peer(1), 250);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.signals[0].quality ==
           telemetry::SignalQuality::unavailable);
    EXPECT(!plan.batch.signals[0].value.present);
}

void test_rejected_plan_retries_same_sequence_and_token_checks() {
    wireless::TelemetryPublishScheduler scheduler{};
    EXPECT(scheduler.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto subscription = policy(
        wireless::TelemetrySignalCode::engine_speed);
    EXPECT(scheduler.add_peer(peer(1), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(scheduler.update_signal(
               subscription.code, snapshot(subscription.code, 1000), 10).accepted());
    auto plan = scheduler.prepare(peer(1), 10);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.sequence == 0);
    EXPECT(scheduler.commit(peer(1), plan.token + 1, true, 10).error ==
           wireless::PublishSchedulerError::plan_token_mismatch);
    EXPECT(scheduler.commit(peer(1), plan.token, true, 9).error ==
           wireless::PublishSchedulerError::clock_regressed);
    EXPECT(scheduler.commit(peer(1), plan.token, false, 10).committed());
    plan = scheduler.prepare(peer(1), 10);
    EXPECT(plan.prepared());
    EXPECT(plan.batch.sequence == 0);
    EXPECT(scheduler.commit(peer(1), plan.token, true, 10).next_sequence == 1);
    const auto status = scheduler.status();
    EXPECT(status.plans_prepared == 2);
    EXPECT(status.plans_transport_rejected == 1);
    EXPECT(status.plans_transport_accepted == 1);
}

void test_peers_have_independent_subscriptions_and_sequences() {
    wireless::TelemetryPublishScheduler scheduler{};
    EXPECT(scheduler.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto speed = policy(wireless::TelemetrySignalCode::engine_speed);
    const auto voltage = policy(
        wireless::TelemetrySignalCode::electrical_voltage);
    EXPECT(scheduler.add_peer(peer(1), &speed, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(scheduler.add_peer(peer(2), &voltage, 1) ==
           wireless::PublishSchedulerError::none);
    EXPECT(scheduler.update_signal(
               speed.code, snapshot(speed.code, 1000), 0).accepted());
    EXPECT(scheduler.update_signal(
               voltage.code, snapshot(voltage.code, 13800), 0).accepted());
    auto first = scheduler.prepare(peer(1), 0);
    auto second = scheduler.prepare(peer(2), 0);
    EXPECT(first.prepared() && second.prepared());
    EXPECT(first.batch.sequence == 0 && second.batch.sequence == 0);
    EXPECT(first.batch.signals[0].code == speed.code);
    EXPECT(second.batch.signals[0].code == voltage.code);
    EXPECT(scheduler.commit(peer(1), first.token, true, 0).next_sequence == 1);
    EXPECT(scheduler.commit(peer(2), second.token, true, 0).next_sequence == 1);
}

void test_fake_transport_acceptance_loss_and_receiver_gap() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport receiver{};
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(receiver.start(peer(2), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) == wireless::EspNowError::none);
    EXPECT(receiver.add_peer({peer(1), 6, true}) == wireless::EspNowError::none);
    sender.connect(receiver);

    wireless::TelemetryPublishScheduler scheduler{};
    EXPECT(scheduler.start({1, 2, 0}) ==
           wireless::PublishSchedulerError::none);
    const auto subscription = policy(
        wireless::TelemetrySignalCode::engine_speed, 0, 1000, 5000, 0);
    EXPECT(scheduler.add_peer(peer(2), &subscription, 1) ==
           wireless::PublishSchedulerError::none);
    wireless::TelemetryStreamTracker tracker{};
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> received{};

    auto deliver = [&](std::int64_t value, std::uint64_t now, bool drop) {
        EXPECT(scheduler.update_signal(
                   subscription.code, snapshot(subscription.code, value), now)
                   .accepted());
        const auto plan = scheduler.prepare(peer(2), now);
        EXPECT(plan.prepared());
        EXPECT(wireless::encode_telemetry_packet(
                   plan.batch, encoded.data(), encoded.size()).encoded());
        if (drop) {
            sender.drop_next_transmissions(1);
        }
        const auto sent = sender.send(
            peer(2), {encoded.data(), encoded.size()}, now);
        EXPECT(sent.accepted());
        EXPECT(scheduler.commit(peer(2), plan.token, sent.accepted(), now)
                   .committed());
        sender.service(now);
        return plan.batch.sequence;
    };

    EXPECT(deliver(1000, 0, false) == 0);
    auto received_result = receiver.receive({received.data(), received.size()});
    EXPECT(received_result.has_frame());
    auto decoded = wireless::decode_telemetry_packet(
        received.data(), received_result.received_bytes);
    EXPECT(decoded.decoded());
    EXPECT(tracker.ingest(decoded.batch, 0).accepted());
    EXPECT(sender.poll_delivery().receipt.radio_delivered());

    EXPECT(deliver(1001, 50, true) == 1);
    EXPECT(receiver.receive({received.data(), received.size()}).error ==
           wireless::EspNowError::no_data);
    EXPECT(sender.poll_delivery().receipt.outcome ==
           wireless::DeliveryOutcome::injected_loss);

    EXPECT(deliver(1002, 100, false) == 2);
    received_result = receiver.receive({received.data(), received.size()});
    EXPECT(received_result.has_frame());
    decoded = wireless::decode_telemetry_packet(
        received.data(), received_result.received_bytes);
    const auto gap = tracker.ingest(decoded.batch, 100);
    EXPECT(gap.accepted());
    EXPECT(gap.disposition == wireless::TelemetrySequenceDisposition::gap);
    EXPECT(gap.missing_packets == 1);
}

}  // namespace

int main() {
    test_lifecycle_peer_policy_and_capacity();
    test_signal_updates_validate_and_coalesce();
    test_initial_three_signal_batch_and_per_peer_sequence();
    test_deadband_minimum_interval_and_periodic_refresh();
    test_quality_and_exact_stale_transitions_bypass_minimum_interval();
    test_rejected_plan_retries_same_sequence_and_token_checks();
    test_peers_have_independent_subscriptions_and_sequences();
    test_fake_transport_acceptance_loss_and_receiver_gap();

    if (failures != 0) {
        std::cerr << failures << " publish scheduler assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 telemetry publish scheduler scenario groups\n";
    return EXIT_SUCCESS;
}
