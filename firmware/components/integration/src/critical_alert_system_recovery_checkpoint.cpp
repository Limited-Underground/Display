#include "opengauge/critical_alert_system_recovery_checkpoint.hpp"

#include <algorithm>

namespace opengauge::integration {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'R', 'S', '0'}};
constexpr std::size_t kHeaderBytes = 24;
constexpr std::size_t kAuthorizationOffset = kHeaderBytes;
constexpr std::size_t kCriticalOffset =
    kAuthorizationOffset + identity::kPeerAuthorizationCheckpointBytes;
constexpr std::size_t kTailOffset =
    kCriticalOffset + kCriticalAlertRecoveryCheckpointBytes;
constexpr std::size_t kCrcOffset =
    kCriticalAlertSystemRecoveryCheckpointBytes - 4;
constexpr std::size_t kAuthorizationCrcOffset =
    identity::kPeerAuthorizationCheckpointBytes - 4;

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    return value;
}

bool all_zero(const std::uint8_t* data, std::size_t size) {
    return std::all_of(data, data + size,
                       [](std::uint8_t value) { return value == 0; });
}

bool valid_authorization(
    const std::array<std::uint8_t,
                     identity::kPeerAuthorizationCheckpointBytes>& value) {
    return value[0] == 'O' && value[1] == 'P' && value[2] == 'A' &&
           value[3] == '0' &&
           value[4] == identity::kPeerAuthorizationCheckpointVersion &&
           read_u32(value.data() + kAuthorizationCrcOffset) ==
               identity::peer_authorization_checkpoint_crc32(
                   value.data(), kAuthorizationCrcOffset);
}

bool valid_critical(
    const std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes>& value,
    std::uint64_t generation) {
    CriticalAlertRecoveryCheckpoint decoded{};
    return decode_critical_alert_recovery_checkpoint(
               value.data(), value.size(), decoded) ==
               CriticalAlertRecoveryCheckpointError::none &&
           decoded.generation == generation;
}

}  // namespace

std::uint32_t critical_alert_system_recovery_checkpoint_crc32(
    const std::uint8_t* data, std::size_t size) {
    if (data == nullptr && size != 0) return 0;
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

CriticalAlertSystemRecoveryCheckpointError
encode_critical_alert_system_recovery_checkpoint(
    const CriticalAlertSystemRecoveryCheckpoint& checkpoint,
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>& output) {
    if (checkpoint.generation == 0)
        return CriticalAlertSystemRecoveryCheckpointError::invalid_generation;
    if (!valid_authorization(checkpoint.authorization))
        return CriticalAlertSystemRecoveryCheckpointError::
            invalid_authorization_checkpoint;
    if (!valid_critical(checkpoint.critical, checkpoint.generation))
        return CriticalAlertSystemRecoveryCheckpointError::
            invalid_critical_checkpoint;

    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    candidate[4] = kCriticalAlertSystemRecoveryCheckpointVersion;
    candidate[5] = static_cast<std::uint8_t>(kHeaderBytes);
    write_u16(candidate.data() + 6,
              static_cast<std::uint16_t>(checkpoint.authorization.size()));
    write_u16(candidate.data() + 8,
              static_cast<std::uint16_t>(checkpoint.critical.size()));
    write_u64(candidate.data() + 16, checkpoint.generation);
    std::copy(checkpoint.authorization.begin(), checkpoint.authorization.end(),
              candidate.begin() + kAuthorizationOffset);
    std::copy(checkpoint.critical.begin(), checkpoint.critical.end(),
              candidate.begin() + kCriticalOffset);
    write_u32(candidate.data() + kCrcOffset,
              critical_alert_system_recovery_checkpoint_crc32(
                  candidate.data(), kCrcOffset));
    output = candidate;
    return CriticalAlertSystemRecoveryCheckpointError::none;
}

CriticalAlertSystemRecoveryCheckpointError
decode_critical_alert_system_recovery_checkpoint(
    const std::uint8_t* encoded,
    std::size_t size,
    CriticalAlertSystemRecoveryCheckpoint& output) {
    if (encoded == nullptr || size != kCriticalAlertSystemRecoveryCheckpointBytes)
        return CriticalAlertSystemRecoveryCheckpointError::invalid_argument;
    if (!std::equal(kMagic.begin(), kMagic.end(), encoded))
        return CriticalAlertSystemRecoveryCheckpointError::malformed;
    if (encoded[4] != kCriticalAlertSystemRecoveryCheckpointVersion)
        return CriticalAlertSystemRecoveryCheckpointError::unsupported_version;
    if (encoded[5] != kHeaderBytes ||
        read_u16(encoded + 6) != identity::kPeerAuthorizationCheckpointBytes ||
        read_u16(encoded + 8) != kCriticalAlertRecoveryCheckpointBytes ||
        !all_zero(encoded + 10, 6) || read_u64(encoded + 16) == 0 ||
        !all_zero(encoded + kTailOffset, kCrcOffset - kTailOffset)) {
        return CriticalAlertSystemRecoveryCheckpointError::malformed;
    }
    if (read_u32(encoded + kCrcOffset) !=
        critical_alert_system_recovery_checkpoint_crc32(encoded, kCrcOffset)) {
        return CriticalAlertSystemRecoveryCheckpointError::integrity_failure;
    }

    CriticalAlertSystemRecoveryCheckpoint candidate{};
    candidate.generation = read_u64(encoded + 16);
    std::copy(encoded + kAuthorizationOffset, encoded + kCriticalOffset,
              candidate.authorization.begin());
    std::copy(encoded + kCriticalOffset, encoded + kTailOffset,
              candidate.critical.begin());
    if (!valid_authorization(candidate.authorization))
        return CriticalAlertSystemRecoveryCheckpointError::
            invalid_authorization_checkpoint;
    if (!valid_critical(candidate.critical, candidate.generation))
        return CriticalAlertSystemRecoveryCheckpointError::
            invalid_critical_checkpoint;
    output = candidate;
    return CriticalAlertSystemRecoveryCheckpointError::none;
}

}  // namespace opengauge::integration
