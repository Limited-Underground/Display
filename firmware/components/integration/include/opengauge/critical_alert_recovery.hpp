#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_recovery_checkpoint.hpp"

namespace opengauge::integration {

enum class CriticalAlertRecoveryError : std::uint8_t {
    none = 0,
    invalid_generation,
    checkpoint_rejected,
    ack_export_failed,
    outbox_export_failed,
    ack_preflight_failed,
    outbox_preflight_failed,
    ack_import_failed,
    outbox_import_failed,
};

struct CriticalAlertRecoveryResult {
    CriticalAlertRecoveryError error{
        CriticalAlertRecoveryError::checkpoint_rejected};
    CriticalAlertRecoveryCheckpointError checkpoint_error{
        CriticalAlertRecoveryCheckpointError::none};
    CriticalAlertAckIngressError ack_error{
        CriticalAlertAckIngressError::none};
    CriticalOutboxError outbox_error{CriticalOutboxError::none};
    std::uint64_t generation{0};

    [[nodiscard]] constexpr bool completed() const {
        return error == CriticalAlertRecoveryError::none;
    }
};

// Caller must provide exclusive ownership of ingress and outbox for the full
// operation. Output and live import state change only after all preflights pass.
[[nodiscard]] CriticalAlertRecoveryResult
export_critical_alert_recovery_checkpoint(
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    std::uint64_t generation,
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes>& output);

[[nodiscard]] CriticalAlertRecoveryResult
import_critical_alert_recovery_checkpoint(
    const std::uint8_t* checkpoint,
    std::size_t checkpoint_size,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms);

}  // namespace opengauge::integration
