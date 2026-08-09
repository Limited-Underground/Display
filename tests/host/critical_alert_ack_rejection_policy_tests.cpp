#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/critical_alert_ack_ingress.hpp"

namespace {

using namespace opengauge::identity;
using namespace opengauge::integration;

constexpr std::uint64_t kProducerId = 1;
constexpr std::uint64_t kConsumerId = 2;
constexpr std::uint32_t kPeerId = 10;
constexpr std::uint32_t kKeyHandle = 100;
constexpr std::uint32_t kBootSession = 500;
constexpr std::uint8_t kChannel = 6;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

CriticalAlert alert(std::uint64_t event_id) {
    CriticalAlert value{};
    value.type = CriticalAlertType::engine_over_temperature;
    value.severity = AlertSeverity::critical;
    value.state = AlertState::asserted;
    value.quality = AlertQuality::valid;
    value.unit = AlertUnit::celsius;
    value.producer_id = kProducerId;
    value.vehicle_id = 3;
    value.event_id = event_id;
    value.condition_id = 1000 + event_id;
    value.age_ms = 10;
    value.value_milli = 110000;
    value.value_present = true;
    return value;
}

std::array<std::uint8_t, kCriticalAlertFrameBytes> alert_frame(
    std::uint64_t event_id) {
    std::array<std::uint8_t, kCriticalAlertFrameBytes> output{};
    EXPECT(encode_critical_alert(alert(event_id), output).encoded());
    return output;
}

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> rejection_frame(
    std::uint64_t event_id,
    std::uint32_t sequence,
    AlertAckReason reason) {
    CriticalAlertAck value{};
    value.disposition = AlertAckDisposition::rejected;
    value.reason = reason;
    value.state = AlertState::asserted;
    value.consumer_id = kConsumerId;
    value.producer_id = kProducerId;
    value.event_id = event_id;
    value.condition_id = 1000 + event_id;
    value.consumer_boot_session_id = kBootSession;
    value.ack_sequence = sequence;
    value.observed_alert_age_ms = 10;
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> output{};
    EXPECT(encode_critical_alert_ack(value, output).encoded());
    return output;
}

CriticalAlertAcknowledgement correlation(std::uint64_t event_id) {
    return {event_id, 1000 + event_id, AlertState::asserted};
}

CriticalAlertAckTransportContext transport() {
    return {true, kPeerId, kKeyHandle, kChannel};
}

void start_stack(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress,
    std::uint8_t maximum_attempts = 3,
    std::uint64_t maximum_lifetime_ms = 10000) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    const PairingCandidate bridge{
        1,
        kPeerId,
        PeerRole::trail_bridge,
        permission_bit(PeerPermission::receive_critical_alert) |
            permission_bit(PeerPermission::publish_alarm_ack),
        kChannel};
    EXPECT(registry.begin_approval(bridge, 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.approve(1, kKeyHandle, 1, 0) ==
           PeerAuthorizationError::none);
    EXPECT(outbox.start(
               {50, 100, 25, maximum_lifetime_ms, maximum_attempts, 1}) ==
           CriticalOutboxError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(ingress.bind_consumer_session(
               kPeerId, kConsumerId, kBootSession) ==
           CriticalAlertAckIngressError::none);
}

void send_alert(
    CriticalAlertOutbox& outbox,
    std::uint64_t event_id,
    std::uint64_t now_ms) {
    EXPECT(outbox.enqueue(alert_frame(event_id), now_ms) ==
           CriticalOutboxError::none);
    const auto prepared = outbox.prepare(now_ms);
    EXPECT(prepared.prepared());
    EXPECT(outbox.commit_local_send(prepared.token, true, now_ms) ==
           CriticalOutboxError::none);
}

CriticalAlertAckIngressResult reject(
    CriticalAlertAckIngress& ingress,
    std::uint64_t event_id,
    std::uint32_t sequence,
    AlertAckReason reason,
    std::uint64_t now_ms) {
    const auto frame = rejection_frame(event_id, sequence, reason);
    return ingress.receive(
        frame.data(), frame.size(), transport(), now_ms);
}

void test_all_reasons_have_one_deterministic_policy() {
    const std::array<AlertAckReason, 8> reasons{
        AlertAckReason::unauthorized,
        AlertAckReason::stale,
        AlertAckReason::duplicate,
        AlertAckReason::conflict,
        AlertAckReason::rate_limited,
        AlertAckReason::malformed,
        AlertAckReason::unsupported,
        AlertAckReason::internal_error};
    for (std::size_t index = 0; index < reasons.size(); ++index) {
        PeerAuthorizationRegistry registry{};
        CriticalAlertOutbox outbox{};
        CriticalAlertAckIngress ingress{};
        start_stack(registry, outbox, ingress);
        send_alert(outbox, index + 1, 0);
        const auto result = reject(
            ingress,
            index + 1,
            static_cast<std::uint32_t>(index + 1),
            reasons[index],
            1);
        const auto retry =
            reasons[index] == AlertAckReason::rate_limited ||
            reasons[index] == AlertAckReason::internal_error;
        EXPECT(result.processed());
        EXPECT(result.retry_released == retry);
        EXPECT(result.terminal_failure == !retry);
        EXPECT(result.remote_rejection_action ==
               (retry ? CriticalRemoteRejectionAction::retry
                      : CriticalRemoteRejectionAction::terminal));
        EXPECT(outbox.status().queued_count == (retry ? 1U : 0U));
        EXPECT(outbox.status().terminal_failures == (retry ? 0U : 1U));
    }
}

void test_retry_uses_exact_outbox_backoff() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_stack(registry, outbox, ingress);
    send_alert(outbox, 1, 0);
    EXPECT(reject(ingress, 1, 1, AlertAckReason::rate_limited, 1)
               .retry_released);
    EXPECT(outbox.prepare(25).error == CriticalOutboxError::no_frame_ready);
    const auto ready = outbox.prepare(26);
    EXPECT(ready.prepared());
    EXPECT(ready.event_id == 1);
}

void test_retry_at_attempt_limit_becomes_terminal() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_stack(registry, outbox, ingress, 2);
    send_alert(outbox, 1, 0);
    EXPECT(reject(ingress, 1, 1, AlertAckReason::internal_error, 1)
               .retry_released);
    const auto prepared = outbox.prepare(26);
    EXPECT(prepared.prepared());
    EXPECT(outbox.commit_local_send(prepared.token, true, 26) ==
           CriticalOutboxError::none);
    const auto terminal = reject(
        ingress, 1, 2, AlertAckReason::internal_error, 27);
    EXPECT(terminal.terminal_failure);
    EXPECT(terminal.remote_rejection_action ==
           CriticalRemoteRejectionAction::terminal);
    EXPECT(terminal.failure.reason ==
           CriticalDeliveryFailure::remote_rejection);
    EXPECT(terminal.failure.remote_reason == AlertAckReason::internal_error);
    EXPECT(terminal.failure.attempts == 2);
    EXPECT(outbox.status().queued_count == 0);
}

void test_terminal_failure_preserves_typed_correlation() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_stack(registry, outbox, ingress);
    send_alert(outbox, 7, 0);
    const auto result = reject(
        ingress, 7, 1, AlertAckReason::conflict, 1);
    EXPECT(result.terminal_failure);
    EXPECT(result.failure.event_id == 7);
    EXPECT(result.failure.condition_id == 1007);
    EXPECT(result.failure.reason == CriticalDeliveryFailure::remote_rejection);
    EXPECT(result.failure.remote_reason == AlertAckReason::conflict);
    EXPECT(result.failure.attempts == 1);
}

void test_late_retry_rejection_cancels_prepared_send() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_stack(registry, outbox, ingress);
    send_alert(outbox, 1, 0);
    EXPECT(outbox.advance(100).retries_released == 1);
    const auto prepared = outbox.prepare(125);
    EXPECT(prepared.prepared());
    const auto result = reject(
        ingress, 1, 1, AlertAckReason::rate_limited, 126);
    EXPECT(result.retry_released);
    EXPECT(!outbox.status().send_prepared);
    EXPECT(outbox.status().queued_count == 1);
    EXPECT(outbox.commit_local_send(prepared.token, true, 126) ==
           CriticalOutboxError::invalid_state);
}

void test_invalid_direct_requests_are_atomic() {
    CriticalAlertOutbox stopped{};
    EXPECT(stopped.apply_remote_rejection(
                      {},
                      AlertAckReason::stale,
                      0)
               .error == CriticalOutboxError::invalid_state);

    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
    send_alert(outbox, 1, 0);
    EXPECT(outbox.apply_remote_rejection(
                      correlation(1),
                      AlertAckReason::none,
                      1)
               .error == CriticalOutboxError::invalid_configuration);
    EXPECT(outbox.apply_remote_rejection(
                      correlation(1),
                      static_cast<AlertAckReason>(99),
                      1)
               .error == CriticalOutboxError::invalid_configuration);
    auto mismatch = correlation(1);
    ++mismatch.condition_id;
    EXPECT(outbox.apply_remote_rejection(
                      mismatch,
                      AlertAckReason::stale,
                      1)
               .error == CriticalOutboxError::acknowledgement_mismatch);
    EXPECT(outbox.status().in_flight_count == 1);
}

void test_remote_retry_still_obeys_maximum_lifetime() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_stack(registry, outbox, ingress, 3, 200);
    send_alert(outbox, 1, 0);
    EXPECT(reject(ingress, 1, 1, AlertAckReason::rate_limited, 190)
               .retry_released);
    const auto advanced = outbox.advance(200);
    EXPECT(advanced.failure_count == 1);
    EXPECT(advanced.failures[0].reason ==
           CriticalDeliveryFailure::maximum_lifetime);
    EXPECT(outbox.status().terminal_failures == 1);
}

void test_applied_rejection_consumes_sequence_once() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_stack(registry, outbox, ingress);
    send_alert(outbox, 1, 0);
    EXPECT(reject(ingress, 1, 9, AlertAckReason::rate_limited, 1)
               .processed());
    EXPECT(reject(ingress, 1, 9, AlertAckReason::rate_limited, 2).error ==
           CriticalAlertAckIngressError::replay_duplicate);
    EXPECT(ingress.status().processed == 1);
    EXPECT(ingress.status().remote_rejections == 1);
    EXPECT(ingress.status().remote_retries == 1);
    EXPECT(ingress.status().remote_terminal_failures == 0);
}

}  // namespace

int main() {
    test_all_reasons_have_one_deterministic_policy();
    test_retry_uses_exact_outbox_backoff();
    test_retry_at_attempt_limit_becomes_terminal();
    test_terminal_failure_preserves_typed_correlation();
    test_late_retry_rejection_cancels_prepared_send();
    test_invalid_direct_requests_are_atomic();
    test_remote_retry_still_obeys_maximum_lifetime();
    test_applied_rejection_consumes_sequence_once();

    if (failures != 0) {
        std::cerr << failures
                  << " critical alert ACK rejection policy assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 critical alert ACK rejection policy scenario groups\n";
    return EXIT_SUCCESS;
}
