#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/critical_alert_system_recovery.hpp"

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

class FakeKeyValidator final : public CriticalAlertSystemRecoveryKeyValidator {
public:
    CriticalAlertSystemRecoveryKeyValidationError response{
        CriticalAlertSystemRecoveryKeyValidationError::none};
    std::size_t calls{0};
    std::uint32_t last_peer_id{0};

    CriticalAlertSystemRecoveryKeyValidationError validate(
        const PeerAuthorizationEntry& peer) override {
        ++calls;
        last_peer_id = peer.logical_peer_id;
        EXPECT(peer.active);
        EXPECT(peer.secure_key_handle != 0);
        return response;
    }
};

void approve_bridge(PeerAuthorizationRegistry& registry,
                    std::uint32_t request = 1,
                    std::uint32_t peer = kPeerId,
                    std::uint32_t key = kKeyHandle,
                    std::uint32_t epoch = 1) {
    const PairingCandidate bridge{
        request, peer, PeerRole::trail_bridge,
        permission_bit(PeerPermission::receive_critical_alert) |
            permission_bit(PeerPermission::publish_alarm_ack),
        kChannel};
    EXPECT(registry.begin_approval(bridge, 0) == PeerAuthorizationError::none);
    EXPECT(registry.approve(request, key, epoch, 0) ==
           PeerAuthorizationError::none);
}

CriticalAlert alert() {
    CriticalAlert value{};
    value.type = CriticalAlertType::engine_over_temperature;
    value.severity = AlertSeverity::critical;
    value.state = AlertState::asserted;
    value.quality = AlertQuality::valid;
    value.unit = AlertUnit::celsius;
    value.producer_id = kProducerId;
    value.vehicle_id = 3;
    value.event_id = 7;
    value.condition_id = 70;
    value.age_ms = 10;
    value.value_milli = 110000;
    value.value_present = true;
    return value;
}

std::array<std::uint8_t, kCriticalAlertFrameBytes> alert_frame() {
    std::array<std::uint8_t, kCriticalAlertFrameBytes> output{};
    EXPECT(encode_critical_alert(alert(), output).encoded());
    return output;
}

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> rejection_frame() {
    CriticalAlertAck value{};
    value.disposition = AlertAckDisposition::rejected;
    value.reason = AlertAckReason::rate_limited;
    value.state = AlertState::asserted;
    value.consumer_id = kConsumerId;
    value.producer_id = kProducerId;
    value.event_id = 7;
    value.condition_id = 70;
    value.consumer_boot_session_id = kBootSession;
    value.ack_sequence = 10;
    value.observed_alert_age_ms = 10;
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> output{};
    EXPECT(encode_critical_alert_ack(value, output).encoded());
    return output;
}

CriticalAlertAckTransportContext transport() {
    return {true, kPeerId, kKeyHandle, kChannel};
}

void start_source(PeerAuthorizationRegistry& registry,
                  CriticalAlertOutbox& outbox,
                  CriticalAlertAckIngress& ingress) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve_bridge(registry);
    approve_bridge(registry, 2, 20, 200);
    EXPECT(registry.revoke(20) == PeerAuthorizationError::none);
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(ingress.bind_consumer_session(
               kPeerId, kConsumerId, kBootSession) ==
           CriticalAlertAckIngressError::none);
    EXPECT(outbox.enqueue(alert_frame(), 0) == CriticalOutboxError::none);
    const auto prepared = outbox.prepare(0);
    EXPECT(prepared.prepared());
    EXPECT(outbox.commit_local_send(prepared.token, true, 0) ==
           CriticalOutboxError::none);
    const auto ack = rejection_frame();
    const auto received = ingress.receive(
        ack.data(), ack.size(), transport(), 1);
    EXPECT(received.processed() && received.retry_released);
}

void start_target(PeerAuthorizationRegistry& registry,
                  CriticalAlertOutbox& outbox,
                  CriticalAlertAckIngress& ingress,
                  std::uint64_t approval_window = 1000,
                  std::uint32_t acknowledgement_timeout = 100) {
    EXPECT(registry.start({approval_window}) == PeerAuthorizationError::none);
    EXPECT(outbox.start({50, acknowledgement_timeout, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
}

std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
checkpoint(std::uint64_t generation = 7) {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_source(registry, outbox, ingress);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        output{};
    EXPECT(export_critical_alert_system_recovery_checkpoint(
               registry, ingress, outbox, 1, generation, output).completed());
    return output;
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

void repair_outer_crc(
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>& bytes) {
    constexpr auto offset = kCriticalAlertSystemRecoveryCheckpointBytes - 4;
    write_u32(bytes.data() + offset,
              critical_alert_system_recovery_checkpoint_crc32(
                  bytes.data(), offset));
}

void repair_authorization_crc(
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes>& bytes) {
    constexpr auto offset = kPeerAuthorizationCheckpointBytes - 4;
    write_u32(bytes.data() + offset,
              peer_authorization_checkpoint_crc32(bytes.data(), offset));
}

void expect_empty(const PeerAuthorizationRegistry& registry,
                  const CriticalAlertOutbox& outbox,
                  const CriticalAlertAckIngress& ingress) {
    EXPECT(registry.status().peer_count == 0);
    EXPECT(outbox.status().queued_count == 0);
    EXPECT(outbox.status().in_flight_count == 0);
    EXPECT(ingress.status().binding_count == 0);
}

void test_layout_decode_and_generation_binding() {
    const auto encoded = checkpoint(0x0102030405060708ULL);
    EXPECT(encoded[0] == 'O' && encoded[1] == 'R' && encoded[2] == 'S' &&
           encoded[3] == '0' && encoded[4] == 0 && encoded[5] == 24);
    EXPECT(encoded[6] == 0 && encoded[7] == 1);
    EXPECT(encoded[8] == 0xC0 && encoded[9] == 0x03);
    EXPECT(encoded[16] == 0x08 && encoded[23] == 0x01);
    EXPECT(encoded[24] == 'O' && encoded[25] == 'P' &&
           encoded[280] == 'O' && encoded[281] == 'C');
    CriticalAlertSystemRecoveryCheckpoint decoded{};
    EXPECT(decode_critical_alert_system_recovery_checkpoint(
               encoded.data(), encoded.size(), decoded) ==
           CriticalAlertSystemRecoveryCheckpointError::none);
    EXPECT(decoded.generation == 0x0102030405060708ULL);
}

void test_all_three_live_owners_restore_together() {
    const auto encoded = checkpoint();
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto restored = import_critical_alert_system_recovery_checkpoint(
        encoded.data(), encoded.size(), registry, ingress, outbox, 100);
    EXPECT(restored.completed() && restored.generation == 7);
    EXPECT(registry.authorize(
               kPeerId, kKeyHandle, kChannel,
               PeerPermission::publish_alarm_ack).authorized);
    EXPECT(ingress.status().binding_count == 1);
    EXPECT(outbox.status().queued_count == 1);
    const auto duplicate = rejection_frame();
    EXPECT(ingress.receive(
               duplicate.data(), duplicate.size(), transport(), 100).error ==
           CriticalAlertAckIngressError::replay_duplicate);
    EXPECT(!outbox.prepare(124).prepared());
    const auto retry = outbox.prepare(125);
    EXPECT(retry.prepared() && retry.event_id == 7);
}

void test_outer_and_nested_corruption_are_atomic() {
    const auto original = checkpoint();
    auto changed = original;
    changed[500] ^= 1;
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    auto result = import_critical_alert_system_recovery_checkpoint(
        changed.data(), changed.size(), registry, ingress, outbox, 100);
    EXPECT(result.error == CriticalAlertSystemRecoveryError::checkpoint_rejected);
    EXPECT(result.checkpoint_error ==
           CriticalAlertSystemRecoveryCheckpointError::integrity_failure);
    expect_empty(registry, outbox, ingress);

    changed = original;
    changed[24 + 100] ^= 1;
    repair_outer_crc(changed);
    result = import_critical_alert_system_recovery_checkpoint(
        changed.data(), changed.size(), registry, ingress, outbox, 100);
    EXPECT(result.checkpoint_error ==
           CriticalAlertSystemRecoveryCheckpointError::
               invalid_authorization_checkpoint);
    expect_empty(registry, outbox, ingress);
}

void test_epoch_mismatch_fails_before_any_live_import() {
    CriticalAlertSystemRecoveryCheckpoint decoded{};
    const auto original = checkpoint();
    EXPECT(decode_critical_alert_system_recovery_checkpoint(
               original.data(), original.size(), decoded) ==
           CriticalAlertSystemRecoveryCheckpointError::none);
    decoded.authorization[24 + 16] = 2;
    repair_authorization_crc(decoded.authorization);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        mismatched{};
    EXPECT(encode_critical_alert_system_recovery_checkpoint(
               decoded, mismatched) ==
           CriticalAlertSystemRecoveryCheckpointError::none);

    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto result = import_critical_alert_system_recovery_checkpoint(
        mismatched.data(), mismatched.size(), registry, ingress, outbox, 100);
    EXPECT(result.error ==
           CriticalAlertSystemRecoveryError::critical_preflight_failed);
    EXPECT(result.critical.error ==
           CriticalAlertRecoveryError::ack_preflight_failed);
    EXPECT(result.critical.ack_error ==
           CriticalAlertAckIngressError::checkpoint_authorization_mismatch);
    expect_empty(registry, outbox, ingress);
}

void test_policy_and_runtime_preflights_are_atomic() {
    const auto encoded = checkpoint();
    PeerAuthorizationRegistry wrong_peer_policy{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(wrong_peer_policy, outbox, ingress, 1001);
    auto result = import_critical_alert_system_recovery_checkpoint(
        encoded.data(), encoded.size(), wrong_peer_policy, ingress, outbox, 100);
    EXPECT(result.error ==
           CriticalAlertSystemRecoveryError::authorization_preflight_failed);
    EXPECT(result.authorization_error ==
           PeerAuthorizationError::checkpoint_incompatible);
    expect_empty(wrong_peer_policy, outbox, ingress);

    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox wrong_outbox{};
    CriticalAlertAckIngress wrong_ingress{};
    start_target(registry, wrong_outbox, wrong_ingress, 1000, 101);
    result = import_critical_alert_system_recovery_checkpoint(
        encoded.data(), encoded.size(), registry, wrong_ingress, wrong_outbox, 100);
    EXPECT(result.error ==
           CriticalAlertSystemRecoveryError::critical_preflight_failed);
    EXPECT(result.critical.outbox_error ==
           CriticalOutboxError::checkpoint_incompatible);
    expect_empty(registry, wrong_outbox, wrong_ingress);

    PeerAuthorizationRegistry dirty_registry{};
    CriticalAlertOutbox dirty_outbox{};
    CriticalAlertAckIngress dirty_ingress{};
    start_target(dirty_registry, dirty_outbox, dirty_ingress);
    dirty_ingress.stop();
    result = import_critical_alert_system_recovery_checkpoint(
        encoded.data(), encoded.size(), dirty_registry,
        dirty_ingress, dirty_outbox, 100);
    EXPECT(result.error ==
           CriticalAlertSystemRecoveryError::critical_preflight_failed);
    EXPECT(result.critical.ack_error ==
           CriticalAlertAckIngressError::invalid_state);
    expect_empty(dirty_registry, dirty_outbox, dirty_ingress);
}

void test_export_failures_preserve_output() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        output{};
    output.fill(0xA5);
    const auto unchanged = output;
    EXPECT(export_critical_alert_system_recovery_checkpoint(
               registry, ingress, outbox, 0, 0, output).error ==
           CriticalAlertSystemRecoveryError::invalid_generation);
    EXPECT(output == unchanged);
    EXPECT(registry.begin_approval(
               {1, kPeerId, PeerRole::trail_bridge,
                permission_bit(PeerPermission::publish_alarm_ack), kChannel},
               0) == PeerAuthorizationError::none);
    const auto pending = export_critical_alert_system_recovery_checkpoint(
        registry, ingress, outbox, 0, 1, output);
    EXPECT(pending.error ==
           CriticalAlertSystemRecoveryError::authorization_export_failed);
    EXPECT(pending.authorization_error == PeerAuthorizationError::invalid_state);
    EXPECT(output == unchanged);
}

void test_key_preflight_accepts_active_and_skips_revoked() {
    const auto encoded = checkpoint();
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    FakeKeyValidator validator{};
    const auto result =
        import_critical_alert_system_recovery_checkpoint_validating_keys(
            encoded.data(), encoded.size(), registry, ingress, outbox, 100,
            validator);
    EXPECT(result.completed());
    EXPECT(validator.calls == 1);
    EXPECT(validator.last_peer_id == kPeerId);
    EXPECT(registry.status().peer_count == 2);
}

void test_key_preflight_failures_are_typed_and_atomic() {
    const auto encoded = checkpoint();
    constexpr std::array<CriticalAlertSystemRecoveryKeyValidationError, 3>
        failures_to_inject{
            CriticalAlertSystemRecoveryKeyValidationError::key_unavailable,
            CriticalAlertSystemRecoveryKeyValidationError::purpose_mismatch,
            CriticalAlertSystemRecoveryKeyValidationError::backend_failure};
    for (const auto injected : failures_to_inject) {
        PeerAuthorizationRegistry registry{};
        CriticalAlertOutbox outbox{};
        CriticalAlertAckIngress ingress{};
        start_target(registry, outbox, ingress);
        FakeKeyValidator validator{};
        validator.response = injected;
        const auto result =
            import_critical_alert_system_recovery_checkpoint_validating_keys(
                encoded.data(), encoded.size(), registry, ingress, outbox, 100,
                validator);
        EXPECT(result.error == CriticalAlertSystemRecoveryError::
                                   authorization_key_preflight_failed);
        EXPECT(result.key_validation_error == injected);
        EXPECT(result.key_validation_peer_id == kPeerId);
        EXPECT(result.generation == 7);
        EXPECT(validator.calls == 1);
        expect_empty(registry, outbox, ingress);
    }
}

}  // namespace

int main() {
    test_layout_decode_and_generation_binding();
    test_all_three_live_owners_restore_together();
    test_outer_and_nested_corruption_are_atomic();
    test_epoch_mismatch_fails_before_any_live_import();
    test_policy_and_runtime_preflights_are_atomic();
    test_export_failures_preserve_output();
    test_key_preflight_accepts_active_and_skips_revoked();
    test_key_preflight_failures_are_typed_and_atomic();
    if (failures != 0) {
        std::cerr << failures << " system recovery assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 critical alert system recovery scenario groups\n";
    return EXIT_SUCCESS;
}
