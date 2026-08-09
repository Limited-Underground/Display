#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_peer_authorization_checkpoint_storage.hpp"

namespace {

using namespace opengauge::identity;
using namespace opengauge::identity::test_support;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

PairingCandidate gauge(std::uint32_t request, std::uint32_t peer) {
    return {request, peer, PeerRole::gauge,
            permission_bit(PeerPermission::receive_telemetry) |
                permission_bit(PeerPermission::publish_alarm_ack),
            6};
}

void approve(PeerAuthorizationRegistry& registry, std::uint32_t request,
             std::uint32_t peer, std::uint32_t key,
             std::uint32_t epoch = 1) {
    EXPECT(registry.begin_approval(gauge(request, peer), 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.approve(request, key, epoch, 0) ==
           PeerAuthorizationError::none);
}

void populate(PeerAuthorizationRegistry& registry) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve(registry, 1, 10, 100);
    approve(registry, 2, 20, 200, 2);
    EXPECT(registry.rotate_key(20, 201, 3) == PeerAuthorizationError::none);
    EXPECT(registry.revoke(10) == PeerAuthorizationError::none);
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
    for (std::size_t index = 0; index < 4; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

void repair_outer_crc(
    std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes>& bytes) {
    constexpr auto offset = kPeerAuthorizationStoredCheckpointBytes - 4;
    write_u32(bytes.data() + offset, crc32(bytes.data(), offset));
}

void test_first_save_has_canonical_envelope() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    const auto saved = store.save_next(source);
    EXPECT(saved.saved());
    EXPECT(saved.written_slot == PeerAuthorizationCheckpointSource::slot_a);
    EXPECT(saved.generation == 1);
    EXPECT(!saved.commit_uncertain);
    std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes> bytes{};
    EXPECT(storage.read_slot(0, bytes.data(), bytes.size()) ==
           PeerAuthorizationCheckpointStorageError::none);
    EXPECT(bytes[0] == 'O' && bytes[1] == 'P' && bytes[2] == 'S' &&
           bytes[3] == '0' && bytes[4] == 0 && bytes[5] == 24);
    EXPECT(bytes[6] == 0 && bytes[7] == 1);
    EXPECT(bytes[8] == 1 && bytes[15] == 0);
    EXPECT(bytes[24] == 'O' && bytes[25] == 'P' && bytes[26] == 'A' &&
           bytes[27] == '0');
}

void test_rotation_and_newest_restore() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    EXPECT(store.save_next(source).saved());
    approve(source, 3, 30, 300);
    const auto second = store.save_next(source);
    EXPECT(second.saved());
    EXPECT(second.written_slot == PeerAuthorizationCheckpointSource::slot_b);
    EXPECT(second.generation == 2);

    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.source == PeerAuthorizationCheckpointSource::slot_b);
    EXPECT(loaded.generation == 2);
    EXPECT(restored.status().peer_count == 3);
    EXPECT(restored.status().active_peer_count == 2);
    EXPECT(restored.authorize(
               10, 100, 6, PeerPermission::receive_telemetry).error ==
           PeerAuthorizationError::peer_revoked);
    EXPECT(restored.authorize(
               20, 201, 6, PeerPermission::receive_telemetry).authorized);
}

void test_interrupted_boundaries_preserve_prior_generation() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    EXPECT(store.save_next(source).saved());
    constexpr std::array<std::size_t, 10> prefixes{
        0, 1, 23, 24, 100, 255, 279, 280, 283, 284};
    for (const auto prefix : prefixes) {
        storage.set_next_partial_write_bytes(1, prefix);
        const auto interrupted = store.save_next(source);
        EXPECT(interrupted.error ==
               PeerAuthorizationCheckpointStoreError::storage_failure);
        EXPECT(interrupted.commit_uncertain);
        EXPECT(interrupted.written_slot ==
               PeerAuthorizationCheckpointSource::slot_b);
        EXPECT(interrupted.generation == 2);

        PeerAuthorizationRegistry restored{};
        EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
        const auto loaded = store.restore(restored);
        EXPECT(loaded.restored);
        EXPECT(loaded.source == PeerAuthorizationCheckpointSource::slot_a);
        EXPECT(loaded.generation == 1);
        EXPECT(loaded.slot_b == PeerAuthorizationCheckpointSlotState::invalid);
        EXPECT(loaded.recovery_required);
    }
}

void test_full_write_error_reconciles_on_boot() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    EXPECT(store.save_next(source).saved());
    approve(source, 3, 30, 300);
    storage.set_next_write_behavior(
        1, FakePeerCheckpointWriteBehavior::fail_after_full_write);
    const auto uncertain = store.save_next(source);
    EXPECT(uncertain.error ==
           PeerAuthorizationCheckpointStoreError::storage_failure);
    EXPECT(uncertain.commit_uncertain && uncertain.generation == 2);

    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored);
    EXPECT(loaded.source == PeerAuthorizationCheckpointSource::slot_b);
    EXPECT(loaded.generation == 2);
    EXPECT(restored.status().peer_count == 3);
}

void test_corruption_and_io_degradation_use_good_slot() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    EXPECT(store.save_next(source).saved());
    EXPECT(store.save_next(source).saved());
    storage.corrupt(1, 100, 0x5A);
    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    const auto corrupt = store.restore(restored);
    EXPECT(corrupt.restored && corrupt.generation == 1);
    EXPECT(corrupt.slot_b == PeerAuthorizationCheckpointSlotState::invalid);
    EXPECT(corrupt.recovery_required);

    FakePeerAuthorizationCheckpointStorage io_storage{};
    PeerAuthorizationCheckpointStore io_store{io_storage};
    EXPECT(io_store.save_next(source).saved());
    EXPECT(io_store.save_next(source).saved());
    io_storage.fail_next_read(1);
    PeerAuthorizationRegistry degraded{};
    EXPECT(degraded.start({1000}) == PeerAuthorizationError::none);
    const auto io = io_store.restore(degraded);
    EXPECT(io.restored && io.generation == 1);
    EXPECT(io.error == PeerAuthorizationCheckpointStoreError::storage_failure);
    EXPECT(io.slot_b == PeerAuthorizationCheckpointSlotState::io_failure);
    EXPECT(io.recovery_required);
}

void test_incompatible_and_nonempty_restore_are_atomic() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    EXPECT(store.save_next(source).saved());

    PeerAuthorizationRegistry incompatible{};
    EXPECT(incompatible.start({1001}) == PeerAuthorizationError::none);
    const auto mismatch = store.restore(incompatible);
    EXPECT(!mismatch.restored);
    EXPECT(mismatch.error ==
           PeerAuthorizationCheckpointStoreError::checkpoint_rejected);
    EXPECT(mismatch.registry_error ==
           PeerAuthorizationError::checkpoint_incompatible);
    EXPECT(incompatible.status().peer_count == 0);

    PeerAuthorizationRegistry nonempty{};
    EXPECT(nonempty.start({1000}) == PeerAuthorizationError::none);
    approve(nonempty, 9, 99, 999);
    const auto busy = store.restore(nonempty);
    EXPECT(!busy.restored);
    EXPECT(busy.registry_error == PeerAuthorizationError::invalid_state);
    EXPECT(nonempty.status().peer_count == 1);
}

void test_generation_rules_and_exhaustion() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    EXPECT(store.save(source, 0).error ==
           PeerAuthorizationCheckpointStoreError::invalid_generation);
    EXPECT(store.save(source, 2).saved());
    EXPECT(store.save(source, 2).error ==
           PeerAuthorizationCheckpointStoreError::stale_generation);
    EXPECT(store.save(source, 1).error ==
           PeerAuthorizationCheckpointStoreError::stale_generation);

    FakePeerAuthorizationCheckpointStorage exhausted_storage{};
    PeerAuthorizationCheckpointStore exhausted{exhausted_storage};
    EXPECT(exhausted.save(
               source, std::numeric_limits<std::uint64_t>::max()).saved());
    const auto next = exhausted.save_next(source);
    EXPECT(next.error ==
           PeerAuthorizationCheckpointStoreError::generation_exhausted);
    EXPECT(exhausted_storage.writes(1) == 0);
}

void test_equal_generation_conflict_fails_closed() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    EXPECT(store.save_next(source).saved());
    std::array<std::uint8_t, kPeerAuthorizationStoredCheckpointBytes> bytes{};
    EXPECT(storage.read_slot(0, bytes.data(), bytes.size()) ==
           PeerAuthorizationCheckpointStorageError::none);
    bytes[24 + 100] ^= 1;
    repair_outer_crc(bytes);
    EXPECT(storage.write_slot(1, bytes.data(), bytes.size()) ==
           PeerAuthorizationCheckpointStorageError::none);

    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    const auto loaded = store.restore(restored);
    EXPECT(!loaded.restored);
    EXPECT(loaded.error ==
           PeerAuthorizationCheckpointStoreError::generation_conflict);
    EXPECT(restored.status().peer_count == 0);
    EXPECT(store.save_next(source).error ==
           PeerAuthorizationCheckpointStoreError::generation_conflict);
}

void test_verification_failure_is_commit_uncertain() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    EXPECT(store.save_next(source).saved());
    storage.set_next_write_behavior(
        1, FakePeerCheckpointWriteBehavior::corrupt_after_success);
    const auto failed = store.save_next(source);
    EXPECT(failed.error ==
           PeerAuthorizationCheckpointStoreError::verification_failure);
    EXPECT(failed.commit_uncertain && failed.generation == 2);
    EXPECT(failed.written_slot == PeerAuthorizationCheckpointSource::slot_b);

    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    const auto loaded = store.restore(restored);
    EXPECT(loaded.restored && loaded.generation == 1);
    EXPECT(loaded.slot_b == PeerAuthorizationCheckpointSlotState::invalid);
}

void test_empty_and_reset_behavior() {
    PeerAuthorizationRegistry source{};
    populate(source);
    FakePeerAuthorizationCheckpointStorage storage{};
    PeerAuthorizationCheckpointStore store{storage};
    PeerAuthorizationRegistry empty_target{};
    EXPECT(empty_target.start({1000}) == PeerAuthorizationError::none);
    const auto empty = store.restore(empty_target);
    EXPECT(empty.error == PeerAuthorizationCheckpointStoreError::no_checkpoint);
    EXPECT(!empty.restored && empty.recovery_required);
    EXPECT(store.save_next(source).saved());
    EXPECT(store.save_next(source).saved());
    EXPECT(store.reset() == PeerAuthorizationCheckpointStoreError::none);
    EXPECT(!storage.present(0) && !storage.present(1));
    storage.fail_next_erase(1);
    EXPECT(store.reset() ==
           PeerAuthorizationCheckpointStoreError::storage_failure);
}

}  // namespace

int main() {
    test_first_save_has_canonical_envelope();
    test_rotation_and_newest_restore();
    test_interrupted_boundaries_preserve_prior_generation();
    test_full_write_error_reconciles_on_boot();
    test_corruption_and_io_degradation_use_good_slot();
    test_incompatible_and_nonempty_restore_are_atomic();
    test_generation_rules_and_exhaustion();
    test_equal_generation_conflict_fails_closed();
    test_verification_failure_is_commit_uncertain();
    test_empty_and_reset_behavior();
    if (failures != 0) {
        std::cerr << failures << " peer checkpoint store assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 10 peer authorization checkpoint store scenario groups\n";
    return EXIT_SUCCESS;
}
