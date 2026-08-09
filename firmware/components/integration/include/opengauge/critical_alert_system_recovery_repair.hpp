#pragma once

#include "opengauge/critical_alert_system_recovery_save.hpp"

namespace opengauge::integration {

enum class CriticalAlertSystemRepairState : std::uint8_t {
    service_required = 0,
    repaired,
    reboot_reconcile_required,
};

enum class CriticalAlertSystemRepairReason : std::uint8_t {
    none = 0,
    boot_not_repairable,
    boot_evidence_stale,
    persistence_failed,
    repair_verification_failed,
};

struct CriticalAlertSystemRepairResult {
    CriticalAlertSystemRepairState state{
        CriticalAlertSystemRepairState::service_required};
    CriticalAlertSystemRepairReason reason{
        CriticalAlertSystemRepairReason::boot_not_repairable};
    CriticalAlertSystemRecoveryInspectionResult before{};
    CriticalAlertSystemPersistenceResult persistence{};
    CriticalAlertSystemRecoveryInspectionResult after{};
    std::uint64_t repaired_generation{0};
    bool transport_allowed{false};

    [[nodiscard]] constexpr bool repaired() const {
        return state == CriticalAlertSystemRepairState::repaired &&
               transport_allowed;
    }
};

class CriticalAlertSystemRecoveryRepairCoordinator {
public:
    CriticalAlertSystemRecoveryRepairCoordinator(
        CriticalAlertSystemRecoveryStore& store,
        CriticalAlertSystemRecoverySaveCoordinator& save_coordinator);

    // Repairs only a known empty/checksum-invalid peer slot following an
    // operational restored_degraded boot. Unreadable storage is never admitted.
    [[nodiscard]] CriticalAlertSystemRepairResult repair(
        const CriticalAlertSystemBootResult& boot,
        identity::PeerAuthorizationRegistry& authorization,
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms);

private:
    CriticalAlertSystemRecoveryStore& store_;
    CriticalAlertSystemRecoverySaveCoordinator& save_coordinator_;
};

}  // namespace opengauge::integration
