#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "opengauge/normalized_signal.hpp"
#include "opengauge/telemetry_cache.hpp"

namespace opengauge::wireless {

inline constexpr std::size_t kTelemetryPacketBytes = 96;
inline constexpr std::size_t kTelemetrySignalsPerPacket = 3;
inline constexpr std::uint8_t kTelemetryPacketVersion = 0;

enum class TelemetrySignalCode : std::uint16_t {
    engine_speed = 1,
    engine_coolant_temperature = 2,
    vehicle_speed = 3,
    electrical_voltage = 4,
};

struct TelemetrySignalDescriptor {
    TelemetrySignalCode code{TelemetrySignalCode::engine_speed};
    std::string_view normalized_id{};
    telemetry::SignalValueType value_type{
        telemetry::SignalValueType::signed_integer};
    telemetry::SignalUnit unit{telemetry::SignalUnit::none};
};

[[nodiscard]] const TelemetrySignalDescriptor*
telemetry_signal_descriptor(TelemetrySignalCode code);

struct WireTelemetrySignal {
    TelemetrySignalCode code{TelemetrySignalCode::engine_speed};
    telemetry::SignalValue value{};
    telemetry::SignalUnit unit{telemetry::SignalUnit::none};
    telemetry::SignalQuality quality{telemetry::SignalQuality::unknown};
    std::uint32_t source_age_ms{0};
};

struct TelemetryBatch {
    std::uint64_t gateway_id{0};
    std::uint32_t boot_session_id{0};
    std::uint32_t sequence{0};
    std::uint64_t gateway_uptime_ms{0};
    std::array<WireTelemetrySignal, kTelemetrySignalsPerPacket> signals{};
    std::uint8_t signal_count{0};
};

enum class TelemetryPacketError : std::uint8_t {
    none = 0,
    invalid_argument,
    buffer_too_small,
    invalid_length,
    invalid_magic,
    unsupported_version,
    unsupported_message_type,
    integrity_failure,
    reserved_nonzero,
    invalid_identity,
    invalid_signal_count,
    duplicate_signal,
    unknown_signal_code,
    unknown_enum,
    incompatible_signal,
    inconsistent_value,
    invalid_value,
    source_age_out_of_range,
    noncanonical_unused_entry,
};

struct TelemetryEncodeResult {
    TelemetryPacketError error{TelemetryPacketError::invalid_argument};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == TelemetryPacketError::none;
    }
};

struct TelemetryDecodeResult {
    TelemetryPacketError error{TelemetryPacketError::invalid_argument};
    TelemetryBatch batch{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == TelemetryPacketError::none;
    }
};

[[nodiscard]] std::uint32_t telemetry_packet_crc32(
    const std::uint8_t* data,
    std::size_t size);

[[nodiscard]] TelemetryPacketError validate_telemetry_batch(
    const TelemetryBatch& batch);

// Converts a cache snapshot to its registered wire form. Fresh/suspect values
// remain numeric. Stale, unavailable, error, out-of-range, and unknown states
// are encoded without a value so a gauge cannot display an invalid number.
[[nodiscard]] TelemetryPacketError make_wire_telemetry_signal(
    TelemetrySignalCode code,
    const telemetry::CachedSignalSnapshot& snapshot,
    WireTelemetrySignal& output);

[[nodiscard]] TelemetryEncodeResult encode_telemetry_packet(
    const TelemetryBatch& batch,
    std::uint8_t* output,
    std::size_t output_capacity);

[[nodiscard]] TelemetryDecodeResult decode_telemetry_packet(
    const std::uint8_t* encoded,
    std::size_t encoded_size);

enum class TelemetrySequenceDisposition : std::uint8_t {
    first = 1,
    in_order,
    gap,
    session_changed,
    duplicate,
    out_of_order,
};

enum class TelemetryStreamError : std::uint8_t {
    none = 0,
    invalid_packet,
    gateway_mismatch,
    clock_regressed,
};

struct TelemetryStreamResult {
    TelemetryStreamError error{TelemetryStreamError::invalid_packet};
    TelemetryPacketError packet_error{TelemetryPacketError::none};
    TelemetrySequenceDisposition disposition{
        TelemetrySequenceDisposition::first};
    std::uint32_t missing_packets{0};
    bool state_advanced{false};

    [[nodiscard]] constexpr bool accepted() const {
        return error == TelemetryStreamError::none && state_advanced;
    }
};

// One instance tracks one provisioned gateway peer. Sequence arithmetic uses
// the 32-bit serial-number half-range rule so ordinary wraparound is in-order.
class TelemetryStreamTracker {
public:
    [[nodiscard]] TelemetryStreamResult ingest(
        const TelemetryBatch& batch,
        std::uint64_t received_at_ms);

    void reset();

    [[nodiscard]] bool initialized() const;
    [[nodiscard]] std::uint64_t gateway_id() const;
    [[nodiscard]] std::uint32_t boot_session_id() const;
    [[nodiscard]] std::uint32_t last_sequence() const;
    [[nodiscard]] std::uint64_t last_received_at_ms() const;

private:
    std::uint64_t gateway_id_{0};
    std::uint32_t boot_session_id_{0};
    std::uint32_t last_sequence_{0};
    std::uint64_t last_received_at_ms_{0};
    bool initialized_{false};
};

enum class TelemetryFreshnessError : std::uint8_t {
    none = 0,
    invalid_signal,
    invalid_stale_threshold,
    clock_regressed,
};

struct WireSignalFreshnessResult {
    TelemetryFreshnessError error{TelemetryFreshnessError::invalid_signal};
    telemetry::SignalQuality effective_quality{
        telemetry::SignalQuality::unknown};
    telemetry::SignalValue display_value{};
    std::uint64_t age_ms{0};

    [[nodiscard]] constexpr bool evaluated() const {
        return error == TelemetryFreshnessError::none;
    }
};

// Adds receiver-local elapsed time to the age observed at the gateway. There
// is deliberately no comparison between gateway uptime and receiver uptime.
[[nodiscard]] WireSignalFreshnessResult evaluate_wire_signal_freshness(
    const WireTelemetrySignal& signal,
    std::uint64_t packet_received_at_ms,
    std::uint64_t now_ms,
    std::uint64_t stale_after_ms);

}  // namespace opengauge::wireless
