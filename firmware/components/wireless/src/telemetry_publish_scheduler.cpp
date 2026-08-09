#include "opengauge/telemetry_publish_scheduler.hpp"

#include <algorithm>
#include <limits>

namespace opengauge::wireless {
namespace {

constexpr std::size_t kNotFound = std::numeric_limits<std::size_t>::max();

bool same_signal_semantics(
    const WireTelemetrySignal& left,
    const WireTelemetrySignal& right) {
    return left.code == right.code &&
           left.value.type == right.value.type &&
           left.value.raw_value == right.value.raw_value &&
           left.value.present == right.value.present &&
           left.unit == right.unit &&
           left.quality == right.quality;
}

std::uint64_t absolute_raw_difference(
    std::int64_t left,
    std::int64_t right) {
    constexpr auto sign = std::uint64_t{1} << 63U;
    const auto ordered_left = static_cast<std::uint64_t>(left) ^ sign;
    const auto ordered_right = static_cast<std::uint64_t>(right) ^ sign;
    return ordered_left >= ordered_right
               ? ordered_left - ordered_right
               : ordered_right - ordered_left;
}

enum class DuePriority : std::uint8_t {
    immediate = 0,
    changed = 1,
    refresh = 2,
    not_due = 3,
};

template <typename SubscriptionState>
DuePriority due_priority(
    const WireTelemetrySignal& current,
    const SubscriptionState& subscription,
    std::uint64_t now_ms,
    PublishSchedulerError& error) {
    if (!subscription.has_published) {
        return DuePriority::immediate;
    }
    if (now_ms < subscription.last_published_at_ms) {
        error = PublishSchedulerError::clock_regressed;
        return DuePriority::not_due;
    }
    const auto elapsed_ms = now_ms - subscription.last_published_at_ms;
    const auto& previous = subscription.last_published;
    if (current.quality != previous.quality ||
        current.value.present != previous.value.present ||
        current.value.type != previous.value.type ||
        current.unit != previous.unit) {
        return DuePriority::immediate;
    }
    if (elapsed_ms >= subscription.policy.maximum_interval_ms) {
        return DuePriority::refresh;
    }
    if (current.value.present && previous.value.present &&
        current.value.raw_value != previous.value.raw_value &&
        absolute_raw_difference(
            current.value.raw_value,
            previous.value.raw_value) >= subscription.policy.deadband_raw &&
        elapsed_ms >= subscription.policy.minimum_interval_ms) {
        return DuePriority::changed;
    }
    return DuePriority::not_due;
}

template <typename SignalState>
PublishSchedulerError materialize_signal(
    const SignalState& state,
    const TelemetrySubscription& subscription,
    std::uint64_t now_ms,
    WireTelemetrySignal& output) {
    const auto freshness = evaluate_wire_signal_freshness(
        state.signal,
        state.observed_at_ms,
        now_ms,
        subscription.stale_after_ms);
    if (freshness.error == TelemetryFreshnessError::clock_regressed) {
        return PublishSchedulerError::clock_regressed;
    }
    if (!freshness.evaluated()) {
        return PublishSchedulerError::signal_rejected;
    }
    output = state.signal;
    output.quality = freshness.effective_quality;
    output.value = freshness.display_value;
    output.source_age_ms = freshness.age_ms >
                                   std::numeric_limits<std::uint32_t>::max()
                               ? std::numeric_limits<std::uint32_t>::max()
                               : static_cast<std::uint32_t>(freshness.age_ms);
    return PublishSchedulerError::none;
}

}  // namespace

PublishSchedulerError TelemetryPublishScheduler::start(
    GatewayPublishIdentity identity) {
    if (running_) {
        return PublishSchedulerError::invalid_state;
    }
    if (identity.gateway_id == 0 || identity.boot_session_id == 0) {
        return PublishSchedulerError::invalid_identity;
    }
    identity_ = identity;
    peers_ = {};
    signals_ = {};
    peer_count_ = 0;
    signal_count_ = 0;
    next_plan_token_ = 1;
    plans_prepared_ = 0;
    plans_transport_accepted_ = 0;
    plans_transport_rejected_ = 0;
    running_ = true;
    return PublishSchedulerError::none;
}

void TelemetryPublishScheduler::stop() {
    identity_ = {};
    peers_ = {};
    signals_ = {};
    peer_count_ = 0;
    signal_count_ = 0;
    next_plan_token_ = 1;
    plans_prepared_ = 0;
    plans_transport_accepted_ = 0;
    plans_transport_rejected_ = 0;
    running_ = false;
}

PublishSchedulerError TelemetryPublishScheduler::add_peer(
    const PeerAddress& address,
    const TelemetrySubscription* subscriptions,
    std::size_t subscription_count,
    PeerPublishPolicy publish_policy) {
    if (!running_) {
        return PublishSchedulerError::invalid_state;
    }
    if (!is_valid_unicast_address(address) || subscriptions == nullptr ||
        subscription_count == 0 ||
        subscription_count > kMaximumSubscriptionsPerGauge ||
        publish_policy.minimum_packet_interval_ms <
            kMinimumPeerPacketIntervalMs ||
        publish_policy.minimum_packet_interval_ms >
            kMaximumPeerPacketIntervalMs) {
        return PublishSchedulerError::invalid_argument;
    }
    if (find_peer(address) != kNotFound) {
        return PublishSchedulerError::peer_already_exists;
    }
    for (std::size_t index = 0; index < subscription_count; ++index) {
        const auto& policy = subscriptions[index];
        if (telemetry_signal_descriptor(policy.code) == nullptr ||
            policy.maximum_interval_ms == 0 ||
            policy.stale_after_ms == 0 ||
            policy.maximum_interval_ms < policy.minimum_interval_ms) {
            return PublishSchedulerError::invalid_subscription;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (subscriptions[earlier].code == policy.code) {
                return PublishSchedulerError::duplicate_subscription;
            }
        }
    }
    auto available = kNotFound;
    for (std::size_t index = 0; index < peers_.size(); ++index) {
        if (!peers_[index].occupied) {
            available = index;
            break;
        }
    }
    if (available == kNotFound) {
        return PublishSchedulerError::peer_capacity_full;
    }
    auto& peer = peers_[available];
    peer = {};
    peer.address = address;
    peer.publish_policy = publish_policy;
    peer.subscription_count = static_cast<std::uint8_t>(subscription_count);
    for (std::size_t index = 0; index < subscription_count; ++index) {
        peer.subscriptions[index].policy = subscriptions[index];
    }
    peer.occupied = true;
    ++peer_count_;
    return PublishSchedulerError::none;
}

PublishSchedulerError TelemetryPublishScheduler::remove_peer(
    const PeerAddress& address) {
    if (!running_) {
        return PublishSchedulerError::invalid_state;
    }
    const auto index = find_peer(address);
    if (index == kNotFound) {
        return PublishSchedulerError::peer_not_found;
    }
    peers_[index] = {};
    --peer_count_;
    return PublishSchedulerError::none;
}

SchedulerUpdateResult TelemetryPublishScheduler::update_signal(
    TelemetrySignalCode code,
    const telemetry::CachedSignalSnapshot& snapshot,
    std::uint64_t observed_at_ms) {
    if (!running_) {
        return {PublishSchedulerError::invalid_state,
                TelemetryPacketError::none,
                false,
                false};
    }
    WireTelemetrySignal candidate{};
    const auto packet_error = make_wire_telemetry_signal(
        code, snapshot, candidate);
    if (packet_error != TelemetryPacketError::none) {
        return {PublishSchedulerError::signal_rejected,
                packet_error,
                false,
                false};
    }
    auto index = find_signal(code);
    if (index != kNotFound) {
        auto& state = signals_[index];
        if (observed_at_ms < state.observed_at_ms) {
            return {PublishSchedulerError::clock_regressed,
                    TelemetryPacketError::none,
                    false,
                    false};
        }
        const auto changed = !same_signal_semantics(state.signal, candidate);
        state.signal = candidate;
        state.observed_at_ms = observed_at_ms;
        if (changed) {
            ++state.revision;
        }
        return {PublishSchedulerError::none,
                TelemetryPacketError::none,
                false,
                changed};
    }
    for (std::size_t candidate_index = 0;
         candidate_index < signals_.size();
         ++candidate_index) {
        if (!signals_[candidate_index].occupied) {
            index = candidate_index;
            break;
        }
    }
    if (index == kNotFound) {
        return {PublishSchedulerError::signal_capacity_full,
                TelemetryPacketError::none,
                false,
                false};
    }
    signals_[index].signal = candidate;
    signals_[index].observed_at_ms = observed_at_ms;
    signals_[index].revision = 1;
    signals_[index].occupied = true;
    ++signal_count_;
    return {PublishSchedulerError::none,
            TelemetryPacketError::none,
            true,
            true};
}

TelemetryPublishPlan TelemetryPublishScheduler::prepare(
    const PeerAddress& destination,
    std::uint64_t now_ms) {
    if (!running_) {
        return {PublishSchedulerError::invalid_state, 0, {}, {}};
    }
    if (now_ms < identity_.boot_started_at_ms) {
        return {PublishSchedulerError::clock_regressed, 0, {}, {}};
    }
    const auto peer_index = find_peer(destination);
    if (peer_index == kNotFound) {
        return {PublishSchedulerError::peer_not_found, 0, {}, {}};
    }
    auto& peer = peers_[peer_index];
    if (peer.pending.active) {
        return {PublishSchedulerError::plan_pending, 0, {}, {}};
    }
    if (peer.has_accepted_packet) {
        if (now_ms < peer.last_packet_accepted_at_ms) {
            return {PublishSchedulerError::clock_regressed, 0, {}, {}};
        }
        if (now_ms - peer.last_packet_accepted_at_ms <
            peer.publish_policy.minimum_packet_interval_ms) {
            return {PublishSchedulerError::no_data, 0, destination, {}};
        }
    }

    PendingPlan pending{};
    pending.batch.gateway_id = identity_.gateway_id;
    pending.batch.boot_session_id = identity_.boot_session_id;
    pending.batch.sequence = peer.next_sequence;
    pending.batch.gateway_uptime_ms = now_ms - identity_.boot_started_at_ms;
    pending.prepared_at_ms = now_ms;

    std::array<bool, kMaximumSubscriptionsPerGauge> selected{};
    for (std::uint8_t requested_priority = 0;
         requested_priority <= static_cast<std::uint8_t>(DuePriority::refresh) &&
         pending.batch.signal_count < kTelemetrySignalsPerPacket;
         ++requested_priority) {
        for (std::size_t subscription_index = 0;
             subscription_index < peer.subscription_count &&
             pending.batch.signal_count < kTelemetrySignalsPerPacket;
             ++subscription_index) {
            if (selected[subscription_index]) {
                continue;
            }
            auto& subscription = peer.subscriptions[subscription_index];
            const auto signal_index = find_signal(subscription.policy.code);
            if (signal_index == kNotFound) {
                continue;
            }
            WireTelemetrySignal current{};
            const auto materialize_error = materialize_signal(
                signals_[signal_index], subscription.policy, now_ms, current);
            if (materialize_error != PublishSchedulerError::none) {
                return {materialize_error, 0, {}, {}};
            }
            auto due_error = PublishSchedulerError::none;
            const auto priority = due_priority(
                current, subscription, now_ms, due_error);
            if (due_error != PublishSchedulerError::none) {
                return {due_error, 0, {}, {}};
            }
            if (static_cast<std::uint8_t>(priority) != requested_priority) {
                continue;
            }
            const auto packet_index = pending.batch.signal_count;
            pending.batch.signals[packet_index] = current;
            pending.subscription_indices[packet_index] =
                static_cast<std::uint8_t>(subscription_index);
            pending.revisions[packet_index] = signals_[signal_index].revision;
            ++pending.batch.signal_count;
            selected[subscription_index] = true;
        }
    }
    if (pending.batch.signal_count == 0) {
        return {PublishSchedulerError::no_data, 0, destination, {}};
    }
    if (validate_telemetry_batch(pending.batch) !=
        TelemetryPacketError::none) {
        return {PublishSchedulerError::signal_rejected, 0, destination, {}};
    }
    auto token = next_plan_token_++;
    if (token == 0) {
        token = next_plan_token_++;
    }
    pending.token = token;
    pending.active = true;
    peer.pending = pending;
    ++plans_prepared_;
    return {PublishSchedulerError::none, token, destination, pending.batch};
}

PublishCommitResult TelemetryPublishScheduler::commit(
    const PeerAddress& destination,
    std::uint32_t plan_token,
    bool transport_accepted,
    std::uint64_t now_ms) {
    if (!running_) {
        return {PublishSchedulerError::invalid_state, false, 0};
    }
    const auto peer_index = find_peer(destination);
    if (peer_index == kNotFound) {
        return {PublishSchedulerError::peer_not_found, false, 0};
    }
    auto& peer = peers_[peer_index];
    if (!peer.pending.active) {
        return {PublishSchedulerError::plan_not_found,
                false,
                peer.next_sequence};
    }
    if (peer.pending.token != plan_token || plan_token == 0) {
        return {PublishSchedulerError::plan_token_mismatch,
                false,
                peer.next_sequence};
    }
    if (now_ms < peer.pending.prepared_at_ms) {
        return {PublishSchedulerError::clock_regressed,
                false,
                peer.next_sequence};
    }
    if (transport_accepted) {
        for (std::size_t index = 0;
             index < peer.pending.batch.signal_count;
             ++index) {
            auto& subscription = peer.subscriptions[
                peer.pending.subscription_indices[index]];
            subscription.last_published = peer.pending.batch.signals[index];
            subscription.last_published_at_ms = now_ms;
            subscription.last_published_revision =
                peer.pending.revisions[index];
            subscription.has_published = true;
        }
        ++peer.next_sequence;
        peer.last_packet_accepted_at_ms = now_ms;
        peer.has_accepted_packet = true;
        ++plans_transport_accepted_;
    } else {
        ++plans_transport_rejected_;
    }
    peer.pending = {};
    return {PublishSchedulerError::none,
            transport_accepted,
            peer.next_sequence};
}

PublishSchedulerStatus TelemetryPublishScheduler::status() const {
    std::size_t pending_count = 0;
    for (const auto& peer : peers_) {
        pending_count += peer.occupied && peer.pending.active ? 1U : 0U;
    }
    return {
        running_,
        peer_count_,
        signal_count_,
        pending_count,
        plans_prepared_,
        plans_transport_accepted_,
        plans_transport_rejected_};
}

std::size_t TelemetryPublishScheduler::find_peer(
    const PeerAddress& address) const {
    for (std::size_t index = 0; index < peers_.size(); ++index) {
        if (peers_[index].occupied &&
            peer_address_equals(peers_[index].address, address)) {
            return index;
        }
    }
    return kNotFound;
}

std::size_t TelemetryPublishScheduler::find_signal(
    TelemetrySignalCode code) const {
    for (std::size_t index = 0; index < signals_.size(); ++index) {
        if (signals_[index].occupied && signals_[index].signal.code == code) {
            return index;
        }
    }
    return kNotFound;
}

}  // namespace opengauge::wireless
