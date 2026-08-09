#pragma once

#include "opengauge/critical_alert_system_recovery_boot.hpp"

namespace opengauge::integration {

enum class CriticalAlertSystemPersistenceState : std::uint8_t {
    service_required = 0,
    committed,
    reboot_reconcile_required,
};

enum class CriticalAlertSystemPersistenceReason : std::uint8_t {
    none = 0,
    trusted_read_failed,
    trusted_generation_invalid,
    recovery_missing,
    rollback_detected,
    trusted_reconciliation_required,
    generation_conflict,
    generation_exhausted,
    storage_failure,
    checkpoint_rejected,
    commit_uncertain,
    trusted_advance_failed,
    trusted_readback_failed,
};

struct CriticalAlertSystemPersistenceResult {
    CriticalAlertSystemPersistenceState state{
        CriticalAlertSystemPersistenceState::service_required};
    CriticalAlertSystemPersistenceReason reason{
        CriticalAlertSystemPersistenceReason::trusted_read_failed};
    CriticalAlertSystemTrustedGenerationError trusted_error{
        CriticalAlertSystemTrustedGenerationError::none};
    CriticalAlertSystemRecoveryInspectionResult inspection{};
    CriticalAlertSystemRecoverySaveResult save{};
    std::uint64_t prior_trusted_generation{0};
    std::uint64_t observed_trusted_readback{0};
    std::uint64_t committed_generation{0};
    bool transport_allowed{false};

    [[nodiscard]] constexpr bool committed() const {
        return state == CriticalAlertSystemPersistenceState::committed &&
               transport_allowed;
    }
};

class CriticalAlertSystemRecoverySaveCoordinator {
public:
    CriticalAlertSystemRecoverySaveCoordinator(
        CriticalAlertSystemRecoveryStore& store,
        CriticalAlertSystemTrustedGenerationSource& trusted_generation);

    // Normal-operation persistence only; first provisioning has a separate
    // authorized workflow. A non-committed result requires transport to remain
    // disabled until service or a boot-time reconciliation succeeds.
    [[nodiscard]] CriticalAlertSystemPersistenceResult save(
        identity::PeerAuthorizationRegistry& authorization,
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms);

private:
    CriticalAlertSystemRecoveryStore& store_;
    CriticalAlertSystemTrustedGenerationSource& trusted_generation_;
};

}  // namespace opengauge::integration
