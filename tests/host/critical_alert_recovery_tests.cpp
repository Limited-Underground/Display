#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/critical_alert_recovery.hpp"

namespace {

using namespace opengauge::identity;
using namespace opengauge::integration;

constexpr std::uint64_t kProducer = 1;
constexpr std::uint64_t kConsumer = 2;
constexpr std::uint32_t kPeer = 10;
constexpr std::uint32_t kKey = 100;
constexpr std::uint32_t kSession = 500;
constexpr std::uint8_t kChannel = 6;

int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

CriticalAlertOutboxConfiguration configuration() {
    return {50, 100, 25, 500, 3, 1};
}

std::array<std::uint8_t, kCriticalAlertFrameBytes> alert_frame() {
    CriticalAlert value{};
    value.type = CriticalAlertType::engine_over_temperature;
    value.severity = AlertSeverity::critical;
    value.state = AlertState::asserted;
    value.quality = AlertQuality::valid;
    value.unit = AlertUnit::celsius;
    value.producer_id = kProducer;
    value.vehicle_id = 3;
    value.event_id = 11;
    value.condition_id = 1011;
    value.age_ms = 10;
    value.value_present = true;
    value.value_milli = 110000;
    std::array<std::uint8_t, kCriticalAlertFrameBytes> output{};
    EXPECT(encode_critical_alert(value, output).encoded());
    return output;
}

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> rejection_frame() {
    CriticalAlertAck value{};
    value.disposition = AlertAckDisposition::rejected;
    value.reason = AlertAckReason::rate_limited;
    value.state = AlertState::asserted;
    value.consumer_id = kConsumer;
    value.producer_id = kProducer;
    value.event_id = 11;
    value.condition_id = 1011;
    value.consumer_boot_session_id = kSession;
    value.ack_sequence = 10;
    value.observed_alert_age_ms = 10;
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> output{};
    EXPECT(encode_critical_alert_ack(value, output).encoded());
    return output;
}

void start_registry(PeerAuthorizationRegistry& registry) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    const PairingCandidate candidate{
        1, kPeer, PeerRole::trail_bridge,
        permission_bit(PeerPermission::receive_critical_alert) |
            permission_bit(PeerPermission::publish_alarm_ack),
        kChannel};
    EXPECT(registry.begin_approval(candidate, 0) == PeerAuthorizationError::none);
    EXPECT(registry.approve(1, kKey, 1, 0) == PeerAuthorizationError::none);
}

void start_ingress(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress,
    bool bind) {
    EXPECT(ingress.start({kProducer, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    if (bind) {
        EXPECT(ingress.bind_consumer_session(kPeer, kConsumer, kSession) ==
               CriticalAlertAckIngressError::none);
    }
}

std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes>
make_live_checkpoint() {
    PeerAuthorizationRegistry registry{};
    start_registry(registry);
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress, true);
    EXPECT(outbox.enqueue(alert_frame(), 0) == CriticalOutboxError::none);
    const auto prepared = outbox.prepare(0);
    EXPECT(outbox.commit_local_send(prepared.token, true, 0) ==
           CriticalOutboxError::none);
    const auto rejected = rejection_frame();
    EXPECT(ingress.receive(
               rejected.data(), rejected.size(),
               {true, kPeer, kKey, kChannel}, 1).processed());
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> output{};
    const auto exported = export_critical_alert_recovery_checkpoint(
        ingress, outbox, 10, 7, output);
    EXPECT(exported.completed() && exported.generation == 7);
    return output;
}

void test_live_export_and_dual_restore() {
    const auto encoded = make_live_checkpoint();
    CriticalAlertRecoveryCheckpoint decoded{};
    EXPECT(decode_critical_alert_recovery_checkpoint(
               encoded.data(), encoded.size(), decoded) ==
           CriticalAlertRecoveryCheckpointError::none);
    EXPECT(decoded.generation == 7);

    PeerAuthorizationRegistry registry{};
    start_registry(registry);
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress, false);
    const auto restored = import_critical_alert_recovery_checkpoint(
        encoded.data(), encoded.size(), ingress, outbox, 100);
    EXPECT(restored.completed() && restored.generation == 7);
    EXPECT(ingress.status().binding_count == 1);
    EXPECT(outbox.status().queued_count == 1);
    EXPECT(outbox.prepare(115).error == CriticalOutboxError::no_frame_ready);
    EXPECT(outbox.prepare(116).event_id == 11);
    const auto duplicate = rejection_frame();
    EXPECT(ingress.receive(
               duplicate.data(), duplicate.size(),
               {true, kPeer, kKey, kChannel}, 117).error ==
           CriticalAlertAckIngressError::replay_duplicate);
}

void test_export_failures_preserve_output() {
    PeerAuthorizationRegistry registry{};
    start_registry(registry);
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress, true);
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> output{};
    output.fill(0xA5);
    const auto unchanged = output;
    EXPECT(export_critical_alert_recovery_checkpoint(
               ingress, outbox, 0, 0, output).error ==
           CriticalAlertRecoveryError::invalid_generation);
    ingress.stop();
    EXPECT(export_critical_alert_recovery_checkpoint(
               ingress, outbox, 0, 1, output).error ==
           CriticalAlertRecoveryError::ack_export_failed);
    EXPECT(output == unchanged);
}

void test_prepared_outbox_export_fails_closed() {
    PeerAuthorizationRegistry registry{};
    start_registry(registry);
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress, true);
    EXPECT(outbox.enqueue(alert_frame(), 0) == CriticalOutboxError::none);
    EXPECT(outbox.prepare(0).prepared());
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> output{};
    output.fill(0xA5);
    const auto unchanged = output;
    const auto result = export_critical_alert_recovery_checkpoint(
        ingress, outbox, 0, 1, output);
    EXPECT(result.error == CriticalAlertRecoveryError::outbox_export_failed);
    EXPECT(result.outbox_error == CriticalOutboxError::invalid_state);
    EXPECT(output == unchanged);
}

void test_policy_preflight_preserves_both_owners() {
    const auto encoded = make_live_checkpoint();
    PeerAuthorizationRegistry registry{};
    start_registry(registry);
    auto changed = configuration();
    ++changed.retry_backoff_ms;
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(changed) == CriticalOutboxError::none);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress, false);
    const auto result = import_critical_alert_recovery_checkpoint(
        encoded.data(), encoded.size(), ingress, outbox, 100);
    EXPECT(result.error ==
           CriticalAlertRecoveryError::outbox_preflight_failed);
    EXPECT(result.outbox_error == CriticalOutboxError::checkpoint_incompatible);
    EXPECT(ingress.status().binding_count == 0);
    EXPECT(outbox.status().queued_count == 0);
}

void test_authorization_and_corruption_preflight_are_atomic() {
    const auto encoded = make_live_checkpoint();
    PeerAuthorizationRegistry registry{};
    start_registry(registry);
    EXPECT(registry.rotate_key(kPeer, 200, 2) == PeerAuthorizationError::none);
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress, false);
    auto result = import_critical_alert_recovery_checkpoint(
        encoded.data(), encoded.size(), ingress, outbox, 100);
    EXPECT(result.error == CriticalAlertRecoveryError::ack_preflight_failed);
    EXPECT(ingress.status().binding_count == 0);
    EXPECT(outbox.status().queued_count == 0);
    auto corrupt = encoded;
    corrupt[100] ^= 1;
    result = import_critical_alert_recovery_checkpoint(
        corrupt.data(), corrupt.size(), ingress, outbox, 100);
    EXPECT(result.error == CriticalAlertRecoveryError::checkpoint_rejected);
    EXPECT(ingress.status().binding_count == 0);
    EXPECT(outbox.status().queued_count == 0);
}

}  // namespace

int main() {
    test_live_export_and_dual_restore();
    test_export_failures_preserve_output();
    test_prepared_outbox_export_fails_closed();
    test_policy_preflight_preserves_both_owners();
    test_authorization_and_corruption_preflight_are_atomic();
    if (failures != 0) {
        std::cerr << failures << " alert recovery assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 5 critical alert live recovery scenario groups\n";
    return EXIT_SUCCESS;
}
