#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opengauge/critical_alert_outbox.hpp"
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

CriticalAlertOutboxConfiguration outbox_configuration() {
    return {50, 100, 25, 500, 2, 1};
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

void test_queued_retry_survives_restart() {
    CriticalAlertOutbox original{};
    EXPECT(original.start(outbox_configuration()) == CriticalOutboxError::none);
    EXPECT(original.enqueue(frame(11), 10) == CriticalOutboxError::none);
    const auto prepared = original.prepare(10);
    EXPECT(prepared.prepared());
    EXPECT(original.commit_local_send(prepared.token, false, 10) ==
           CriticalOutboxError::none);

    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    EXPECT(original.export_checkpoint(20, encoded) ==
           CriticalOutboxError::none);

    CriticalAlertOutbox restored{};
    EXPECT(restored.start(outbox_configuration()) == CriticalOutboxError::none);
    EXPECT(restored.import_checkpoint(
               encoded.data(), encoded.size(), 1000) ==
           CriticalOutboxError::none);
    EXPECT(restored.status().queued_count == 1);
    EXPECT(restored.prepare(1014).error ==
           CriticalOutboxError::no_frame_ready);
    EXPECT(restored.prepare(1015).event_id == 11);
}

void test_in_flight_ack_timeout_survives_restart() {
    CriticalAlertOutbox original{};
    EXPECT(original.start(outbox_configuration()) == CriticalOutboxError::none);
    EXPECT(original.enqueue(frame(12), 10) == CriticalOutboxError::none);
    const auto prepared = original.prepare(10);
    EXPECT(original.commit_local_send(prepared.token, true, 10) ==
           CriticalOutboxError::none);

    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    EXPECT(original.export_checkpoint(50, encoded) ==
           CriticalOutboxError::none);

    CriticalAlertOutbox restored{};
    EXPECT(restored.start(outbox_configuration()) == CriticalOutboxError::none);
    EXPECT(restored.import_checkpoint(
               encoded.data(), encoded.size(), 1000) ==
           CriticalOutboxError::none);
    EXPECT(restored.status().in_flight_count == 1);
    EXPECT(restored.advance(1059).retries_released == 0);
    EXPECT(restored.advance(1060).retries_released == 1);
    EXPECT(restored.prepare(1084).error ==
           CriticalOutboxError::no_frame_ready);
    EXPECT(restored.prepare(1085).event_id == 12);
}

void test_maximum_lifetime_survives_restart() {
    CriticalAlertOutbox original{};
    EXPECT(original.start(outbox_configuration()) == CriticalOutboxError::none);
    EXPECT(original.enqueue(frame(13), 10) == CriticalOutboxError::none);
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    EXPECT(original.export_checkpoint(400, encoded) ==
           CriticalOutboxError::none);

    CriticalAlertOutbox restored{};
    EXPECT(restored.start(outbox_configuration()) == CriticalOutboxError::none);
    EXPECT(restored.import_checkpoint(
               encoded.data(), encoded.size(), 1000) ==
           CriticalOutboxError::none);
    EXPECT(restored.advance(1109).failure_count == 0);
    const auto expired = restored.advance(1110);
    EXPECT(expired.failure_count == 1);
    EXPECT(expired.failures[0].event_id == 13);
    EXPECT(expired.failures[0].reason ==
           CriticalDeliveryFailure::maximum_lifetime);
}

void test_checkpoint_import_is_atomic_and_boot_only() {
    CriticalAlertOutbox original{};
    EXPECT(original.start(outbox_configuration()) == CriticalOutboxError::none);
    EXPECT(original.enqueue(frame(14), 0) == CriticalOutboxError::none);
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> encoded{};
    EXPECT(original.export_checkpoint(0, encoded) ==
           CriticalOutboxError::none);

    auto incompatible_configuration = outbox_configuration();
    ++incompatible_configuration.retry_backoff_ms;
    CriticalAlertOutbox restored{};
    EXPECT(restored.start(incompatible_configuration) ==
           CriticalOutboxError::none);
    EXPECT(restored.import_checkpoint(
               encoded.data(), encoded.size(), 100) ==
           CriticalOutboxError::checkpoint_incompatible);
    EXPECT(restored.status().queued_count == 0);
    auto corrupt = encoded;
    corrupt[20] ^= 1;
    EXPECT(restored.import_checkpoint(
               corrupt.data(), corrupt.size(), 100) ==
           CriticalOutboxError::checkpoint_rejected);
    EXPECT(restored.status().queued_count == 0);
    restored.stop();
    EXPECT(restored.start(outbox_configuration()) == CriticalOutboxError::none);
    EXPECT(restored.import_checkpoint(
               encoded.data(), encoded.size(), 100) ==
           CriticalOutboxError::none);
    EXPECT(restored.import_checkpoint(
               encoded.data(), encoded.size(), 100) ==
           CriticalOutboxError::invalid_state);
}

void test_prepared_and_unrepresentable_exports_are_refused() {
    CriticalAlertOutbox prepared_outbox{};
    EXPECT(prepared_outbox.start(outbox_configuration()) ==
           CriticalOutboxError::none);
    EXPECT(prepared_outbox.enqueue(frame(15), 0) == CriticalOutboxError::none);
    EXPECT(prepared_outbox.prepare(0).prepared());
    std::array<std::uint8_t, kCriticalAlertOutboxCheckpointBytes> output{};
    output.fill(0xA5);
    const auto unchanged = output;
    EXPECT(prepared_outbox.export_checkpoint(0, output) ==
           CriticalOutboxError::invalid_state);
    EXPECT(output == unchanged);

    auto oversized = outbox_configuration();
    oversized.maximum_lifetime_ms =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) +
        1U;
    CriticalAlertOutbox oversized_outbox{};
    EXPECT(oversized_outbox.start(oversized) == CriticalOutboxError::none);
    EXPECT(oversized_outbox.export_checkpoint(0, output) ==
           CriticalOutboxError::checkpoint_incompatible);
    EXPECT(output == unchanged);
}

void test_configuration_fingerprint_is_canonical_and_sensitive() {
    const auto baseline = outbox_configuration();
    const auto expected =
        critical_alert_outbox_configuration_fingerprint(baseline);
    EXPECT(expected != 0);
    EXPECT(critical_alert_outbox_configuration_fingerprint(baseline) ==
           expected);
    for (std::size_t field = 0; field < 6; ++field) {
        auto changed = baseline;
        switch (field) {
            case 0: ++changed.local_commit_timeout_ms; break;
            case 1: ++changed.acknowledgement_timeout_ms; break;
            case 2: ++changed.retry_backoff_ms; break;
            case 3: ++changed.maximum_lifetime_ms; break;
            case 4: ++changed.maximum_attempts; break;
            case 5: ++changed.emergency_reserve; break;
        }
        EXPECT(critical_alert_outbox_configuration_fingerprint(changed) !=
               expected);
    }
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
    test_queued_retry_survives_restart();
    test_in_flight_ack_timeout_survives_restart();
    test_maximum_lifetime_survives_restart();
    test_checkpoint_import_is_atomic_and_boot_only();
    test_prepared_and_unrepresentable_exports_are_refused();
    test_configuration_fingerprint_is_canonical_and_sensitive();
    if (failures != 0) {
        std::cerr << failures << " outbox checkpoint assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 critical alert outbox checkpoint scenario groups\n";
    return EXIT_SUCCESS;
}
