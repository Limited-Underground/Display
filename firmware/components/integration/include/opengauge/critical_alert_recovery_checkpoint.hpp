#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_ack_ingress.hpp"
#include "opengauge/critical_alert_outbox_checkpoint.hpp"

namespace opengauge::integration {

inline constexpr std::uint8_t kCriticalAlertRecoveryCheckpointVersion = 0;
inline constexpr std::size_t kCriticalAlertRecoveryCheckpointBytes = 960;

struct CriticalAlertRecoveryCheckpoint {
    std::uint64_t generation{0};
    std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes> ack{};
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> outbox{};
};

enum class CriticalAlertRecoveryCheckpointError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_generation,
    invalid_ack_checkpoint,
    invalid_outbox_checkpoint,
    malformed,
    unsupported_version,
    integrity_failure,
};

[[nodiscard]] std::uint32_t critical_alert_recovery_checkpoint_crc32(
    const std::uint8_t* data,
    std::size_t size);
[[nodiscard]] CriticalAlertRecoveryCheckpointError
encode_critical_alert_recovery_checkpoint(
    const CriticalAlertRecoveryCheckpoint& checkpoint,
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes>& output);
[[nodiscard]] CriticalAlertRecoveryCheckpointError
decode_critical_alert_recovery_checkpoint(
    const std::uint8_t* encoded,
    std::size_t size,
    CriticalAlertRecoveryCheckpoint& output);

}  // namespace opengauge::integration
