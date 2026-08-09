#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_recovery_checkpoint.hpp"
#include "opengauge/peer_authorization.hpp"

namespace opengauge::integration {

inline constexpr std::uint8_t kCriticalAlertSystemRecoveryCheckpointVersion = 0;
inline constexpr std::size_t kCriticalAlertSystemRecoveryCheckpointBytes = 1280;

struct CriticalAlertSystemRecoveryCheckpoint {
    std::uint64_t generation{0};
    std::array<std::uint8_t, identity::kPeerAuthorizationCheckpointBytes>
        authorization{};
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> critical{};
};

enum class CriticalAlertSystemRecoveryCheckpointError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_generation,
    invalid_authorization_checkpoint,
    invalid_critical_checkpoint,
    malformed,
    unsupported_version,
    integrity_failure,
};

[[nodiscard]] std::uint32_t critical_alert_system_recovery_checkpoint_crc32(
    const std::uint8_t* data, std::size_t size);
[[nodiscard]] CriticalAlertSystemRecoveryCheckpointError
encode_critical_alert_system_recovery_checkpoint(
    const CriticalAlertSystemRecoveryCheckpoint& checkpoint,
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>& output);
[[nodiscard]] CriticalAlertSystemRecoveryCheckpointError
decode_critical_alert_system_recovery_checkpoint(
    const std::uint8_t* encoded,
    std::size_t size,
    CriticalAlertSystemRecoveryCheckpoint& output);

}  // namespace opengauge::integration
