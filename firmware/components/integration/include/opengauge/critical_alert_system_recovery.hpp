#pragma once

#include "opengauge/critical_alert_recovery.hpp"
#include "opengauge/critical_alert_system_recovery_checkpoint.hpp"

namespace opengauge::integration {

enum class CriticalAlertSystemRecoveryKeyValidationError : std::uint8_t {
    none = 0,
    key_unavailable,
    purpose_mismatch,
    backend_failure,
    registry_snapshot_failed,
};

// Validates only an opaque handle and its logical authorization metadata.
// Implementations must not export raw key bytes through this boundary.
class CriticalAlertSystemRecoveryKeyValidator {
public:
    virtual ~CriticalAlertSystemRecoveryKeyValidator() = default;
    [[nodiscard]] virtual CriticalAlertSystemRecoveryKeyValidationError validate(
        const identity::PeerAuthorizationEntry& peer) = 0;
};

enum class CriticalAlertSystemRecoveryError : std::uint8_t {
    none = 0,
    invalid_generation,
    checkpoint_rejected,
    authorization_export_failed,
    critical_export_failed,
    authorization_preflight_failed,
    authorization_key_preflight_failed,
    critical_preflight_failed,
    authorization_import_failed,
    critical_import_failed,
};

struct CriticalAlertSystemRecoveryResult {
    CriticalAlertSystemRecoveryError error{
        CriticalAlertSystemRecoveryError::checkpoint_rejected};
    CriticalAlertSystemRecoveryCheckpointError checkpoint_error{
        CriticalAlertSystemRecoveryCheckpointError::none};
    identity::PeerAuthorizationError authorization_error{
        identity::PeerAuthorizationError::none};
    CriticalAlertSystemRecoveryKeyValidationError key_validation_error{
        CriticalAlertSystemRecoveryKeyValidationError::none};
    std::uint32_t key_validation_peer_id{0};
    CriticalAlertRecoveryResult critical{};
    std::uint64_t generation{0};

    [[nodiscard]] constexpr bool completed() const {
        return error == CriticalAlertSystemRecoveryError::none;
    }
};

// Caller must provide exclusive ownership of all three live owners for the
// complete operation. Output and live import state change only after every
// nested component preflight passes against dependency-correct candidates.
[[nodiscard]] CriticalAlertSystemRecoveryResult
export_critical_alert_system_recovery_checkpoint(
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    std::uint64_t generation,
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>& output);

[[nodiscard]] CriticalAlertSystemRecoveryResult
import_critical_alert_system_recovery_checkpoint(
    const std::uint8_t* checkpoint,
    std::size_t checkpoint_size,
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms);

// Target-boot form. Every active restored opaque key handle is validated on the
// private authorization candidate before outbox/ACK preflight or live import.
[[nodiscard]] CriticalAlertSystemRecoveryResult
import_critical_alert_system_recovery_checkpoint_validating_keys(
    const std::uint8_t* checkpoint,
    std::size_t checkpoint_size,
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    CriticalAlertSystemRecoveryKeyValidator& key_validator);

}  // namespace opengauge::integration
