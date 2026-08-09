#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/j1939_identifier.hpp"
#include "opengauge/normalized_signal.hpp"

namespace opengauge::can {

inline constexpr std::size_t kMaximumClassicalCanPayloadBytes = 8;
inline constexpr std::size_t kMaximumRegisteredJ1939Decoders = 8;
inline constexpr std::uint32_t kEec1Pgn = 0xF004U;
inline constexpr std::uint32_t kEngineSpeedSpn = 190U;

struct J1939Message {
    std::uint32_t raw_identifier{0};
    CanFrameFormat format{CanFrameFormat::standard};
    std::array<std::uint8_t, kMaximumClassicalCanPayloadBytes> payload{};
    std::uint8_t data_length{0};
    std::uint64_t received_at_ms{0};
};

struct J1939DecoderRequest {
    J1939Identifier identifier{};
    const std::uint8_t* payload{nullptr};
    std::size_t payload_size{0};
    std::uint64_t received_at_ms{0};
};

enum class J1939DecodeError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_identifier,
    invalid_payload_length,
    duplicate_pgn,
    registry_full,
    unsupported_pgn,
    insufficient_output_capacity,
    decoder_failure,
    invalid_signal,
};

struct J1939DecodeResult {
    J1939DecodeError error{J1939DecodeError::invalid_argument};
    J1939IdentifierError identifier_error{J1939IdentifierError::none};
    telemetry::SignalModelError signal_error{
        telemetry::SignalModelError::none};
    std::size_t signal_count{0};

    [[nodiscard]] constexpr bool decoded() const {
        return error == J1939DecodeError::none;
    }
};

using J1939DecoderFunction = J1939DecodeResult (*)(
    const J1939DecoderRequest& request,
    telemetry::NormalizedSignal* output,
    std::size_t output_capacity);

class J1939DecoderRegistry {
public:
    [[nodiscard]] J1939DecodeError register_decoder(
        std::uint32_t parameter_group_number,
        J1939DecoderFunction decoder);

    [[nodiscard]] J1939DecodeResult decode(
        const J1939Message& message,
        telemetry::NormalizedSignal* output,
        std::size_t output_capacity) const;

    [[nodiscard]] constexpr std::size_t size() const {
        return size_;
    }

private:
    struct Entry {
        std::uint32_t parameter_group_number{0};
        J1939DecoderFunction decoder{nullptr};
    };

    std::array<Entry, kMaximumRegisteredJ1939Decoders> entries_{};
    std::size_t size_{0};
};

// Reference fixture for PGN 61444, SPN 190 only. This is not a complete EEC1
// decoder and must be checked against the licensed/current J1939 data source
// and captured traffic before vehicle use.
[[nodiscard]] J1939DecodeResult decode_eec1_engine_speed(
    const J1939DecoderRequest& request,
    telemetry::NormalizedSignal* output,
    std::size_t output_capacity);

}  // namespace opengauge::can
