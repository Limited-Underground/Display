#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/critical_alert_recovery_checkpoint.hpp"

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

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
}

CriticalAlertRecoveryCheckpoint checkpoint() {
    CriticalAlertRecoveryCheckpoint value{};
    value.generation = 0x0102030405060708ULL;
    value.ack[0] = 'O'; value.ack[1] = 'A';
    value.ack[2] = 'I'; value.ack[3] = '0';
    value.ack[8] = 1;
    write_u32(
        value.ack.data() + value.ack.size() - 4,
        critical_alert_recovery_checkpoint_crc32(
            value.ack.data(), value.ack.size() - 4));
    CriticalAlertOutboxCheckpoint outbox{};
    outbox.configuration_fingerprint = 1;
    EXPECT(encode_critical_alert_outbox_checkpoint(outbox, value.outbox) ==
           CriticalAlertOutboxCheckpointError::none);
    return value;
}

void repair_outer_crc(
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes>& value) {
    write_u32(
        value.data() + value.size() - 4,
        critical_alert_recovery_checkpoint_crc32(
            value.data(), value.size() - 4));
}

void test_round_trip_layout_and_determinism() {
    const auto value = checkpoint();
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> first{};
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> second{};
    EXPECT(encode_critical_alert_recovery_checkpoint(value, first) ==
           CriticalAlertRecoveryCheckpointError::none);
    EXPECT(encode_critical_alert_recovery_checkpoint(value, second) ==
           CriticalAlertRecoveryCheckpointError::none);
    EXPECT(first == second);
    EXPECT(first[0] == 'O' && first[1] == 'C' && first[2] == 'R' &&
           first[3] == '0' && first[5] == 24);
    EXPECT(first[6] == 0x18 && first[7] == 0x01);
    EXPECT(first[16] == 0x80 && first[17] == 0x02);
    EXPECT(first[24] == 'O' && first[25] == 'A');
    EXPECT(first[304] == 'O' && first[305] == 'O');
    CriticalAlertRecoveryCheckpoint decoded{};
    EXPECT(decode_critical_alert_recovery_checkpoint(
               first.data(), first.size(), decoded) ==
           CriticalAlertRecoveryCheckpointError::none);
    EXPECT(decoded.generation == value.generation);
    EXPECT(decoded.ack == value.ack && decoded.outbox == value.outbox);
}

void test_generation_and_nested_inputs_fail_before_output() {
    auto value = checkpoint();
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> output{};
    output.fill(0xA5);
    const auto unchanged = output;
    value.generation = 0;
    EXPECT(encode_critical_alert_recovery_checkpoint(value, output) ==
           CriticalAlertRecoveryCheckpointError::invalid_generation);
    EXPECT(output == unchanged);
    value = checkpoint(); value.ack[20] ^= 1;
    EXPECT(encode_critical_alert_recovery_checkpoint(value, output) ==
           CriticalAlertRecoveryCheckpointError::invalid_ack_checkpoint);
    value = checkpoint(); value.outbox[20] ^= 1;
    EXPECT(encode_critical_alert_recovery_checkpoint(value, output) ==
           CriticalAlertRecoveryCheckpointError::invalid_outbox_checkpoint);
    EXPECT(output == unchanged);
}

void test_outer_shape_version_integrity_and_padding() {
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> encoded{};
    EXPECT(encode_critical_alert_recovery_checkpoint(checkpoint(), encoded) ==
           CriticalAlertRecoveryCheckpointError::none);
    CriticalAlertRecoveryCheckpoint decoded{};
    EXPECT(decode_critical_alert_recovery_checkpoint(
               nullptr, encoded.size(), decoded) ==
           CriticalAlertRecoveryCheckpointError::invalid_argument);
    auto changed = encoded; changed[4] = 1;
    EXPECT(decode_critical_alert_recovery_checkpoint(
               changed.data(), changed.size(), decoded) ==
           CriticalAlertRecoveryCheckpointError::unsupported_version);
    changed = encoded; changed[100] ^= 1;
    EXPECT(decode_critical_alert_recovery_checkpoint(
               changed.data(), changed.size(), decoded) ==
           CriticalAlertRecoveryCheckpointError::integrity_failure);
    changed = encoded; changed[18] = 1; repair_outer_crc(changed);
    EXPECT(decode_critical_alert_recovery_checkpoint(
               changed.data(), changed.size(), decoded) ==
           CriticalAlertRecoveryCheckpointError::malformed);
}

void test_nested_tamper_and_decode_are_atomic() {
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> encoded{};
    EXPECT(encode_critical_alert_recovery_checkpoint(checkpoint(), encoded) ==
           CriticalAlertRecoveryCheckpointError::none);
    CriticalAlertRecoveryCheckpoint decoded{};
    decoded.generation = 77;
    encoded[24 + 20] ^= 1; repair_outer_crc(encoded);
    EXPECT(decode_critical_alert_recovery_checkpoint(
               encoded.data(), encoded.size(), decoded) ==
           CriticalAlertRecoveryCheckpointError::invalid_ack_checkpoint);
    EXPECT(decoded.generation == 77);
}

}  // namespace

int main() {
    test_round_trip_layout_and_determinism();
    test_generation_and_nested_inputs_fail_before_output();
    test_outer_shape_version_integrity_and_padding();
    test_nested_tamper_and_decode_are_atomic();
    if (failures != 0) {
        std::cerr << failures << " recovery checkpoint assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 4 critical alert recovery checkpoint scenario groups\n";
    return EXIT_SUCCESS;
}
