#include "opengauge/critical_alert_system_recovery_status.hpp"

namespace opengauge::integration {
namespace {

CriticalAlertSystemOperatorReason map_boot_reason(
    CriticalAlertSystemBootReason reason) {
    switch (reason) {
        case CriticalAlertSystemBootReason::none:
            return CriticalAlertSystemOperatorReason::none;
        case CriticalAlertSystemBootReason::clean_first_boot:
            return CriticalAlertSystemOperatorReason::clean_first_boot;
        case CriticalAlertSystemBootReason::provisioning_unknown:
            return CriticalAlertSystemOperatorReason::provisioning_unknown;
        case CriticalAlertSystemBootReason::first_boot_state_conflict:
            return CriticalAlertSystemOperatorReason::provisioning_conflict;
        case CriticalAlertSystemBootReason::trusted_read_failed:
            return CriticalAlertSystemOperatorReason::trusted_state_unavailable;
        case CriticalAlertSystemBootReason::trusted_generation_invalid:
            return CriticalAlertSystemOperatorReason::trusted_state_invalid;
        case CriticalAlertSystemBootReason::recovery_missing:
            return CriticalAlertSystemOperatorReason::recovery_missing;
        case CriticalAlertSystemBootReason::storage_unavailable:
            return CriticalAlertSystemOperatorReason::storage_unavailable;
        case CriticalAlertSystemBootReason::rollback_detected:
            return CriticalAlertSystemOperatorReason::rollback_detected;
        case CriticalAlertSystemBootReason::generation_conflict:
            return CriticalAlertSystemOperatorReason::generation_conflict;
        case CriticalAlertSystemBootReason::checkpoint_rejected:
            return CriticalAlertSystemOperatorReason::checkpoint_rejected;
        case CriticalAlertSystemBootReason::protected_key_unavailable:
            return CriticalAlertSystemOperatorReason::protected_key_unavailable;
        case CriticalAlertSystemBootReason::trusted_advance_failed:
        case CriticalAlertSystemBootReason::trusted_readback_failed:
            return CriticalAlertSystemOperatorReason::trust_update_failed;
    }
    return CriticalAlertSystemOperatorReason::invalid_result;
}

CriticalAlertSystemOperatorReason map_persistence_reason(
    CriticalAlertSystemPersistenceReason reason) {
    switch (reason) {
        case CriticalAlertSystemPersistenceReason::none:
            return CriticalAlertSystemOperatorReason::none;
        case CriticalAlertSystemPersistenceReason::trusted_read_failed:
            return CriticalAlertSystemOperatorReason::trusted_state_unavailable;
        case CriticalAlertSystemPersistenceReason::trusted_generation_invalid:
            return CriticalAlertSystemOperatorReason::trusted_state_invalid;
        case CriticalAlertSystemPersistenceReason::recovery_missing:
            return CriticalAlertSystemOperatorReason::recovery_missing;
        case CriticalAlertSystemPersistenceReason::rollback_detected:
            return CriticalAlertSystemOperatorReason::rollback_detected;
        case CriticalAlertSystemPersistenceReason::trusted_reconciliation_required:
            return CriticalAlertSystemOperatorReason::
                trusted_reconciliation_required;
        case CriticalAlertSystemPersistenceReason::generation_conflict:
            return CriticalAlertSystemOperatorReason::generation_conflict;
        case CriticalAlertSystemPersistenceReason::generation_exhausted:
            return CriticalAlertSystemOperatorReason::generation_exhausted;
        case CriticalAlertSystemPersistenceReason::storage_failure:
            return CriticalAlertSystemOperatorReason::storage_unavailable;
        case CriticalAlertSystemPersistenceReason::checkpoint_rejected:
            return CriticalAlertSystemOperatorReason::checkpoint_rejected;
        case CriticalAlertSystemPersistenceReason::commit_uncertain:
            return CriticalAlertSystemOperatorReason::commit_uncertain;
        case CriticalAlertSystemPersistenceReason::trusted_advance_failed:
        case CriticalAlertSystemPersistenceReason::trusted_readback_failed:
            return CriticalAlertSystemOperatorReason::trust_update_failed;
    }
    return CriticalAlertSystemOperatorReason::invalid_result;
}

bool boot_result_is_coherent(const CriticalAlertSystemBootResult& result) {
    switch (result.state) {
        case CriticalAlertSystemBootState::first_boot:
            return result.reason == CriticalAlertSystemBootReason::clean_first_boot &&
                   !result.transport_allowed && !result.repair_required;
        case CriticalAlertSystemBootState::restored:
            return result.operational() && !result.repair_required &&
                   result.active_generation != 0 &&
                   result.reason == CriticalAlertSystemBootReason::none;
        case CriticalAlertSystemBootState::restored_degraded:
            return result.operational() && result.repair_required &&
                   result.active_generation != 0 &&
                   result.reason == CriticalAlertSystemBootReason::none;
        case CriticalAlertSystemBootState::safe_mode:
            return !result.transport_allowed &&
                   (result.reason ==
                        CriticalAlertSystemBootReason::rollback_detected ||
                    result.reason ==
                        CriticalAlertSystemBootReason::generation_conflict);
        case CriticalAlertSystemBootState::service_required:
            return !result.transport_allowed &&
                   result.reason != CriticalAlertSystemBootReason::none &&
                   map_boot_reason(result.reason) !=
                       CriticalAlertSystemOperatorReason::invalid_result;
    }
    return false;
}

bool persistence_result_is_coherent(
    const CriticalAlertSystemPersistenceResult& result) {
    switch (result.state) {
        case CriticalAlertSystemPersistenceState::committed:
            return result.committed() && result.committed_generation != 0 &&
                   result.save.saved() &&
                   result.observed_trusted_readback ==
                       result.committed_generation &&
                   result.reason == CriticalAlertSystemPersistenceReason::none;
        case CriticalAlertSystemPersistenceState::reboot_reconcile_required:
        case CriticalAlertSystemPersistenceState::service_required:
            return !result.transport_allowed &&
                   result.reason !=
                       CriticalAlertSystemPersistenceReason::none &&
                   map_persistence_reason(result.reason) !=
                       CriticalAlertSystemOperatorReason::invalid_result;
    }
    return false;
}

bool repair_result_is_coherent(const CriticalAlertSystemRepairResult& result) {
    switch (result.state) {
        case CriticalAlertSystemRepairState::repaired:
            return result.repaired() && result.repaired_generation != 0 &&
                   result.reason == CriticalAlertSystemRepairReason::none;
        case CriticalAlertSystemRepairState::reboot_reconcile_required:
        case CriticalAlertSystemRepairState::service_required:
            return !result.transport_allowed &&
                   result.reason != CriticalAlertSystemRepairReason::none;
    }
    return false;
}

CriticalAlertSystemRecoveryStatus invalid_status(
    CriticalAlertSystemRecoveryOperation operation) {
    CriticalAlertSystemRecoveryStatus status{};
    status.operation = operation;
    return status;
}

}  // namespace

CriticalAlertSystemRecoveryStatus make_recovery_status(
    const CriticalAlertSystemBootResult& result) {
    if (!boot_result_is_coherent(result)) {
        return invalid_status(CriticalAlertSystemRecoveryOperation::boot);
    }

    CriticalAlertSystemRecoveryStatus status{};
    status.operation = CriticalAlertSystemRecoveryOperation::boot;
    status.reason = map_boot_reason(result.reason);
    status.slot_a = result.inspection.slot_a;
    status.slot_b = result.inspection.slot_b;
    status.observed_generation = result.active_generation;
    status.trusted_generation = result.trusted_generation;
    status.transport_allowed = result.transport_allowed;
    status.repair_required = result.repair_required;
    status.protected_key_error = result.load.recovery.key_validation_error;
    status.sensitive_detail_redacted =
        result.load.recovery.key_validation_peer_id != 0;

    switch (result.state) {
        case CriticalAlertSystemBootState::first_boot:
            status.state = CriticalAlertSystemOperatorState::first_boot;
            status.action = CriticalAlertSystemOperatorAction::provision;
            break;
        case CriticalAlertSystemBootState::restored:
            status.state = CriticalAlertSystemOperatorState::operational;
            status.action = CriticalAlertSystemOperatorAction::none;
            status.attention_required = false;
            break;
        case CriticalAlertSystemBootState::restored_degraded:
            status.state =
                CriticalAlertSystemOperatorState::operational_degraded;
            status.action =
                CriticalAlertSystemOperatorAction::repair_redundancy;
            break;
        case CriticalAlertSystemBootState::safe_mode:
            status.state = CriticalAlertSystemOperatorState::safe_mode;
            status.action = CriticalAlertSystemOperatorAction::service;
            break;
        case CriticalAlertSystemBootState::service_required:
            status.state =
                CriticalAlertSystemOperatorState::service_required;
            status.action = CriticalAlertSystemOperatorAction::service;
            break;
    }
    return status;
}

CriticalAlertSystemRecoveryStatus make_recovery_status(
    const CriticalAlertSystemPersistenceResult& result) {
    if (!persistence_result_is_coherent(result)) {
        return invalid_status(CriticalAlertSystemRecoveryOperation::save);
    }

    CriticalAlertSystemRecoveryStatus status{};
    status.operation = CriticalAlertSystemRecoveryOperation::save;
    status.reason = map_persistence_reason(result.reason);
    status.slot_a = result.inspection.slot_a;
    status.slot_b = result.inspection.slot_b;
    status.observed_generation = result.committed_generation != 0
                                     ? result.committed_generation
                                     : result.inspection.generation;
    status.trusted_generation = result.observed_trusted_readback != 0
                                    ? result.observed_trusted_readback
                                    : result.prior_trusted_generation;
    status.transport_allowed = result.transport_allowed;

    switch (result.state) {
        case CriticalAlertSystemPersistenceState::committed:
            status.state = CriticalAlertSystemOperatorState::operational;
            status.action = CriticalAlertSystemOperatorAction::none;
            status.attention_required = false;
            break;
        case CriticalAlertSystemPersistenceState::reboot_reconcile_required:
            status.state = CriticalAlertSystemOperatorState::
                reboot_reconcile_required;
            status.action =
                CriticalAlertSystemOperatorAction::reboot_and_reconcile;
            break;
        case CriticalAlertSystemPersistenceState::service_required:
            status.state =
                CriticalAlertSystemOperatorState::service_required;
            status.action = CriticalAlertSystemOperatorAction::service;
            break;
    }
    return status;
}

CriticalAlertSystemRecoveryStatus make_recovery_status(
    const CriticalAlertSystemRepairResult& result) {
    if (!repair_result_is_coherent(result)) {
        return invalid_status(CriticalAlertSystemRecoveryOperation::repair);
    }

    CriticalAlertSystemRecoveryStatus status{};
    status.operation = CriticalAlertSystemRecoveryOperation::repair;
    const bool has_persistence_detail =
        result.reason == CriticalAlertSystemRepairReason::persistence_failed;
    status.reason = has_persistence_detail
                        ? map_persistence_reason(result.persistence.reason)
                        : CriticalAlertSystemOperatorReason::invalid_result;
    if (has_persistence_detail &&
        status.reason == CriticalAlertSystemOperatorReason::invalid_result) {
        return invalid_status(CriticalAlertSystemRecoveryOperation::repair);
    }
    status.observed_generation = result.repaired_generation != 0
                                     ? result.repaired_generation
                                     : result.persistence.committed_generation;
    if (status.observed_generation == 0) {
        status.observed_generation = result.before.generation;
    }
    status.trusted_generation = result.persistence.observed_trusted_readback != 0
                                    ? result.persistence.observed_trusted_readback
                                    : result.persistence.prior_trusted_generation;
    status.transport_allowed = result.transport_allowed;

    const bool use_after = result.persistence.committed_generation != 0;
    status.slot_a = use_after ? result.after.slot_a : result.before.slot_a;
    status.slot_b = use_after ? result.after.slot_b : result.before.slot_b;

    switch (result.state) {
        case CriticalAlertSystemRepairState::repaired:
            status.state = CriticalAlertSystemOperatorState::operational;
            status.reason = CriticalAlertSystemOperatorReason::none;
            status.action = CriticalAlertSystemOperatorAction::none;
            status.attention_required = false;
            break;
        case CriticalAlertSystemRepairState::reboot_reconcile_required:
            status.state = CriticalAlertSystemOperatorState::
                reboot_reconcile_required;
            status.action =
                CriticalAlertSystemOperatorAction::reboot_and_reconcile;
            break;
        case CriticalAlertSystemRepairState::service_required:
            status.state =
                CriticalAlertSystemOperatorState::service_required;
            status.action = CriticalAlertSystemOperatorAction::service;
            switch (result.reason) {
                case CriticalAlertSystemRepairReason::boot_not_repairable:
                    status.reason = CriticalAlertSystemOperatorReason::
                        repair_not_admitted;
                    break;
                case CriticalAlertSystemRepairReason::boot_evidence_stale:
                    status.reason = CriticalAlertSystemOperatorReason::
                        stale_repair_evidence;
                    break;
                case CriticalAlertSystemRepairReason::repair_verification_failed:
                    status.reason = CriticalAlertSystemOperatorReason::
                        repair_verification_failed;
                    break;
                case CriticalAlertSystemRepairReason::persistence_failed:
                    break;
                case CriticalAlertSystemRepairReason::none:
                    status.reason =
                        CriticalAlertSystemOperatorReason::invalid_result;
                    break;
            }
            break;
    }
    return status;
}

}  // namespace opengauge::integration
