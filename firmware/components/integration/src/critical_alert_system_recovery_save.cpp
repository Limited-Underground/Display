#include "opengauge/critical_alert_system_recovery_save.hpp"

namespace opengauge::integration {
namespace {

CriticalAlertSystemPersistenceReason store_failure_reason(
    const CriticalAlertSystemRecoverySaveResult& save) {
    if (save.commit_uncertain)
        return CriticalAlertSystemPersistenceReason::commit_uncertain;
    switch (save.error) {
        case CriticalAlertSystemRecoveryStoreError::generation_conflict:
            return CriticalAlertSystemPersistenceReason::generation_conflict;
        case CriticalAlertSystemRecoveryStoreError::generation_exhausted:
            return CriticalAlertSystemPersistenceReason::generation_exhausted;
        case CriticalAlertSystemRecoveryStoreError::checkpoint_rejected:
            return CriticalAlertSystemPersistenceReason::checkpoint_rejected;
        default:
            return CriticalAlertSystemPersistenceReason::storage_failure;
    }
}

}  // namespace

CriticalAlertSystemRecoverySaveCoordinator::
    CriticalAlertSystemRecoverySaveCoordinator(
        CriticalAlertSystemRecoveryStore& store,
        CriticalAlertSystemTrustedGenerationSource& trusted_generation)
    : store_(store), trusted_generation_(trusted_generation) {}

CriticalAlertSystemPersistenceResult
CriticalAlertSystemRecoverySaveCoordinator::save(
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms) {
    CriticalAlertSystemPersistenceResult result{};
    const auto trusted = trusted_generation_.read();
    result.trusted_error = trusted.error;
    result.prior_trusted_generation = trusted.generation;
    if (trusted.error != CriticalAlertSystemTrustedGenerationError::none) {
        result.reason =
            CriticalAlertSystemPersistenceReason::trusted_read_failed;
        return result;
    }
    if (trusted.generation == 0) {
        result.reason =
            CriticalAlertSystemPersistenceReason::trusted_generation_invalid;
        return result;
    }

    result.inspection = store_.inspect();
    if (!result.inspection.checkpoint_available) {
        result.reason = result.inspection.error ==
                                CriticalAlertSystemRecoveryStoreError::
                                    generation_conflict
                            ? CriticalAlertSystemPersistenceReason::
                                  generation_conflict
                        : result.inspection.error ==
                                  CriticalAlertSystemRecoveryStoreError::
                                      storage_failure
                            ? CriticalAlertSystemPersistenceReason::
                                  storage_failure
                            : CriticalAlertSystemPersistenceReason::
                                  recovery_missing;
        return result;
    }
    if (result.inspection.error ==
        CriticalAlertSystemRecoveryStoreError::storage_failure) {
        result.reason = CriticalAlertSystemPersistenceReason::storage_failure;
        return result;
    }
    if (result.inspection.generation < trusted.generation) {
        result.reason = CriticalAlertSystemPersistenceReason::rollback_detected;
        return result;
    }
    if (result.inspection.generation > trusted.generation) {
        result.state =
            CriticalAlertSystemPersistenceState::reboot_reconcile_required;
        result.reason = CriticalAlertSystemPersistenceReason::
            trusted_reconciliation_required;
        return result;
    }

    result.save = store_.save_next_after(
        authorization, ingress, outbox, now_ms, trusted.generation);
    result.committed_generation = result.save.generation;
    if (!result.save.saved()) {
        result.reason = store_failure_reason(result.save);
        result.state = result.save.commit_uncertain
            ? CriticalAlertSystemPersistenceState::reboot_reconcile_required
            : CriticalAlertSystemPersistenceState::service_required;
        return result;
    }

    result.trusted_error =
        trusted_generation_.advance_to(result.save.generation);
    if (result.trusted_error !=
        CriticalAlertSystemTrustedGenerationError::none) {
        result.state =
            CriticalAlertSystemPersistenceState::reboot_reconcile_required;
        result.reason =
            CriticalAlertSystemPersistenceReason::trusted_advance_failed;
        return result;
    }
    const auto verified = trusted_generation_.read();
    result.trusted_error = verified.error;
    result.observed_trusted_readback = verified.generation;
    if (verified.error != CriticalAlertSystemTrustedGenerationError::none ||
        verified.generation != result.save.generation) {
        result.state =
            CriticalAlertSystemPersistenceState::reboot_reconcile_required;
        result.reason =
            CriticalAlertSystemPersistenceReason::trusted_readback_failed;
        return result;
    }

    result.state = CriticalAlertSystemPersistenceState::committed;
    result.reason = CriticalAlertSystemPersistenceReason::none;
    result.transport_allowed = true;
    return result;
}

}  // namespace opengauge::integration
