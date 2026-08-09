#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_critical_alert_recovery_storage.hpp"

namespace {

using namespace opengauge::identity;
using namespace opengauge::integration;
using namespace opengauge::integration::test_support;

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

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> rejection(
    std::uint32_t sequence) {
    CriticalAlertAck value{};
    value.disposition = AlertAckDisposition::rejected;
    value.reason = AlertAckReason::rate_limited;
    value.state = AlertState::asserted;
    value.consumer_id = kConsumer;
    value.producer_id = kProducer;
    value.event_id = 11;
    value.condition_id = 1011;
    value.consumer_boot_session_id = kSession;
    value.ack_sequence = sequence;
    value.observed_alert_age_ms = 10;
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> output{};
    EXPECT(encode_critical_alert_ack(value, output).encoded());
    return output;
}

struct Owners {
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};

    explicit Owners(
        CriticalAlertOutboxConfiguration config = configuration(),
        bool bind = true) {
        EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
        const PairingCandidate candidate{
            1, kPeer, PeerRole::trail_bridge,
            permission_bit(PeerPermission::receive_critical_alert) |
                permission_bit(PeerPermission::publish_alarm_ack),
            kChannel};
        EXPECT(registry.begin_approval(candidate, 0) ==
               PeerAuthorizationError::none);
        EXPECT(registry.approve(1, kKey, 1, 0) ==
               PeerAuthorizationError::none);
        EXPECT(outbox.start(config) == CriticalOutboxError::none);
        EXPECT(ingress.start({kProducer, 1000}, registry, outbox) ==
               CriticalAlertAckIngressError::none);
        if (bind) {
            EXPECT(ingress.bind_consumer_session(kPeer, kConsumer, kSession) ==
                   CriticalAlertAckIngressError::none);
        }
    }

    void create_retry(std::uint32_t sequence = 10, std::uint64_t now_ms = 0) {
        if (outbox.status().queued_count == 0 &&
            outbox.status().in_flight_count == 0) {
            EXPECT(outbox.enqueue(alert_frame(), now_ms) ==
                   CriticalOutboxError::none);
        }
        const auto prepared = outbox.prepare(now_ms);
        EXPECT(prepared.prepared());
        EXPECT(outbox.commit_local_send(prepared.token, true, now_ms) ==
               CriticalOutboxError::none);
        const auto frame = rejection(sequence);
        EXPECT(ingress.receive(
                   frame.data(), frame.size(),
                   {true, kPeer, kKey, kChannel}, now_ms + 1).processed());
    }
};

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}
void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}
void repair_crc(
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes>& bytes) {
    write_u32(bytes.data() + bytes.size() - 4,
              critical_alert_recovery_checkpoint_crc32(
                  bytes.data(), bytes.size() - 4));
}

void test_first_save_layout_and_newest_restore() {
    Owners source{};
    source.create_retry();
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    EXPECT(store.save(source.ingress, source.outbox, 10, 1).saved());
    EXPECT(store.save(source.ingress, source.outbox, 11, 2).saved());
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> bytes{};
    EXPECT(storage.read_slot(1, bytes.data(), bytes.size()) ==
           CriticalAlertRecoveryStorageError::none);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'C' && bytes[2] == 'R' && bytes[3] == '0');
    EXPECT(bytes[8] == 2);
    Owners restored{configuration(), false};
    const auto loaded = store.restore(restored.ingress, restored.outbox, 100);
    EXPECT(loaded.restored && loaded.generation == 2);
    EXPECT(loaded.source == CriticalAlertRecoverySource::slot_b);
    EXPECT(restored.outbox.prepare(114).error == CriticalOutboxError::no_frame_ready);
    EXPECT(restored.outbox.prepare(115).event_id == 11);
    const auto duplicate = rejection(10);
    EXPECT(restored.ingress.receive(
               duplicate.data(), duplicate.size(),
               {true, kPeer, kKey, kChannel}, 116).error ==
           CriticalAlertAckIngressError::replay_duplicate);
}

void test_empty_invalid_generation_and_prepared_export() {
    Owners owners{};
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    Owners empty_restore{configuration(), false};
    EXPECT(store.restore(empty_restore.ingress, empty_restore.outbox, 0).error ==
           CriticalAlertRecoveryStoreError::no_checkpoint);
    EXPECT(store.save(owners.ingress, owners.outbox, 0, 0).error ==
           CriticalAlertRecoveryStoreError::invalid_generation);
    EXPECT(owners.outbox.enqueue(alert_frame(), 0) == CriticalOutboxError::none);
    EXPECT(owners.outbox.prepare(0).prepared());
    const auto rejected = store.save(owners.ingress, owners.outbox, 0, 1);
    EXPECT(rejected.error == CriticalAlertRecoveryStoreError::checkpoint_rejected);
    EXPECT(!storage.present(0));
}

void test_partial_write_preserves_last_good() {
    Owners source{};
    source.create_retry();
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    EXPECT(store.save(source.ingress, source.outbox, 10, 1).saved());
    storage.set_next_write_behavior(1, FakeRecoveryWriteBehavior::fail_after_partial_write);
    const auto interrupted = store.save(source.ingress, source.outbox, 11, 2);
    EXPECT(interrupted.error == CriticalAlertRecoveryStoreError::storage_failure);
    EXPECT(interrupted.commit_uncertain && interrupted.generation == 2);
    Owners restored{configuration(), false};
    const auto loaded = store.restore(restored.ingress, restored.outbox, 100);
    EXPECT(loaded.restored && loaded.generation == 1);
    EXPECT(loaded.slot_b == CriticalAlertRecoverySlotState::invalid);
    EXPECT(loaded.recovery_required);
}

void test_interrupted_overwrite_boundaries_preserve_newest_good() {
    constexpr std::array<std::size_t, 16> boundaries{{
        0, 1, 4, 5, 8, 16, 24, 25,
        303, 304, 305, 943, 944, 955, 956, 959}};
    for (const auto boundary : boundaries) {
        Owners source{};
        source.create_retry();
        FakeCriticalAlertRecoveryStorage storage{};
        CriticalAlertRecoveryStore store{storage};
        EXPECT(store.save_next(source.ingress, source.outbox, 10).saved());
        EXPECT(store.save_next(source.ingress, source.outbox, 11).saved());
        storage.set_next_partial_write_bytes(0, boundary);
        const auto interrupted =
            store.save_next(source.ingress, source.outbox, 12);
        EXPECT(interrupted.error ==
               CriticalAlertRecoveryStoreError::storage_failure);
        EXPECT(interrupted.commit_uncertain && interrupted.generation == 3);
        Owners restored{configuration(), false};
        const auto loaded = store.restore(
            restored.ingress, restored.outbox, 100);
        EXPECT(loaded.restored && loaded.generation == 2);
        EXPECT(loaded.source == CriticalAlertRecoverySource::slot_b);
    }
}

void test_full_write_failure_reconciles_committed_generation() {
    Owners source{};
    source.create_retry();
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    EXPECT(store.save_next(source.ingress, source.outbox, 10).saved());
    storage.set_next_write_behavior(
        1, FakeRecoveryWriteBehavior::fail_after_full_write);
    const auto uncertain = store.save_next(source.ingress, source.outbox, 11);
    EXPECT(uncertain.error == CriticalAlertRecoveryStoreError::storage_failure);
    EXPECT(uncertain.commit_uncertain && uncertain.generation == 2);
    Owners restored{configuration(), false};
    const auto loaded = store.restore(restored.ingress, restored.outbox, 100);
    EXPECT(loaded.restored && loaded.generation == 2);
    EXPECT(loaded.source == CriticalAlertRecoverySource::slot_b);
}

void test_corrupt_readback_preserves_other_good() {
    Owners source{};
    source.create_retry();
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    EXPECT(store.save(source.ingress, source.outbox, 10, 1).saved());
    EXPECT(store.save(source.ingress, source.outbox, 11, 2).saved());
    storage.set_next_write_behavior(0, FakeRecoveryWriteBehavior::corrupt_after_success);
    EXPECT(store.save(source.ingress, source.outbox, 12, 3).error ==
           CriticalAlertRecoveryStoreError::verification_failure);
    Owners restored{configuration(), false};
    const auto loaded = store.restore(restored.ingress, restored.outbox, 100);
    EXPECT(loaded.restored && loaded.generation == 2);
    EXPECT(loaded.slot_a == CriticalAlertRecoverySlotState::invalid);
}

void test_io_degradation_is_visible_with_restore() {
    Owners source{};
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    EXPECT(store.save(source.ingress, source.outbox, 0, 1).saved());
    storage.fail_next_read(1);
    Owners restored{configuration(), false};
    const auto loaded = store.restore(restored.ingress, restored.outbox, 100);
    EXPECT(loaded.restored);
    EXPECT(loaded.error == CriticalAlertRecoveryStoreError::storage_failure);
    EXPECT(loaded.slot_b == CriticalAlertRecoverySlotState::io_failure);
}

void test_policy_and_authorization_rejection_are_atomic() {
    Owners source{};
    source.create_retry();
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    EXPECT(store.save(source.ingress, source.outbox, 10, 1).saved());
    auto changed = configuration();
    ++changed.retry_backoff_ms;
    Owners policy{changed, false};
    auto loaded = store.restore(policy.ingress, policy.outbox, 100);
    EXPECT(!loaded.restored && loaded.error ==
           CriticalAlertRecoveryStoreError::checkpoint_rejected);
    EXPECT(policy.ingress.status().binding_count == 0 &&
           policy.outbox.status().queued_count == 0);
    Owners authorization{configuration(), false};
    EXPECT(authorization.registry.rotate_key(kPeer, 200, 2) ==
           PeerAuthorizationError::none);
    loaded = store.restore(authorization.ingress, authorization.outbox, 100);
    EXPECT(!loaded.restored);
    EXPECT(authorization.ingress.status().binding_count == 0 &&
           authorization.outbox.status().queued_count == 0);
}

void test_stale_generation_and_reset_failures() {
    Owners source{};
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    EXPECT(store.save(source.ingress, source.outbox, 0, 1).saved());
    EXPECT(store.save(source.ingress, source.outbox, 1, 1).error ==
           CriticalAlertRecoveryStoreError::stale_generation);
    storage.fail_next_erase(0);
    EXPECT(store.reset() == CriticalAlertRecoveryStoreError::storage_failure);
    EXPECT(storage.erases(0) == 1 && storage.erases(1) == 1);
    EXPECT(!storage.present(1));
}

void test_equal_generation_conflict_fails_closed() {
    Owners source{};
    source.create_retry();
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    EXPECT(store.save(source.ingress, source.outbox, 10, 5).saved());
    EXPECT(store.save(source.ingress, source.outbox, 11, 6).saved());
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> second{};
    EXPECT(storage.read_slot(1, second.data(), second.size()) ==
           CriticalAlertRecoveryStorageError::none);
    write_u64(second.data() + 8, 5);
    repair_crc(second);
    EXPECT(storage.write_slot(1, second.data(), second.size()) ==
           CriticalAlertRecoveryStorageError::none);
    Owners restored{configuration(), false};
    const auto loaded = store.restore(restored.ingress, restored.outbox, 100);
    EXPECT(!loaded.restored);
    EXPECT(loaded.error == CriticalAlertRecoveryStoreError::generation_conflict);
    EXPECT(store.save_next(source.ingress, source.outbox, 12).error ==
           CriticalAlertRecoveryStoreError::generation_conflict);
    EXPECT(restored.ingress.status().binding_count == 0 &&
           restored.outbox.status().queued_count == 0);
}

void test_store_allocates_and_rotates_next_generation() {
    Owners source{};
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    auto saved = store.save_next(source.ingress, source.outbox, 0);
    EXPECT(saved.saved() && saved.generation == 1);
    EXPECT(saved.written_slot == CriticalAlertRecoverySource::slot_a);
    saved = store.save_next(source.ingress, source.outbox, 1);
    EXPECT(saved.saved() && saved.generation == 2);
    EXPECT(saved.written_slot == CriticalAlertRecoverySource::slot_b);
    saved = store.save_next(source.ingress, source.outbox, 2);
    EXPECT(saved.saved() && saved.generation == 3);
    EXPECT(saved.written_slot == CriticalAlertRecoverySource::slot_a);
}

void test_next_generation_exhaustion_fails_before_write() {
    Owners source{};
    FakeCriticalAlertRecoveryStorage storage{};
    CriticalAlertRecoveryStore store{storage};
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    EXPECT(store.save(source.ingress, source.outbox, 0, maximum).saved());
    const auto writes = storage.writes(0) + storage.writes(1);
    const auto exhausted = store.save_next(source.ingress, source.outbox, 1);
    EXPECT(exhausted.error ==
           CriticalAlertRecoveryStoreError::generation_exhausted);
    EXPECT(storage.writes(0) + storage.writes(1) == writes);
}

}  // namespace

int main() {
    test_first_save_layout_and_newest_restore();
    test_empty_invalid_generation_and_prepared_export();
    test_partial_write_preserves_last_good();
    test_interrupted_overwrite_boundaries_preserve_newest_good();
    test_full_write_failure_reconciles_committed_generation();
    test_corrupt_readback_preserves_other_good();
    test_io_degradation_is_visible_with_restore();
    test_policy_and_authorization_rejection_are_atomic();
    test_stale_generation_and_reset_failures();
    test_equal_generation_conflict_fails_closed();
    test_store_allocates_and_rotates_next_generation();
    test_next_generation_exhaustion_fails_before_write();
    if (failures != 0) {
        std::cerr << failures << " recovery store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 12 critical alert recovery store scenario groups\n";
    return EXIT_SUCCESS;
}
