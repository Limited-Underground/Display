#include <algorithm>
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
constexpr std::size_t kEntriesOffset = 20;
constexpr std::size_t kEntryBytes = 32;
constexpr std::size_t kCrcOffset = 276;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void refresh_crc(
    std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes>& checkpoint) {
    write_u32(checkpoint.data() + kCrcOffset,
              crc32(checkpoint.data(), kCrcOffset));
}

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

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> rejected_ack_frame(
    std::uint64_t event_id,
    std::uint32_t sequence) {
    CriticalAlertAck value{};
    value.disposition = AlertAckDisposition::rejected;
    // Keep the correlated event retained while exercising replay persistence.
    value.reason = AlertAckReason::rate_limited;
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

CriticalAlertAckTransportContext transport(
    std::uint32_t key_handle = kKeyHandle) {
    return {true, kPeerId, key_handle, kChannel};
}

void start_dependencies(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox,
    std::uint32_t authorization_epoch = 1,
    std::uint32_t key_handle = kKeyHandle) {
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
    EXPECT(registry.approve(1, key_handle, authorization_epoch, 0) ==
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
    std::uint64_t now_ms) {
    EXPECT(outbox.enqueue(alert_frame(event_id), now_ms) ==
           CriticalOutboxError::none);
    const auto prepared = outbox.prepare(now_ms);
    EXPECT(prepared.prepared());
    EXPECT(outbox.commit_local_send(prepared.token, true, now_ms) ==
           CriticalOutboxError::none);
}

std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes> checkpoint_with_replay(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress) {
    start_ingress(registry, outbox, ingress);
    send_alert(outbox, 1, 0);
    const auto ack = rejected_ack_frame(1, 10);
    EXPECT(ingress.receive(ack.data(), ack.size(), transport(), 1).processed());
    std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes> checkpoint{};
    EXPECT(ingress.export_checkpoint(checkpoint) ==
           CriticalAlertAckIngressError::none);
    return checkpoint;
}

void test_empty_and_bound_checkpoint_are_canonical() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress empty{};
    EXPECT(empty.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes> checkpoint{};
    EXPECT(empty.export_checkpoint(checkpoint) ==
           CriticalAlertAckIngressError::none);
    EXPECT(checkpoint[0] == 'O' && checkpoint[1] == 'A' &&
           checkpoint[2] == 'I' && checkpoint[3] == '0');
    EXPECT(checkpoint[4] == kCriticalAlertAckCheckpointVersion);
    EXPECT(checkpoint[16] == 0);
    EXPECT(std::all_of(
        checkpoint.begin() + kEntriesOffset,
        checkpoint.begin() + kCrcOffset,
        [](std::uint8_t byte) { return byte == 0; }));

    EXPECT(empty.bind_consumer_session(
               kPeerId, kConsumerId, kBootSession) ==
           CriticalAlertAckIngressError::none);
    EXPECT(empty.export_checkpoint(checkpoint) ==
           CriticalAlertAckIngressError::none);
    EXPECT(checkpoint[16] == 1);
    EXPECT(checkpoint[kEntriesOffset] == 1);
    EXPECT(checkpoint[kEntriesOffset + 1] == 0);
    EXPECT(crc32(checkpoint.data(), kCrcOffset) ==
           read_u32(checkpoint.data() + kCrcOffset));
}

void test_replay_window_restores_and_rejects_duplicate() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress source{};
    const auto checkpoint = checkpoint_with_replay(
        registry, outbox, source);
    CriticalAlertAckIngress restored{};
    EXPECT(restored.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::none);
    const auto duplicate = rejected_ack_frame(1, 10);
    EXPECT(restored.receive(
               duplicate.data(), duplicate.size(), transport(), 2).error ==
           CriticalAlertAckIngressError::replay_duplicate);

    send_alert(outbox, 2, 2);
    const auto next = rejected_ack_frame(2, 11);
    EXPECT(restored.receive(next.data(), next.size(), transport(), 3)
               .processed());
    EXPECT(restored.status().checkpoint_imports == 1);
}

void test_malformed_and_noncanonical_import_is_atomic() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress source{};
    auto checkpoint = checkpoint_with_replay(registry, outbox, source);
    CriticalAlertAckIngress restored{};
    EXPECT(restored.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(restored.bind_consumer_session(kPeerId, 99, 99) ==
           CriticalAlertAckIngressError::none);
    EXPECT(restored.import_checkpoint(nullptr, checkpoint.size()) ==
           CriticalAlertAckIngressError::checkpoint_malformed);
    EXPECT(restored.status().binding_count == 1);
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size() - 1) ==
           CriticalAlertAckIngressError::checkpoint_malformed);
    checkpoint[5] = 1;
    refresh_crc(checkpoint);
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::checkpoint_malformed);
    EXPECT(restored.status().binding_count == 1);
}

void test_version_and_producer_mismatch_are_incompatible() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress source{};
    auto checkpoint = checkpoint_with_replay(registry, outbox, source);
    CriticalAlertAckIngress restored{};
    EXPECT(restored.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    checkpoint[4] = 1;
    refresh_crc(checkpoint);
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::checkpoint_incompatible);
    checkpoint[4] = 0;
    checkpoint[8] ^= 1U;
    refresh_crc(checkpoint);
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::checkpoint_incompatible);
}

void test_crc_corruption_is_rejected_without_mutation() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress source{};
    auto checkpoint = checkpoint_with_replay(registry, outbox, source);
    CriticalAlertAckIngress restored{};
    EXPECT(restored.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(restored.bind_consumer_session(kPeerId, 77, 77) ==
           CriticalAlertAckIngressError::none);
    checkpoint[40] ^= 1U;
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::checkpoint_integrity_failure);
    EXPECT(restored.status().binding_count == 1);
}

void test_rotation_and_revoke_invalidate_binding_and_checkpoint() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress source{};
    const auto checkpoint = checkpoint_with_replay(
        registry, outbox, source);
    EXPECT(registry.rotate_key(kPeerId, 200, 2) ==
           PeerAuthorizationError::none);
    std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes> preserved{};
    preserved.fill(0xA5);
    EXPECT(source.export_checkpoint(preserved) ==
           CriticalAlertAckIngressError::checkpoint_authorization_mismatch);
    EXPECT(std::all_of(
        preserved.begin(), preserved.end(),
        [](std::uint8_t byte) { return byte == 0xA5; }));

    CriticalAlertAckIngress restored{};
    EXPECT(restored.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::checkpoint_authorization_mismatch);
    EXPECT(restored.bind_consumer_session(
               kPeerId, kConsumerId, kBootSession) ==
           CriticalAlertAckIngressError::none);
    const auto ack = rejected_ack_frame(1, 10);
    EXPECT(restored.receive(
               ack.data(), ack.size(), transport(200), 2).processed());
    EXPECT(registry.revoke(kPeerId) == PeerAuthorizationError::none);
    EXPECT(restored.export_checkpoint(preserved) ==
           CriticalAlertAckIngressError::checkpoint_authorization_mismatch);
}

void test_import_after_traffic_is_rejected() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress source{};
    const auto checkpoint = checkpoint_with_replay(
        registry, outbox, source);
    EXPECT(source.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::invalid_state);
    EXPECT(source.status().binding_count == 1);
    EXPECT(source.status().checkpoint_rejections == 1);
}

void test_duplicate_and_replay_invariants_fail_closed() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress source{};
    auto checkpoint = checkpoint_with_replay(registry, outbox, source);
    CriticalAlertAckIngress restored{};
    EXPECT(restored.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);

    checkpoint[kEntriesOffset + 28] = 0;
    refresh_crc(checkpoint);
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::checkpoint_malformed);

    EXPECT(source.export_checkpoint(checkpoint) ==
           CriticalAlertAckIngressError::none);
    checkpoint[16] = 2;
    auto* second = checkpoint.data() + kEntriesOffset + kEntryBytes;
    std::copy(
        checkpoint.data() + kEntriesOffset,
        checkpoint.data() + kEntriesOffset + kEntryBytes,
        second);
    refresh_crc(checkpoint);
    EXPECT(restored.import_checkpoint(
               checkpoint.data(), checkpoint.size()) ==
           CriticalAlertAckIngressError::checkpoint_malformed);
    EXPECT(restored.status().binding_count == 0);
}

}  // namespace

int main() {
    test_empty_and_bound_checkpoint_are_canonical();
    test_replay_window_restores_and_rejects_duplicate();
    test_malformed_and_noncanonical_import_is_atomic();
    test_version_and_producer_mismatch_are_incompatible();
    test_crc_corruption_is_rejected_without_mutation();
    test_rotation_and_revoke_invalidate_binding_and_checkpoint();
    test_import_after_traffic_is_rejected();
    test_duplicate_and_replay_invariants_fail_closed();

    if (failures != 0) {
        std::cerr << failures << " ACK checkpoint assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 critical alert ACK checkpoint scenario groups\n";
    return EXIT_SUCCESS;
}
