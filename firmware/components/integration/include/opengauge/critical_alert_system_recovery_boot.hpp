#pragma once

#include "opengauge/critical_alert_system_recovery_store.hpp"

namespace opengauge::integration {

enum class CriticalAlertSystemTrustedGenerationError : std::uint8_t {
    none = 0,
    not_initialized,
    io_failure,
    invalid_state,
    rejected,
};

struct CriticalAlertSystemTrustedGenerationRead {
    CriticalAlertSystemTrustedGenerationError error{
        CriticalAlertSystemTrustedGenerationError::io_failure};
    std::uint64_t generation{0};
};

class CriticalAlertSystemTrustedGenerationSource {
public:
    virtual ~CriticalAlertSystemTrustedGenerationSource() = default;
    [[nodiscard]] virtual CriticalAlertSystemTrustedGenerationRead read() = 0;
    // A successful implementation must persist a value greater than or equal
    // to its prior value. The coordinator performs an exact readback check.
    [[nodiscard]] virtual CriticalAlertSystemTrustedGenerationError advance_to(
        std::uint64_t generation) = 0;
};

enum class CriticalAlertSystemProvisioningState : std::uint8_t {
    unknown = 0,
    unprovisioned,
    provisioned,
};

enum class CriticalAlertSystemBootState : std::uint8_t {
    service_required = 0,
    first_boot,
    restored,
    restored_degraded,
    safe_mode,
};

enum class CriticalAlertSystemBootReason : std::uint8_t {
    none = 0,
    clean_first_boot,
    provisioning_unknown,
    first_boot_state_conflict,
    trusted_read_failed,
    trusted_generation_invalid,
    recovery_missing,
    storage_unavailable,
    rollback_detected,
    generation_conflict,
    checkpoint_rejected,
    protected_key_unavailable,
    trusted_advance_failed,
    trusted_readback_failed,
};

struct CriticalAlertSystemBootResult {
    CriticalAlertSystemBootState state{
        CriticalAlertSystemBootState::service_required};
    CriticalAlertSystemBootReason reason{
        CriticalAlertSystemBootReason::trusted_read_failed};
    CriticalAlertSystemTrustedGenerationError trusted_error{
        CriticalAlertSystemTrustedGenerationError::none};
    CriticalAlertSystemRecoveryInspectionResult inspection{};
    CriticalAlertSystemRecoveryLoadResult load{};
    std::uint64_t trusted_generation{0};
    std::uint64_t active_generation{0};
    bool transport_allowed{false};
    bool repair_required{false};

    [[nodiscard]] constexpr bool operational() const {
        return transport_allowed &&
               (state == CriticalAlertSystemBootState::restored ||
                state == CriticalAlertSystemBootState::restored_degraded);
    }
};

class CriticalAlertSystemRecoveryBootCoordinator {
public:
    CriticalAlertSystemRecoveryBootCoordinator(
        CriticalAlertSystemRecoveryStore& store,
        CriticalAlertSystemTrustedGenerationSource& trusted_generation,
        CriticalAlertSystemRecoveryKeyValidator& key_validator);

    // Caller owns exclusive boot-time access. Live owners must already be
    // started with their target policies but must not have accepted runtime
    // state. Transport remains disabled unless operational() is true.
    [[nodiscard]] CriticalAlertSystemBootResult boot(
        CriticalAlertSystemProvisioningState provisioning,
        identity::PeerAuthorizationRegistry& authorization,
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms);

private:
    CriticalAlertSystemRecoveryStore& store_;
    CriticalAlertSystemTrustedGenerationSource& trusted_generation_;
    CriticalAlertSystemRecoveryKeyValidator& key_validator_;
};

}  // namespace opengauge::integration
