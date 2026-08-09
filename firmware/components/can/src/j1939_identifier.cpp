#include "opengauge/j1939_identifier.hpp"

namespace opengauge::can {

J1939IdentifierResult parse_j1939_identifier(
    std::uint32_t raw_identifier,
    CanFrameFormat format) {
    if (format != CanFrameFormat::extended) {
        return {J1939IdentifierError::standard_frame, {}};
    }
    if (raw_identifier > kMaximumExtendedCanIdentifier) {
        return {J1939IdentifierError::identifier_out_of_range, {}};
    }

    // Bit 25 was reserved in Classical J1939 and is the extended data-page
    // bit in J1939-22. This bounded parser intentionally does not claim
    // J1939-22 support.
    if ((raw_identifier & (1U << 25U)) != 0) {
        return {
            J1939IdentifierError::unsupported_extended_data_page,
            {}};
    }

    J1939Identifier identifier{};
    identifier.raw_identifier = raw_identifier;
    identifier.priority =
        static_cast<std::uint8_t>((raw_identifier >> 26U) & 0x07U);
    identifier.data_page =
        static_cast<std::uint8_t>((raw_identifier >> 24U) & 0x01U);
    identifier.pdu_format =
        static_cast<std::uint8_t>((raw_identifier >> 16U) & 0xFFU);
    identifier.pdu_specific =
        static_cast<std::uint8_t>((raw_identifier >> 8U) & 0xFFU);
    identifier.source_address =
        static_cast<std::uint8_t>(raw_identifier & 0xFFU);
    identifier.is_pdu1 = identifier.pdu_format < 0xF0U;
    identifier.destination_present = identifier.is_pdu1;
    identifier.destination_address =
        identifier.is_pdu1 ? identifier.pdu_specific : 0U;

    identifier.parameter_group_number =
        (static_cast<std::uint32_t>(identifier.data_page) << 16U) |
        (static_cast<std::uint32_t>(identifier.pdu_format) << 8U);
    if (!identifier.is_pdu1) {
        identifier.parameter_group_number |= identifier.pdu_specific;
    }

    return {J1939IdentifierError::none, identifier};
}

}  // namespace opengauge::can
