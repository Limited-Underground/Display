#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/j1939_decoder.hpp"

namespace {

using namespace opengauge::can;
using namespace opengauge::telemetry;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

J1939Message eec1_message(std::uint16_t raw_engine_speed) {
    J1939Message message{};
    message.raw_identifier = 0x0CF0042AU;
    message.format = CanFrameFormat::extended;
    message.payload.fill(0xFFU);
    message.payload[3] =
        static_cast<std::uint8_t>(raw_engine_speed & 0xFFU);
    message.payload[4] =
        static_cast<std::uint8_t>(raw_engine_speed >> 8U);
    message.data_length = 8;
    message.received_at_ms = 1234;
    return message;
}

J1939DecodeResult invalid_signal_decoder(
    const J1939DecoderRequest&,
    NormalizedSignal* output,
    std::size_t output_capacity) {
    if (output == nullptr || output_capacity == 0) {
        return {
            J1939DecodeError::insufficient_output_capacity,
            J1939IdentifierError::none,
            SignalModelError::none,
            0};
    }
    output[0] = {};
    return {
        J1939DecodeError::none,
        J1939IdentifierError::none,
        SignalModelError::none,
        1};
}

void test_registry_registration_rules() {
    J1939DecoderRegistry registry{};
    EXPECT(registry.register_decoder(kEec1Pgn, nullptr) ==
           J1939DecodeError::invalid_argument);
    EXPECT(registry.register_decoder(
               opengauge::can::kMaximumClassicalJ1939Pgn + 1U,
               decode_eec1_engine_speed) ==
           J1939DecodeError::invalid_argument);
    EXPECT(registry.register_decoder(
               0xEA01U,
               decode_eec1_engine_speed) ==
           J1939DecodeError::invalid_argument);
    EXPECT(registry.register_decoder(
               kEec1Pgn,
               decode_eec1_engine_speed) == J1939DecodeError::none);
    EXPECT(registry.register_decoder(
               kEec1Pgn,
               decode_eec1_engine_speed) ==
           J1939DecodeError::duplicate_pgn);

    for (std::uint32_t index = 1;
         index < kMaximumRegisteredJ1939Decoders;
         ++index) {
        EXPECT(registry.register_decoder(
                   0xF100U + index,
                   decode_eec1_engine_speed) == J1939DecodeError::none);
    }
    EXPECT(registry.size() == kMaximumRegisteredJ1939Decoders);
    EXPECT(registry.register_decoder(
               0xF200U,
               decode_eec1_engine_speed) ==
           J1939DecodeError::registry_full);
}

void test_eec1_valid_engine_speed_boundaries() {
    J1939DecoderRegistry registry{};
    EXPECT(registry.register_decoder(
               kEec1Pgn,
               decode_eec1_engine_speed) == J1939DecodeError::none);
    NormalizedSignal output{};

    auto decoded = registry.decode(eec1_message(8000U), &output, 1);
    EXPECT(decoded.decoded());
    EXPECT(decoded.signal_count == 1);
    EXPECT(signal_id_equals(output.id, "engine.speed"));
    EXPECT(output.value.present);
    EXPECT(output.value.raw_value == 1000000);
    EXPECT(output.unit ==
           SignalUnit::milli_revolutions_per_minute);
    EXPECT(output.quality == SignalQuality::valid);
    EXPECT(output.source.source_address == 0x2AU);
    EXPECT(output.source.parameter_group_number == kEec1Pgn);
    EXPECT(output.source.parameter_number == kEngineSpeedSpn);
    EXPECT(output.received_at_ms == 1234U);

    decoded = registry.decode(eec1_message(0xFAFFU), &output, 1);
    EXPECT(decoded.decoded());
    EXPECT(output.value.present);
    EXPECT(output.value.raw_value == 8031875);
}

void test_eec1_special_encodings_remain_non_numeric() {
    J1939DecoderRegistry registry{};
    EXPECT(registry.register_decoder(
               kEec1Pgn,
               decode_eec1_engine_speed) == J1939DecodeError::none);
    NormalizedSignal output{};

    const std::array<std::uint16_t, 3> out_of_range{
        0xFB00U,
        0xFC00U,
        0xFDFFU};
    for (const auto raw : out_of_range) {
        EXPECT(registry.decode(eec1_message(raw), &output, 1).decoded());
        EXPECT(output.quality == SignalQuality::out_of_range);
        EXPECT(!output.value.present);
        EXPECT(output.value.raw_value == 0);
    }

    const std::array<std::uint16_t, 2> error{0xFE00U, 0xFEFFU};
    for (const auto raw : error) {
        EXPECT(registry.decode(eec1_message(raw), &output, 1).decoded());
        EXPECT(output.quality == SignalQuality::error);
        EXPECT(!output.value.present);
    }

    const std::array<std::uint16_t, 2> unavailable{0xFF00U, 0xFFFFU};
    for (const auto raw : unavailable) {
        EXPECT(registry.decode(eec1_message(raw), &output, 1).decoded());
        EXPECT(output.quality == SignalQuality::unavailable);
        EXPECT(!output.value.present);
    }
}

void test_dispatch_rejects_bad_frames_and_capacity() {
    J1939DecoderRegistry registry{};
    EXPECT(registry.register_decoder(
               kEec1Pgn,
               decode_eec1_engine_speed) == J1939DecodeError::none);
    NormalizedSignal output{};

    auto message = eec1_message(8000U);
    message.format = CanFrameFormat::standard;
    auto result = registry.decode(message, &output, 1);
    EXPECT(result.error == J1939DecodeError::invalid_identifier);
    EXPECT(result.identifier_error == J1939IdentifierError::standard_frame);

    message = eec1_message(8000U);
    message.raw_identifier |= 1U << 25U;
    result = registry.decode(message, &output, 1);
    EXPECT(result.error == J1939DecodeError::invalid_identifier);
    EXPECT(result.identifier_error ==
           J1939IdentifierError::unsupported_extended_data_page);

    message = eec1_message(8000U);
    message.data_length = 9;
    EXPECT(registry.decode(message, &output, 1).error ==
           J1939DecodeError::invalid_payload_length);

    message = eec1_message(8000U);
    message.data_length = 7;
    EXPECT(registry.decode(message, &output, 1).error ==
           J1939DecodeError::invalid_payload_length);

    EXPECT(registry.decode(eec1_message(8000U), nullptr, 0).error ==
           J1939DecodeError::insufficient_output_capacity);
    EXPECT(registry.decode(eec1_message(8000U), nullptr, 1).error ==
           J1939DecodeError::invalid_argument);
}

void test_unknown_pgn_and_invalid_decoder_output_fail_closed() {
    J1939DecoderRegistry registry{};
    NormalizedSignal output{};
    EXPECT(registry.decode(eec1_message(8000U), &output, 1).error ==
           J1939DecodeError::unsupported_pgn);

    EXPECT(registry.register_decoder(kEec1Pgn, invalid_signal_decoder) ==
           J1939DecodeError::none);
    const auto result = registry.decode(eec1_message(8000U), &output, 1);
    EXPECT(result.error == J1939DecodeError::invalid_signal);
    EXPECT(result.signal_error == SignalModelError::invalid_signal_id);
    EXPECT(result.signal_count == 0);
}

}  // namespace

int main() {
    test_registry_registration_rules();
    test_eec1_valid_engine_speed_boundaries();
    test_eec1_special_encodings_remain_non_numeric();
    test_dispatch_rejects_bad_frames_and_capacity();
    test_unknown_pgn_and_invalid_decoder_output_fail_closed();

    if (failures != 0) {
        std::cerr << failures << " J1939 decoder assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 5 J1939 decoder scenario groups\n";
    return EXIT_SUCCESS;
}
