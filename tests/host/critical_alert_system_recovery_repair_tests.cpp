#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_critical_alert_system_recovery_storage.hpp"
#include "opengauge/critical_alert_system_recovery_repair.hpp"

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
    CriticalAlertSystemTrustedGenerationError advance_error{
        CriticalAlertSystemTrustedGenerationError::none};
    std::uint64_t generation{1};
    std::size_t advances{0};

    CriticalAlertSystemTrustedGenerationRead read() override {
        return {CriticalAlertSystemTrustedGenerationError::none, generation};
    }

    CriticalAlertSystemTrustedGenerationError advance_to(
        std::uint64_t requested) override {
        ++advances;
        if (advance_error ==
            CriticalAlertSystemTrustedGenerationError::none) {
            generation = requested;
        }
        return advance_error;
    }
};

class AcceptKeys final : public CriticalAlertSystemRecoveryKeyValidator {
public:
    CriticalAlertSystemRecoveryKeyValidationError validate(
        const PeerAuthorizationEntry& peer) override {
        EXPECT(peer.active && peer.secure_key_handle == kKeyHandle);
        return CriticalAlertSystemRecoveryKeyValidationError::none;
    }
};

PairingCandidate bridge() {
    return {1, kPeerId, PeerRole::trail_bridge,
            permission_bit(PeerPermission::receive_critical_alert) |
                permission_bit(PeerPermission::publish_alarm_ack),
            kChannel};
}

void start_live(PeerAuthorizationRegistry& registry,
                CriticalAlertOutbox& outbox,
                CriticalAlertAckIngress& ingress) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    EXPECT(registry.begin_approval(bridge(), 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.approve(1, kKeyHandle, 1, 0) ==
           PeerAuthorizationError::none);
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

void seed(FakeCriticalAlertSystemRecoveryStorage& storage,
          std::size_t generations) {
    PeerAuthorizationRegistry source{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_live(source, outbox, ingress);
    CriticalAlertSystemRecoveryStore store{storage};
    for (std::size_t index = 0; index < generations; ++index) {
        EXPECT(store.save_next(source, ingress, outbox, 1).saved());
    }
}

CriticalAlertSystemBootResult boot(
    CriticalAlertSystemRecoveryStore& store,
    FakeTrustedGeneration& trusted,
    AcceptKeys& keys,
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress) {
    CriticalAlertSystemRecoveryBootCoordinator coordinator{
        store, trusted, keys};
    return coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        registry, ingress, outbox, 10);
}

void test_repairs_known_empty_and_invalid_peer_slots() {
    for (const bool corrupt_peer : {false, true}) {
        FakeCriticalAlertSystemRecoveryStorage storage{};
        seed(storage, corrupt_peer ? 2 : 1);
        if (corrupt_peer) storage.corrupt(0, 100, 0x5AU);
        CriticalAlertSystemRecoveryStore store{storage};
        FakeTrustedGeneration trusted{};
        trusted.generation = corrupt_peer ? 2 : 1;
        AcceptKeys keys{};
        PeerAuthorizationRegistry registry{};
        CriticalAlertOutbox outbox{};
        CriticalAlertAckIngress ingress{};
        start_target(registry, outbox, ingress);
        const auto boot_result =
            boot(store, trusted, keys, registry, outbox, ingress);
        EXPECT(boot_result.state ==
               CriticalAlertSystemBootState::restored_degraded);

        CriticalAlertSystemRecoverySaveCoordinator saver{store, trusted};
        CriticalAlertSystemRecoveryRepairCoordinator repairer{store, saver};
        const auto repaired = repairer.repair(
            boot_result, registry, ingress, outbox, 20);
        EXPECT(repaired.repaired());
        EXPECT(repaired.repaired_generation ==
               (corrupt_peer ? 3 : 2));
        EXPECT(repaired.before.recovery_required);
        EXPECT(!repaired.after.recovery_required);
        EXPECT(repaired.after.slot_a ==
               CriticalAlertSystemRecoverySlotState::valid);
        EXPECT(repaired.after.slot_b ==
               CriticalAlertSystemRecoverySlotState::valid);
        EXPECT(trusted.generation == repaired.repaired_generation);
    }
}

void test_rejects_non_degraded_and_unreadable_boot_results() {
    FakeCriticalAlertSystemRecoveryStorage healthy_storage{};
    seed(healthy_storage, 2);
    CriticalAlertSystemRecoveryStore healthy_store{healthy_storage};
    FakeTrustedGeneration healthy_trusted{};
    healthy_trusted.generation = 2;
    AcceptKeys healthy_keys{};
    PeerAuthorizationRegistry healthy_registry{};
    CriticalAlertOutbox healthy_outbox{};
    CriticalAlertAckIngress healthy_ingress{};
    start_target(healthy_registry, healthy_outbox, healthy_ingress);
    const auto healthy_boot = boot(
        healthy_store, healthy_trusted, healthy_keys, healthy_registry,
        healthy_outbox, healthy_ingress);
    EXPECT(healthy_boot.state == CriticalAlertSystemBootState::restored);
    CriticalAlertSystemRecoverySaveCoordinator healthy_saver{
        healthy_store, healthy_trusted};
    CriticalAlertSystemRecoveryRepairCoordinator healthy_repairer{
        healthy_store, healthy_saver};
    const auto unnecessary = healthy_repairer.repair(
        healthy_boot, healthy_registry, healthy_ingress, healthy_outbox, 20);
    EXPECT(unnecessary.reason ==
           CriticalAlertSystemRepairReason::boot_not_repairable);

    FakeCriticalAlertSystemRecoveryStorage unreadable_storage{};
    seed(unreadable_storage, 2);
    unreadable_storage.fail_next_read(1);
    CriticalAlertSystemRecoveryStore unreadable_store{unreadable_storage};
    FakeTrustedGeneration unreadable_trusted{};
    AcceptKeys unreadable_keys{};
    PeerAuthorizationRegistry unreadable_registry{};
    CriticalAlertOutbox unreadable_outbox{};
    CriticalAlertAckIngress unreadable_ingress{};
    start_target(unreadable_registry, unreadable_outbox, unreadable_ingress);
    const auto unreadable_boot = boot(
        unreadable_store, unreadable_trusted, unreadable_keys,
        unreadable_registry, unreadable_outbox, unreadable_ingress);
    EXPECT(unreadable_boot.state ==
           CriticalAlertSystemBootState::service_required);
    CriticalAlertSystemRecoverySaveCoordinator unreadable_saver{
        unreadable_store, unreadable_trusted};
    CriticalAlertSystemRecoveryRepairCoordinator unreadable_repairer{
        unreadable_store, unreadable_saver};
    const auto refused = unreadable_repairer.repair(
        unreadable_boot, unreadable_registry, unreadable_ingress,
        unreadable_outbox, 20);
    EXPECT(refused.reason ==
           CriticalAlertSystemRepairReason::boot_not_repairable);
}

void test_rejects_stale_boot_evidence_without_writing() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    seed(storage, 1);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    AcceptKeys keys{};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto boot_result =
        boot(store, trusted, keys, registry, outbox, ingress);
    CriticalAlertSystemRecoverySaveCoordinator saver{store, trusted};
    EXPECT(saver.save(registry, ingress, outbox, 20).committed());
    const auto writes_before = storage.writes(0) + storage.writes(1);
    CriticalAlertSystemRecoveryRepairCoordinator repairer{store, saver};
    const auto stale = repairer.repair(
        boot_result, registry, ingress, outbox, 30);
    EXPECT(stale.reason ==
           CriticalAlertSystemRepairReason::boot_evidence_stale);
    EXPECT(storage.writes(0) + storage.writes(1) == writes_before);
}

void test_uncertain_repair_requires_reboot_reconciliation() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    seed(storage, 1);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    AcceptKeys keys{};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto boot_result =
        boot(store, trusted, keys, registry, outbox, ingress);
    storage.set_next_write_behavior(
        1, FakeSystemRecoveryWriteBehavior::fail_after_full_write);
    CriticalAlertSystemRecoverySaveCoordinator saver{store, trusted};
    CriticalAlertSystemRecoveryRepairCoordinator repairer{store, saver};
    const auto result = repairer.repair(
        boot_result, registry, ingress, outbox, 20);
    EXPECT(result.state ==
           CriticalAlertSystemRepairState::reboot_reconcile_required);
    EXPECT(result.reason ==
           CriticalAlertSystemRepairReason::persistence_failed);
    EXPECT(result.persistence.reason ==
           CriticalAlertSystemPersistenceReason::commit_uncertain);
    EXPECT(!result.transport_allowed && trusted.generation == 1);
}

void test_failed_trust_advance_requires_reboot_reconciliation() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    seed(storage, 1);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    AcceptKeys keys{};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto boot_result =
        boot(store, trusted, keys, registry, outbox, ingress);
    trusted.advance_error =
        CriticalAlertSystemTrustedGenerationError::io_failure;
    CriticalAlertSystemRecoverySaveCoordinator saver{store, trusted};
    CriticalAlertSystemRecoveryRepairCoordinator repairer{store, saver};
    const auto result = repairer.repair(
        boot_result, registry, ingress, outbox, 20);
    EXPECT(result.state ==
           CriticalAlertSystemRepairState::reboot_reconcile_required);
    EXPECT(result.persistence.reason ==
           CriticalAlertSystemPersistenceReason::trusted_advance_failed);
    EXPECT(result.persistence.save.saved());
    EXPECT(!result.transport_allowed && trusted.generation == 1);
}

}  // namespace

int main() {
    test_repairs_known_empty_and_invalid_peer_slots();
    test_rejects_non_degraded_and_unreadable_boot_results();
    test_rejects_stale_boot_evidence_without_writing();
    test_uncertain_repair_requires_reboot_reconciliation();
    test_failed_trust_advance_requires_reboot_reconciliation();
    if (failures != 0) {
        std::cerr << failures << " recovery repair assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 5 critical alert system recovery repair groups\n";
    return EXIT_SUCCESS;
}
