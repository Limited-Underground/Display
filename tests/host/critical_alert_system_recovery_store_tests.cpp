#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_critical_alert_system_recovery_storage.hpp"

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

class FakeKeyValidator final : public CriticalAlertSystemRecoveryKeyValidator {
public:
    CriticalAlertSystemRecoveryKeyValidationError response{
        CriticalAlertSystemRecoveryKeyValidationError::none};
    std::size_t calls{0};

    CriticalAlertSystemRecoveryKeyValidationError validate(
        const PeerAuthorizationEntry& peer) override {
        ++calls;
        EXPECT(peer.active && peer.secure_key_handle != 0);
        return response;
    }
};

PairingCandidate bridge(std::uint32_t request, std::uint32_t peer) {
    return {request, peer, PeerRole::trail_bridge,
            permission_bit(PeerPermission::receive_critical_alert) |
                permission_bit(PeerPermission::publish_alarm_ack),
            kChannel};
}

void approve(PeerAuthorizationRegistry& registry, std::uint32_t request,
             std::uint32_t peer, std::uint32_t key) {
    EXPECT(registry.begin_approval(bridge(request, peer), 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.approve(request, key, 1, 0) ==
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

void start_source(PeerAuthorizationRegistry& registry,
                  CriticalAlertOutbox& outbox,
                  CriticalAlertAckIngress& ingress) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve(registry, 1, kPeerId, kKeyHandle);
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
    const CriticalAlertAckTransportContext transport{
        true, kPeerId, kKeyHandle, kChannel};
    EXPECT(ingress.receive(
               ack.data(), ack.size(), transport, 1).retry_released);
}

void start_target(PeerAuthorizationRegistry& registry,
                  CriticalAlertOutbox& outbox,
                  CriticalAlertAckIngress& ingress,
                  std::uint64_t approval_window = 1000) {
    EXPECT(registry.start({approval_window}) == PeerAuthorizationError::none);
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
}

void expect_restored(const PeerAuthorizationRegistry& registry,
                     const CriticalAlertOutbox& outbox,
                     const CriticalAlertAckIngress& ingress) {
    EXPECT(registry.status().peer_count >= 1);
    EXPECT(outbox.status().queued_count == 1);
    EXPECT(ingress.status().binding_count == 1);
}

void expect_empty(const PeerAuthorizationRegistry& registry,
                  const CriticalAlertOutbox& outbox,
                  const CriticalAlertAckIngress& ingress) {
    EXPECT(registry.status().peer_count == 0);
    EXPECT(outbox.status().queued_count == 0);
    EXPECT(outbox.status().in_flight_count == 0);
    EXPECT(ingress.status().binding_count == 0);
}

void test_first_save_and_empty_restore() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    const auto saved = store.save_next(source, source_ingress, source_outbox, 1);
    EXPECT(saved.saved() && saved.generation == 1);
    EXPECT(saved.written_slot == CriticalAlertSystemRecoverySource::slot_a);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes> bytes{};
    EXPECT(storage.read_slot(0, bytes.data(), bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::none);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'R' && bytes[2] == 'S' &&
           bytes[3] == '0');

    FakeCriticalAlertSystemRecoveryStorage empty_storage{};
    CriticalAlertSystemRecoveryStore empty_store{empty_storage};
    PeerAuthorizationRegistry target{};
    CriticalAlertOutbox target_outbox{};
    CriticalAlertAckIngress target_ingress{};
    start_target(target, target_outbox, target_ingress);
    const auto empty = empty_store.restore(
        target, target_ingress, target_outbox, 100);
    EXPECT(empty.error == CriticalAlertSystemRecoveryStoreError::no_checkpoint);
    EXPECT(!empty.restored && empty.recovery_required);
}

void test_rotation_and_newest_joint_restore() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());
    approve(source, 2, 20, 200);
    const auto second =
        store.save_next(source, source_ingress, source_outbox, 1);
    EXPECT(second.saved() && second.generation == 2);
    EXPECT(second.written_slot == CriticalAlertSystemRecoverySource::slot_b);

    PeerAuthorizationRegistry target{};
    CriticalAlertOutbox target_outbox{};
    CriticalAlertAckIngress target_ingress{};
    start_target(target, target_outbox, target_ingress);
    const auto loaded = store.restore(
        target, target_ingress, target_outbox, 100);
    EXPECT(loaded.restored && loaded.generation == 2);
    EXPECT(loaded.source == CriticalAlertSystemRecoverySource::slot_b);
    EXPECT(target.status().peer_count == 2);
    expect_restored(target, target_outbox, target_ingress);
}

void test_interrupted_boundaries_preserve_prior_generation() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());
    constexpr std::array<std::size_t, 11> prefixes{
        0, 1, 23, 24, 279, 280, 500, 1239, 1240, 1275, 1276};
    for (const auto prefix : prefixes) {
        storage.set_next_partial_write_bytes(1, prefix);
        const auto failed =
            store.save_next(source, source_ingress, source_outbox, 1);
        EXPECT(failed.error ==
               CriticalAlertSystemRecoveryStoreError::storage_failure);
        EXPECT(failed.commit_uncertain && failed.generation == 2);
        EXPECT(failed.written_slot == CriticalAlertSystemRecoverySource::slot_b);

        PeerAuthorizationRegistry target{};
        CriticalAlertOutbox target_outbox{};
        CriticalAlertAckIngress target_ingress{};
        start_target(target, target_outbox, target_ingress);
        const auto loaded = store.restore(
            target, target_ingress, target_outbox, 100);
        EXPECT(loaded.restored && loaded.generation == 1);
        EXPECT(loaded.source == CriticalAlertSystemRecoverySource::slot_a);
        EXPECT(loaded.slot_b == CriticalAlertSystemRecoverySlotState::invalid);
        EXPECT(loaded.recovery_required);
        expect_restored(target, target_outbox, target_ingress);
    }
}

void test_full_write_error_reconciles_new_generation() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());
    approve(source, 2, 20, 200);
    storage.set_next_write_behavior(
        1, FakeSystemRecoveryWriteBehavior::fail_after_full_write);
    const auto uncertain =
        store.save_next(source, source_ingress, source_outbox, 1);
    EXPECT(uncertain.error ==
           CriticalAlertSystemRecoveryStoreError::storage_failure);
    EXPECT(uncertain.commit_uncertain && uncertain.generation == 2);

    PeerAuthorizationRegistry target{};
    CriticalAlertOutbox target_outbox{};
    CriticalAlertAckIngress target_ingress{};
    start_target(target, target_outbox, target_ingress);
    const auto loaded = store.restore(
        target, target_ingress, target_outbox, 100);
    EXPECT(loaded.restored && loaded.generation == 2);
    EXPECT(target.status().peer_count == 2);
}

void test_corrupt_and_unreadable_new_slot_degrade_visibly() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());
    storage.corrupt(1, 500, 0x5A);
    PeerAuthorizationRegistry target{};
    CriticalAlertOutbox target_outbox{};
    CriticalAlertAckIngress target_ingress{};
    start_target(target, target_outbox, target_ingress);
    auto loaded = store.restore(target, target_ingress, target_outbox, 100);
    EXPECT(loaded.restored && loaded.generation == 1);
    EXPECT(loaded.slot_b == CriticalAlertSystemRecoverySlotState::invalid);

    FakeCriticalAlertSystemRecoveryStorage io_storage{};
    CriticalAlertSystemRecoveryStore io_store{io_storage};
    EXPECT(io_store.save_next(source, source_ingress, source_outbox, 1).saved());
    EXPECT(io_store.save_next(source, source_ingress, source_outbox, 1).saved());
    io_storage.fail_next_read(1);
    PeerAuthorizationRegistry io_target{};
    CriticalAlertOutbox io_outbox{};
    CriticalAlertAckIngress io_ingress{};
    start_target(io_target, io_outbox, io_ingress);
    loaded = io_store.restore(io_target, io_ingress, io_outbox, 100);
    EXPECT(loaded.restored && loaded.generation == 1);
    EXPECT(loaded.error == CriticalAlertSystemRecoveryStoreError::storage_failure);
    EXPECT(loaded.slot_b == CriticalAlertSystemRecoverySlotState::io_failure);
    EXPECT(loaded.recovery_required);
}

void test_restore_rejection_is_atomic() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());
    PeerAuthorizationRegistry target{};
    CriticalAlertOutbox target_outbox{};
    CriticalAlertAckIngress target_ingress{};
    start_target(target, target_outbox, target_ingress, 1001);
    const auto loaded = store.restore(
        target, target_ingress, target_outbox, 100);
    EXPECT(!loaded.restored);
    EXPECT(loaded.error ==
           CriticalAlertSystemRecoveryStoreError::checkpoint_rejected);
    EXPECT(loaded.recovery.error ==
           CriticalAlertSystemRecoveryError::authorization_preflight_failed);
    expect_empty(target, target_outbox, target_ingress);
}

void test_generation_conflict_stale_and_exhaustion() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save(source, source_ingress, source_outbox, 1, 0).error ==
           CriticalAlertSystemRecoveryStoreError::invalid_generation);
    EXPECT(store.save(source, source_ingress, source_outbox, 1, 1).saved());
    EXPECT(store.save(source, source_ingress, source_outbox, 1, 1).error ==
           CriticalAlertSystemRecoveryStoreError::stale_generation);
    approve(source, 2, 20, 200);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        conflicting{};
    EXPECT(export_critical_alert_system_recovery_checkpoint(
               source, source_ingress, source_outbox, 1, 1,
               conflicting).completed());
    EXPECT(storage.write_slot(1, conflicting.data(), conflicting.size()) ==
           CriticalAlertSystemRecoveryStorageError::none);
    PeerAuthorizationRegistry target{};
    CriticalAlertOutbox target_outbox{};
    CriticalAlertAckIngress target_ingress{};
    start_target(target, target_outbox, target_ingress);
    EXPECT(store.restore(target, target_ingress, target_outbox, 100).error ==
           CriticalAlertSystemRecoveryStoreError::generation_conflict);
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).error ==
           CriticalAlertSystemRecoveryStoreError::generation_conflict);
    expect_empty(target, target_outbox, target_ingress);

    FakeCriticalAlertSystemRecoveryStorage max_storage{};
    CriticalAlertSystemRecoveryStore max_store{max_storage};
    EXPECT(max_store.save(
               source, source_ingress, source_outbox, 1,
               std::numeric_limits<std::uint64_t>::max()).saved());
    EXPECT(max_store.save_next(source, source_ingress, source_outbox, 1).error ==
           CriticalAlertSystemRecoveryStoreError::generation_exhausted);
    EXPECT(max_storage.writes(1) == 0);
}

void test_verification_failure_and_reset() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());
    storage.set_next_write_behavior(
        1, FakeSystemRecoveryWriteBehavior::corrupt_after_success);
    const auto failed =
        store.save_next(source, source_ingress, source_outbox, 1);
    EXPECT(failed.error ==
           CriticalAlertSystemRecoveryStoreError::verification_failure);
    EXPECT(failed.commit_uncertain && failed.generation == 2);
    EXPECT(store.reset() == CriticalAlertSystemRecoveryStoreError::none);
    EXPECT(!storage.present(0) && !storage.present(1));
    storage.fail_next_erase(1);
    EXPECT(store.reset() ==
           CriticalAlertSystemRecoveryStoreError::storage_failure);
}

void test_trusted_restore_floor_rejects_rollback() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());

    PeerAuthorizationRegistry accepted{};
    CriticalAlertOutbox accepted_outbox{};
    CriticalAlertAckIngress accepted_ingress{};
    start_target(accepted, accepted_outbox, accepted_ingress);
    const auto allowed = store.restore_at_or_above(
        accepted, accepted_ingress, accepted_outbox, 100, 2);
    EXPECT(allowed.restored && allowed.generation == 2);
    expect_restored(accepted, accepted_outbox, accepted_ingress);

    PeerAuthorizationRegistry rejected{};
    CriticalAlertOutbox rejected_outbox{};
    CriticalAlertAckIngress rejected_ingress{};
    start_target(rejected, rejected_outbox, rejected_ingress);
    const auto rollback = store.restore_at_or_above(
        rejected, rejected_ingress, rejected_outbox, 100, 3);
    EXPECT(!rollback.restored && rollback.generation == 2);
    EXPECT(rollback.error ==
           CriticalAlertSystemRecoveryStoreError::rollback_detected);
    EXPECT(rollback.recovery_required);
    expect_empty(rejected, rejected_outbox, rejected_ingress);
}

void test_trusted_generation_advances_new_saves() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    const auto first = store.save_next_after(
        source, source_ingress, source_outbox, 1, 41);
    EXPECT(first.saved() && first.generation == 42);
    const auto second = store.save_next_after(
        source, source_ingress, source_outbox, 1, 40);
    EXPECT(second.saved() && second.generation == 43);
    const auto writes_before = storage.writes(0) + storage.writes(1);
    const auto exhausted = store.save_next_after(
        source, source_ingress, source_outbox, 1,
        std::numeric_limits<std::uint64_t>::max());
    EXPECT(exhausted.error ==
           CriticalAlertSystemRecoveryStoreError::generation_exhausted);
    EXPECT(storage.writes(0) + storage.writes(1) == writes_before);
}

void test_store_restore_preflights_protected_key_handles() {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox source_outbox{};
    CriticalAlertAckIngress source_ingress{};
    start_source(source, source_outbox, source_ingress);
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(source, source_ingress, source_outbox, 1).saved());

    PeerAuthorizationRegistry rejected{};
    CriticalAlertOutbox rejected_outbox{};
    CriticalAlertAckIngress rejected_ingress{};
    start_target(rejected, rejected_outbox, rejected_ingress);
    FakeKeyValidator unavailable{};
    unavailable.response =
        CriticalAlertSystemRecoveryKeyValidationError::key_unavailable;
    const auto failed = store.restore_at_or_above_validating_keys(
        rejected, rejected_ingress, rejected_outbox, 100, 1, unavailable);
    EXPECT(!failed.restored);
    EXPECT(failed.error ==
           CriticalAlertSystemRecoveryStoreError::checkpoint_rejected);
    EXPECT(failed.recovery.error == CriticalAlertSystemRecoveryError::
                                         authorization_key_preflight_failed);
    EXPECT(failed.recovery.key_validation_error ==
           CriticalAlertSystemRecoveryKeyValidationError::key_unavailable);
    EXPECT(unavailable.calls == 1);
    expect_empty(rejected, rejected_outbox, rejected_ingress);

    PeerAuthorizationRegistry accepted{};
    CriticalAlertOutbox accepted_outbox{};
    CriticalAlertAckIngress accepted_ingress{};
    start_target(accepted, accepted_outbox, accepted_ingress);
    FakeKeyValidator available{};
    const auto loaded = store.restore_at_or_above_validating_keys(
        accepted, accepted_ingress, accepted_outbox, 100, 1, available);
    EXPECT(loaded.restored && loaded.generation == 1);
    EXPECT(available.calls == 1);
    expect_restored(accepted, accepted_outbox, accepted_ingress);
}

}  // namespace

int main() {
    test_first_save_and_empty_restore();
    test_rotation_and_newest_joint_restore();
    test_interrupted_boundaries_preserve_prior_generation();
    test_full_write_error_reconciles_new_generation();
    test_corrupt_and_unreadable_new_slot_degrade_visibly();
    test_restore_rejection_is_atomic();
    test_generation_conflict_stale_and_exhaustion();
    test_verification_failure_and_reset();
    test_trusted_restore_floor_rejects_rollback();
    test_trusted_generation_advances_new_saves();
    test_store_restore_preflights_protected_key_handles();
    if (failures != 0) {
        std::cerr << failures << " system recovery store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 11 critical alert system recovery store scenario groups\n";
    return EXIT_SUCCESS;
}
