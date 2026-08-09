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

CriticalAlert alert(
    std::uint64_t event_id,
    AlertState state = AlertState::asserted) {
    CriticalAlert value{};
    value.type = CriticalAlertType::engine_over_temperature;
    value.severity = AlertSeverity::critical;
    value.state = state;
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
    std::uint64_t event_id,
    AlertState state = AlertState::asserted) {
    std::array<std::uint8_t, kCriticalAlertFrameBytes> output{};
    EXPECT(encode_critical_alert(alert(event_id, state), output).encoded());
    return output;
}

CriticalAlertAck acknowledgement(
    std::uint64_t event_id,
    std::uint32_t sequence,
    AlertAckDisposition disposition = AlertAckDisposition::accepted,
    AlertAckReason reason = AlertAckReason::none,
    AlertState state = AlertState::asserted) {
    CriticalAlertAck value{};
    value.disposition = disposition;
    value.reason = reason;
    value.state = state;
    value.consumer_id = kConsumerId;
    value.producer_id = kProducerId;
    value.event_id = event_id;
    value.condition_id = 1000 + event_id;
    value.consumer_boot_session_id = kBootSession;
    value.ack_sequence = sequence;
    value.observed_alert_age_ms = 10;
    return value;
}

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> ack_frame(
    const CriticalAlertAck& value) {
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> output{};
    EXPECT(encode_critical_alert_ack(value, output).encoded());
    return output;
}

CriticalAlertAckTransportContext transport() {
    return {true, kPeerId, kKeyHandle, kChannel};
}

void start_dependencies(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox) {
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
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
}

void start_ingress(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress) {
    start_dependencies(registry, outbox);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(ingress.bind_consumer_session(
               kPeerId, kConsumerId, kBootSession) ==
           CriticalAlertAckIngressError::none);
}

void send_alert(
    CriticalAlertOutbox& outbox,
    std::uint64_t event_id,
    std::uint64_t now_ms,
    AlertState state = AlertState::asserted) {
    EXPECT(outbox.enqueue(alert_frame(event_id, state), now_ms) ==
           CriticalOutboxError::none);
    const auto prepared = outbox.prepare(now_ms);
    EXPECT(prepared.prepared());
    EXPECT(outbox.commit_local_send(prepared.token, true, now_ms) ==
           CriticalOutboxError::none);
}

void test_lifecycle_configuration_and_bounded_bindings() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    EXPECT(ingress.bind_consumer_session(1, 2, 3) ==
           CriticalAlertAckIngressError::invalid_state);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::invalid_configuration);
    start_dependencies(registry, outbox);
    EXPECT(ingress.start({0, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::invalid_configuration);
    EXPECT(ingress.start({kProducerId, 86400001U}, registry, outbox) ==
           CriticalAlertAckIngressError::invalid_configuration);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::invalid_state);
    EXPECT(ingress.bind_consumer_session(0, 2, 3) ==
           CriticalAlertAckIngressError::invalid_binding);
    EXPECT(ingress.bind_consumer_session(1, 100, 1) ==
           CriticalAlertAckIngressError::none);
    EXPECT(ingress.bind_consumer_session(2, 100, 1) ==
           CriticalAlertAckIngressError::duplicate_consumer);
    for (std::uint32_t peer = 2; peer <= 8; ++peer) {
        EXPECT(ingress.bind_consumer_session(
                   peer, 100 + peer, peer) ==
               CriticalAlertAckIngressError::none);
    }
    EXPECT(ingress.bind_consumer_session(9, 109, 9) ==
           CriticalAlertAckIngressError::capacity_full);
    EXPECT(ingress.unbind_consumer(9) ==
           CriticalAlertAckIngressError::unknown_consumer);
    EXPECT(ingress.unbind_consumer(8) ==
           CriticalAlertAckIngressError::none);
    EXPECT(ingress.status().binding_count == 7);
    ingress.stop();
    EXPECT(ingress.receive(nullptr, 0, transport(), 0).error ==
           CriticalAlertAckIngressError::invalid_state);
}

void test_authenticated_accepted_ack_completes_exact_outbox_entry() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    send_alert(outbox, 1, 0);
    const auto frame = ack_frame(acknowledgement(1, 7));
    const auto result = ingress.receive(
        frame.data(), frame.size(), transport(), 1);
    EXPECT(result.processed());
    EXPECT(result.disposition == AlertAckDisposition::accepted);
    EXPECT(result.outbox_completed);
    EXPECT(outbox.status().queued_count == 0);
    EXPECT(outbox.status().in_flight_count == 0);
    EXPECT(outbox.status().acknowledgements == 1);
    EXPECT(ingress.status().accepted == 1);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 2).error ==
           CriticalAlertAckIngressError::replay_duplicate);
}

void test_transport_authentication_authorization_and_role_scope() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    send_alert(outbox, 1, 0);
    const auto frame = ack_frame(acknowledgement(1, 1));
    auto context = transport();
    context.authenticated = false;
    EXPECT(ingress.receive(frame.data(), frame.size(), context, 1).error ==
           CriticalAlertAckIngressError::transport_not_authenticated);
    context = transport();
    context.secure_key_handle += 1;
    auto result = ingress.receive(frame.data(), frame.size(), context, 1);
    EXPECT(result.error ==
           CriticalAlertAckIngressError::authorization_denied);
    EXPECT(result.authorization_error ==
           PeerAuthorizationError::key_mismatch);
    context = transport();
    context.channel += 1;
    result = ingress.receive(frame.data(), frame.size(), context, 1);
    EXPECT(result.authorization_error ==
           PeerAuthorizationError::channel_mismatch);
    EXPECT(outbox.status().in_flight_count == 1);
    EXPECT(ingress.status().transport_denials == 3);
}

void test_codec_identity_session_and_age_fail_closed() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    send_alert(outbox, 1, 0);
    auto value = acknowledgement(1, 1);
    auto frame = ack_frame(value);
    frame[0] = 'X';
    auto result = ingress.receive(frame.data(), frame.size(), transport(), 1);
    EXPECT(result.error == CriticalAlertAckIngressError::codec_rejected);
    EXPECT(result.codec_error == AlertAckCodecError::malformed);
    value.consumer_id += 1;
    frame = ack_frame(value);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 1).error ==
           CriticalAlertAckIngressError::consumer_mismatch);
    value = acknowledgement(1, 1);
    value.producer_id += 1;
    frame = ack_frame(value);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 1).error ==
           CriticalAlertAckIngressError::producer_mismatch);
    value = acknowledgement(1, 1);
    value.consumer_boot_session_id += 1;
    frame = ack_frame(value);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 1).error ==
           CriticalAlertAckIngressError::session_mismatch);
    value = acknowledgement(1, 1);
    value.observed_alert_age_ms = 1001;
    frame = ack_frame(value);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 1).error ==
           CriticalAlertAckIngressError::observed_age_exceeded);
    EXPECT(outbox.status().in_flight_count == 1);
    EXPECT(ingress.status().codec_rejections == 1);
    EXPECT(ingress.status().identity_rejections == 4);
}

void test_replay_window_accepts_bounded_reordering_and_rejects_duplicates() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    for (std::uint64_t event = 1; event <= 3; ++event) {
        send_alert(outbox, event, event - 1);
    }
    auto frame = ack_frame(acknowledgement(
        1, 100, AlertAckDisposition::rejected, AlertAckReason::stale));
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 3)
               .processed());
    frame = ack_frame(acknowledgement(
        2, 102, AlertAckDisposition::rejected, AlertAckReason::stale));
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 4)
               .processed());
    frame = ack_frame(acknowledgement(
        3, 101, AlertAckDisposition::rejected, AlertAckReason::stale));
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 5)
               .processed());
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 6).error ==
           CriticalAlertAckIngressError::replay_duplicate);
    EXPECT(outbox.status().in_flight_count == 3);
    EXPECT(ingress.status().remote_rejections == 3);
}

void test_replay_old_ambiguous_and_explicit_session_rebind() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    for (std::uint64_t event = 1; event <= 4; ++event) {
        send_alert(outbox, event, event - 1);
    }
    auto frame = ack_frame(acknowledgement(
        1, 100, AlertAckDisposition::rejected, AlertAckReason::stale));
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 4)
               .processed());
    frame = ack_frame(acknowledgement(
        2, 132, AlertAckDisposition::rejected, AlertAckReason::stale));
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 5)
               .processed());
    frame = ack_frame(acknowledgement(
        1, 100, AlertAckDisposition::rejected, AlertAckReason::stale));
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 6).error ==
           CriticalAlertAckIngressError::replay_too_old);

    EXPECT(ingress.bind_consumer_session(kPeerId, kConsumerId, 501) ==
           CriticalAlertAckIngressError::none);
    auto value = acknowledgement(
        3, 0, AlertAckDisposition::rejected, AlertAckReason::stale);
    frame = ack_frame(value);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 7).error ==
           CriticalAlertAckIngressError::session_mismatch);
    value.consumer_boot_session_id = 501;
    frame = ack_frame(value);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 7)
               .processed());
    value = acknowledgement(
        4,
        0x80000000U,
        AlertAckDisposition::rejected,
        AlertAckReason::stale);
    value.consumer_boot_session_id = 501;
    frame = ack_frame(value);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 8).error ==
           CriticalAlertAckIngressError::replay_ambiguous);
}

void test_remote_rejection_is_correlated_but_never_success() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    send_alert(outbox, 1, 0);
    const auto frame = ack_frame(acknowledgement(
        1,
        1,
        AlertAckDisposition::rejected,
        AlertAckReason::rate_limited));
    const auto result = ingress.receive(
        frame.data(), frame.size(), transport(), 1);
    EXPECT(result.processed());
    EXPECT(result.disposition == AlertAckDisposition::rejected);
    EXPECT(result.reason == AlertAckReason::rate_limited);
    EXPECT(!result.outbox_completed);
    EXPECT(outbox.status().in_flight_count == 1);
    EXPECT(outbox.status().acknowledgements == 0);
    EXPECT(ingress.status().remote_rejections == 1);
}

void test_outbox_mismatch_and_clock_regression_do_not_consume_sequence() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    EXPECT(outbox.enqueue(alert_frame(1), 0) == CriticalOutboxError::none);
    auto frame = ack_frame(acknowledgement(1, 1));
    auto result = ingress.receive(frame.data(), frame.size(), transport(), 1);
    EXPECT(result.error == CriticalAlertAckIngressError::outbox_mismatch);
    EXPECT(result.outbox_error ==
           CriticalOutboxError::acknowledgement_mismatch);
    const auto prepared = outbox.prepare(1);
    EXPECT(outbox.commit_local_send(prepared.token, true, 1) ==
           CriticalOutboxError::none);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 2)
               .processed());

    send_alert(outbox, 2, 2);
    frame = ack_frame(acknowledgement(2, 2));
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 1).error ==
           CriticalAlertAckIngressError::clock_regression);
    EXPECT(outbox.status().in_flight_count == 1);
    EXPECT(ingress.receive(frame.data(), frame.size(), transport(), 3)
               .processed());
    EXPECT(ingress.status().outbox_rejections == 1);
    EXPECT(ingress.status().clock_regressions == 1);
}

}  // namespace

int main() {
    test_lifecycle_configuration_and_bounded_bindings();
    test_authenticated_accepted_ack_completes_exact_outbox_entry();
    test_transport_authentication_authorization_and_role_scope();
    test_codec_identity_session_and_age_fail_closed();
    test_replay_window_accepts_bounded_reordering_and_rejects_duplicates();
    test_replay_old_ambiguous_and_explicit_session_rebind();
    test_remote_rejection_is_correlated_but_never_success();
    test_outbox_mismatch_and_clock_regression_do_not_consume_sequence();

    if (failures != 0) {
        std::cerr << failures << " critical alert ACK ingress assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 critical alert ACK ingress scenario groups\n";
    return EXIT_SUCCESS;
}
