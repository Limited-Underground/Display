#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_can_receiver.hpp"
#include "opengauge/j1939_decoder.hpp"
#include "opengauge/telemetry_cache.hpp"

namespace {

using namespace opengauge;
using can::test_support::FakeCanReceiver;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

can::CanFrame data_frame(
    std::uint32_t identifier,
    can::CanFrameFormat format,
    std::uint64_t received_at_ms,
    std::uint8_t data_length = 8) {
    can::CanFrame frame{};
    frame.identifier = identifier;
    frame.format = format;
    frame.kind = can::CanFrameKind::data;
    frame.data_length = data_length;
    frame.received_at_ms = received_at_ms;
    for (std::size_t index = 0; index < data_length; ++index) {
        frame.data[index] = static_cast<std::uint8_t>(index + 1U);
    }
    return frame;
}

can::CanListenPolicy j1939_policy() {
    return {250000, false, true, false};
}

void test_listen_only_lifecycle_and_policy() {
    FakeCanReceiver receiver{};
    EXPECT(receiver.status().mode == can::CanReceiverMode::offline);
    EXPECT(receiver.receive().error == can::CanReceiverError::invalid_state);
    EXPECT(receiver.start_listen_only({123456, false, true, false}) ==
           can::CanReceiverError::invalid_policy);
    EXPECT(receiver.start_listen_only({250000, false, false, false}) ==
           can::CanReceiverError::invalid_policy);
    EXPECT(receiver.start_listen_only(j1939_policy()) ==
           can::CanReceiverError::none);
    const auto status = receiver.status();
    EXPECT(status.mode == can::CanReceiverMode::listen_only);
    EXPECT(status.bitrate == 250000);
    EXPECT(status.queue_capacity == FakeCanReceiver::kQueueCapacity);
    EXPECT(receiver.start_listen_only(j1939_policy()) ==
           can::CanReceiverError::invalid_state);
    receiver.stop();
    EXPECT(receiver.status().mode == can::CanReceiverMode::offline);
    EXPECT(receiver.receive().error == can::CanReceiverError::invalid_state);
}

void test_frame_validation_is_classical_and_canonical() {
    auto frame = data_frame(
        can::kMaximumStandardCanIdentifier,
        can::CanFrameFormat::standard,
        0,
        1);
    EXPECT(can::validate_can_frame(frame) == can::CanReceiverError::none);
    frame.identifier += 1;
    EXPECT(can::validate_can_frame(frame) ==
           can::CanReceiverError::invalid_frame);
    frame = data_frame(
        can::kMaximumExtendedCanIdentifier,
        can::CanFrameFormat::extended,
        0,
        8);
    EXPECT(can::validate_can_frame(frame) == can::CanReceiverError::none);
    frame.identifier += 1;
    EXPECT(can::validate_can_frame(frame) ==
           can::CanReceiverError::invalid_frame);
    frame = data_frame(1, can::CanFrameFormat::standard, 0, 8);
    frame.data_length = 9;
    EXPECT(can::validate_can_frame(frame) ==
           can::CanReceiverError::invalid_frame);
    frame = data_frame(1, can::CanFrameFormat::standard, 0, 1);
    frame.data[7] = 1;
    EXPECT(can::validate_can_frame(frame) ==
           can::CanReceiverError::invalid_frame);
    frame = {};
    frame.identifier = 1;
    frame.kind = can::CanFrameKind::remote;
    frame.data_length = 2;
    EXPECT(can::validate_can_frame(frame) == can::CanReceiverError::none);
    frame.data[0] = 1;
    EXPECT(can::validate_can_frame(frame) ==
           can::CanReceiverError::invalid_frame);
    frame = {};
    frame.format = static_cast<can::CanFrameFormat>(9);
    EXPECT(can::validate_can_frame(frame) ==
           can::CanReceiverError::invalid_frame);
    frame = {};
    frame.kind = static_cast<can::CanFrameKind>(9);
    EXPECT(can::validate_can_frame(frame) ==
           can::CanReceiverError::invalid_frame);
}

void test_format_and_remote_filters_are_counted() {
    FakeCanReceiver receiver{};
    EXPECT(receiver.start_listen_only(j1939_policy()) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(data_frame(
               1, can::CanFrameFormat::standard, 0)) ==
           can::CanReceiverError::filtered);
    auto remote = data_frame(1, can::CanFrameFormat::extended, 1, 0);
    remote.kind = can::CanFrameKind::remote;
    EXPECT(receiver.inject(remote) == can::CanReceiverError::filtered);
    EXPECT(receiver.inject(data_frame(
               0x18FF0001U, can::CanFrameFormat::extended, 2)) ==
           can::CanReceiverError::none);
    EXPECT(receiver.status().frames_filtered == 2);
    EXPECT(receiver.status().frames_received == 1);
    EXPECT(receiver.receive().has_frame());

    receiver.stop();
    EXPECT(receiver.start_listen_only({500000, true, true, true}) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(remote) == can::CanReceiverError::none);
    EXPECT(receiver.receive().frame.kind == can::CanFrameKind::remote);
}

void test_fifo_timestamps_bus_metadata_and_clock_regression() {
    FakeCanReceiver receiver{};
    EXPECT(receiver.start_listen_only(j1939_policy()) ==
           can::CanReceiverError::none);
    EXPECT(receiver.set_bus_state(can::CanBusState::error_warning) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(data_frame(
               0x18FF0001U, can::CanFrameFormat::extended, 10)) ==
           can::CanReceiverError::none);
    EXPECT(receiver.set_bus_state(can::CanBusState::error_passive) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(data_frame(
               0x18FF0002U, can::CanFrameFormat::extended, 11)) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(data_frame(
               0x18FF0003U, can::CanFrameFormat::extended, 10)) ==
           can::CanReceiverError::clock_regressed);
    auto received = receiver.receive();
    EXPECT(received.has_frame());
    EXPECT(received.frame.identifier == 0x18FF0001U);
    EXPECT(received.metadata.bus_state == can::CanBusState::error_warning);
    EXPECT(received.metadata.queue_depth_after_receive == 1);
    received = receiver.receive();
    EXPECT(received.frame.identifier == 0x18FF0002U);
    EXPECT(received.metadata.bus_state == can::CanBusState::error_passive);
    EXPECT(received.metadata.queue_depth_after_receive == 0);
}

void test_queue_overflow_drops_newest_and_reports_cumulative_count() {
    FakeCanReceiver receiver{};
    EXPECT(receiver.start_listen_only(j1939_policy()) ==
           can::CanReceiverError::none);
    for (std::size_t index = 0;
         index < FakeCanReceiver::kQueueCapacity;
         ++index) {
        EXPECT(receiver.inject(data_frame(
                   0x18FF0000U + static_cast<std::uint32_t>(index),
                   can::CanFrameFormat::extended,
                   index)) == can::CanReceiverError::none);
    }
    EXPECT(receiver.inject(data_frame(
               0x18FF00FFU,
               can::CanFrameFormat::extended,
               FakeCanReceiver::kQueueCapacity)) ==
           can::CanReceiverError::queue_full);
    EXPECT(receiver.status().frames_dropped_overflow == 1);
    EXPECT(receiver.status().queue_depth == FakeCanReceiver::kQueueCapacity);
    const auto first = receiver.receive();
    EXPECT(first.has_frame());
    EXPECT(first.frame.identifier == 0x18FF0000U);
    EXPECT(first.metadata.overflow_count == 1);
    EXPECT(first.metadata.queue_depth_after_receive ==
           FakeCanReceiver::kQueueCapacity - 1);
}

void test_bus_off_recovery_and_hardware_failure() {
    FakeCanReceiver receiver{};
    EXPECT(receiver.start_listen_only(j1939_policy()) ==
           can::CanReceiverError::none);
    EXPECT(receiver.set_bus_state(can::CanBusState::bus_off) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(data_frame(
               0x18FF0001U, can::CanFrameFormat::extended, 0)) ==
           can::CanReceiverError::bus_off);
    EXPECT(receiver.set_bus_state(can::CanBusState::error_active) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(data_frame(
               0x18FF0001U, can::CanFrameFormat::extended, 1)) ==
           can::CanReceiverError::none);
    EXPECT(receiver.status().bus_state_changes == 2);
    receiver.fail_hardware(true);
    EXPECT(receiver.receive().error == can::CanReceiverError::hardware_failure);
    EXPECT(receiver.inject(data_frame(
               0x18FF0002U, can::CanFrameFormat::extended, 2)) ==
           can::CanReceiverError::hardware_failure);
    receiver.fail_hardware(false);
    EXPECT(receiver.receive().has_frame());
}

void test_stop_and_restart_clear_queue_and_counters() {
    FakeCanReceiver receiver{};
    EXPECT(receiver.start_listen_only(j1939_policy()) ==
           can::CanReceiverError::none);
    EXPECT(receiver.inject(data_frame(
               0x18FF0001U, can::CanFrameFormat::extended, 0)) ==
           can::CanReceiverError::none);
    receiver.stop();
    EXPECT(receiver.status().queue_depth == 0);
    EXPECT(receiver.start_listen_only({500000, false, true, false}) ==
           can::CanReceiverError::none);
    EXPECT(receiver.status().bitrate == 500000);
    EXPECT(receiver.status().frames_received == 0);
    EXPECT(receiver.receive().error == can::CanReceiverError::no_frame);
}

void test_received_eec1_frame_decodes_and_enters_cache() {
    FakeCanReceiver receiver{};
    EXPECT(receiver.start_listen_only(j1939_policy()) ==
           can::CanReceiverError::none);
    auto frame = data_frame(
        0x0CF0042AU, can::CanFrameFormat::extended, 100, 8);
    frame.data.fill(0xFFU);
    frame.data[3] = 0x40U;
    frame.data[4] = 0x1FU;
    EXPECT(receiver.inject(frame) == can::CanReceiverError::none);
    const auto received = receiver.receive();
    EXPECT(received.has_frame());

    can::J1939Message message{};
    message.raw_identifier = received.frame.identifier;
    message.format = received.frame.format;
    message.payload = received.frame.data;
    message.data_length = received.frame.data_length;
    message.received_at_ms = received.frame.received_at_ms;
    can::J1939DecoderRegistry registry{};
    EXPECT(registry.register_decoder(
               can::kEec1Pgn,
               can::decode_eec1_engine_speed) == can::J1939DecodeError::none);
    telemetry::NormalizedSignal output{};
    EXPECT(registry.decode(message, &output, 1).decoded());
    EXPECT(output.value.raw_value == 1000000);
    telemetry::TelemetryCache cache{};
    EXPECT(cache.upsert(output, 500).accepted());
    const auto cached = cache.read("engine.speed", 100);
    EXPECT(cached.found());
    EXPECT(cached.snapshot.signal.value.raw_value == 1000000);
}

}  // namespace

int main() {
    test_listen_only_lifecycle_and_policy();
    test_frame_validation_is_classical_and_canonical();
    test_format_and_remote_filters_are_counted();
    test_fifo_timestamps_bus_metadata_and_clock_regression();
    test_queue_overflow_drops_newest_and_reports_cumulative_count();
    test_bus_off_recovery_and_hardware_failure();
    test_stop_and_restart_clear_queue_and_counters();
    test_received_eec1_frame_decodes_and_enters_cache();

    if (failures != 0) {
        std::cerr << failures << " CAN receiver assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 CAN receiver scenario groups\n";
    return EXIT_SUCCESS;
}
