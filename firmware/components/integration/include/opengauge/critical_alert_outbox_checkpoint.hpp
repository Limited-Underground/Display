#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert.hpp"

namespace opengauge::integration {

inline constexpr std::uint8_t kCriticalAlertOutboxCheckpointVersion = 0;
inline constexpr std::size_t kCriticalAlertOutboxCheckpointCapacity = 8;
inline constexpr std::size_t kCriticalAlertOutboxCheckpointBytes = 640;

enum class CriticalAlertOutboxCheckpointState : std::uint8_t {
    queued = 1,
    in_flight = 2,
};

struct CriticalAlertOutboxCheckpointEntry {
    bool active{false};
    CriticalAlertOutboxCheckpointState state{
        CriticalAlertOutboxCheckpointState::queued};
    std::uint8_t attempts{0};
    std::uint32_t remaining_lifetime_ms{0};
    std::uint32_t remaining_action_ms{0};
    std::array<std::uint8_t, kCriticalAlertFrameBytes> frame{};
};

struct CriticalAlertOutboxCheckpoint {
    std::uint32_t configuration_fingerprint{0};
    std::array<CriticalAlertOutboxCheckpointEntry,
               kCriticalAlertOutboxCheckpointCapacity> entries{};
};

enum class CriticalAlertOutboxCheckpointError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_configuration,
    invalid_entry,
    duplicate_event,
    malformed,
    unsupported_version,
    integrity_failure,
};

[[nodiscard]] std::uint32_t critical_alert_outbox_checkpoint_crc32(
    const std::uint8_t* data,
    std::size_t size);
[[nodiscard]] CriticalAlertOutboxCheckpointError
encode_critical_alert_outbox_checkpoint(
    const CriticalAlertOutboxCheckpoint& checkpoint,
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes>& output);
[[nodiscard]] CriticalAlertOutboxCheckpointError
decode_critical_alert_outbox_checkpoint(
    const std::uint8_t* encoded,
    std::size_t size,
    CriticalAlertOutboxCheckpoint& output);

}  // namespace opengauge::integration
