#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_critical_alert_system_recovery_storage.hpp"
#include "opengauge/critical_alert_system_recovery_boot.hpp"

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

class FakeKeyValidator final : public CriticalAlertSystemRecoveryKeyValidator {
public:
    CriticalAlertSystemRecoveryKeyValidationError response{
        CriticalAlertSystemRecoveryKeyValidationError::none};
    std::size_t calls{0};

    CriticalAlertSystemRecoveryKeyValidationError validate(
        const PeerAuthorizationEntry& peer) override {
        ++calls;
        EXPECT(peer.active && peer.logical_peer_id == kPeerId);
        EXPECT(peer.secure_key_handle == kKeyHandle);
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
    std::array<std::uint8_t, kCriticalAlertFrameBytes> frame{};
    EXPECT(encode_critical_alert(alert(), frame).encoded());
    EXPECT(outbox.enqueue(frame, 0) == CriticalOutboxError::none);
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
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_source(registry, outbox, ingress);
    CriticalAlertSystemRecoveryStore store{storage};
    for (std::size_t index = 0; index < generations; ++index) {
        EXPECT(store.save_next(registry, ingress, outbox, 1).saved());
    }
}

void expect_empty(const PeerAuthorizationRegistry& registry,
                  const CriticalAlertOutbox& outbox,
                  const CriticalAlertAckIngress& ingress) {
    EXPECT(registry.status().peer_count == 0);
    EXPECT(outbox.status().queued_count == 0);
    EXPECT(ingress.status().binding_count == 0);
}

void test_clean_first_boot_requires_two_independent_empty_states() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.read_error =
        CriticalAlertSystemTrustedGenerationError::not_initialized;
    FakeKeyValidator keys{};
    CriticalAlertSystemRecoveryBootCoordinator coordinator{
        store, trusted, keys};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto first = coordinator.boot(
        CriticalAlertSystemProvisioningState::unprovisioned,
        registry, ingress, outbox, 10);
    EXPECT(first.state == CriticalAlertSystemBootState::first_boot);
    EXPECT(first.reason == CriticalAlertSystemBootReason::clean_first_boot);
    EXPECT(first.inspection.slot_a ==
           CriticalAlertSystemRecoverySlotState::empty);
    EXPECT(first.inspection.slot_b ==
           CriticalAlertSystemRecoverySlotState::empty);
    EXPECT(!first.transport_allowed && !first.operational());
    expect_empty(registry, outbox, ingress);

    seed(storage, 1);
    storage.corrupt(0, 100, 0x5AU);
    PeerAuthorizationRegistry corrupt_registry{};
    CriticalAlertOutbox corrupt_outbox{};
    CriticalAlertAckIngress corrupt_ingress{};
    start_target(corrupt_registry, corrupt_outbox, corrupt_ingress);
    const auto conflict = coordinator.boot(
        CriticalAlertSystemProvisioningState::unprovisioned,
        corrupt_registry, corrupt_ingress, corrupt_outbox, 10);
    EXPECT(conflict.state == CriticalAlertSystemBootState::service_required);
    EXPECT(conflict.reason ==
           CriticalAlertSystemBootReason::first_boot_state_conflict);
    EXPECT(conflict.inspection.slot_a ==
           CriticalAlertSystemRecoverySlotState::invalid);
    expect_empty(corrupt_registry, corrupt_outbox, corrupt_ingress);
}

void test_provisioning_and_trusted_input_fail_closed() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    FakeKeyValidator keys{};
    CriticalAlertSystemRecoveryBootCoordinator coordinator{
        store, trusted, keys};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);

    const auto unknown = coordinator.boot(
        CriticalAlertSystemProvisioningState::unknown,
        registry, ingress, outbox, 10);
    EXPECT(unknown.reason ==
           CriticalAlertSystemBootReason::provisioning_unknown);
    EXPECT(trusted.reads == 0);

    trusted.read_error = CriticalAlertSystemTrustedGenerationError::io_failure;
    const auto unreadable = coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        registry, ingress, outbox, 10);
    EXPECT(unreadable.reason ==
           CriticalAlertSystemBootReason::trusted_read_failed);

    trusted.read_error = CriticalAlertSystemTrustedGenerationError::none;
    trusted.generation = 0;
    const auto zero = coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        registry, ingress, outbox, 10);
    EXPECT(zero.reason ==
           CriticalAlertSystemBootReason::trusted_generation_invalid);

    trusted.generation = 1;
    const auto inconsistent = coordinator.boot(
        CriticalAlertSystemProvisioningState::unprovisioned,
        registry, ingress, outbox, 10);
    EXPECT(inconsistent.reason ==
           CriticalAlertSystemBootReason::first_boot_state_conflict);
    expect_empty(registry, outbox, ingress);
}

void test_exact_restore_enables_transport() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    seed(storage, 2);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.generation = 2;
    FakeKeyValidator keys{};
    CriticalAlertSystemRecoveryBootCoordinator coordinator{
        store, trusted, keys};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto result = coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        registry, ingress, outbox, 10);
    EXPECT(result.state == CriticalAlertSystemBootState::restored);
    EXPECT(result.reason == CriticalAlertSystemBootReason::none);
    EXPECT(result.operational() && !result.repair_required);
    EXPECT(result.active_generation == 2 && trusted.advances == 0);
    EXPECT(keys.calls == 1);
}

void test_degraded_restore_is_visible_but_operational() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    seed(storage, 2);
    storage.corrupt(0, 100, 0x5AU);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.generation = 2;
    FakeKeyValidator keys{};
    CriticalAlertSystemRecoveryBootCoordinator coordinator{
        store, trusted, keys};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto result = coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        registry, ingress, outbox, 10);
    EXPECT(result.state ==
           CriticalAlertSystemBootState::restored_degraded);
    EXPECT(result.operational() && result.repair_required);
    EXPECT(result.load.slot_a ==
           CriticalAlertSystemRecoverySlotState::invalid);
}

void test_rollback_and_generation_conflict_enter_safe_mode() {
    FakeCriticalAlertSystemRecoveryStorage rollback_storage{};
    seed(rollback_storage, 1);
    CriticalAlertSystemRecoveryStore rollback_store{rollback_storage};
    FakeTrustedGeneration rollback_trusted{};
    rollback_trusted.generation = 2;
    FakeKeyValidator rollback_keys{};
    CriticalAlertSystemRecoveryBootCoordinator rollback_coordinator{
        rollback_store, rollback_trusted, rollback_keys};
    PeerAuthorizationRegistry rollback_registry{};
    CriticalAlertOutbox rollback_outbox{};
    CriticalAlertAckIngress rollback_ingress{};
    start_target(rollback_registry, rollback_outbox, rollback_ingress);
    const auto rollback = rollback_coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        rollback_registry, rollback_ingress, rollback_outbox, 10);
    EXPECT(rollback.state == CriticalAlertSystemBootState::safe_mode);
    EXPECT(rollback.reason ==
           CriticalAlertSystemBootReason::rollback_detected);
    expect_empty(rollback_registry, rollback_outbox, rollback_ingress);

    FakeCriticalAlertSystemRecoveryStorage conflict_storage{};
    seed(conflict_storage, 1);
    PeerAuthorizationRegistry alternate{};
    CriticalAlertOutbox alternate_outbox{};
    CriticalAlertAckIngress alternate_ingress{};
    start_source(alternate, alternate_outbox, alternate_ingress);
    approve(alternate, 2, 20, 200);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        alternate_bytes{};
    EXPECT(export_critical_alert_system_recovery_checkpoint(
               alternate, alternate_ingress, alternate_outbox, 1, 1,
               alternate_bytes).completed());
    EXPECT(conflict_storage.write_slot(
               1, alternate_bytes.data(), alternate_bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::none);
    CriticalAlertSystemRecoveryStore conflict_store{conflict_storage};
    FakeTrustedGeneration conflict_trusted{};
    FakeKeyValidator conflict_keys{};
    CriticalAlertSystemRecoveryBootCoordinator conflict_coordinator{
        conflict_store, conflict_trusted, conflict_keys};
    PeerAuthorizationRegistry conflict_registry{};
    CriticalAlertOutbox conflict_outbox{};
    CriticalAlertAckIngress conflict_ingress{};
    start_target(conflict_registry, conflict_outbox, conflict_ingress);
    const auto conflict = conflict_coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        conflict_registry, conflict_ingress, conflict_outbox, 10);
    EXPECT(conflict.state == CriticalAlertSystemBootState::safe_mode);
    EXPECT(conflict.reason ==
           CriticalAlertSystemBootReason::generation_conflict);
    expect_empty(conflict_registry, conflict_outbox, conflict_ingress);
}

void test_missing_protected_key_requires_service_without_import() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    seed(storage, 2);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.generation = 2;
    FakeKeyValidator keys{};
    keys.response =
        CriticalAlertSystemRecoveryKeyValidationError::key_unavailable;
    CriticalAlertSystemRecoveryBootCoordinator coordinator{
        store, trusted, keys};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto result = coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        registry, ingress, outbox, 10);
    EXPECT(result.state == CriticalAlertSystemBootState::service_required);
    EXPECT(result.reason ==
           CriticalAlertSystemBootReason::protected_key_unavailable);
    EXPECT(result.load.recovery.key_validation_error ==
           CriticalAlertSystemRecoveryKeyValidationError::key_unavailable);
    EXPECT(!result.transport_allowed);
    expect_empty(registry, outbox, ingress);
}

void test_interrupted_trusted_advance_is_reconciled_and_verified() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    seed(storage, 2);
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    trusted.generation = 1;
    FakeKeyValidator keys{};
    CriticalAlertSystemRecoveryBootCoordinator coordinator{
        store, trusted, keys};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto result = coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        registry, ingress, outbox, 10);
    EXPECT(result.state == CriticalAlertSystemBootState::restored);
    EXPECT(result.operational());
    EXPECT(trusted.advances == 1 && trusted.requested_generation == 2);
    EXPECT(trusted.reads == 2 && result.trusted_generation == 2);
}

void test_trusted_advance_failures_keep_transport_disabled() {
    for (const bool fail_write : {true, false}) {
        FakeCriticalAlertSystemRecoveryStorage storage{};
        seed(storage, 2);
        CriticalAlertSystemRecoveryStore store{storage};
        FakeTrustedGeneration trusted{};
        trusted.generation = 1;
        if (fail_write) {
            trusted.advance_error =
                CriticalAlertSystemTrustedGenerationError::io_failure;
        } else {
            trusted.persist_advance = false;
        }
        FakeKeyValidator keys{};
        CriticalAlertSystemRecoveryBootCoordinator coordinator{
            store, trusted, keys};
        PeerAuthorizationRegistry registry{};
        CriticalAlertOutbox outbox{};
        CriticalAlertAckIngress ingress{};
        start_target(registry, outbox, ingress);
        const auto result = coordinator.boot(
            CriticalAlertSystemProvisioningState::provisioned,
            registry, ingress, outbox, 10);
        EXPECT(result.state ==
               CriticalAlertSystemBootState::service_required);
        EXPECT(result.reason ==
               (fail_write
                    ? CriticalAlertSystemBootReason::trusted_advance_failed
                    : CriticalAlertSystemBootReason::trusted_readback_failed));
        EXPECT(!result.transport_allowed && !result.operational());
        EXPECT(registry.status().peer_count == 1);
    }
}

void test_initialized_trust_without_checkpoint_requires_service() {
    FakeCriticalAlertSystemRecoveryStorage storage{};
    CriticalAlertSystemRecoveryStore store{storage};
    FakeTrustedGeneration trusted{};
    FakeKeyValidator keys{};
    CriticalAlertSystemRecoveryBootCoordinator coordinator{
        store, trusted, keys};
    PeerAuthorizationRegistry registry{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_target(registry, outbox, ingress);
    const auto result = coordinator.boot(
        CriticalAlertSystemProvisioningState::provisioned,
        registry, ingress, outbox, 10);
    EXPECT(result.state == CriticalAlertSystemBootState::service_required);
    EXPECT(result.reason == CriticalAlertSystemBootReason::recovery_missing);
    expect_empty(registry, outbox, ingress);
}

}  // namespace

int main() {
    test_clean_first_boot_requires_two_independent_empty_states();
    test_provisioning_and_trusted_input_fail_closed();
    test_exact_restore_enables_transport();
    test_degraded_restore_is_visible_but_operational();
    test_rollback_and_generation_conflict_enter_safe_mode();
    test_missing_protected_key_requires_service_without_import();
    test_interrupted_trusted_advance_is_reconciled_and_verified();
    test_trusted_advance_failures_keep_transport_disabled();
    test_initialized_trust_without_checkpoint_requires_service();
    if (failures != 0) {
        std::cerr << failures << " recovery boot assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 critical alert system recovery boot groups\n";
    return EXIT_SUCCESS;
}
