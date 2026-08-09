#include "opengauge/critical_alert_ack_ingress.hpp"

#include <algorithm>
#include <limits>

namespace opengauge::integration {
namespace {

constexpr std::size_t kNotFound = std::numeric_limits<std::size_t>::max();
constexpr std::uint32_t kMaximumCodecObservedAgeMs = 86400000U;
constexpr std::uint32_t kSerialHalfRange = 0x80000000U;
constexpr std::array<std::uint8_t, 4> kCheckpointMagic{{'O', 'A', 'I', '0'}};
constexpr std::size_t kCheckpointEntriesOffset = 20;
constexpr std::size_t kCheckpointEntryBytes = 32;
constexpr std::size_t kCheckpointCrcOffset = 276;

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint32_t checkpoint_crc32(const std::uint8_t* data, std::size_t size) {
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

}  // namespace

CriticalAlertAckIngressError CriticalAlertAckIngress::start(
    const CriticalAlertAckIngressConfiguration& configuration,
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertOutbox& outbox) {
    if (status_.running) {
        return CriticalAlertAckIngressError::invalid_state;
    }
    if (configuration.local_producer_id == 0 ||
        configuration.maximum_observed_alert_age_ms == 0 ||
        configuration.maximum_observed_alert_age_ms >
            kMaximumCodecObservedAgeMs ||
        !authorization.status().running || !outbox.status().running) {
        return CriticalAlertAckIngressError::invalid_configuration;
    }
    configuration_ = configuration;
    authorization_ = &authorization;
    outbox_ = &outbox;
    bindings_ = {};
    has_clock_ = false;
    last_monotonic_ms_ = 0;
    status_ = {};
    status_.running = true;
    return CriticalAlertAckIngressError::none;
}

void CriticalAlertAckIngress::stop() {
    status_.running = false;
    authorization_ = nullptr;
    outbox_ = nullptr;
    bindings_ = {};
    status_.binding_count = 0;
}

CriticalAlertAckIngressError CriticalAlertAckIngress::bind_consumer_session(
    std::uint32_t logical_peer_id,
    std::uint64_t consumer_id,
    std::uint32_t consumer_boot_session_id) {
    if (!status_.running) {
        return CriticalAlertAckIngressError::invalid_state;
    }
    if (logical_peer_id == 0 || consumer_id == 0 ||
        consumer_boot_session_id == 0) {
        return CriticalAlertAckIngressError::invalid_binding;
    }
    std::uint32_t authorization_epoch = 0;
    if (!current_authorization_epoch(
            logical_peer_id, authorization_epoch)) {
        return CriticalAlertAckIngressError::authorization_denied;
    }
    for (const auto& binding : bindings_) {
        if (binding.active && binding.logical_peer_id != logical_peer_id &&
            binding.consumer_id == consumer_id) {
            return CriticalAlertAckIngressError::duplicate_consumer;
        }
    }
    auto index = find_binding(logical_peer_id);
    if (index == kNotFound) {
        for (std::size_t candidate = 0; candidate < bindings_.size();
             ++candidate) {
            if (!bindings_[candidate].active) {
                index = candidate;
                break;
            }
        }
        if (index == kNotFound) {
            return CriticalAlertAckIngressError::capacity_full;
        }
        ++status_.binding_count;
    }
    bindings_[index] = {
        true,
        logical_peer_id,
        consumer_id,
        consumer_boot_session_id,
        authorization_epoch,
        false,
        0,
        0};
    return CriticalAlertAckIngressError::none;
}

CriticalAlertAckIngressError CriticalAlertAckIngress::unbind_consumer(
    std::uint32_t logical_peer_id) {
    if (!status_.running) {
        return CriticalAlertAckIngressError::invalid_state;
    }
    const auto index = find_binding(logical_peer_id);
    if (index == kNotFound) {
        return CriticalAlertAckIngressError::unknown_consumer;
    }
    bindings_[index] = {};
    --status_.binding_count;
    return CriticalAlertAckIngressError::none;
}

CriticalAlertAckIngressError CriticalAlertAckIngress::export_checkpoint(
    std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes>& output) {
    if (!status_.running || authorization_ == nullptr) {
        return CriticalAlertAckIngressError::invalid_state;
    }
    std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes> candidate{};
    std::copy(kCheckpointMagic.begin(), kCheckpointMagic.end(),
              candidate.begin());
    candidate[4] = kCriticalAlertAckCheckpointVersion;
    write_u64(candidate.data() + 8, configuration_.local_producer_id);
    candidate[16] = static_cast<std::uint8_t>(status_.binding_count);
    for (std::size_t index = 0; index < bindings_.size(); ++index) {
        const auto& binding = bindings_[index];
        if (!binding.active) {
            continue;
        }
        std::uint32_t current_epoch = 0;
        if (!current_authorization_epoch(
                binding.logical_peer_id, current_epoch) ||
            current_epoch != binding.authorization_epoch) {
            saturating_increment(status_.checkpoint_rejections);
            return CriticalAlertAckIngressError::
                checkpoint_authorization_mismatch;
        }
        auto* entry = candidate.data() + kCheckpointEntriesOffset +
                      index * kCheckpointEntryBytes;
        entry[0] = 1;
        entry[1] = binding.has_sequence ? 1 : 0;
        write_u32(entry + 4, binding.logical_peer_id);
        write_u64(entry + 8, binding.consumer_id);
        write_u32(entry + 16, binding.consumer_boot_session_id);
        write_u32(entry + 20, binding.authorization_epoch);
        write_u32(entry + 24, binding.highest_sequence);
        write_u32(entry + 28, binding.replay_bitmap);
    }
    write_u32(candidate.data() + kCheckpointCrcOffset,
              checkpoint_crc32(candidate.data(), kCheckpointCrcOffset));
    output = candidate;
    saturating_increment(status_.checkpoint_exports);
    return CriticalAlertAckIngressError::none;
}

CriticalAlertAckIngressError CriticalAlertAckIngress::import_checkpoint(
    const std::uint8_t* checkpoint,
    std::size_t checkpoint_size) {
    const auto reject = [this](CriticalAlertAckIngressError error) {
        saturating_increment(status_.checkpoint_rejections);
        return error;
    };
    if (!status_.running || authorization_ == nullptr) {
        return CriticalAlertAckIngressError::invalid_state;
    }
    if (has_clock_ || status_.processed != 0) {
        return reject(CriticalAlertAckIngressError::invalid_state);
    }
    if (checkpoint == nullptr ||
        checkpoint_size != kCriticalAlertAckCheckpointBytes ||
        !std::equal(kCheckpointMagic.begin(), kCheckpointMagic.end(),
                    checkpoint)) {
        return reject(CriticalAlertAckIngressError::checkpoint_malformed);
    }
    if (checkpoint[4] != kCriticalAlertAckCheckpointVersion ||
        read_u64(checkpoint + 8) != configuration_.local_producer_id) {
        return reject(CriticalAlertAckIngressError::checkpoint_incompatible);
    }
    if (checkpoint[5] != 0 || checkpoint[6] != 0 || checkpoint[7] != 0 ||
        checkpoint[17] != 0 || checkpoint[18] != 0 ||
        checkpoint[19] != 0) {
        return reject(CriticalAlertAckIngressError::checkpoint_malformed);
    }
    if (read_u32(checkpoint + kCheckpointCrcOffset) !=
        checkpoint_crc32(checkpoint, kCheckpointCrcOffset)) {
        return reject(
            CriticalAlertAckIngressError::checkpoint_integrity_failure);
    }

    std::array<ConsumerBinding, kCriticalAlertAckConsumerCapacity> candidate{};
    std::size_t active_count = 0;
    for (std::size_t index = 0; index < candidate.size(); ++index) {
        const auto* entry = checkpoint + kCheckpointEntriesOffset +
                            index * kCheckpointEntryBytes;
        if (entry[0] == 0) {
            if (!std::all_of(
                    entry,
                    entry + kCheckpointEntryBytes,
                    [](std::uint8_t byte) { return byte == 0; })) {
                return reject(
                    CriticalAlertAckIngressError::checkpoint_malformed);
            }
            continue;
        }
        if (entry[0] != 1 || entry[1] > 1 || entry[2] != 0 ||
            entry[3] != 0) {
            return reject(CriticalAlertAckIngressError::checkpoint_malformed);
        }
        ConsumerBinding binding{};
        binding.active = true;
        binding.has_sequence = entry[1] == 1;
        binding.logical_peer_id = read_u32(entry + 4);
        binding.consumer_id = read_u64(entry + 8);
        binding.consumer_boot_session_id = read_u32(entry + 16);
        binding.authorization_epoch = read_u32(entry + 20);
        binding.highest_sequence = read_u32(entry + 24);
        binding.replay_bitmap = read_u32(entry + 28);
        if (binding.logical_peer_id == 0 || binding.consumer_id == 0 ||
            binding.consumer_boot_session_id == 0 ||
            binding.authorization_epoch == 0 ||
            (!binding.has_sequence &&
             (binding.highest_sequence != 0 ||
              binding.replay_bitmap != 0)) ||
            (binding.has_sequence &&
             (binding.replay_bitmap & 1U) == 0)) {
            return reject(CriticalAlertAckIngressError::checkpoint_malformed);
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (candidate[prior].active &&
                (candidate[prior].logical_peer_id ==
                     binding.logical_peer_id ||
                 candidate[prior].consumer_id == binding.consumer_id)) {
                return reject(
                    CriticalAlertAckIngressError::checkpoint_malformed);
            }
        }
        std::uint32_t current_epoch = 0;
        if (!current_authorization_epoch(
                binding.logical_peer_id, current_epoch) ||
            current_epoch != binding.authorization_epoch) {
            return reject(CriticalAlertAckIngressError::
                          checkpoint_authorization_mismatch);
        }
        candidate[index] = binding;
        ++active_count;
    }
    if (active_count != checkpoint[16]) {
        return reject(CriticalAlertAckIngressError::checkpoint_malformed);
    }
    bindings_ = candidate;
    status_.binding_count = active_count;
    saturating_increment(status_.checkpoint_imports);
    return CriticalAlertAckIngressError::none;
}

CriticalAlertAckIngressResult CriticalAlertAckIngress::receive(
    const std::uint8_t* frame,
    std::size_t frame_size,
    const CriticalAlertAckTransportContext& transport,
    std::uint64_t now_ms) {
    if (!status_.running || authorization_ == nullptr || outbox_ == nullptr) {
        return {CriticalAlertAckIngressError::invalid_state};
    }
    if (has_clock_ && now_ms < last_monotonic_ms_) {
        saturating_increment(status_.clock_regressions);
        return {CriticalAlertAckIngressError::clock_regression};
    }
    if (!transport.authenticated) {
        saturating_increment(status_.transport_denials);
        return {CriticalAlertAckIngressError::transport_not_authenticated};
    }
    const auto authorization = authorization_->authorize(
        transport.logical_peer_id,
        transport.secure_key_handle,
        transport.channel,
        identity::PeerPermission::publish_alarm_ack);
    if (!authorization.authorized) {
        saturating_increment(status_.transport_denials);
        CriticalAlertAckIngressResult result{
            CriticalAlertAckIngressError::authorization_denied};
        result.authorization_error = authorization.error;
        return result;
    }
    const auto binding_index = find_binding(transport.logical_peer_id);
    if (binding_index == kNotFound) {
        saturating_increment(status_.identity_rejections);
        return {CriticalAlertAckIngressError::unknown_consumer};
    }
    std::uint32_t current_epoch = 0;
    if (!current_authorization_epoch(
            transport.logical_peer_id, current_epoch) ||
        current_epoch != bindings_[binding_index].authorization_epoch) {
        saturating_increment(status_.identity_rejections);
        return {
            CriticalAlertAckIngressError::authorization_epoch_mismatch};
    }
    const auto decoded = decode_critical_alert_ack(frame, frame_size);
    if (!decoded.decoded()) {
        saturating_increment(status_.codec_rejections);
        CriticalAlertAckIngressResult result{
            CriticalAlertAckIngressError::codec_rejected};
        result.codec_error = decoded.error;
        return result;
    }
    auto& binding = bindings_[binding_index];
    if (decoded.acknowledgement.consumer_id != binding.consumer_id) {
        saturating_increment(status_.identity_rejections);
        return {CriticalAlertAckIngressError::consumer_mismatch};
    }
    if (decoded.acknowledgement.producer_id !=
        configuration_.local_producer_id) {
        saturating_increment(status_.identity_rejections);
        return {CriticalAlertAckIngressError::producer_mismatch};
    }
    if (decoded.acknowledgement.consumer_boot_session_id !=
        binding.consumer_boot_session_id) {
        saturating_increment(status_.identity_rejections);
        return {CriticalAlertAckIngressError::session_mismatch};
    }
    if (decoded.acknowledgement.observed_alert_age_ms >
        configuration_.maximum_observed_alert_age_ms) {
        saturating_increment(status_.identity_rejections);
        return {CriticalAlertAckIngressError::observed_age_exceeded};
    }
    const auto replay = preview_sequence(
        binding, decoded.acknowledgement.ack_sequence);
    if (replay.error != CriticalAlertAckIngressError::none) {
        saturating_increment(status_.replay_rejections);
        return {replay.error};
    }

    const CriticalAlertAcknowledgement correlation{
        decoded.acknowledgement.event_id,
        decoded.acknowledgement.condition_id,
        decoded.acknowledgement.state};
    auto outbox_error = outbox_->validate_acknowledgement(correlation);
    if (outbox_error != CriticalOutboxError::none) {
        saturating_increment(status_.outbox_rejections);
        CriticalAlertAckIngressResult result{
            CriticalAlertAckIngressError::outbox_mismatch};
        result.outbox_error = outbox_error;
        return result;
    }

    bool completed = false;
    CriticalRemoteRejectionResult remote{};
    if (decoded.acknowledgement.disposition ==
        AlertAckDisposition::accepted) {
        outbox_error = outbox_->acknowledge(correlation, now_ms);
        if (outbox_error != CriticalOutboxError::none) {
            saturating_increment(status_.outbox_rejections);
            CriticalAlertAckIngressResult result{
                outbox_error == CriticalOutboxError::clock_regression
                    ? CriticalAlertAckIngressError::clock_regression
                    : CriticalAlertAckIngressError::outbox_mismatch};
            result.outbox_error = outbox_error;
            if (outbox_error == CriticalOutboxError::clock_regression) {
                saturating_increment(status_.clock_regressions);
            }
            return result;
        }
        completed = true;
        saturating_increment(status_.accepted);
    } else {
        remote = outbox_->apply_remote_rejection(
            correlation,
            decoded.acknowledgement.reason,
            now_ms);
        if (remote.error != CriticalOutboxError::none) {
            saturating_increment(status_.outbox_rejections);
            CriticalAlertAckIngressResult result{
                remote.error == CriticalOutboxError::clock_regression
                    ? CriticalAlertAckIngressError::clock_regression
                    : CriticalAlertAckIngressError::outbox_mismatch};
            result.outbox_error = remote.error;
            if (remote.error == CriticalOutboxError::clock_regression) {
                saturating_increment(status_.clock_regressions);
            }
            return result;
        }
        saturating_increment(status_.remote_rejections);
        if (remote.retry_released) {
            saturating_increment(status_.remote_retries);
        }
        if (remote.terminal_failure) {
            saturating_increment(status_.remote_terminal_failures);
        }
    }

    binding.has_sequence = replay.has_sequence;
    binding.highest_sequence = replay.highest_sequence;
    binding.replay_bitmap = replay.replay_bitmap;
    has_clock_ = true;
    last_monotonic_ms_ = now_ms;
    saturating_increment(status_.processed);

    CriticalAlertAckIngressResult result{CriticalAlertAckIngressError::none};
    result.disposition = decoded.acknowledgement.disposition;
    result.reason = decoded.acknowledgement.reason;
    result.outbox_completed = completed;
    if (decoded.acknowledgement.disposition ==
        AlertAckDisposition::rejected) {
        result.remote_rejection_action = remote.action;
        result.retry_released = remote.retry_released;
        result.terminal_failure = remote.terminal_failure;
        result.failure = remote.failure;
    }
    return result;
}

CriticalAlertAckIngressStatus CriticalAlertAckIngress::status() const {
    return status_;
}

std::size_t CriticalAlertAckIngress::find_binding(
    std::uint32_t logical_peer_id) const {
    for (std::size_t index = 0; index < bindings_.size(); ++index) {
        if (bindings_[index].active &&
            bindings_[index].logical_peer_id == logical_peer_id) {
            return index;
        }
    }
    return kNotFound;
}

bool CriticalAlertAckIngress::current_authorization_epoch(
    std::uint32_t logical_peer_id,
    std::uint32_t& authorization_epoch) const {
    if (authorization_ == nullptr) {
        return false;
    }
    std::array<identity::PeerAuthorizationEntry,
               identity::kMaximumAuthorizedPeers> entries{};
    std::size_t count = 0;
    if (authorization_->snapshot(
            entries.data(), entries.size(), count) !=
        identity::PeerAuthorizationError::none) {
        return false;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto& entry = entries[index];
        if (entry.logical_peer_id == logical_peer_id && entry.active &&
            (entry.permissions & identity::permission_bit(
                 identity::PeerPermission::publish_alarm_ack)) != 0) {
            authorization_epoch = entry.authorization_epoch;
            return authorization_epoch != 0;
        }
    }
    return false;
}

CriticalAlertAckIngress::ReplayCandidate
CriticalAlertAckIngress::preview_sequence(
    const ConsumerBinding& binding,
    std::uint32_t sequence) const {
    if (!binding.has_sequence) {
        return {
            CriticalAlertAckIngressError::none,
            true,
            sequence,
            1};
    }
    const auto forward = sequence - binding.highest_sequence;
    if (forward == 0) {
        return {CriticalAlertAckIngressError::replay_duplicate};
    }
    if (forward == kSerialHalfRange) {
        return {CriticalAlertAckIngressError::replay_ambiguous};
    }
    if (forward < kSerialHalfRange) {
        const auto bitmap = forward >= kCriticalAlertAckReplayWindow
                                ? 1U
                                : static_cast<std::uint32_t>(
                                      (binding.replay_bitmap << forward) | 1U);
        return {
            CriticalAlertAckIngressError::none,
            true,
            sequence,
            bitmap};
    }
    const auto backward = binding.highest_sequence - sequence;
    if (backward >= kCriticalAlertAckReplayWindow) {
        return {CriticalAlertAckIngressError::replay_too_old};
    }
    const auto bit = static_cast<std::uint32_t>(1U << backward);
    if ((binding.replay_bitmap & bit) != 0) {
        return {CriticalAlertAckIngressError::replay_duplicate};
    }
    return {
        CriticalAlertAckIngressError::none,
        true,
        binding.highest_sequence,
        static_cast<std::uint32_t>(binding.replay_bitmap | bit)};
}

}  // namespace opengauge::integration
