#include "opengauge/critical_alert_outbox_checkpoint.hpp"

#include <algorithm>
#include <array>

namespace opengauge::integration {
namespace {

constexpr std::size_t kHeaderBytes = 16;
constexpr std::size_t kEntryBytes = 76;
constexpr std::size_t kCrcOffset = kCriticalAlertOutboxCheckpointBytes - 4;

void write_u32(std::uint8_t* output, std::uint32_t value) {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
    output[2] = static_cast<std::uint8_t>(value >> 16U);
    output[3] = static_cast<std::uint8_t>(value >> 24U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    return static_cast<std::uint32_t>(input[0]) |
           (static_cast<std::uint32_t>(input[1]) << 8U) |
           (static_cast<std::uint32_t>(input[2]) << 16U) |
           (static_cast<std::uint32_t>(input[3]) << 24U);
}

bool all_zero(const std::uint8_t* input, std::size_t size) {
    return std::all_of(input, input + size, [](std::uint8_t value) {
        return value == 0;
    });
}

CriticalAlertOutboxCheckpointError validate_entry(
    const CriticalAlertOutboxCheckpointEntry& entry,
    std::uint64_t& event_id) {
    if (entry.state != CriticalAlertOutboxCheckpointState::queued &&
        entry.state != CriticalAlertOutboxCheckpointState::in_flight) {
        return CriticalAlertOutboxCheckpointError::invalid_entry;
    }
    if (entry.remaining_lifetime_ms == 0 ||
        entry.remaining_action_ms > entry.remaining_lifetime_ms ||
        (entry.state == CriticalAlertOutboxCheckpointState::in_flight &&
         entry.attempts == 0)) {
        return CriticalAlertOutboxCheckpointError::invalid_entry;
    }
    const auto decoded = decode_critical_alert(
        entry.frame.data(), entry.frame.size());
    if (!decoded.decoded()) {
        return CriticalAlertOutboxCheckpointError::invalid_entry;
    }
    event_id = decoded.alert.event_id;
    return CriticalAlertOutboxCheckpointError::none;
}

}  // namespace

std::uint32_t critical_alert_outbox_checkpoint_crc32(
    const std::uint8_t* data,
    std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1U) ^
                  (0xEDB88320U &
                   static_cast<std::uint32_t>(-
                       static_cast<std::int32_t>(crc & 1U)));
        }
    }
    return ~crc;
}

CriticalAlertOutboxCheckpointError encode_critical_alert_outbox_checkpoint(
    const CriticalAlertOutboxCheckpoint& checkpoint,
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes>& output) {
    if (checkpoint.configuration_fingerprint == 0) {
        return CriticalAlertOutboxCheckpointError::invalid_configuration;
    }
    std::array<std::uint64_t, kCriticalAlertOutboxCheckpointCapacity> ids{};
    std::size_t active_count = 0;
    for (const auto& entry : checkpoint.entries) {
        if (!entry.active) {
            continue;
        }
        std::uint64_t event_id = 0;
        const auto validation = validate_entry(entry, event_id);
        if (validation != CriticalAlertOutboxCheckpointError::none) {
            return validation;
        }
        for (std::size_t index = 0; index < active_count; ++index) {
            if (ids[index] == event_id) {
                return CriticalAlertOutboxCheckpointError::duplicate_event;
            }
        }
        ids[active_count++] = event_id;
    }

    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> candidate{};
    candidate[0] = 'O';
    candidate[1] = 'O';
    candidate[2] = 'C';
    candidate[3] = '0';
    candidate[4] = kCriticalAlertOutboxCheckpointVersion;
    candidate[5] = static_cast<std::uint8_t>(active_count);
    write_u32(candidate.data() + 8, checkpoint.configuration_fingerprint);
    for (std::size_t index = 0; index < checkpoint.entries.size(); ++index) {
        const auto& entry = checkpoint.entries[index];
        if (!entry.active) {
            continue;
        }
        auto* encoded = candidate.data() + kHeaderBytes + index * kEntryBytes;
        encoded[0] = 1;
        encoded[1] = static_cast<std::uint8_t>(entry.state);
        encoded[2] = entry.attempts;
        write_u32(encoded + 4, entry.remaining_lifetime_ms);
        write_u32(encoded + 8, entry.remaining_action_ms);
        std::copy(entry.frame.begin(), entry.frame.end(), encoded + 12);
    }
    write_u32(
        candidate.data() + kCrcOffset,
        critical_alert_outbox_checkpoint_crc32(candidate.data(), kCrcOffset));
    output = candidate;
    return CriticalAlertOutboxCheckpointError::none;
}

CriticalAlertOutboxCheckpointError decode_critical_alert_outbox_checkpoint(
    const std::uint8_t* encoded,
    std::size_t size,
    CriticalAlertOutboxCheckpoint& output) {
    if (encoded == nullptr || size != kCriticalAlertOutboxCheckpointBytes) {
        return CriticalAlertOutboxCheckpointError::invalid_argument;
    }
    if (encoded[0] != 'O' || encoded[1] != 'O' || encoded[2] != 'C' ||
        encoded[3] != '0') {
        return CriticalAlertOutboxCheckpointError::malformed;
    }
    if (encoded[4] != kCriticalAlertOutboxCheckpointVersion) {
        return CriticalAlertOutboxCheckpointError::unsupported_version;
    }
    if (encoded[5] > kCriticalAlertOutboxCheckpointCapacity ||
        encoded[6] != 0 || encoded[7] != 0 ||
        !all_zero(encoded + 12, 4) ||
        !all_zero(encoded + kHeaderBytes +
                      kEntryBytes * kCriticalAlertOutboxCheckpointCapacity,
                  kCrcOffset - (kHeaderBytes +
                      kEntryBytes * kCriticalAlertOutboxCheckpointCapacity))) {
        return CriticalAlertOutboxCheckpointError::malformed;
    }
    if (read_u32(encoded + 8) == 0) {
        return CriticalAlertOutboxCheckpointError::invalid_configuration;
    }
    if (read_u32(encoded + kCrcOffset) !=
        critical_alert_outbox_checkpoint_crc32(encoded, kCrcOffset)) {
        return CriticalAlertOutboxCheckpointError::integrity_failure;
    }

    CriticalAlertOutboxCheckpoint candidate{};
    candidate.configuration_fingerprint = read_u32(encoded + 8);
    std::array<std::uint64_t, kCriticalAlertOutboxCheckpointCapacity> ids{};
    std::size_t active_count = 0;
    for (std::size_t index = 0; index < candidate.entries.size(); ++index) {
        const auto* entry = encoded + kHeaderBytes + index * kEntryBytes;
        if (entry[0] == 0) {
            if (!all_zero(entry, kEntryBytes)) {
                return CriticalAlertOutboxCheckpointError::malformed;
            }
            continue;
        }
        if (entry[0] != 1 || entry[3] != 0) {
            return CriticalAlertOutboxCheckpointError::malformed;
        }
        auto& decoded_entry = candidate.entries[index];
        decoded_entry.active = true;
        decoded_entry.state =
            static_cast<CriticalAlertOutboxCheckpointState>(entry[1]);
        decoded_entry.attempts = entry[2];
        decoded_entry.remaining_lifetime_ms = read_u32(entry + 4);
        decoded_entry.remaining_action_ms = read_u32(entry + 8);
        std::copy(entry + 12, entry + kEntryBytes, decoded_entry.frame.begin());
        std::uint64_t event_id = 0;
        const auto validation = validate_entry(decoded_entry, event_id);
        if (validation != CriticalAlertOutboxCheckpointError::none) {
            return validation;
        }
        for (std::size_t prior = 0; prior < active_count; ++prior) {
            if (ids[prior] == event_id) {
                return CriticalAlertOutboxCheckpointError::duplicate_event;
            }
        }
        ids[active_count++] = event_id;
    }
    if (active_count != encoded[5]) {
        return CriticalAlertOutboxCheckpointError::malformed;
    }
    output = candidate;
    return CriticalAlertOutboxCheckpointError::none;
}

}  // namespace opengauge::integration
