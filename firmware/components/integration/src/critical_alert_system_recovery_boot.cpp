#include "opengauge/critical_alert_system_recovery_boot.hpp"

namespace opengauge::integration {
namespace {

bool exactly_empty(
    const CriticalAlertSystemRecoveryInspectionResult& inspection) {
    return !inspection.checkpoint_available &&
           inspection.error ==
               CriticalAlertSystemRecoveryStoreError::no_checkpoint &&
           inspection.slot_a == CriticalAlertSystemRecoverySlotState::empty &&
           inspection.slot_b == CriticalAlertSystemRecoverySlotState::empty;
}

CriticalAlertSystemBootReason load_failure_reason(
    const CriticalAlertSystemRecoveryLoadResult& load) {
    switch (load.error) {
        case CriticalAlertSystemRecoveryStoreError::rollback_detected:
            return CriticalAlertSystemBootReason::rollback_detected;
        case CriticalAlertSystemRecoveryStoreError::generation_conflict:
            return CriticalAlertSystemBootReason::generation_conflict;
        case CriticalAlertSystemRecoveryStoreError::no_checkpoint:
            return CriticalAlertSystemBootReason::recovery_missing;
        case CriticalAlertSystemRecoveryStoreError::storage_failure:
            return CriticalAlertSystemBootReason::storage_unavailable;
        case CriticalAlertSystemRecoveryStoreError::checkpoint_rejected:
            return load.recovery.error == CriticalAlertSystemRecoveryError::
                                              authorization_key_preflight_failed
                ? CriticalAlertSystemBootReason::protected_key_unavailable
                : CriticalAlertSystemBootReason::checkpoint_rejected;
        default:
            return CriticalAlertSystemBootReason::checkpoint_rejected;
    }
}

bool safe_mode_failure(CriticalAlertSystemBootReason reason) {
    return reason == CriticalAlertSystemBootReason::rollback_detected ||
           reason == CriticalAlertSystemBootReason::generation_conflict ||
           reason == CriticalAlertSystemBootReason::checkpoint_rejected;
}

}  // namespace

CriticalAlertSystemRecoveryBootCoordinator::
    CriticalAlertSystemRecoveryBootCoordinator(
        CriticalAlertSystemRecoveryStore& store,
        CriticalAlertSystemTrustedGenerationSource& trusted_generation,
        CriticalAlertSystemRecoveryKeyValidator& key_validator)
    : store_(store),
      trusted_generation_(trusted_generation),
      key_validator_(key_validator) {}

CriticalAlertSystemBootResult CriticalAlertSystemRecoveryBootCoordinator::boot(
    CriticalAlertSystemProvisioningState provisioning,
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms) {
    CriticalAlertSystemBootResult result{};
    if (provisioning == CriticalAlertSystemProvisioningState::unknown) {
        result.reason = CriticalAlertSystemBootReason::provisioning_unknown;
        return result;
    }

    const auto trusted = trusted_generation_.read();
    result.trusted_error = trusted.error;
    result.trusted_generation = trusted.generation;
    if (trusted.error ==
        CriticalAlertSystemTrustedGenerationError::not_initialized) {
        result.inspection = store_.inspect();
        if (provisioning ==
                CriticalAlertSystemProvisioningState::unprovisioned &&
            exactly_empty(result.inspection)) {
            result.state = CriticalAlertSystemBootState::first_boot;
            result.reason = CriticalAlertSystemBootReason::clean_first_boot;
            return result;
        }
        result.reason =
            CriticalAlertSystemBootReason::first_boot_state_conflict;
        return result;
    }
    if (trusted.error != CriticalAlertSystemTrustedGenerationError::none) {
        result.reason = CriticalAlertSystemBootReason::trusted_read_failed;
        return result;
    }
    if (trusted.generation == 0) {
        result.reason =
            CriticalAlertSystemBootReason::trusted_generation_invalid;
        return result;
    }
    if (provisioning != CriticalAlertSystemProvisioningState::provisioned) {
        result.reason =
            CriticalAlertSystemBootReason::first_boot_state_conflict;
        return result;
    }

    result.load = store_.restore_at_or_above_validating_keys(
        authorization, ingress, outbox, now_ms, trusted.generation,
        key_validator_);
    result.active_generation = result.load.generation;
    result.repair_required = result.load.recovery_required;
    if (!result.load.restored) {
        result.reason = load_failure_reason(result.load);
        result.state = safe_mode_failure(result.reason)
            ? CriticalAlertSystemBootState::safe_mode
            : CriticalAlertSystemBootState::service_required;
        return result;
    }

    if (result.load.generation > trusted.generation) {
        result.trusted_error =
            trusted_generation_.advance_to(result.load.generation);
        if (result.trusted_error !=
            CriticalAlertSystemTrustedGenerationError::none) {
            result.reason =
                CriticalAlertSystemBootReason::trusted_advance_failed;
            return result;
        }
        const auto verified = trusted_generation_.read();
        result.trusted_error = verified.error;
        result.trusted_generation = verified.generation;
        if (verified.error !=
                CriticalAlertSystemTrustedGenerationError::none ||
            verified.generation != result.load.generation) {
            result.reason =
                CriticalAlertSystemBootReason::trusted_readback_failed;
            return result;
        }
    }

    result.state = result.load.recovery_required
        ? CriticalAlertSystemBootState::restored_degraded
        : CriticalAlertSystemBootState::restored;
    result.reason = CriticalAlertSystemBootReason::none;
    result.transport_allowed = true;
    return result;
}

}  // namespace opengauge::integration
