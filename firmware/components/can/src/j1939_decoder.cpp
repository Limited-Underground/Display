#include "opengauge/j1939_decoder.hpp"

namespace opengauge::can {
namespace {

constexpr std::uint16_t kMaximumValidEngineSpeedRaw = 0xFAFFU;
constexpr std::uint16_t kFirstErrorEngineSpeedRaw = 0xFE00U;
constexpr std::uint16_t kFirstUnavailableEngineSpeedRaw = 0xFF00U;
constexpr std::int64_t kEngineSpeedMilliRpmPerBit = 125;

bool is_canonical_classical_pgn(std::uint32_t pgn) {
    if (pgn > kMaximumClassicalJ1939Pgn) {
        return false;
    }
    const auto pdu_format = static_cast<std::uint8_t>(
        (pgn >> 8U) & 0xFFU);
    return pdu_format >= 0xF0U || (pgn & 0xFFU) == 0;
}

J1939DecodeResult result(J1939DecodeError error) {
    return {
        error,
        J1939IdentifierError::none,
        telemetry::SignalModelError::none,
        0};
}

}  // namespace

J1939DecodeError J1939DecoderRegistry::register_decoder(
    std::uint32_t parameter_group_number,
    J1939DecoderFunction decoder) {
    if (!is_canonical_classical_pgn(parameter_group_number) ||
        decoder == nullptr) {
        return J1939DecodeError::invalid_argument;
    }
    for (std::size_t index = 0; index < size_; ++index) {
        if (entries_[index].parameter_group_number ==
            parameter_group_number) {
            return J1939DecodeError::duplicate_pgn;
        }
    }
    if (size_ == entries_.size()) {
        return J1939DecodeError::registry_full;
    }
    entries_[size_] = {parameter_group_number, decoder};
    ++size_;
    return J1939DecodeError::none;
}

J1939DecodeResult J1939DecoderRegistry::decode(
    const J1939Message& message,
    telemetry::NormalizedSignal* output,
    std::size_t output_capacity) const {
    if (output == nullptr && output_capacity != 0) {
        return result(J1939DecodeError::invalid_argument);
    }
    if (message.data_length > message.payload.size()) {
        return result(J1939DecodeError::invalid_payload_length);
    }

    const auto parsed = parse_j1939_identifier(
        message.raw_identifier,
        message.format);
    if (!parsed.parsed()) {
        auto invalid = result(J1939DecodeError::invalid_identifier);
        invalid.identifier_error = parsed.error;
        return invalid;
    }

    J1939DecoderFunction decoder = nullptr;
    for (std::size_t index = 0; index < size_; ++index) {
        if (entries_[index].parameter_group_number ==
            parsed.identifier.parameter_group_number) {
            decoder = entries_[index].decoder;
            break;
        }
    }
    if (decoder == nullptr) {
        return result(J1939DecodeError::unsupported_pgn);
    }

    const J1939DecoderRequest request{
        parsed.identifier,
        message.payload.data(),
        message.data_length,
        message.received_at_ms};
    auto decoded = decoder(request, output, output_capacity);
    if (!decoded.decoded()) {
        decoded.signal_count = 0;
        return decoded;
    }
    if (decoded.signal_count > output_capacity ||
        (decoded.signal_count != 0 && output == nullptr)) {
        return result(J1939DecodeError::decoder_failure);
    }
    for (std::size_t index = 0; index < decoded.signal_count; ++index) {
        const auto validation =
            telemetry::validate_normalized_signal(output[index]);
        if (validation != telemetry::SignalModelError::none) {
            auto invalid = result(J1939DecodeError::invalid_signal);
            invalid.signal_error = validation;
            return invalid;
        }
    }
    return decoded;
}

J1939DecodeResult decode_eec1_engine_speed(
    const J1939DecoderRequest& request,
    telemetry::NormalizedSignal* output,
    std::size_t output_capacity) {
    if (request.identifier.parameter_group_number != kEec1Pgn ||
        request.payload == nullptr) {
        return result(J1939DecodeError::decoder_failure);
    }
    if (request.payload_size != kMaximumClassicalCanPayloadBytes) {
        return result(J1939DecodeError::invalid_payload_length);
    }
    if (output == nullptr || output_capacity < 1) {
        return result(J1939DecodeError::insufficient_output_capacity);
    }

    telemetry::NormalizedSignal signal{};
    const auto id_error = telemetry::make_signal_id(
        "engine.speed",
        signal.id);
    if (id_error != telemetry::SignalModelError::none) {
        auto invalid = result(J1939DecodeError::invalid_signal);
        invalid.signal_error = id_error;
        return invalid;
    }

    const auto raw = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(request.payload[3]) |
        (static_cast<std::uint16_t>(request.payload[4]) << 8U));
    signal.value.type = telemetry::SignalValueType::unsigned_integer;
    signal.unit = telemetry::SignalUnit::milli_revolutions_per_minute;
    if (raw <= kMaximumValidEngineSpeedRaw) {
        signal.value.raw_value =
            static_cast<std::int64_t>(raw) *
            kEngineSpeedMilliRpmPerBit;
        signal.value.present = true;
        signal.quality = telemetry::SignalQuality::valid;
    } else if (raw >= kFirstUnavailableEngineSpeedRaw) {
        signal.quality = telemetry::SignalQuality::unavailable;
    } else if (raw >= kFirstErrorEngineSpeedRaw) {
        signal.quality = telemetry::SignalQuality::error;
    } else {
        signal.quality = telemetry::SignalQuality::out_of_range;
    }

    signal.source.protocol = telemetry::SignalSourceProtocol::j1939;
    signal.source.source_address = request.identifier.source_address;
    signal.source.parameter_group_number = kEec1Pgn;
    signal.source.parameter_number = kEngineSpeedSpn;
    signal.source.source_address_present = true;
    signal.source.parameter_group_present = true;
    signal.source.parameter_present = true;
    signal.received_at_ms = request.received_at_ms;

    const auto validation = telemetry::validate_normalized_signal(signal);
    if (validation != telemetry::SignalModelError::none) {
        auto invalid = result(J1939DecodeError::invalid_signal);
        invalid.signal_error = validation;
        return invalid;
    }
    output[0] = signal;
    auto decoded = result(J1939DecodeError::none);
    decoded.signal_count = 1;
    return decoded;
}

}  // namespace opengauge::can
