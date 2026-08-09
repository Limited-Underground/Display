#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "opengauge/critical_alert.hpp"

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

std::vector<std::string> split(const std::string& text, char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(text);
    std::string field;
    while (std::getline(stream, field, delimiter)) {
        fields.push_back(field);
    }
    return fields;
}

std::string to_hex(
    const std::array<std::uint8_t, kCriticalAlertFrameBytes>& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        text.push_back(digits[(byte >> 4U) & 0x0FU]);
        text.push_back(digits[byte & 0x0FU]);
    }
    return text;
}

CriticalAlert alert_from_fields(const std::vector<std::string>& fields) {
    CriticalAlert alert{};
    alert.type = static_cast<CriticalAlertType>(std::stoul(fields[1]));
    alert.severity = static_cast<AlertSeverity>(std::stoul(fields[2]));
    alert.state = static_cast<AlertState>(std::stoul(fields[3]));
    alert.quality = static_cast<AlertQuality>(std::stoul(fields[4]));
    alert.unit = static_cast<AlertUnit>(std::stoul(fields[5]));
    alert.producer_id = std::stoull(fields[6]);
    alert.vehicle_id = std::stoull(fields[7]);
    alert.event_id = std::stoull(fields[8]);
    alert.condition_id = std::stoull(fields[9]);
    alert.utc_present = std::stoul(fields[10]) != 0;
    alert.event_time_utc_s =
        static_cast<std::uint32_t>(std::stoul(fields[11]));
    alert.age_ms = static_cast<std::uint32_t>(std::stoul(fields[12]));
    alert.value_present = std::stoul(fields[13]) != 0;
    alert.value_milli =
        static_cast<std::int32_t>(std::stol(fields[14]));
    alert.diagnostic_code =
        static_cast<std::uint32_t>(std::stoul(fields[15]));
    return alert;
}

CriticalAlert base_alert() {
    return {
        CriticalAlertType::oil_pressure_low,
        AlertSeverity::critical,
        AlertState::asserted,
        AlertQuality::valid,
        AlertUnit::kilopascal,
        1,
        2,
        3,
        4,
        1786243200U,
        500U,
        125000,
        5,
        true,
        true,
    };
}

void test_crc_and_shared_golden_vectors() {
    const std::array<std::uint8_t, 9> check{
        '1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT(alert_crc32(check.data(), check.size()) == 0xCBF43926U);

    std::ifstream input("tests/fixtures/critical_alert_v0_vectors.csv");
    EXPECT(input.good());
    std::string line;
    EXPECT(static_cast<bool>(std::getline(input, line)));
    std::size_t rows = 0;
    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto fields = split(line, ',');
        EXPECT(fields.size() == 17);
        if (fields.size() != 17) {
            continue;
        }
        const auto alert = alert_from_fields(fields);
        std::array<std::uint8_t, kCriticalAlertFrameBytes> frame{};
        EXPECT(encode_critical_alert(alert, frame).encoded());
        EXPECT(to_hex(frame) == fields[16]);

        const auto decoded = decode_critical_alert(frame.data(), frame.size());
        EXPECT(decoded.decoded());
        EXPECT(decoded.alert.producer_id == alert.producer_id);
        EXPECT(decoded.alert.vehicle_id == alert.vehicle_id);
        EXPECT(decoded.alert.event_id == alert.event_id);
        EXPECT(decoded.alert.condition_id == alert.condition_id);
        ++rows;
    }
    EXPECT(rows == 3);
}

void test_producer_rejects_invalid_semantics() {
    auto alert = base_alert();
    alert.vehicle_id = 0;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::invalid_identity);

    alert = base_alert();
    alert.unit = AlertUnit::celsius;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::invalid_type_unit);

    alert = base_alert();
    alert.value_milli = -1;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::value_out_of_range);

    alert = base_alert();
    alert.value_present = false;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::inconsistent_value);

    alert = base_alert();
    alert.quality = AlertQuality::error;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::inconsistent_quality);

    alert = base_alert();
    alert.type = CriticalAlertType::rollover_detected;
    alert.unit = AlertUnit::boolean;
    alert.value_milli = 1000;
    EXPECT(validate_critical_alert(alert) ==
           AlertCodecError::invalid_severity);
}

void test_decoder_rejects_incompatible_or_corrupt_frames() {
    auto alert = base_alert();
    std::array<std::uint8_t, kCriticalAlertFrameBytes> frame{};
    EXPECT(encode_critical_alert(alert, frame).encoded());

    EXPECT(decode_critical_alert(nullptr, frame.size()).error ==
           AlertCodecError::invalid_argument);
    EXPECT(decode_critical_alert(frame.data(), frame.size() - 1).error ==
           AlertCodecError::malformed);

    auto mutated = frame;
    mutated[4] = 1;
    EXPECT(decode_critical_alert(mutated.data(), mutated.size()).error ==
           AlertCodecError::unsupported_version);

    mutated = frame;
    mutated[6] |= 0x40U;
    EXPECT(decode_critical_alert(mutated.data(), mutated.size()).error ==
           AlertCodecError::reserved_flags_set);

    mutated = frame;
    mutated[52] ^= 0x01U;
    EXPECT(decode_critical_alert(mutated.data(), mutated.size()).error ==
           AlertCodecError::integrity_failure);
}

void test_assert_and_clear_use_distinct_events_for_one_condition() {
    auto asserted = base_alert();
    auto cleared = asserted;
    cleared.state = AlertState::cleared;
    cleared.quality = AlertQuality::unavailable;
    cleared.unit = AlertUnit::none;
    cleared.value_present = false;
    cleared.value_milli = 0;
    ++cleared.event_id;

    EXPECT(asserted.condition_id == cleared.condition_id);
    EXPECT(asserted.event_id != cleared.event_id);

    std::array<std::uint8_t, kCriticalAlertFrameBytes> asserted_frame{};
    std::array<std::uint8_t, kCriticalAlertFrameBytes> cleared_frame{};
    EXPECT(encode_critical_alert(asserted, asserted_frame).encoded());
    EXPECT(encode_critical_alert(cleared, cleared_frame).encoded());
    EXPECT(asserted_frame != cleared_frame);
    EXPECT(decode_critical_alert(
               cleared_frame.data(), cleared_frame.size()).decoded());
}

}  // namespace

int main() {
    test_crc_and_shared_golden_vectors();
    test_producer_rejects_invalid_semantics();
    test_decoder_rejects_incompatible_or_corrupt_frames();
    test_assert_and_clear_use_distinct_events_for_one_condition();

    if (failures != 0) {
        std::cerr << failures << " critical alert exporter assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 4 critical alert exporter scenario groups\n";
    return EXIT_SUCCESS;
}
