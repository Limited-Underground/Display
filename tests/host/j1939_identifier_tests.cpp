#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/j1939_identifier.hpp"

namespace {

using namespace opengauge::can;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

std::uint32_t make_identifier(
    std::uint8_t priority,
    std::uint8_t data_page,
    std::uint8_t pdu_format,
    std::uint8_t pdu_specific,
    std::uint8_t source_address) {
    return (static_cast<std::uint32_t>(priority) << 26U) |
           (static_cast<std::uint32_t>(data_page) << 24U) |
           (static_cast<std::uint32_t>(pdu_format) << 16U) |
           (static_cast<std::uint32_t>(pdu_specific) << 8U) |
           source_address;
}

void test_pdu1_global_request() {
    const auto result = parse_j1939_identifier(
        0x18EAFF00U,
        CanFrameFormat::extended);
    EXPECT(result.parsed());
    EXPECT(result.identifier.priority == 6U);
    EXPECT(result.identifier.data_page == 0U);
    EXPECT(result.identifier.pdu_format == 0xEAU);
    EXPECT(result.identifier.pdu_specific == 0xFFU);
    EXPECT(result.identifier.source_address == 0x00U);
    EXPECT(result.identifier.is_pdu1);
    EXPECT(result.identifier.destination_present);
    EXPECT(result.identifier.destination_address == 0xFFU);
    EXPECT(result.identifier.parameter_group_number == 0xEA00U);
}

void test_pdu2_engine_temperature() {
    const auto result = parse_j1939_identifier(
        0x18FEEE2AU,
        CanFrameFormat::extended);
    EXPECT(result.parsed());
    EXPECT(result.identifier.priority == 6U);
    EXPECT(result.identifier.pdu_format == 0xFEU);
    EXPECT(result.identifier.pdu_specific == 0xEEU);
    EXPECT(result.identifier.source_address == 0x2AU);
    EXPECT(!result.identifier.is_pdu1);
    EXPECT(!result.identifier.destination_present);
    EXPECT(result.identifier.destination_address == 0U);
    EXPECT(result.identifier.parameter_group_number == 0xFEEEU);
}

void test_pdu_format_boundary_changes_pgn_rules() {
    const auto pdu1 = parse_j1939_identifier(
        make_identifier(0U, 0U, 0xEFU, 0x34U, 0xFFU),
        CanFrameFormat::extended);
    const auto pdu2 = parse_j1939_identifier(
        make_identifier(7U, 0U, 0xF0U, 0x34U, 0x00U),
        CanFrameFormat::extended);

    EXPECT(pdu1.parsed());
    EXPECT(pdu1.identifier.is_pdu1);
    EXPECT(pdu1.identifier.parameter_group_number == 0xEF00U);
    EXPECT(pdu1.identifier.destination_address == 0x34U);
    EXPECT(pdu1.identifier.priority == 0U);
    EXPECT(pdu1.identifier.source_address == 0xFFU);

    EXPECT(pdu2.parsed());
    EXPECT(!pdu2.identifier.is_pdu1);
    EXPECT(pdu2.identifier.parameter_group_number == 0xF034U);
    EXPECT(pdu2.identifier.priority == 7U);
    EXPECT(pdu2.identifier.source_address == 0x00U);
}

void test_data_page_is_preserved_in_pgn() {
    const auto result = parse_j1939_identifier(
        make_identifier(3U, 1U, 0xF0U, 0xABU, 0xCDU),
        CanFrameFormat::extended);
    EXPECT(result.parsed());
    EXPECT(result.identifier.data_page == 1U);
    EXPECT(result.identifier.parameter_group_number == 0x1F0ABU);
    EXPECT(result.identifier.parameter_group_number <=
           kMaximumClassicalJ1939Pgn);
}

void test_non_j1939_and_malformed_identifiers_are_rejected() {
    EXPECT(parse_j1939_identifier(0x7FFU, CanFrameFormat::standard).error ==
           J1939IdentifierError::standard_frame);
    EXPECT(parse_j1939_identifier(
               kMaximumExtendedCanIdentifier + 1U,
               CanFrameFormat::extended).error ==
           J1939IdentifierError::identifier_out_of_range);
    EXPECT(parse_j1939_identifier(
               1U << 25U,
               CanFrameFormat::extended).error ==
           J1939IdentifierError::unsupported_extended_data_page);
}

}  // namespace

int main() {
    test_pdu1_global_request();
    test_pdu2_engine_temperature();
    test_pdu_format_boundary_changes_pgn_rules();
    test_data_page_is_preserved_in_pgn();
    test_non_j1939_and_malformed_identifiers_are_rejected();

    if (failures != 0) {
        std::cerr << failures << " J1939 identifier assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 5 J1939 identifier scenario groups\n";
    return EXIT_SUCCESS;
}
