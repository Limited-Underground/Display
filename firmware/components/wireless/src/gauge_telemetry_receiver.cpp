#include "opengauge/gauge_telemetry_receiver.hpp"

#include <array>
#include <limits>

namespace opengauge::wireless {
namespace {

constexpr std::size_t kNotFound =
    std::numeric_limits<std::size_t>::max();

void record_cycle_error(
    GaugeReceiverCycleResult& result,
    GaugeReceiverError error) {
    if (result.error == GaugeReceiverError::none ||
        result.error == GaugeReceiverError::no_data) {
        result.error = error;
    }
}

}  // namespace

GaugeTelemetryReceiver::GaugeTelemetryReceiver(EspNowTransport& transport)
    : transport_(transport) {}

GaugeReceiverError GaugeTelemetryReceiver::start(
    GaugeReceiverConfiguration configuration) {
    if (status_.running) {
        return GaugeReceiverError::invalid_state;
    }
    if (!is_valid_unicast_address(
            configuration.expected_gateway_address) ||
        configuration.expected_gateway_id == 0 ||
        configuration.expected_channel == 0 ||
        configuration.expected_channel > 14 ||
        configuration.maximum_packets_per_cycle == 0 ||
        configuration.maximum_packets_per_cycle >
            kMaximumGaugePacketsPerCycle) {
        return GaugeReceiverError::invalid_configuration;
    }
    configuration_ = configuration;
    stream_.reset();
    clear_store();
    status_ = {};
    status_.running = true;
    return GaugeReceiverError::none;
}

void GaugeTelemetryReceiver::stop() {
    configuration_ = {};
    stream_.reset();
    clear_store();
    status_ = {};
}

GaugeReceiverCycleResult GaugeTelemetryReceiver::service(
    std::uint64_t now_ms) {
    if (!status_.running) {
        return {GaugeReceiverError::invalid_state};
    }
    transport_.service(now_ms);
    GaugeReceiverCycleResult result{};
    std::array<std::uint8_t, kMaximumEspNowPayloadBytes> encoded{};
    for (std::size_t index = 0;
         index < configuration_.maximum_packets_per_cycle;
         ++index) {
        const auto received = transport_.receive(
            {encoded.data(), encoded.size()});
        if (received.error == EspNowError::no_data) {
            break;
        }
        if (!received.has_frame()) {
            result.transport_error = received.error;
            record_cycle_error(
                result, GaugeReceiverError::transport_failure);
            break;
        }
        ++result.datagrams_received;
        ++status_.datagrams_received;

        if (!peer_address_equals(
                received.metadata.source,
                configuration_.expected_gateway_address)) {
            ++result.unauthorized_datagrams;
            ++status_.unauthorized_datagrams;
            record_cycle_error(
                result, GaugeReceiverError::unauthorized_source);
            continue;
        }
        if (!received.metadata.encrypted) {
            ++result.unencrypted_datagrams;
            ++status_.unencrypted_datagrams;
            record_cycle_error(
                result, GaugeReceiverError::encryption_required);
            continue;
        }
        if (received.metadata.channel != configuration_.expected_channel) {
            ++result.channel_mismatches;
            ++status_.channel_mismatches;
            record_cycle_error(
                result, GaugeReceiverError::channel_mismatch);
            continue;
        }

        const auto decoded = decode_telemetry_packet(
            encoded.data(), received.received_bytes);
        if (!decoded.decoded()) {
            result.packet_error = decoded.error;
            ++result.malformed_packets;
            ++status_.malformed_packets;
            record_cycle_error(result, GaugeReceiverError::packet_failure);
            continue;
        }
        if (decoded.batch.gateway_id !=
            configuration_.expected_gateway_id) {
            result.stream_error = TelemetryStreamError::gateway_mismatch;
            ++result.gateway_identity_mismatches;
            ++result.stream_failures;
            ++status_.gateway_identity_mismatches;
            ++status_.stream_failures;
            record_cycle_error(result, GaugeReceiverError::stream_failure);
            continue;
        }
        const bool session_will_reset =
            stream_.initialized() &&
            decoded.batch.boot_session_id != stream_.boot_session_id();
        if (!can_store_batch(decoded.batch, session_will_reset)) {
            record_cycle_error(
                result, GaugeReceiverError::signal_capacity_full);
            continue;
        }

        const auto stream = stream_.ingest(
            decoded.batch, received.metadata.received_at_ms);
        if (stream.error != TelemetryStreamError::none) {
            result.stream_error = stream.error;
            ++result.stream_failures;
            ++status_.stream_failures;
            record_cycle_error(result, GaugeReceiverError::stream_failure);
            continue;
        }
        if (!stream.state_advanced) {
            if (stream.disposition ==
                TelemetrySequenceDisposition::duplicate) {
                ++result.duplicate_packets;
                ++status_.duplicate_packets;
            } else {
                ++result.out_of_order_packets;
                ++status_.out_of_order_packets;
            }
            continue;
        }
        if (stream.disposition ==
            TelemetrySequenceDisposition::session_changed) {
            clear_store();
            ++result.session_resets;
            ++status_.session_resets;
        } else if (stream.disposition ==
                   TelemetrySequenceDisposition::gap) {
            ++result.gap_packets;
            ++status_.gap_packets;
            result.missing_packets += stream.missing_packets;
            status_.missing_packets += stream.missing_packets;
        }

        store_batch(decoded.batch, received.metadata.received_at_ms);
        ++result.packets_accepted;
        ++status_.packets_accepted;
        result.signals_updated += decoded.batch.signal_count;
        status_.signals_updated += decoded.batch.signal_count;
    }

    result.packet_budget_exhausted =
        result.datagrams_received ==
            configuration_.maximum_packets_per_cycle &&
        transport_.status().receive_queue_depth != 0;
    if (result.datagrams_received != 0 &&
        (result.error == GaugeReceiverError::no_data)) {
        result.error = GaugeReceiverError::none;
    }
    status_.last_error = result.error;
    status_.signal_count = signal_count_;
    return result;
}

GaugeSignalReadResult GaugeTelemetryReceiver::read(
    TelemetrySignalCode code,
    std::uint64_t now_ms,
    std::uint64_t stale_after_ms) const {
    if (!status_.running) {
        return {GaugeReceiverError::invalid_state};
    }
    const auto index = find_signal(code);
    if (index == kNotFound) {
        return {GaugeReceiverError::signal_not_found};
    }
    const auto& stored = signals_[index];
    const auto freshness = evaluate_wire_signal_freshness(
        stored.signal,
        stored.packet_received_at_ms,
        now_ms,
        stale_after_ms);
    if (!freshness.evaluated()) {
        return {
            GaugeReceiverError::freshness_failure,
            freshness.error,
            code};
    }
    return {
        GaugeReceiverError::none,
        TelemetryFreshnessError::none,
        code,
        freshness.effective_quality,
        freshness.display_value,
        stored.signal.unit,
        freshness.age_ms,
        stored.boot_session_id,
        stored.packet_sequence,
        stored.packet_received_at_ms};
}

GaugeReceiverStatus GaugeTelemetryReceiver::status() const {
    return status_;
}

std::size_t GaugeTelemetryReceiver::find_signal(
    TelemetrySignalCode code) const {
    for (std::size_t index = 0; index < signals_.size(); ++index) {
        if (signals_[index].occupied &&
            signals_[index].signal.code == code) {
            return index;
        }
    }
    return kNotFound;
}

bool GaugeTelemetryReceiver::can_store_batch(
    const TelemetryBatch& batch,
    bool clear_existing_first) const {
    if (clear_existing_first) {
        return batch.signal_count <= signals_.size();
    }
    std::size_t required = 0;
    for (std::size_t index = 0; index < batch.signal_count; ++index) {
        if (find_signal(batch.signals[index].code) == kNotFound) {
            ++required;
        }
    }
    return required <= signals_.size() - signal_count_;
}

void GaugeTelemetryReceiver::store_batch(
    const TelemetryBatch& batch,
    std::uint64_t packet_received_at_ms) {
    for (std::size_t batch_index = 0;
         batch_index < batch.signal_count;
         ++batch_index) {
        auto index = find_signal(batch.signals[batch_index].code);
        if (index == kNotFound) {
            for (std::size_t candidate = 0;
                 candidate < signals_.size();
                 ++candidate) {
                if (!signals_[candidate].occupied) {
                    index = candidate;
                    ++signal_count_;
                    break;
                }
            }
        }
        signals_[index] = {
            batch.signals[batch_index],
            packet_received_at_ms,
            batch.boot_session_id,
            batch.sequence,
            true};
    }
    status_.signal_count = signal_count_;
}

void GaugeTelemetryReceiver::clear_store() {
    signals_ = {};
    signal_count_ = 0;
    status_.signal_count = 0;
}

}  // namespace opengauge::wireless
