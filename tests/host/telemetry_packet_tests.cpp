#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "fake_esp_now_transport.hpp"
#include "opengauge/telemetry_packet.hpp"

namespace {

using namespace opengauge;
using wireless::test_support::FakeEspNowTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

telemetry::CachedSignalSnapshot snapshot(
    wireless::TelemetrySignalCode code,
    telemetry::SignalQuality effective_quality,
    std::int64_t value,
    std::uint64_t age_ms) {
    telemetry::CachedSignalSnapshot result{};
    const char* id = nullptr;
    telemetry::SignalValueType type =
        telemetry::SignalValueType::unsigned_integer;
    telemetry::SignalUnit unit = telemetry::SignalUnit::none;
    switch (code) {
        case wireless::TelemetrySignalCode::engine_speed:
            id = "engine.speed";
            unit = telemetry::SignalUnit::milli_revolutions_per_minute;
            break;
        case wireless::TelemetrySignalCode::engine_coolant_temperature:
            id = "engine.coolant_temperature";
            type = telemetry::SignalValueType::signed_integer;
            unit = telemetry::SignalUnit::milli_celsius;
            break;
        case wireless::TelemetrySignalCode::vehicle_speed:
            id = "vehicle.speed";
            unit = telemetry::SignalUnit::millimetres_per_second;
            break;
        case wireless::TelemetrySignalCode::electrical_voltage:
            id = "electrical.system_voltage";
            unit = telemetry::SignalUnit::millivolt;
            break;
    }
    EXPECT(telemetry::make_signal_id(id, result.signal.id) ==
           telemetry::SignalModelError::none);
    result.signal.value = {type, value, true};
    result.signal.unit = unit;
    result.signal.quality = telemetry::SignalQuality::valid;
    result.signal.source.protocol = telemetry::SignalSourceProtocol::synthetic;
    result.signal.received_at_ms = 100;
    result.effective_quality = effective_quality;
    result.age_ms = age_ms;
    return result;
}

wireless::WireTelemetrySignal wire_signal(
    wireless::TelemetrySignalCode code,
    telemetry::SignalQuality quality,
    std::int64_t value,
    std::uint32_t age_ms) {
    auto input = snapshot(code, quality, value, age_ms);
    wireless::WireTelemetrySignal output{};
    EXPECT(wireless::make_wire_telemetry_signal(code, input, output) ==
           wireless::TelemetryPacketError::none);
    return output;
}

wireless::TelemetryBatch sample_batch(std::uint32_t sequence = 7) {
    wireless::TelemetryBatch batch{};
    batch.gateway_id = 0x0102030405060708ULL;
    batch.boot_session_id = 0x11223344U;
    batch.sequence = sequence;
    batch.gateway_uptime_ms = 0x1020304050607080ULL;
    batch.signal_count = 3;
    batch.signals[0] = wire_signal(
        wireless::TelemetrySignalCode::engine_speed,
        telemetry::SignalQuality::valid,
        1500750,
        25);
    batch.signals[1] = wire_signal(
        wireless::TelemetrySignalCode::engine_coolant_temperature,
        telemetry::SignalQuality::suspect,
        -12500,
        40);
    batch.signals[2] = wire_signal(
        wireless::TelemetrySignalCode::electrical_voltage,
        telemetry::SignalQuality::unavailable,
        13800,
        60);
    return batch;
}

void rewrite_crc(std::array<std::uint8_t, wireless::kTelemetryPacketBytes>& data) {
    const auto crc = wireless::telemetry_packet_crc32(data.data(), 92);
    for (std::size_t index = 0; index < 4; ++index) {
        data[92 + index] = static_cast<std::uint8_t>(
            (crc >> (index * 8U)) & 0xFFU);
    }
}

wireless::PeerAddress peer(std::uint8_t suffix) {
    return {{0x02U, 0, 0, 0, 0, suffix}};
}

void start_pair(FakeEspNowTransport& sender, FakeEspNowTransport& receiver) {
    EXPECT(sender.start(peer(1), {6, true}) == wireless::EspNowError::none);
    EXPECT(receiver.start(peer(2), {6, true}) == wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) == wireless::EspNowError::none);
    EXPECT(receiver.add_peer({peer(1), 6, true}) == wireless::EspNowError::none);
    sender.connect(receiver);
}

void test_fixed_layout_round_trip_and_crc() {
    EXPECT(wireless::kTelemetryPacketBytes <=
           wireless::kMaximumEspNowPayloadBytes);
    const std::array<std::uint8_t, 9> crc_check{
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT(wireless::telemetry_packet_crc32(
               crc_check.data(), crc_check.size()) == 0xCBF43926U);

    const auto batch = sample_batch();
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    const auto result = wireless::encode_telemetry_packet(
        batch, encoded.data(), encoded.size());
    EXPECT(result.encoded());
    EXPECT(result.encoded_bytes == encoded.size());
    const std::array<std::uint8_t, wireless::kTelemetryPacketBytes> golden{{
        0x4FU, 0x47U, 0x54U, 0x30U, 0x00U, 0x01U, 0x60U, 0x00U, 0x08U, 0x07U, 0x06U, 0x05U,
        0x04U, 0x03U, 0x02U, 0x01U, 0x44U, 0x33U, 0x22U, 0x11U, 0x07U, 0x00U, 0x00U, 0x00U,
        0x80U, 0x70U, 0x60U, 0x50U, 0x40U, 0x30U, 0x20U, 0x10U, 0x03U, 0x00U, 0x00U, 0x00U,
        0x01U, 0x00U, 0x03U, 0x07U, 0x01U, 0x01U, 0x4EU, 0xE6U, 0x16U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x19U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x02U, 0x02U, 0x02U, 0x01U,
        0x2CU, 0xCFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x28U, 0x00U, 0x00U, 0x00U,
        0x04U, 0x00U, 0x03U, 0x04U, 0x03U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x3CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x5BU, 0x4BU, 0xFBU, 0x6CU,
    }};
    EXPECT(encoded == golden);
    EXPECT(encoded[0] == 'O' && encoded[1] == 'G' &&
           encoded[2] == 'T' && encoded[3] == '0');
    EXPECT(encoded[4] == 0);
    EXPECT(encoded[5] == 1);
    EXPECT(encoded[6] == 96 && encoded[7] == 0);
    EXPECT(encoded[8] == 0x08 && encoded[15] == 0x01);
    EXPECT(encoded[16] == 0x44 && encoded[19] == 0x11);
    EXPECT(encoded[20] == 7 && encoded[23] == 0);
    EXPECT(encoded[32] == 3);
    EXPECT(encoded[36] == 1 && encoded[37] == 0);
    EXPECT(encoded[54] == 2 && encoded[55] == 0);
    EXPECT(encoded[72] == 4 && encoded[73] == 0);

    const auto decoded = wireless::decode_telemetry_packet(
        encoded.data(), encoded.size());
    EXPECT(decoded.decoded());
    EXPECT(decoded.batch.gateway_id == batch.gateway_id);
    EXPECT(decoded.batch.boot_session_id == batch.boot_session_id);
    EXPECT(decoded.batch.sequence == 7);
    EXPECT(decoded.batch.gateway_uptime_ms == batch.gateway_uptime_ms);
    EXPECT(decoded.batch.signal_count == 3);
    EXPECT(decoded.batch.signals[0].value.raw_value == 1500750);
    EXPECT(decoded.batch.signals[1].value.raw_value == -12500);
    EXPECT(!decoded.batch.signals[2].value.present);
    EXPECT(decoded.batch.signals[2].value.raw_value == 0);
}

void test_snapshot_conversion_strips_nonnumeric_quality() {
    const auto stale = snapshot(
        wireless::TelemetrySignalCode::engine_speed,
        telemetry::SignalQuality::stale,
        2000000,
        1000);
    wireless::WireTelemetrySignal output{};
    EXPECT(wireless::make_wire_telemetry_signal(
               wireless::TelemetrySignalCode::engine_speed,
               stale,
               output) == wireless::TelemetryPacketError::none);
    EXPECT(output.quality == telemetry::SignalQuality::stale);
    EXPECT(!output.value.present);
    EXPECT(output.value.raw_value == 0);
    EXPECT(output.source_age_ms == 1000);

    auto wrong_code = stale;
    EXPECT(wireless::make_wire_telemetry_signal(
               wireless::TelemetrySignalCode::vehicle_speed,
               wrong_code,
               output) == wireless::TelemetryPacketError::incompatible_signal);

    auto too_old = stale;
    too_old.age_ms = static_cast<std::uint64_t>(
                         std::numeric_limits<std::uint32_t>::max()) +
                     1U;
    EXPECT(wireless::make_wire_telemetry_signal(
               wireless::TelemetrySignalCode::engine_speed,
               too_old,
               output) ==
           wireless::TelemetryPacketError::source_age_out_of_range);
}

void test_batch_validation_and_output_preservation() {
    auto batch = sample_batch();
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> output{};
    output.fill(0xA5U);
    batch.gateway_id = 0;
    EXPECT(wireless::encode_telemetry_packet(
               batch, output.data(), output.size()).error ==
           wireless::TelemetryPacketError::invalid_identity);
    EXPECT(output[0] == 0xA5U && output.back() == 0xA5U);

    batch = sample_batch();
    batch.signal_count = 0;
    EXPECT(wireless::validate_telemetry_batch(batch) ==
           wireless::TelemetryPacketError::invalid_signal_count);
    batch.signal_count = 3;
    batch.signals[2] = batch.signals[0];
    EXPECT(wireless::validate_telemetry_batch(batch) ==
           wireless::TelemetryPacketError::duplicate_signal);

    batch = sample_batch();
    batch.signals[0].value.present = false;
    EXPECT(wireless::validate_telemetry_batch(batch) ==
           wireless::TelemetryPacketError::inconsistent_value);
    batch = sample_batch();
    batch.signals[0].unit = telemetry::SignalUnit::millivolt;
    EXPECT(wireless::validate_telemetry_batch(batch) ==
           wireless::TelemetryPacketError::incompatible_signal);
    EXPECT(wireless::encode_telemetry_packet(
               sample_batch(), nullptr, output.size()).error ==
           wireless::TelemetryPacketError::invalid_argument);
    EXPECT(wireless::encode_telemetry_packet(
               sample_batch(), output.data(), output.size() - 1).error ==
           wireless::TelemetryPacketError::buffer_too_small);
}

void test_corrupt_incompatible_and_reserved_frames_fail_closed() {
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               sample_batch(), encoded.data(), encoded.size()).encoded());

    auto changed = encoded;
    changed[40] ^= 0x01U;
    EXPECT(wireless::decode_telemetry_packet(
               changed.data(), changed.size()).error ==
           wireless::TelemetryPacketError::integrity_failure);
    EXPECT(wireless::decode_telemetry_packet(
               encoded.data(), encoded.size() - 1).error ==
           wireless::TelemetryPacketError::invalid_length);
    changed = encoded;
    changed[0] = 'X';
    EXPECT(wireless::decode_telemetry_packet(
               changed.data(), changed.size()).error ==
           wireless::TelemetryPacketError::invalid_magic);
    changed = encoded;
    changed[4] = 1;
    EXPECT(wireless::decode_telemetry_packet(
               changed.data(), changed.size()).error ==
           wireless::TelemetryPacketError::unsupported_version);
    changed = encoded;
    changed[5] = 2;
    EXPECT(wireless::decode_telemetry_packet(
               changed.data(), changed.size()).error ==
           wireless::TelemetryPacketError::unsupported_message_type);
    changed = encoded;
    changed[33] = 1;
    rewrite_crc(changed);
    EXPECT(wireless::decode_telemetry_packet(
               changed.data(), changed.size()).error ==
           wireless::TelemetryPacketError::reserved_nonzero);
    changed = encoded;
    changed[41] = 0x02U;
    rewrite_crc(changed);
    EXPECT(wireless::decode_telemetry_packet(
               changed.data(), changed.size()).error ==
           wireless::TelemetryPacketError::reserved_nonzero);
}

void test_unused_entries_and_unknown_codes_are_noncanonical() {
    auto batch = sample_batch();
    batch.signal_count = 1;
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               batch, encoded.data(), encoded.size()).encoded());
    for (std::size_t index = 54; index < 92; ++index) {
        EXPECT(encoded[index] == 0);
    }
    encoded[54] = 1;
    rewrite_crc(encoded);
    EXPECT(wireless::decode_telemetry_packet(
               encoded.data(), encoded.size()).error ==
           wireless::TelemetryPacketError::noncanonical_unused_entry);

    EXPECT(wireless::encode_telemetry_packet(
               sample_batch(), encoded.data(), encoded.size()).encoded());
    encoded[36] = 0xFFU;
    encoded[37] = 0x7FU;
    rewrite_crc(encoded);
    EXPECT(wireless::decode_telemetry_packet(
               encoded.data(), encoded.size()).error ==
           wireless::TelemetryPacketError::unknown_signal_code);
}

void test_sequence_gap_duplicate_wrap_and_session_change() {
    wireless::TelemetryStreamTracker tracker{};
    auto result = tracker.ingest(sample_batch(10), 100);
    EXPECT(result.accepted());
    EXPECT(result.disposition == wireless::TelemetrySequenceDisposition::first);
    result = tracker.ingest(sample_batch(11), 110);
    EXPECT(result.accepted());
    EXPECT(result.disposition ==
           wireless::TelemetrySequenceDisposition::in_order);
    result = tracker.ingest(sample_batch(14), 120);
    EXPECT(result.accepted());
    EXPECT(result.disposition == wireless::TelemetrySequenceDisposition::gap);
    EXPECT(result.missing_packets == 2);
    result = tracker.ingest(sample_batch(14), 130);
    EXPECT(!result.accepted());
    EXPECT(result.disposition ==
           wireless::TelemetrySequenceDisposition::duplicate);
    result = tracker.ingest(sample_batch(13), 140);
    EXPECT(!result.accepted());
    EXPECT(result.disposition ==
           wireless::TelemetrySequenceDisposition::out_of_order);

    auto restarted = sample_batch(0);
    restarted.boot_session_id += 1;
    result = tracker.ingest(restarted, 150);
    EXPECT(result.accepted());
    EXPECT(result.disposition ==
           wireless::TelemetrySequenceDisposition::session_changed);
    EXPECT(tracker.last_sequence() == 0);

    tracker.reset();
    EXPECT(!tracker.initialized());
    EXPECT(tracker.ingest(
               sample_batch(std::numeric_limits<std::uint32_t>::max()),
               200).accepted());
    result = tracker.ingest(sample_batch(0), 201);
    EXPECT(result.accepted());
    EXPECT(result.disposition ==
           wireless::TelemetrySequenceDisposition::in_order);
    EXPECT(tracker.ingest(sample_batch(1), 199).error ==
           wireless::TelemetryStreamError::clock_regressed);

    auto other_gateway = sample_batch(1);
    other_gateway.gateway_id += 1;
    EXPECT(tracker.ingest(other_gateway, 202).error ==
           wireless::TelemetryStreamError::gateway_mismatch);
}

void test_receiver_local_freshness_strips_expired_value() {
    const auto signal = wire_signal(
        wireless::TelemetrySignalCode::engine_speed,
        telemetry::SignalQuality::valid,
        1000000,
        250);
    auto result = wireless::evaluate_wire_signal_freshness(
        signal, 1000, 1749, 1000);
    EXPECT(result.evaluated());
    EXPECT(result.age_ms == 999);
    EXPECT(result.effective_quality == telemetry::SignalQuality::valid);
    EXPECT(result.display_value.present);
    result = wireless::evaluate_wire_signal_freshness(
        signal, 1000, 1750, 1000);
    EXPECT(result.evaluated());
    EXPECT(result.age_ms == 1000);
    EXPECT(result.effective_quality == telemetry::SignalQuality::stale);
    EXPECT(!result.display_value.present);
    EXPECT(result.display_value.raw_value == 0);
    EXPECT(wireless::evaluate_wire_signal_freshness(
               signal, 1000, 999, 1000).error ==
           wireless::TelemetryFreshnessError::clock_regressed);
    EXPECT(wireless::evaluate_wire_signal_freshness(
               signal, 1000, 1000, 0).error ==
           wireless::TelemetryFreshnessError::invalid_stale_threshold);
}

void test_fake_transport_delivery_loss_and_gap_integration() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport receiver{};
    start_pair(sender, receiver);
    wireless::TelemetryStreamTracker tracker{};

    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               sample_batch(1), encoded.data(), encoded.size()).encoded());
    EXPECT(sender.send(
               peer(2), {encoded.data(), encoded.size()}, 100).accepted());
    sender.service(100);
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> received{};
    auto receive_result = receiver.receive(
        {received.data(), received.size()});
    EXPECT(receive_result.has_frame());
    auto decoded = wireless::decode_telemetry_packet(
        received.data(), receive_result.received_bytes);
    EXPECT(decoded.decoded());
    EXPECT(tracker.ingest(
               decoded.batch,
               receive_result.metadata.received_at_ms).accepted());

    sender.drop_next_transmissions(1);
    EXPECT(wireless::encode_telemetry_packet(
               sample_batch(2), encoded.data(), encoded.size()).encoded());
    EXPECT(sender.send(
               peer(2), {encoded.data(), encoded.size()}, 200).accepted());
    sender.service(200);
    EXPECT(receiver.receive({received.data(), received.size()}).error ==
           wireless::EspNowError::no_data);
    EXPECT(sender.poll_delivery().has_receipt());
    const auto lost = sender.poll_delivery();
    EXPECT(lost.has_receipt());
    EXPECT(lost.receipt.outcome == wireless::DeliveryOutcome::injected_loss);

    EXPECT(wireless::encode_telemetry_packet(
               sample_batch(3), encoded.data(), encoded.size()).encoded());
    EXPECT(sender.send(
               peer(2), {encoded.data(), encoded.size()}, 300).accepted());
    sender.service(300);
    receive_result = receiver.receive({received.data(), received.size()});
    EXPECT(receive_result.has_frame());
    decoded = wireless::decode_telemetry_packet(
        received.data(), receive_result.received_bytes);
    const auto gap = tracker.ingest(
        decoded.batch, receive_result.metadata.received_at_ms);
    EXPECT(gap.accepted());
    EXPECT(gap.disposition == wireless::TelemetrySequenceDisposition::gap);
    EXPECT(gap.missing_packets == 1);
}

}  // namespace

int main() {
    test_fixed_layout_round_trip_and_crc();
    test_snapshot_conversion_strips_nonnumeric_quality();
    test_batch_validation_and_output_preservation();
    test_corrupt_incompatible_and_reserved_frames_fail_closed();
    test_unused_entries_and_unknown_codes_are_noncanonical();
    test_sequence_gap_duplicate_wrap_and_session_change();
    test_receiver_local_freshness_strips_expired_value();
    test_fake_transport_delivery_loss_and_gap_integration();

    if (failures != 0) {
        std::cerr << failures << " telemetry packet assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 telemetry packet scenario groups\n";
    return EXIT_SUCCESS;
}
