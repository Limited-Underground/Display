#pragma once

#include <cstdint>
#include <type_traits>

#include "opengauge/critical_alert_system_recovery_repair.hpp"

namespace opengauge::integration {

enum class CriticalAlertSystemRecoveryOperation : std::uint8_t {
    boot = 0,
    save,
    repair,
};

enum class CriticalAlertSystemOperatorState : std::uint8_t {
    service_required = 0,
    first_boot,
    operational,
    operational_degraded,
    safe_mode,
    reboot_reconcile_required,
};

enum class CriticalAlertSystemOperatorReason : std::uint8_t {
    none = 0,
    clean_first_boot,
    provisioning_unknown,
    provisioning_conflict,
    trusted_state_unavailable,
    trusted_state_invalid,
    recovery_missing,
    storage_unavailable,
    rollback_detected,
    generation_conflict,
    checkpoint_rejected,
    protected_key_unavailable,
    trusted_reconciliation_required,
    trust_update_failed,
    generation_exhausted,
    commit_uncertain,
    repair_not_admitted,
    stale_repair_evidence,
    repair_verification_failed,
    invalid_result,
};

enum class CriticalAlertSystemOperatorAction : std::uint8_t {
    none = 0,
    provision,
    repair_redundancy,
    reboot_and_reconcile,
    service,
};

// Fixed-shape, pointer-free status for target logs or operator presentation.
// It deliberately carries no peer ID, key handle, address, credential, or raw
// checkpoint bytes. Generation values are non-secret recovery audit evidence.
struct CriticalAlertSystemRecoveryStatus {
    CriticalAlertSystemRecoveryOperation operation{
        CriticalAlertSystemRecoveryOperation::boot};
    CriticalAlertSystemOperatorState state{
        CriticalAlertSystemOperatorState::service_required};
    CriticalAlertSystemOperatorReason reason{
        CriticalAlertSystemOperatorReason::invalid_result};
    CriticalAlertSystemOperatorAction action{
        CriticalAlertSystemOperatorAction::service};
    CriticalAlertSystemRecoverySlotState slot_a{
        CriticalAlertSystemRecoverySlotState::empty};
    CriticalAlertSystemRecoverySlotState slot_b{
        CriticalAlertSystemRecoverySlotState::empty};
    CriticalAlertSystemRecoveryKeyValidationError protected_key_error{
        CriticalAlertSystemRecoveryKeyValidationError::none};
    std::uint64_t observed_generation{0};
    std::uint64_t trusted_generation{0};
    bool transport_allowed{false};
    bool attention_required{true};
    bool repair_required{false};
    bool sensitive_detail_redacted{false};
};

static_assert(
    std::is_trivially_copyable_v<CriticalAlertSystemRecoveryStatus>,
    "Recovery status must remain a fixed-value diagnostic record");
static_assert(
    sizeof(CriticalAlertSystemRecoveryStatus) <= 40,
    "Recovery status must remain bounded for embedded diagnostics");

[[nodiscard]] CriticalAlertSystemRecoveryStatus make_recovery_status(
    const CriticalAlertSystemBootResult& result);
[[nodiscard]] CriticalAlertSystemRecoveryStatus make_recovery_status(
    const CriticalAlertSystemPersistenceResult& result);
[[nodiscard]] CriticalAlertSystemRecoveryStatus make_recovery_status(
    const CriticalAlertSystemRepairResult& result);

}  // namespace opengauge::integration
