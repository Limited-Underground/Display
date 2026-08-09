#pragma once

#include <cstdint>

namespace opengauge::can {

inline constexpr std::uint32_t kMaximumExtendedCanIdentifier = 0x1FFFFFFFU;
inline constexpr std::uint32_t kMaximumClassicalJ1939Pgn = 0x1FFFFU;

enum class CanFrameFormat : std::uint8_t {
    standard = 0,
    extended = 1,
};

enum class J1939IdentifierError : std::uint8_t {
    none = 0,
    standard_frame,
    identifier_out_of_range,
    unsupported_extended_data_page,
};

struct J1939Identifier {
    std::uint32_t raw_identifier{0};
    std::uint32_t parameter_group_number{0};
    std::uint8_t priority{0};
    std::uint8_t data_page{0};
    std::uint8_t pdu_format{0};
    std::uint8_t pdu_specific{0};
    std::uint8_t source_address{0};
    std::uint8_t destination_address{0};
    bool is_pdu1{false};
    bool destination_present{false};
};

struct J1939IdentifierResult {
    J1939IdentifierError error{J1939IdentifierError::standard_frame};
    J1939Identifier identifier{};

    [[nodiscard]] constexpr bool parsed() const {
        return error == J1939IdentifierError::none;
    }
};

[[nodiscard]] J1939IdentifierResult parse_j1939_identifier(
    std::uint32_t raw_identifier,
    CanFrameFormat format);

}  // namespace opengauge::can
