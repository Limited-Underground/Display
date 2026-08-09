#include "opengauge/critical_alert_ack_ingress.hpp"

#include <limits>

namespace opengauge::integration {
namespace {

constexpr std::size_t kNotFound = std::numeric_limits<std::size_t>::max();
constexpr std::uint32_t kMaximumCodecObservedAgeMs = 86400000U;
constexpr std::uint32_t kSerialHalfRange = 0x80000000U;

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
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
        saturating_increment(status_.remote_rejections);
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
