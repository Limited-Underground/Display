#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "opengauge/critical_alert_ack.hpp"

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

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream stream{line};
    std::string field;
    while (std::getline(stream, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> from_hex(
    const std::string& hex) {
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> bytes{};
    EXPECT(hex.size() == bytes.size() * 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            std::stoul(hex.substr(index * 2, 2), nullptr, 16));
    }
    return bytes;
}

CriticalAlertAck accepted() {
    return {
        AlertAckDisposition::accepted,
        AlertAckReason::none,
        AlertState::asserted,
        1,
        2,
        3,
        4,
        5,
        6,
        7};
}

void test_normative_vectors_round_trip_exact_bytes() {
    std::ifstream input{"tests/fixtures/critical_alert_ack_v0_vectors.csv"};
    EXPECT(input.good());
    std::string line;
    EXPECT(static_cast<bool>(std::getline(input, line)));
    std::size_t rows = 0;
    while (std::getline(input, line)) {
        const auto fields = split(line);
        EXPECT(fields.size() == 12);
        if (fields.size() != 12) {
            continue;
        }
        CriticalAlertAck value{};
        value.disposition = static_cast<AlertAckDisposition>(
            std::stoul(fields[1]));
        value.reason = static_cast<AlertAckReason>(std::stoul(fields[2]));
        value.state = static_cast<AlertState>(std::stoul(fields[3]));
        value.consumer_id = std::stoull(fields[4]);
        value.producer_id = std::stoull(fields[5]);
        value.event_id = std::stoull(fields[6]);
        value.condition_id = std::stoull(fields[7]);
        value.consumer_boot_session_id =
            static_cast<std::uint32_t>(std::stoul(fields[8]));
        value.ack_sequence =
            static_cast<std::uint32_t>(std::stoul(fields[9]));
        value.observed_alert_age_ms =
            static_cast<std::uint32_t>(std::stoul(fields[10]));
        const auto expected = from_hex(fields[11]);
        std::array<std::uint8_t, kCriticalAlertAckFrameBytes> encoded{};
        EXPECT(encode_critical_alert_ack(value, encoded).encoded());
        EXPECT(encoded == expected);
        const auto decoded = decode_critical_alert_ack(
            expected.data(), expected.size());
        EXPECT(decoded.decoded());
        EXPECT(decoded.acknowledgement.event_id == value.event_id);
        EXPECT(decoded.acknowledgement.reason == value.reason);
        EXPECT(decoded.acknowledgement.ack_sequence == value.ack_sequence);
        ++rows;
    }
    EXPECT(rows == 3);
}

void test_disposition_identity_and_age_validation() {
    auto value = accepted();
    EXPECT(validate_critical_alert_ack(value) == AlertAckCodecError::none);
    value.reason = AlertAckReason::stale;
    EXPECT(validate_critical_alert_ack(value) ==
           AlertAckCodecError::inconsistent_disposition);
    value.disposition = AlertAckDisposition::rejected;
    EXPECT(validate_critical_alert_ack(value) == AlertAckCodecError::none);
    value.reason = AlertAckReason::none;
    EXPECT(validate_critical_alert_ack(value) ==
           AlertAckCodecError::inconsistent_disposition);
    value = accepted();
    value.consumer_id = 0;
    EXPECT(validate_critical_alert_ack(value) ==
           AlertAckCodecError::invalid_identity);
    value = accepted();
    value.observed_alert_age_ms = 86400001;
    EXPECT(validate_critical_alert_ack(value) ==
           AlertAckCodecError::age_out_of_range);
}

void test_malformed_version_reserved_and_crc_rejection() {
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> bytes{};
    EXPECT(encode_critical_alert_ack(accepted(), bytes).encoded());
    EXPECT(decode_critical_alert_ack(nullptr, bytes.size()).error ==
           AlertAckCodecError::invalid_argument);
    EXPECT(decode_critical_alert_ack(bytes.data(), bytes.size() - 1).error ==
           AlertAckCodecError::invalid_argument);
    auto changed = bytes;
    changed[0] = 'X';
    EXPECT(decode_critical_alert_ack(changed.data(), changed.size()).error ==
           AlertAckCodecError::malformed);
    changed = bytes;
    changed[4] = 1;
    EXPECT(decode_critical_alert_ack(changed.data(), changed.size()).error ==
           AlertAckCodecError::unsupported_version);
    changed = bytes;
    changed[9] = 1;
    EXPECT(decode_critical_alert_ack(changed.data(), changed.size()).error ==
           AlertAckCodecError::noncanonical);
    changed = bytes;
    changed[20] ^= 1;
    EXPECT(decode_critical_alert_ack(changed.data(), changed.size()).error ==
           AlertAckCodecError::integrity_failure);
}

void test_unknown_enums_and_encode_output_rollback() {
    auto value = accepted();
    value.disposition = static_cast<AlertAckDisposition>(99);
    EXPECT(validate_critical_alert_ack(value) ==
           AlertAckCodecError::unknown_enum);
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> output{};
    output.fill(0xA5U);
    EXPECT(!encode_critical_alert_ack(value, output).encoded());
    EXPECT(output.front() == 0xA5U && output.back() == 0xA5U);
}

}  // namespace

int main() {
    test_normative_vectors_round_trip_exact_bytes();
    test_disposition_identity_and_age_validation();
    test_malformed_version_reserved_and_crc_rejection();
    test_unknown_enums_and_encode_output_rollback();
    if (failures != 0) {
        std::cerr << failures << " critical alert ACK assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 4 critical alert ACK scenario groups\n";
    return EXIT_SUCCESS;
}
