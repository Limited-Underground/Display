#include "opengauge/telemetry_packet.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>

namespace opengauge::wireless {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'O', 'G', 'T', '0'};
constexpr std::uint8_t kMessageTypeTelemetryBatch = 1;
constexpr std::size_t kHeaderBytes = 36;
constexpr std::size_t kSignalEntryBytes = 18;
constexpr std::size_t kReservedTailOffset = 90;
constexpr std::size_t kChecksumOffset = 92;
constexpr std::uint8_t kValuePresentFlag = 0x01U;

constexpr std::array<TelemetrySignalDescriptor, 4> kSignalDefinitions{{
    {TelemetrySignalCode::engine_speed,
     "powertrain.engine_speed",
     telemetry::SignalValueType::unsigned_integer,
     telemetry::SignalUnit::milli_revolutions_per_minute},
    {TelemetrySignalCode::engine_coolant_temperature,
     "powertrain.engine_coolant_temperature",
     telemetry::SignalValueType::signed_integer,
     telemetry::SignalUnit::milli_celsius},
    {TelemetrySignalCode::vehicle_speed,
     "vehicle.speed",
     telemetry::SignalValueType::unsigned_integer,
     telemetry::SignalUnit::millimetres_per_second},
    {TelemetrySignalCode::electrical_voltage,
     "electrical.system_voltage",
     telemetry::SignalValueType::unsigned_integer,
     telemetry::SignalUnit::millivolt},
}};

const TelemetrySignalDescriptor* find_definition(TelemetrySignalCode code) {
    const auto match = std::find_if(
        kSignalDefinitions.begin(),
        kSignalDefinitions.end(),
        [code](const TelemetrySignalDescriptor& definition) {
            return definition.code == code;
        });
    return match == kSignalDefinitions.end() ? nullptr : &*match;
}

bool quality_allows_value(telemetry::SignalQuality quality) {
    return quality == telemetry::SignalQuality::valid ||
           quality == telemetry::SignalQuality::suspect;
}

bool known_quality(telemetry::SignalQuality quality) {
    switch (quality) {
        case telemetry::SignalQuality::valid:
        case telemetry::SignalQuality::suspect:
        case telemetry::SignalQuality::unavailable:
        case telemetry::SignalQuality::error:
        case telemetry::SignalQuality::out_of_range:
        case telemetry::SignalQuality::stale:
        case telemetry::SignalQuality::unknown:
            return true;
    }
    return false;
}

bool known_value_type(telemetry::SignalValueType type) {
    switch (type) {
        case telemetry::SignalValueType::boolean:
        case telemetry::SignalValueType::signed_integer:
        case telemetry::SignalValueType::unsigned_integer:
            return true;
    }
    return false;
}

bool known_unit(telemetry::SignalUnit unit) {
    switch (unit) {
        case telemetry::SignalUnit::none:
        case telemetry::SignalUnit::count:
        case telemetry::SignalUnit::milli_celsius:
        case telemetry::SignalUnit::pascal:
        case telemetry::SignalUnit::millivolt:
        case telemetry::SignalUnit::milliampere:
        case telemetry::SignalUnit::milli_percent:
        case telemetry::SignalUnit::milli_revolutions_per_minute:
        case telemetry::SignalUnit::millimetres_per_second:
            return true;
    }
    return false;
}

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
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

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t result = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        result |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    return result;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        result |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    }
    return result;
}

void write_i64(std::uint8_t* output, std::int64_t value) {
    std::uint64_t bits = 0;
    if (value >= 0) {
        bits = static_cast<std::uint64_t>(value);
    } else {
        const auto magnitude = static_cast<std::uint64_t>(-(value + 1)) + 1U;
        bits = (~magnitude) + 1U;
    }
    write_u64(output, bits);
}

bool read_i64(const std::uint8_t* input, std::int64_t& output) {
    const auto bits = read_u64(input);
    if ((bits & (std::uint64_t{1} << 63U)) == 0) {
        output = static_cast<std::int64_t>(bits);
        return true;
    }
    const auto magnitude = (~bits) + 1U;
    if (magnitude == (std::uint64_t{1} << 63U)) {
        output = std::numeric_limits<std::int64_t>::min();
        return true;
    }
    if (magnitude > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
        return false;
    }
    output = -static_cast<std::int64_t>(magnitude);
    return true;
}

TelemetryPacketError validate_wire_signal(const WireTelemetrySignal& signal) {
    const auto* definition = find_definition(signal.code);
    if (definition == nullptr) {
        return TelemetryPacketError::unknown_signal_code;
    }
    if (!known_value_type(signal.value.type) ||
        !known_unit(signal.unit) ||
        !known_quality(signal.quality)) {
        return TelemetryPacketError::unknown_enum;
    }
    if (signal.value.type != definition->value_type ||
        signal.unit != definition->unit) {
        return TelemetryPacketError::incompatible_signal;
    }
    if (quality_allows_value(signal.quality) != signal.value.present ||
        (!signal.value.present && signal.value.raw_value != 0)) {
        return TelemetryPacketError::inconsistent_value;
    }

    telemetry::NormalizedSignal normalized{};
    if (telemetry::make_signal_id(definition->normalized_id, normalized.id) !=
        telemetry::SignalModelError::none) {
        return TelemetryPacketError::invalid_value;
    }
    normalized.value = signal.value;
    normalized.unit = signal.unit;
    normalized.quality = signal.quality;
    normalized.source.protocol = telemetry::SignalSourceProtocol::synthetic;
    const auto model_error = telemetry::validate_normalized_signal(normalized);
    if (model_error != telemetry::SignalModelError::none) {
        return TelemetryPacketError::invalid_value;
    }
    return TelemetryPacketError::none;
}

bool bytes_are_zero(
    const std::uint8_t* data,
    std::size_t begin,
    std::size_t end) {
    for (auto index = begin; index < end; ++index) {
        if (data[index] != 0) {
            return false;
        }
    }
    return true;
}

void write_signal_entry(
    std::uint8_t* output,
    const WireTelemetrySignal& signal) {
    write_u16(output, static_cast<std::uint16_t>(signal.code));
    output[2] = static_cast<std::uint8_t>(signal.value.type);
    output[3] = static_cast<std::uint8_t>(signal.unit);
    output[4] = static_cast<std::uint8_t>(signal.quality);
    output[5] = signal.value.present ? kValuePresentFlag : 0;
    write_i64(output + 6, signal.value.raw_value);
    write_u32(output + 14, signal.source_age_ms);
}

TelemetryPacketError read_signal_entry(
    const std::uint8_t* input,
    WireTelemetrySignal& output) {
    if ((input[5] & ~kValuePresentFlag) != 0) {
        return TelemetryPacketError::reserved_nonzero;
    }
    output = {};
    output.code = static_cast<TelemetrySignalCode>(read_u16(input));
    output.value.type = static_cast<telemetry::SignalValueType>(input[2]);
    output.unit = static_cast<telemetry::SignalUnit>(input[3]);
    output.quality = static_cast<telemetry::SignalQuality>(input[4]);
    output.value.present = (input[5] & kValuePresentFlag) != 0;
    if (!read_i64(input + 6, output.value.raw_value)) {
        return TelemetryPacketError::invalid_value;
    }
    output.source_age_ms = read_u32(input + 14);
    return validate_wire_signal(output);
}

}  // namespace

const TelemetrySignalDescriptor* telemetry_signal_descriptor(
    TelemetrySignalCode code) {
    return find_definition(code);
}

std::uint32_t telemetry_packet_crc32(
    const std::uint8_t* data,
    std::size_t size) {
    if (data == nullptr && size != 0) {
        return 0;
    }
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -static_cast<std::int32_t>(crc & 1U));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

TelemetryPacketError validate_telemetry_batch(const TelemetryBatch& batch) {
    if (batch.gateway_id == 0 || batch.boot_session_id == 0) {
        return TelemetryPacketError::invalid_identity;
    }
    if (batch.signal_count == 0 ||
        batch.signal_count > kTelemetrySignalsPerPacket) {
        return TelemetryPacketError::invalid_signal_count;
    }
    for (std::size_t index = 0; index < batch.signal_count; ++index) {
        const auto error = validate_wire_signal(batch.signals[index]);
        if (error != TelemetryPacketError::none) {
            return error;
        }
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (batch.signals[earlier].code == batch.signals[index].code) {
                return TelemetryPacketError::duplicate_signal;
            }
        }
    }
    return TelemetryPacketError::none;
}

TelemetryPacketError make_wire_telemetry_signal(
    TelemetrySignalCode code,
    const telemetry::CachedSignalSnapshot& snapshot,
    WireTelemetrySignal& output) {
    const auto* definition = find_definition(code);
    if (definition == nullptr) {
        return TelemetryPacketError::unknown_signal_code;
    }
    if (telemetry::validate_normalized_signal(snapshot.signal) !=
        telemetry::SignalModelError::none) {
        return TelemetryPacketError::invalid_value;
    }
    if (!telemetry::signal_id_equals(
            snapshot.signal.id, definition->normalized_id) ||
        snapshot.signal.value.type != definition->value_type ||
        snapshot.signal.unit != definition->unit) {
        return TelemetryPacketError::incompatible_signal;
    }
    if (!known_quality(snapshot.effective_quality)) {
        return TelemetryPacketError::unknown_enum;
    }
    if (snapshot.age_ms >
        std::numeric_limits<std::uint32_t>::max()) {
        return TelemetryPacketError::source_age_out_of_range;
    }

    WireTelemetrySignal candidate{};
    candidate.code = code;
    candidate.value.type = definition->value_type;
    candidate.unit = definition->unit;
    candidate.quality = snapshot.effective_quality;
    candidate.source_age_ms = static_cast<std::uint32_t>(snapshot.age_ms);
    if (quality_allows_value(candidate.quality)) {
        if (!snapshot.signal.value.present) {
            return TelemetryPacketError::inconsistent_value;
        }
        candidate.value = snapshot.signal.value;
    } else {
        candidate.value.raw_value = 0;
        candidate.value.present = false;
    }
    const auto error = validate_wire_signal(candidate);
    if (error == TelemetryPacketError::none) {
        output = candidate;
    }
    return error;
}

TelemetryEncodeResult encode_telemetry_packet(
    const TelemetryBatch& batch,
    std::uint8_t* output,
    std::size_t output_capacity) {
    if (output == nullptr) {
        return {TelemetryPacketError::invalid_argument, 0};
    }
    if (output_capacity < kTelemetryPacketBytes) {
        return {TelemetryPacketError::buffer_too_small, 0};
    }
    const auto validation = validate_telemetry_batch(batch);
    if (validation != TelemetryPacketError::none) {
        return {validation, 0};
    }

    std::array<std::uint8_t, kTelemetryPacketBytes> encoded{};
    std::copy(kMagic.begin(), kMagic.end(), encoded.begin());
    encoded[4] = kTelemetryPacketVersion;
    encoded[5] = kMessageTypeTelemetryBatch;
    write_u16(encoded.data() + 6,
              static_cast<std::uint16_t>(kTelemetryPacketBytes));
    write_u64(encoded.data() + 8, batch.gateway_id);
    write_u32(encoded.data() + 16, batch.boot_session_id);
    write_u32(encoded.data() + 20, batch.sequence);
    write_u64(encoded.data() + 24, batch.gateway_uptime_ms);
    encoded[32] = batch.signal_count;
    for (std::size_t index = 0; index < batch.signal_count; ++index) {
        write_signal_entry(
            encoded.data() + kHeaderBytes + index * kSignalEntryBytes,
            batch.signals[index]);
    }
    write_u32(encoded.data() + kChecksumOffset,
              telemetry_packet_crc32(encoded.data(), kChecksumOffset));
    std::copy(encoded.begin(), encoded.end(), output);
    return {TelemetryPacketError::none, kTelemetryPacketBytes};
}

TelemetryDecodeResult decode_telemetry_packet(
    const std::uint8_t* encoded,
    std::size_t encoded_size) {
    if (encoded == nullptr) {
        return {TelemetryPacketError::invalid_argument, {}};
    }
    if (encoded_size != kTelemetryPacketBytes) {
        return {TelemetryPacketError::invalid_length, {}};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), encoded)) {
        return {TelemetryPacketError::invalid_magic, {}};
    }
    if (encoded[4] != kTelemetryPacketVersion) {
        return {TelemetryPacketError::unsupported_version, {}};
    }
    if (encoded[5] != kMessageTypeTelemetryBatch) {
        return {TelemetryPacketError::unsupported_message_type, {}};
    }
    if (read_u16(encoded + 6) != kTelemetryPacketBytes) {
        return {TelemetryPacketError::invalid_length, {}};
    }
    if (read_u32(encoded + kChecksumOffset) !=
        telemetry_packet_crc32(encoded, kChecksumOffset)) {
        return {TelemetryPacketError::integrity_failure, {}};
    }
    if (!bytes_are_zero(encoded, 33, 36) ||
        !bytes_are_zero(encoded, kReservedTailOffset, kChecksumOffset)) {
        return {TelemetryPacketError::reserved_nonzero, {}};
    }

    TelemetryBatch batch{};
    batch.gateway_id = read_u64(encoded + 8);
    batch.boot_session_id = read_u32(encoded + 16);
    batch.sequence = read_u32(encoded + 20);
    batch.gateway_uptime_ms = read_u64(encoded + 24);
    batch.signal_count = encoded[32];
    if (batch.signal_count == 0 ||
        batch.signal_count > kTelemetrySignalsPerPacket) {
        return {TelemetryPacketError::invalid_signal_count, {}};
    }
    for (std::size_t index = 0; index < batch.signal_count; ++index) {
        const auto entry_error = read_signal_entry(
            encoded + kHeaderBytes + index * kSignalEntryBytes,
            batch.signals[index]);
        if (entry_error != TelemetryPacketError::none) {
            return {entry_error, {}};
        }
    }
    const auto unused_begin = kHeaderBytes +
                              batch.signal_count * kSignalEntryBytes;
    if (!bytes_are_zero(encoded, unused_begin, kReservedTailOffset)) {
        return {TelemetryPacketError::noncanonical_unused_entry, {}};
    }
    const auto validation = validate_telemetry_batch(batch);
    if (validation != TelemetryPacketError::none) {
        return {validation, {}};
    }
    return {TelemetryPacketError::none, batch};
}

TelemetryStreamResult TelemetryStreamTracker::ingest(
    const TelemetryBatch& batch,
    std::uint64_t received_at_ms) {
    const auto packet_error = validate_telemetry_batch(batch);
    if (packet_error != TelemetryPacketError::none) {
        return {
            TelemetryStreamError::invalid_packet,
            packet_error,
            TelemetrySequenceDisposition::first,
            0,
            false};
    }
    if (!initialized_) {
        gateway_id_ = batch.gateway_id;
        boot_session_id_ = batch.boot_session_id;
        last_sequence_ = batch.sequence;
        last_received_at_ms_ = received_at_ms;
        initialized_ = true;
        return {
            TelemetryStreamError::none,
            TelemetryPacketError::none,
            TelemetrySequenceDisposition::first,
            0,
            true};
    }
    if (batch.gateway_id != gateway_id_) {
        return {
            TelemetryStreamError::gateway_mismatch,
            TelemetryPacketError::none,
            TelemetrySequenceDisposition::out_of_order,
            0,
            false};
    }
    if (received_at_ms < last_received_at_ms_) {
        return {
            TelemetryStreamError::clock_regressed,
            TelemetryPacketError::none,
            TelemetrySequenceDisposition::out_of_order,
            0,
            false};
    }
    if (batch.boot_session_id != boot_session_id_) {
        boot_session_id_ = batch.boot_session_id;
        last_sequence_ = batch.sequence;
        last_received_at_ms_ = received_at_ms;
        return {
            TelemetryStreamError::none,
            TelemetryPacketError::none,
            TelemetrySequenceDisposition::session_changed,
            0,
            true};
    }

    const auto delta = static_cast<std::uint32_t>(
        batch.sequence - last_sequence_);
    if (delta == 0) {
        return {
            TelemetryStreamError::none,
            TelemetryPacketError::none,
            TelemetrySequenceDisposition::duplicate,
            0,
            false};
    }
    if (delta >= 0x80000000U) {
        return {
            TelemetryStreamError::none,
            TelemetryPacketError::none,
            TelemetrySequenceDisposition::out_of_order,
            0,
            false};
    }

    last_sequence_ = batch.sequence;
    last_received_at_ms_ = received_at_ms;
    return {
        TelemetryStreamError::none,
        TelemetryPacketError::none,
        delta == 1 ? TelemetrySequenceDisposition::in_order
                   : TelemetrySequenceDisposition::gap,
        delta - 1U,
        true};
}

void TelemetryStreamTracker::reset() {
    gateway_id_ = 0;
    boot_session_id_ = 0;
    last_sequence_ = 0;
    last_received_at_ms_ = 0;
    initialized_ = false;
}

bool TelemetryStreamTracker::initialized() const {
    return initialized_;
}

std::uint64_t TelemetryStreamTracker::gateway_id() const {
    return gateway_id_;
}

std::uint32_t TelemetryStreamTracker::boot_session_id() const {
    return boot_session_id_;
}

std::uint32_t TelemetryStreamTracker::last_sequence() const {
    return last_sequence_;
}

std::uint64_t TelemetryStreamTracker::last_received_at_ms() const {
    return last_received_at_ms_;
}

WireSignalFreshnessResult evaluate_wire_signal_freshness(
    const WireTelemetrySignal& signal,
    std::uint64_t packet_received_at_ms,
    std::uint64_t now_ms,
    std::uint64_t stale_after_ms) {
    if (validate_wire_signal(signal) != TelemetryPacketError::none) {
        return {
            TelemetryFreshnessError::invalid_signal,
            telemetry::SignalQuality::unknown,
            {},
            0};
    }
    if (stale_after_ms == 0) {
        return {
            TelemetryFreshnessError::invalid_stale_threshold,
            telemetry::SignalQuality::unknown,
            {},
            0};
    }
    if (now_ms < packet_received_at_ms) {
        return {
            TelemetryFreshnessError::clock_regressed,
            telemetry::SignalQuality::unknown,
            {},
            0};
    }
    const auto elapsed_ms = now_ms - packet_received_at_ms;
    const auto source_age = static_cast<std::uint64_t>(signal.source_age_ms);
    const auto age_ms = elapsed_ms >
                                std::numeric_limits<std::uint64_t>::max() -
                                    source_age
                            ? std::numeric_limits<std::uint64_t>::max()
                            : source_age + elapsed_ms;
    auto quality = signal.quality;
    auto value = signal.value;
    if (quality_allows_value(quality) && age_ms >= stale_after_ms) {
        quality = telemetry::SignalQuality::stale;
        value.raw_value = 0;
        value.present = false;
    }
    return {TelemetryFreshnessError::none, quality, value, age_ms};
}

}  // namespace opengauge::wireless
