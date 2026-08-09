#include "opengauge/critical_alert_system_recovery_repair.hpp"

namespace opengauge::integration {
namespace {

bool known_single_slot_degradation(
    const CriticalAlertSystemRecoveryInspectionResult& inspection) {
    const bool a_valid =
        inspection.slot_a == CriticalAlertSystemRecoverySlotState::valid;
    const bool b_valid =
        inspection.slot_b == CriticalAlertSystemRecoverySlotState::valid;
    const bool a_known_missing =
        inspection.slot_a == CriticalAlertSystemRecoverySlotState::empty ||
        inspection.slot_a == CriticalAlertSystemRecoverySlotState::invalid;
    const bool b_known_missing =
        inspection.slot_b == CriticalAlertSystemRecoverySlotState::empty ||
        inspection.slot_b == CriticalAlertSystemRecoverySlotState::invalid;
    return (a_valid && b_known_missing) || (b_valid && a_known_missing);
}

}  // namespace

CriticalAlertSystemRecoveryRepairCoordinator::
    CriticalAlertSystemRecoveryRepairCoordinator(
        CriticalAlertSystemRecoveryStore& store,
        CriticalAlertSystemRecoverySaveCoordinator& save_coordinator)
    : store_(store), save_coordinator_(save_coordinator) {}

CriticalAlertSystemRepairResult
CriticalAlertSystemRecoveryRepairCoordinator::repair(
    const CriticalAlertSystemBootResult& boot,
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms) {
    CriticalAlertSystemRepairResult result{};
    if (boot.state != CriticalAlertSystemBootState::restored_degraded ||
        !boot.operational() || !boot.repair_required ||
        boot.load.error != CriticalAlertSystemRecoveryStoreError::none ||
        boot.active_generation == 0 ||
        boot.active_generation != boot.trusted_generation) {
        return result;
    }

    result.before = store_.inspect();
    if (result.before.error !=
            CriticalAlertSystemRecoveryStoreError::none ||
        !result.before.checkpoint_available ||
        !result.before.recovery_required ||
        result.before.generation != boot.active_generation ||
        !known_single_slot_degradation(result.before)) {
        result.reason = CriticalAlertSystemRepairReason::boot_evidence_stale;
        return result;
    }

    result.persistence = save_coordinator_.save(
        authorization, ingress, outbox, now_ms);
    if (!result.persistence.committed()) {
        result.state = result.persistence.state ==
                               CriticalAlertSystemPersistenceState::
                                   reboot_reconcile_required
                           ? CriticalAlertSystemRepairState::
                                 reboot_reconcile_required
                           : CriticalAlertSystemRepairState::service_required;
        result.reason = CriticalAlertSystemRepairReason::persistence_failed;
        return result;
    }

    result.repaired_generation = result.persistence.committed_generation;
    result.after = store_.inspect();
    if (result.after.error != CriticalAlertSystemRecoveryStoreError::none ||
        !result.after.checkpoint_available || result.after.recovery_required ||
        result.after.generation != result.repaired_generation ||
        result.after.slot_a != CriticalAlertSystemRecoverySlotState::valid ||
        result.after.slot_b != CriticalAlertSystemRecoverySlotState::valid) {
        result.reason =
            CriticalAlertSystemRepairReason::repair_verification_failed;
        return result;
    }

    result.state = CriticalAlertSystemRepairState::repaired;
    result.reason = CriticalAlertSystemRepairReason::none;
    result.transport_allowed = true;
    return result;
}

}  // namespace opengauge::integration
