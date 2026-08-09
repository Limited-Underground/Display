#include "opengauge/gateway_telemetry_loop.hpp"

#include <array>
#include <limits>

namespace opengauge::gateway {
namespace {

constexpr std::size_t kNotFound =
    std::numeric_limits<std::size_t>::max();

void record_cycle_error(
    GatewayLoopCycleResult& result,
    GatewayLoopError error) {
    if (result.error == GatewayLoopError::none) {
        result.error = error;
    }
}

can::J1939Message to_j1939_message(const can::CanFrame& frame) {
    return {
        frame.identifier,
        frame.format,
        frame.data,
        frame.data_length,
        frame.received_at_ms};
}

}  // namespace

GatewayTelemetryLoop::GatewayTelemetryLoop(
    can::CanReceiver& receiver,
    const can::J1939DecoderRegistry& decoders,
    telemetry::TelemetryCache& cache,
    wireless::TelemetryGatewayPublisher& publisher,
    wireless::EspNowTransport& transport)
    : receiver_(receiver),
      decoders_(decoders),
      cache_(cache),
      publisher_(publisher),
      transport_(transport) {}

GatewayLoopStartResult GatewayTelemetryLoop::start(
    GatewayLoopConfiguration configuration) {
    if (status_.running) {
        return {GatewayLoopError::invalid_state};
    }
    if (configuration.maximum_can_frames_per_cycle == 0 ||
        configuration.maximum_can_frames_per_cycle >
            kMaximumCanFramesPerGatewayCycle ||
        configuration.cache_stale_after_ms == 0 ||
        configuration.publish_identity.gateway_id == 0 ||
        configuration.publish_identity.boot_session_id == 0 ||
        decoders_.size() == 0) {
        return {GatewayLoopError::invalid_configuration};
    }

    const auto can_error =
        receiver_.start_listen_only(configuration.can_policy);
    if (can_error != can::CanReceiverError::none) {
        return {
            GatewayLoopError::can_start_failure,
            can_error};
    }
    const auto publisher_error =
        publisher_.start(configuration.publish_identity);
    if (publisher_error != wireless::PublishSchedulerError::none) {
        receiver_.stop();
        return {
            GatewayLoopError::publisher_start_failure,
            can::CanReceiverError::none,
            publisher_error};
    }

    cache_.clear();
    configuration_ = configuration;
    peers_ = {};
    status_ = {};
    status_.running = true;
    return {GatewayLoopError::none};
}

void GatewayTelemetryLoop::stop() {
    if (status_.running) {
        receiver_.stop();
        publisher_.stop();
    }
    configuration_ = {};
    peers_ = {};
    status_.running = false;
    status_.peer_count = 0;
}

wireless::PublishSchedulerError GatewayTelemetryLoop::add_peer(
    const wireless::PeerAddress& address,
    const wireless::TelemetrySubscription* subscriptions,
    std::size_t subscription_count,
    wireless::PeerPublishPolicy publish_policy) {
    if (!status_.running) {
        return wireless::PublishSchedulerError::invalid_state;
    }
    const auto error = publisher_.add_peer(
        address, subscriptions, subscription_count, publish_policy);
    if (error != wireless::PublishSchedulerError::none) {
        return error;
    }
    const auto index = find_empty_peer();
    if (index == kNotFound) {
        const auto ignored = publisher_.remove_peer(address);
        static_cast<void>(ignored);
        return wireless::PublishSchedulerError::peer_capacity_full;
    }
    peers_[index] = {address, true};
    ++status_.peer_count;
    return wireless::PublishSchedulerError::none;
}

wireless::PublishSchedulerError GatewayTelemetryLoop::remove_peer(
    const wireless::PeerAddress& address) {
    if (!status_.running) {
        return wireless::PublishSchedulerError::invalid_state;
    }
    const auto index = find_peer(address);
    if (index == kNotFound) {
        return wireless::PublishSchedulerError::peer_not_found;
    }
    const auto error = publisher_.remove_peer(address);
    if (error != wireless::PublishSchedulerError::none) {
        return error;
    }
    peers_[index] = {};
    --status_.peer_count;
    return wireless::PublishSchedulerError::none;
}

GatewayLoopCycleResult GatewayTelemetryLoop::service(
    std::uint64_t now_ms) {
    if (!status_.running) {
        GatewayLoopCycleResult result{};
        result.error = GatewayLoopError::invalid_state;
        return result;
    }

    GatewayLoopCycleResult result{};
    std::array<telemetry::NormalizedSignal,
               kMaximumDecodedSignalsPerCanFrame>
        decoded_signals{};
    for (std::size_t index = 0;
         index < configuration_.maximum_can_frames_per_cycle;
         ++index) {
        const auto received = receiver_.receive();
        if (received.error == can::CanReceiverError::no_frame) {
            break;
        }
        if (!received.has_frame()) {
            result.can_error = received.error;
            ++status_.can_receive_failures;
            record_cycle_error(
                result, GatewayLoopError::can_receive_failure);
            break;
        }
        ++result.can_frames_received;
        ++status_.can_frames_received;

        const auto decoded = decoders_.decode(
            to_j1939_message(received.frame),
            decoded_signals.data(),
            decoded_signals.size());
        if (decoded.error == can::J1939DecodeError::unsupported_pgn) {
            ++result.unsupported_can_frames;
            ++status_.unsupported_can_frames;
            continue;
        }
        if (!decoded.decoded()) {
            result.decode_error = decoded.error;
            ++result.decode_failures;
            ++status_.decode_failures;
            record_cycle_error(result, GatewayLoopError::decode_failure);
            continue;
        }

        ++result.can_frames_decoded;
        ++status_.can_frames_decoded;
        result.signals_decoded += decoded.signal_count;
        status_.signals_decoded +=
            static_cast<std::uint32_t>(decoded.signal_count);
        for (std::size_t signal_index = 0;
             signal_index < decoded.signal_count;
             ++signal_index) {
            const auto written = cache_.upsert(
                decoded_signals[signal_index],
                configuration_.cache_stale_after_ms);
            if (written.accepted()) {
                ++result.cache_writes_accepted;
                ++status_.cache_writes_accepted;
            } else {
                result.cache_error = written.error;
                ++result.cache_writes_rejected;
                ++status_.cache_writes_rejected;
                record_cycle_error(
                    result, GatewayLoopError::cache_write_failure);
            }
        }
    }

    const auto receiver_status = receiver_.status();
    result.bus_state = receiver_status.bus_state;
    result.receiver_overflow_count =
        receiver_status.frames_dropped_overflow;
    result.can_frame_budget_exhausted =
        result.can_frames_received ==
            configuration_.maximum_can_frames_per_cycle &&
        receiver_status.queue_depth != 0;
    status_.last_bus_state = receiver_status.bus_state;
    status_.last_receiver_overflow_count =
        receiver_status.frames_dropped_overflow;

    const auto polled = publisher_.poll_cache(cache_, now_ms);
    if (polled.polled()) {
        result.cache_snapshots_collected = polled.snapshots_collected;
    } else {
        result.cache_error = polled.cache_error;
        result.publisher_error = polled.error;
        record_cycle_error(result, GatewayLoopError::cache_poll_failure);
    }

    for (const auto& peer : peers_) {
        if (!peer.occupied) {
            continue;
        }
        ++result.peers_serviced;
        const auto serviced = publisher_.service_peer(
            transport_, peer.address, now_ms);
        if (serviced.queued()) {
            ++result.packets_queued;
            ++status_.packets_queued;
        } else if (serviced.error ==
                   wireless::GatewayPublisherError::no_data) {
            ++result.peers_with_no_data;
        } else {
            result.publisher_error = serviced.error;
            ++result.peer_service_failures;
            ++status_.peer_service_failures;
            record_cycle_error(
                result, GatewayLoopError::peer_service_failure);
        }
    }
    transport_.service(now_ms);
    ++status_.cycles_serviced;
    return result;
}

GatewayLoopStatus GatewayTelemetryLoop::status() const {
    return status_;
}

std::size_t GatewayTelemetryLoop::find_peer(
    const wireless::PeerAddress& address) const {
    for (std::size_t index = 0; index < peers_.size(); ++index) {
        if (peers_[index].occupied &&
            wireless::peer_address_equals(
                peers_[index].address, address)) {
            return index;
        }
    }
    return kNotFound;
}

std::size_t GatewayTelemetryLoop::find_empty_peer() const {
    for (std::size_t index = 0; index < peers_.size(); ++index) {
        if (!peers_[index].occupied) {
            return index;
        }
    }
    return kNotFound;
}

}  // namespace opengauge::gateway
