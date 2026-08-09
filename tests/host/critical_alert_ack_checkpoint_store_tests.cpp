#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_critical_alert_ack_checkpoint_storage.hpp"

namespace {

using namespace opengauge::identity;
using namespace opengauge::integration;
using namespace opengauge::integration::test_support;

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
    std::uint32_t sequence) {
    CriticalAlertAck value{};
    value.disposition = AlertAckDisposition::rejected;
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

void approve_bridge(
    PeerAuthorizationRegistry& registry,
    std::uint32_t key_handle = kKeyHandle,
    std::uint32_t authorization_epoch = 1) {
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
}

void start_dependencies(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve_bridge(registry);
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
}

void start_ingress(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress,
    bool bind = true) {
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    if (bind) {
        EXPECT(ingress.bind_consumer_session(
                   kPeerId, kConsumerId, kBootSession) ==
               CriticalAlertAckIngressError::none);
    }
}

void send_and_reject(
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress,
    std::uint64_t event_id,
    std::uint32_t sequence,
    std::uint64_t now_ms) {
    EXPECT(outbox.enqueue(alert_frame(event_id), now_ms) ==
           CriticalOutboxError::none);
    const auto prepared = outbox.prepare(now_ms);
    EXPECT(prepared.prepared());
    EXPECT(outbox.commit_local_send(prepared.token, true, now_ms) ==
           CriticalOutboxError::none);
    const auto frame = rejection_frame(event_id, sequence);
    EXPECT(ingress.receive(
               frame.data(), frame.size(), transport(), now_ms + 1)
               .processed());
}

void resend_and_reject(
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress,
    std::uint64_t event_id,
    std::uint32_t sequence,
    std::uint64_t now_ms) {
    const auto prepared = outbox.prepare(now_ms);
    EXPECT(prepared.prepared());
    EXPECT(prepared.event_id == event_id);
    EXPECT(outbox.commit_local_send(prepared.token, true, now_ms) ==
           CriticalOutboxError::none);
    const auto frame = rejection_frame(event_id, sequence);
    EXPECT(ingress.receive(
               frame.data(), frame.size(), transport(), now_ms + 1)
               .processed());
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

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void repair_envelope_crc(
    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes>& bytes) {
    constexpr std::size_t offset = kCriticalAlertAckStoredCheckpointBytes - 4;
    write_u32(bytes.data() + offset, crc32(bytes.data(), offset));
}

void test_explicit_envelope_and_first_save() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    constexpr std::uint64_t generation = 0x0102030405060708ULL;
    const auto saved = store.save(ingress, generation);
    EXPECT(saved.saved());
    EXPECT(saved.written_slot == CriticalAlertAckCheckpointSource::slot_a);
    EXPECT(saved.generation == generation);
    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes> bytes{};
    EXPECT(storage.read_slot(0, bytes.data(), bytes.size()) ==
           CriticalAlertAckCheckpointStorageError::none);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'A' &&
           bytes[2] == 'S' && bytes[3] == '0');
    EXPECT(bytes[4] == 0 && bytes[5] == 24);
    EXPECT(bytes[6] == 0x18 && bytes[7] == 0x01);
    EXPECT(bytes[8] == 0x08 && bytes[15] == 0x01);
    EXPECT(bytes[24] == 'O' && bytes[25] == 'A' &&
           bytes[26] == 'I' && bytes[27] == '0');
}

void test_empty_invalid_generation_and_stopped_export() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress, false);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    const auto empty = store.restore(ingress);
    EXPECT(empty.error == CriticalAlertAckCheckpointStoreError::no_checkpoint);
    EXPECT(!empty.restored);
    EXPECT(ingress.status().binding_count == 0);
    EXPECT(store.save(ingress, 0).error ==
           CriticalAlertAckCheckpointStoreError::invalid_generation);
    ingress.stop();
    const auto stopped = store.save(ingress, 1);
    EXPECT(stopped.error ==
           CriticalAlertAckCheckpointStoreError::checkpoint_rejected);
    EXPECT(stopped.ingress_error == CriticalAlertAckIngressError::invalid_state);
    EXPECT(!storage.present(0));
}

void test_newest_generation_restores_replay_atomically() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress source{};
    start_ingress(registry, outbox, source);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    send_and_reject(outbox, source, 1, 10, 0);
    EXPECT(store.save(source, 1).saved());
    resend_and_reject(outbox, source, 1, 11, 30);
    EXPECT(store.save(source, 2).saved());
    source.stop();

    CriticalAlertAckIngress restored{};
    start_ingress(registry, outbox, restored, false);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.error == CriticalAlertAckCheckpointStoreError::none);
    EXPECT(loaded.source == CriticalAlertAckCheckpointSource::slot_b);
    EXPECT(loaded.generation == 2);
    EXPECT(restored.status().binding_count == 1);
    const auto duplicate = rejection_frame(1, 11);
    EXPECT(restored.receive(
               duplicate.data(), duplicate.size(), transport(), 40).error ==
           CriticalAlertAckIngressError::replay_duplicate);
}

void test_partial_write_preserves_last_good_checkpoint() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress source{};
    start_ingress(registry, outbox, source);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    send_and_reject(outbox, source, 1, 10, 0);
    EXPECT(store.save(source, 1).saved());
    storage.set_next_write_behavior(
        1, FakeCheckpointWriteBehavior::fail_after_partial_write);
    EXPECT(store.save(source, 2).error ==
           CriticalAlertAckCheckpointStoreError::storage_failure);
    source.stop();

    CriticalAlertAckIngress restored{};
    start_ingress(registry, outbox, restored, false);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.source == CriticalAlertAckCheckpointSource::slot_a);
    EXPECT(loaded.slot_b == CriticalAlertAckCheckpointSlotState::invalid);
    EXPECT(loaded.recovery_required);
    EXPECT(loaded.generation == 1);
}

void test_corrupt_readback_preserves_other_good_checkpoint() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress source{};
    start_ingress(registry, outbox, source);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    send_and_reject(outbox, source, 1, 10, 0);
    EXPECT(store.save(source, 1).saved());
    EXPECT(store.save(source, 2).saved());
    storage.set_next_write_behavior(
        0, FakeCheckpointWriteBehavior::corrupt_after_success);
    EXPECT(store.save(source, 3).error ==
           CriticalAlertAckCheckpointStoreError::verification_failure);
    source.stop();

    CriticalAlertAckIngress restored{};
    start_ingress(registry, outbox, restored, false);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.source == CriticalAlertAckCheckpointSource::slot_b);
    EXPECT(loaded.slot_a == CriticalAlertAckCheckpointSlotState::invalid);
    EXPECT(loaded.generation == 2);
}

void test_io_degradation_remains_visible_with_valid_restore() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress source{};
    start_ingress(registry, outbox, source);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    EXPECT(store.save(source, 1).saved());
    source.stop();
    storage.fail_next_read(1);

    CriticalAlertAckIngress restored{};
    start_ingress(registry, outbox, restored, false);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.error ==
           CriticalAlertAckCheckpointStoreError::storage_failure);
    EXPECT(loaded.slot_b == CriticalAlertAckCheckpointSlotState::io_failure);
    EXPECT(loaded.source == CriticalAlertAckCheckpointSource::slot_a);
}

void test_rotation_rejects_old_checkpoint_without_state_replacement() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress source{};
    start_ingress(registry, outbox, source);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    EXPECT(store.save(source, 1).saved());
    source.stop();
    EXPECT(registry.rotate_key(kPeerId, 200, 2) ==
           PeerAuthorizationError::none);

    CriticalAlertAckIngress restored{};
    start_ingress(registry, outbox, restored, true);
    const auto loaded = store.restore(restored);
    EXPECT(!loaded.restored);
    EXPECT(loaded.error ==
           CriticalAlertAckCheckpointStoreError::checkpoint_rejected);
    EXPECT(loaded.ingress_error == CriticalAlertAckIngressError::
                                      checkpoint_authorization_mismatch);
    EXPECT(restored.status().binding_count == 1);
}

void test_nested_checkpoint_tamper_is_rejected_atomically() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress source{};
    start_ingress(registry, outbox, source);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    EXPECT(store.save(source, 1).saved());
    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes> bytes{};
    EXPECT(storage.read_slot(0, bytes.data(), bytes.size()) ==
           CriticalAlertAckCheckpointStorageError::none);
    bytes[24 + 40] ^= 0x01U;
    repair_envelope_crc(bytes);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           CriticalAlertAckCheckpointStorageError::none);
    source.stop();

    CriticalAlertAckIngress restored{};
    start_ingress(registry, outbox, restored, true);
    const auto loaded = store.restore(restored);
    EXPECT(!loaded.restored);
    EXPECT(loaded.error ==
           CriticalAlertAckCheckpointStoreError::checkpoint_rejected);
    EXPECT(loaded.ingress_error == CriticalAlertAckIngressError::
                                      checkpoint_integrity_failure);
    EXPECT(restored.status().binding_count == 1);
}

void test_stale_generation_and_reset_failures_are_explicit() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress ingress{};
    start_ingress(registry, outbox, ingress);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    EXPECT(store.save(ingress, 1).saved());
    EXPECT(store.save(ingress, 1).error ==
           CriticalAlertAckCheckpointStoreError::stale_generation);
    storage.fail_next_erase(0);
    EXPECT(store.reset() ==
           CriticalAlertAckCheckpointStoreError::storage_failure);
    EXPECT(storage.erases(0) == 1 && storage.erases(1) == 1);
    EXPECT(!storage.present(1));
}

void test_equal_generation_conflict_fails_closed() {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    start_dependencies(registry, outbox);
    CriticalAlertAckIngress source{};
    start_ingress(registry, outbox, source);
    FakeCriticalAlertAckCheckpointStorage storage{};
    CriticalAlertAckCheckpointStore store{storage};
    send_and_reject(outbox, source, 1, 10, 0);
    EXPECT(store.save(source, 5).saved());
    resend_and_reject(outbox, source, 1, 11, 30);
    EXPECT(store.save(source, 6).saved());
    std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes> second{};
    EXPECT(storage.read_slot(1, second.data(), second.size()) ==
           CriticalAlertAckCheckpointStorageError::none);
    write_u64(second.data() + 8, 5);
    repair_envelope_crc(second);
    EXPECT(storage.write_slot(1, second.data(), second.size()) ==
           CriticalAlertAckCheckpointStorageError::none);
    source.stop();

    CriticalAlertAckIngress restored{};
    start_ingress(registry, outbox, restored, false);
    const auto loaded = store.restore(restored);
    EXPECT(!loaded.restored);
    EXPECT(loaded.error ==
           CriticalAlertAckCheckpointStoreError::generation_conflict);
    EXPECT(loaded.source == CriticalAlertAckCheckpointSource::none);
    EXPECT(restored.status().binding_count == 0);
}

}  // namespace

int main() {
    test_explicit_envelope_and_first_save();
    test_empty_invalid_generation_and_stopped_export();
    test_newest_generation_restores_replay_atomically();
    test_partial_write_preserves_last_good_checkpoint();
    test_corrupt_readback_preserves_other_good_checkpoint();
    test_io_degradation_remains_visible_with_valid_restore();
    test_rotation_rejects_old_checkpoint_without_state_replacement();
    test_nested_checkpoint_tamper_is_rejected_atomically();
    test_stale_generation_and_reset_failures_are_explicit();
    test_equal_generation_conflict_fails_closed();

    if (failures != 0) {
        std::cerr << failures
                  << " critical alert ACK checkpoint store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 critical alert ACK checkpoint store scenario groups\n";
    return EXIT_SUCCESS;
}
