#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_critical_alert_system_recovery_storage.hpp"
#include "opengauge/critical_alert_system_recovery_save.hpp"

namespace {

using namespace opengauge::identity;
using namespace opengauge::integration;
using namespace opengauge::integration::test_support;

constexpr std::uint64_t kProducerId = 1;
constexpr std::uint32_t kPeerId = 10;
constexpr std::uint32_t kKeyHandle = 100;
constexpr std::uint8_t kChannel = 6;

int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeTrustedGeneration final
    : public CriticalAlertSystemTrustedGenerationSource {
public:
    CriticalAlertSystemTrustedGenerationError read_error{
        CriticalAlertSystemTrustedGenerationError::none};
    CriticalAlertSystemTrustedGenerationError advance_error{
        CriticalAlertSystemTrustedGenerationError::none};
    CriticalAlertSystemTrustedGenerationError readback_error{
        CriticalAlertSystemTrustedGenerationError::none};
    std::uint64_t generation{1};
    std::uint64_t requested_generation{0};
    std::size_t reads{0};
    std::size_t advances{0};
    bool persist_advance{true};

    CriticalAlertSystemTrustedGenerationRead read() override {
        ++reads;
        const auto error = advances == 0 ? read_error : readback_error;
        return {error, generation};
    }

    CriticalAlertSystemTrustedGenerationError advance_to(
        std::uint64_t requested) override {
        ++advances;
        requested_generation = requested;
        if (advance_error ==
                CriticalAlertSystemTrustedGenerationError::none &&
            persist_advance) {
            generation = requested;
        }
        return advance_error;
    }
};

class AcceptKeys final : public CriticalAlertSystemRecoveryKeyValidator {
public:
    CriticalAlertSystemRecoveryKeyValidationError validate(
        const PeerAuthorizationEntry& peer) override {
        EXPECT(peer.active && peer.secure_key_handle != 0);
        return CriticalAlertSystemRecoveryKeyValidationError::none;
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

void start_live(PeerAuthorizationRegistry& registry,
                CriticalAlertOutbox& outbox,
                CriticalAlertAckIngress& ingress) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve(registry, 1, kPeerId, kKeyHandle);
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
}

void start_target(PeerAuthorizationRegistry& registry,
                  CriticalAlertOutbox& outbox,
                  CriticalAlertAckIngress& ingress) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
}

void seed_one(FakeCriticalAlertSystemRecoveryStorage& storage,
              PeerAuthorizationRegistry& registry,
              CriticalAlertOutbox& outbox,
              CriticalAlertAckIngress& ingress) {
    CriticalAlertSystemRecoveryStore store{storage};
    EXPECT(store.save_next(registry, ingress, outbox, 1).saved());
}

void test_verified_save_advances_and_reads_back_trust() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_live(registry, outbox, ingress);
    seed_one(storage, registry, outbox, ingress);
    approve(registry, 2, 20, 200);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    CriticalAlertSystemRecoverySaveCoordinator coordinator{store, trusted};
    const auto result = coordinator.save(registry, ingress, outbox, 2);
    EXPECT(result.committed());
    EXPECT(result.reason == CriticalAlertSystemPersistenceReason::none);
    EXPECT(result.prior_trusted_generation == 1);
    EXPECT(result.observed_trusted_readback == 2);
    EXPECT(result.committed_generation == 2);
    EXPECT(trusted.advances == 1 && trusted.requested_generation == 2);
    EXPECT(trusted.reads == 2 && trusted.generation == 2);
    const auto inspected = store.inspect();
    EXPECT(inspected.checkpoint_available && inspected.generation == 2);
    EXPECT(!inspected.recovery_required);
}

void test_invalid_trusted_input_never_writes() {
    for (const auto trusted_error : {
             CriticalAlertSystemTrustedGenerationError::not_initialized,
             CriticalAlertSystemTrustedGenerationError::io_failure,
             CriticalAlertSystemTrustedGenerationError::invalid_state}) {
        FakeCriticalAlertSystemRecoveryStorage storage{};
        PeerAuthorizationRegistry registry{};
        CriticalAlertOutbox outbox{};
        CriticalAlertAckIngress ingress{};
        start_live(registry, outbox, ingress);
        CriticalAlertSystemRecoveryStore store{storage};
        FakeTrustedGeneration trusted{};
        trusted.read_error = trusted_error;
        CriticalAlertSystemRecoverySaveCoordinator coordinator{store, trusted};
        const auto result = coordinator.save(registry, ingress, outbox, 1);
        EXPECT(result.state ==
               CriticalAlertSystemPersistenceState::service_required);
        EXPECT(result.reason ==
               CriticalAlertSystemPersistenceReason::trusted_read_failed);
        EXPECT(storage.writes(0) + storage.writes(1) == 0);
    }

    FakeCriticalAlertSystemRecoveryStorage storage{};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_live(registry, outbox, ingress);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.generation = 0;
    CriticalAlertSystemRecoverySaveCoordinator coordinator{store, trusted};
    const auto zero = coordinator.save(registry, ingress, outbox, 1);
    EXPECT(zero.reason ==
           CriticalAlertSystemPersistenceReason::trusted_generation_invalid);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);

    trusted.generation = 1;
    const auto missing = coordinator.save(registry, ingress, outbox, 1);
    EXPECT(missing.reason ==
           CriticalAlertSystemPersistenceReason::recovery_missing);
    EXPECT(storage.writes(0) + storage.writes(1) == 0);
}

void test_conflict_and_exhaustion_are_typed_before_write() {
    FakeCriticalAlertSystemRecoveryStorage conflict_storage{};
    PeerAuthorizationRegistry original{};
    CriticalAlertOutbox original_outbox{};
    CriticalAlertAckIngress original_ingress{};
    start_live(original, original_outbox, original_ingress);
    seed_one(conflict_storage, original, original_outbox, original_ingress);
    PeerAuthorizationRegistry alternate{};
    CriticalAlertOutbox alternate_outbox{};
    CriticalAlertAckIngress alternate_ingress{};
    start_live(alternate, alternate_outbox, alternate_ingress);
    approve(alternate, 2, 20, 200);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        alternate_bytes{};
    EXPECT(export_critical_alert_system_recovery_checkpoint(
               alternate, alternate_ingress, alternate_outbox, 1, 1,
               alternate_bytes).completed());
    EXPECT(conflict_storage.write_slot(
               1, alternate_bytes.data(), alternate_bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::none);
    const auto writes_before = conflict_storage.writes(0) +
                               conflict_storage.writes(1);
    CriticalAlertSystemRecoveryStore conflict_store{conflict_storage};
    FakeTrustedGeneration conflict_trusted{};
    CriticalAlertSystemRecoverySaveCoordinator conflict_coordinator{
        conflict_store, conflict_trusted};
    const auto conflict = conflict_coordinator.save(
        original, original_ingress, original_outbox, 2);
    EXPECT(conflict.reason ==
           CriticalAlertSystemPersistenceReason::generation_conflict);
    EXPECT(conflict_storage.writes(0) + conflict_storage.writes(1) ==
           writes_before);

    FakeCriticalAlertSystemRecoveryStorage exhausted_storage{};
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        exhausted_bytes{};
    EXPECT(export_critical_alert_system_recovery_checkpoint(
               original, original_ingress, original_outbox, 1,
               std::numeric_limits<std::uint64_t>::max(),
               exhausted_bytes).completed());
    EXPECT(exhausted_storage.write_slot(
               0, exhausted_bytes.data(), exhausted_bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::none);
    CriticalAlertSystemRecoveryStore exhausted_store{exhausted_storage};
    FakeTrustedGeneration exhausted_trusted{};
    exhausted_trusted.generation = std::numeric_limits<std::uint64_t>::max();
    CriticalAlertSystemRecoverySaveCoordinator exhausted_coordinator{
        exhausted_store, exhausted_trusted};
    const auto exhausted = exhausted_coordinator.save(
        original, original_ingress, original_outbox, 2);
    EXPECT(exhausted.reason ==
           CriticalAlertSystemPersistenceReason::generation_exhausted);
    EXPECT(exhausted_storage.writes(0) + exhausted_storage.writes(1) == 1);
}

void test_checkpoint_rejection_is_not_reported_as_committed() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_live(registry, outbox, ingress);
    seed_one(storage, registry, outbox, ingress);
    EXPECT(registry.begin_approval(bridge(2, 20), 0) ==
           PeerAuthorizationError::none);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    CriticalAlertSystemRecoverySaveCoordinator coordinator{store, trusted};
    const auto result = coordinator.save(registry, ingress, outbox, 1);
    EXPECT(result.state ==
           CriticalAlertSystemPersistenceState::service_required);
    EXPECT(result.reason ==
           CriticalAlertSystemPersistenceReason::checkpoint_rejected);
    EXPECT(result.save.error ==
           CriticalAlertSystemRecoveryStoreError::checkpoint_rejected);
    EXPECT(!result.save.commit_uncertain && trusted.advances == 0);
}

void test_mismatched_store_and_trust_require_boot_reconciliation() {
    for (const std::uint64_t trusted_generation : {1ULL, 3ULL}) {
        FakeCriticalAlertSystemRecoveryStorage storage{};
        PeerAuthorizationRegistry registry{};
        CriticalAlertOutbox outbox{};
        CriticalAlertAckIngress ingress{};
        start_live(registry, outbox, ingress);
        CriticalAlertSystemRecoveryStore seed_store{storage};
        EXPECT(seed_store.save(registry, ingress, outbox, 1, 2).saved());
        CriticalAlertSystemRecoveryStore store{storage};
        FakeTrustedGeneration trusted{};
        trusted.generation = trusted_generation;
        CriticalAlertSystemRecoverySaveCoordinator coordinator{store, trusted};
        const auto result = coordinator.save(registry, ingress, outbox, 2);
        EXPECT(result.reason ==
               (trusted_generation < 2
                    ? CriticalAlertSystemPersistenceReason::
                          trusted_reconciliation_required
                    : CriticalAlertSystemPersistenceReason::rollback_detected));
        EXPECT(result.state ==
               (trusted_generation < 2
                    ? CriticalAlertSystemPersistenceState::
                          reboot_reconcile_required
                    : CriticalAlertSystemPersistenceState::service_required));
        EXPECT(storage.writes(0) + storage.writes(1) == 1);
        EXPECT(trusted.advances == 0);
    }
}

void test_uncertain_slot_commit_requires_reboot_reconciliation() {
    for (const auto behavior : {
             FakeSystemRecoveryWriteBehavior::fail_after_full_write,
             FakeSystemRecoveryWriteBehavior::corrupt_after_success}) {
        FakeCriticalAlertSystemRecoveryStorage storage{};
        PeerAuthorizationRegistry registry{};
        CriticalAlertOutbox outbox{};
        CriticalAlertAckIngress ingress{};
        start_live(registry, outbox, ingress);
        seed_one(storage, registry, outbox, ingress);
        storage.set_next_write_behavior(1, behavior);
        CriticalAlertSystemRecoveryStore store{storage};
        FakeTrustedGeneration trusted{};
        CriticalAlertSystemRecoverySaveCoordinator coordinator{store, trusted};
        const auto result = coordinator.save(registry, ingress, outbox, 2);
        EXPECT(result.state == CriticalAlertSystemPersistenceState::
                                   reboot_reconcile_required);
        EXPECT(result.reason ==
               CriticalAlertSystemPersistenceReason::commit_uncertain);
        EXPECT(result.save.commit_uncertain);
        EXPECT(!result.transport_allowed && trusted.advances == 0);
    }
}

void test_trusted_advance_failures_preserve_committed_generation() {
    for (const bool fail_advance : {true, false}) {
        FakeCriticalAlertSystemRecoveryStorage storage{};
        PeerAuthorizationRegistry registry{};
        CriticalAlertOutbox outbox{};
        CriticalAlertAckIngress ingress{};
        start_live(registry, outbox, ingress);
        seed_one(storage, registry, outbox, ingress);
        CriticalAlertSystemRecoveryStore store{storage};
        FakeTrustedGeneration trusted{};
        if (fail_advance) {
            trusted.advance_error =
                CriticalAlertSystemTrustedGenerationError::io_failure;
        } else {
            trusted.persist_advance = false;
        }
        CriticalAlertSystemRecoverySaveCoordinator coordinator{store, trusted};
        const auto result = coordinator.save(registry, ingress, outbox, 2);
        EXPECT(result.state == CriticalAlertSystemPersistenceState::
                                   reboot_reconcile_required);
        EXPECT(result.reason ==
               (fail_advance
                    ? CriticalAlertSystemPersistenceReason::
                          trusted_advance_failed
                    : CriticalAlertSystemPersistenceReason::
                          trusted_readback_failed));
        EXPECT(result.save.saved() && result.committed_generation == 2);
        EXPECT(store.inspect().generation == 2);
        EXPECT(!result.transport_allowed);
    }
}

void test_next_boot_reconciles_failed_trusted_advance() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    PeerAuthorizationRegistry live{};
    CriticalAlertOutbox live_outbox{};
    CriticalAlertAckIngress live_ingress{};
    start_live(live, live_outbox, live_ingress);
    seed_one(storage, live, live_outbox, live_ingress);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.advance_error =
        CriticalAlertSystemTrustedGenerationError::io_failure;
    CriticalAlertSystemRecoverySaveCoordinator saver{store, trusted};
    const auto save = saver.save(live, live_ingress, live_outbox, 2);
    EXPECT(save.state == CriticalAlertSystemPersistenceState::
                             reboot_reconcile_required);
    EXPECT(trusted.generation == 1);

    trusted.advance_error = CriticalAlertSystemTrustedGenerationError::none;
    trusted.readback_error = CriticalAlertSystemTrustedGenerationError::none;
    AcceptKeys keys{};
    CriticalAlertSystemRecoveryBootCoordinator boot{store, trusted, keys};
    PeerAuthorizationRegistry restored{};
    CriticalAlertOutbox restored_outbox{};
    CriticalAlertAckIngress restored_ingress{};
    start_target(restored, restored_outbox, restored_ingress);
    const auto recovered = boot.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        restored, restored_ingress, restored_outbox, 10);
    EXPECT(recovered.operational() && recovered.active_generation == 2);
    EXPECT(trusted.generation == 2);
}

}  // namespace

int main() {
    test_verified_save_advances_and_reads_back_trust();
    test_invalid_trusted_input_never_writes();
    test_conflict_and_exhaustion_are_typed_before_write();
    test_checkpoint_rejection_is_not_reported_as_committed();
    test_mismatched_store_and_trust_require_boot_reconciliation();
    test_uncertain_slot_commit_requires_reboot_reconciliation();
    test_trusted_advance_failures_preserve_committed_generation();
    test_next_boot_reconciles_failed_trusted_advance();
    if (failures != 0) {
        std::cerr << failures << " recovery save assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 critical alert system recovery save groups\n";
    return EXIT_SUCCESS;
}
