#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/critical_alert_outbox_checkpoint.hpp"

namespace {

using namespace opengauge::integration;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

std::array<std::uint8_t, kCriticalAlertFrameBytes> frame(
    std::uint64_t event_id) {
    CriticalAlert alert{};
    alert.type = CriticalAlertType::engine_over_temperature;
    alert.severity = AlertSeverity::critical;
    alert.state = AlertState::asserted;
    alert.quality = AlertQuality::valid;
    alert.unit = AlertUnit::celsius;
    alert.producer_id = 1;
    alert.vehicle_id = 2;
    alert.event_id = event_id;
    alert.condition_id = 1000 + event_id;
    alert.age_ms = 10;
    alert.value_present = true;
    alert.value_milli = 110000;
    std::array<std::uint8_t, kCriticalAlertFrameBytes> output{};
    EXPECT(encode_critical_alert(alert, output).encoded());
    return output;
}

CriticalAlertOutboxCheckpointEntry entry(
    std::uint64_t event_id,
    CriticalAlertOutboxCheckpointState state,
    std::uint8_t attempts,
    std::uint32_t lifetime,
    std::uint32_t action) {
    CriticalAlertOutboxCheckpointEntry value{};
    value.active = true;
    value.state = state;
    value.attempts = attempts;
    value.remaining_lifetime_ms = lifetime;
    value.remaining_action_ms = action;
    value.frame = frame(event_id);
    return value;
}

CriticalAlertOutboxCheckpoint checkpoint() {
    CriticalAlertOutboxCheckpoint value{};
    value.configuration_fingerprint = 0x12345678U;
    value.entries[0] = entry(
        1, CriticalAlertOutboxCheckpointState::queued, 1, 400, 25);
    value.entries[3] = entry(
        2, CriticalAlertOutboxCheckpointState::in_flight, 2, 300, 100);
    return value;
}

void refresh_crc(
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes>& encoded) {
    const auto offset = encoded.size() - 4;
    const auto crc = critical_alert_outbox_checkpoint_crc32(
        encoded.data(), offset);
    encoded[offset] = static_cast<std::uint8_t>(crc);
    encoded[offset + 1] = static_cast<std::uint8_t>(crc >> 8U);
    encoded[offset + 2] = static_cast<std::uint8_t>(crc >> 16U);
    encoded[offset + 3] = static_cast<std::uint8_t>(crc >> 24U);
}

void test_round_trip_and_determinism() {
    const auto value = checkpoint();
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> first{};
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> second{};
    EXPECT(encode_critical_alert_outbox_checkpoint(value, first) ==
           CriticalAlertOutboxCheckpointError::none);
    EXPECT(encode_critical_alert_outbox_checkpoint(value, second) ==
           CriticalAlertOutboxCheckpointError::none);
    EXPECT(first == second);
    EXPECT(first[0] == 'O' && first[1] == 'O' && first[2] == 'C' &&
           first[3] == '0' && first[5] == 2);

    CriticalAlertOutboxCheckpoint decoded{};
    EXPECT(decode_critical_alert_outbox_checkpoint(
               first.data(), first.size(), decoded) ==
           CriticalAlertOutboxCheckpointError::none);
    EXPECT(decoded.configuration_fingerprint == value.configuration_fingerprint);
    EXPECT(decoded.entries[0].active);
    EXPECT(decoded.entries[0].remaining_action_ms == 25);
    EXPECT(decoded.entries[3].attempts == 2);
    EXPECT(decoded.entries[3].frame == value.entries[3].frame);
}

void test_empty_checkpoint_is_canonical() {
    CriticalAlertOutboxCheckpoint value{};
    value.configuration_fingerprint = 9;
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    EXPECT(encode_critical_alert_outbox_checkpoint(value, encoded) ==
           CriticalAlertOutboxCheckpointError::none);
    EXPECT(encoded[5] == 0);
    for (std::size_t index = 16; index < encoded.size() - 4; ++index) {
        EXPECT(encoded[index] == 0);
    }
}

void test_configuration_and_entry_invariants() {
    auto value = checkpoint();
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    value.configuration_fingerprint = 0;
    EXPECT(encode_critical_alert_outbox_checkpoint(value, encoded) ==
           CriticalAlertOutboxCheckpointError::invalid_configuration);

    value = checkpoint();
    value.entries[0].remaining_lifetime_ms = 0;
    EXPECT(encode_critical_alert_outbox_checkpoint(value, encoded) ==
           CriticalAlertOutboxCheckpointError::invalid_entry);
    value = checkpoint();
    value.entries[0].remaining_action_ms = 401;
    EXPECT(encode_critical_alert_outbox_checkpoint(value, encoded) ==
           CriticalAlertOutboxCheckpointError::invalid_entry);
    value = checkpoint();
    value.entries[3].attempts = 0;
    EXPECT(encode_critical_alert_outbox_checkpoint(value, encoded) ==
           CriticalAlertOutboxCheckpointError::invalid_entry);
    value = checkpoint();
    value.entries[0].state =
        static_cast<CriticalAlertOutboxCheckpointState>(9);
    EXPECT(encode_critical_alert_outbox_checkpoint(value, encoded) ==
           CriticalAlertOutboxCheckpointError::invalid_entry);
}

void test_frame_and_event_uniqueness() {
    auto value = checkpoint();
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    value.entries[0].frame[0] = 'X';
    EXPECT(encode_critical_alert_outbox_checkpoint(value, encoded) ==
           CriticalAlertOutboxCheckpointError::invalid_entry);

    value = checkpoint();
    value.entries[3].frame = value.entries[0].frame;
    EXPECT(encode_critical_alert_outbox_checkpoint(value, encoded) ==
           CriticalAlertOutboxCheckpointError::duplicate_event);
}

void test_decode_rejects_shape_version_integrity_and_padding() {
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    EXPECT(encode_critical_alert_outbox_checkpoint(checkpoint(), encoded) ==
           CriticalAlertOutboxCheckpointError::none);
    CriticalAlertOutboxCheckpoint output{};
    EXPECT(decode_critical_alert_outbox_checkpoint(
               nullptr, encoded.size(), output) ==
           CriticalAlertOutboxCheckpointError::invalid_argument);
    EXPECT(decode_critical_alert_outbox_checkpoint(
               encoded.data(), encoded.size() - 1, output) ==
           CriticalAlertOutboxCheckpointError::invalid_argument);

    auto corrupt = encoded;
    corrupt[0] = 'X';
    EXPECT(decode_critical_alert_outbox_checkpoint(
               corrupt.data(), corrupt.size(), output) ==
           CriticalAlertOutboxCheckpointError::malformed);
    corrupt = encoded;
    corrupt[4] = 1;
    EXPECT(decode_critical_alert_outbox_checkpoint(
               corrupt.data(), corrupt.size(), output) ==
           CriticalAlertOutboxCheckpointError::unsupported_version);
    corrupt = encoded;
    corrupt[20] ^= 1;
    EXPECT(decode_critical_alert_outbox_checkpoint(
               corrupt.data(), corrupt.size(), output) ==
           CriticalAlertOutboxCheckpointError::integrity_failure);
    corrupt = encoded;
    corrupt[12] = 1;
    refresh_crc(corrupt);
    EXPECT(decode_critical_alert_outbox_checkpoint(
               corrupt.data(), corrupt.size(), output) ==
           CriticalAlertOutboxCheckpointError::malformed);
}

void test_decode_is_atomic_and_checks_count() {
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    EXPECT(encode_critical_alert_outbox_checkpoint(checkpoint(), encoded) ==
           CriticalAlertOutboxCheckpointError::none);
    CriticalAlertOutboxCheckpoint output{};
    output.configuration_fingerprint = 77;
    auto corrupt = encoded;
    corrupt[5] = 1;
    refresh_crc(corrupt);
    EXPECT(decode_critical_alert_outbox_checkpoint(
               corrupt.data(), corrupt.size(), output) ==
           CriticalAlertOutboxCheckpointError::malformed);
    EXPECT(output.configuration_fingerprint == 77);
}

void test_inactive_slot_must_be_zero() {
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    EXPECT(encode_critical_alert_outbox_checkpoint(checkpoint(), encoded) ==
           CriticalAlertOutboxCheckpointError::none);
    encoded[16 + 76] = 0;
    encoded[16 + 76 + 1] = 1;
    refresh_crc(encoded);
    CriticalAlertOutboxCheckpoint output{};
    EXPECT(decode_critical_alert_outbox_checkpoint(
               encoded.data(), encoded.size(), output) ==
           CriticalAlertOutboxCheckpointError::malformed);
}

}  // namespace

int main() {
    test_round_trip_and_determinism();
    test_empty_checkpoint_is_canonical();
    test_configuration_and_entry_invariants();
    test_frame_and_event_uniqueness();
    test_decode_rejects_shape_version_integrity_and_padding();
    test_decode_is_atomic_and_checks_count();
    test_inactive_slot_must_be_zero();
    if (failures != 0) {
        std::cerr << failures << " outbox checkpoint assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 critical alert outbox checkpoint scenario groups\n";
    return EXIT_SUCCESS;
}
