#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace opengauge::telemetry {

inline constexpr std::size_t kMaximumSignalIdBytes = 47;
inline constexpr std::uint32_t kMaximumClassicalJ1939Pgn = 0x1FFFFU;
inline constexpr std::uint32_t kMaximumJ1939Spn = 0x7FFFFU;

struct SignalId {
    std::array<char, kMaximumSignalIdBytes + 1> bytes{};
    std::uint8_t length{0};
};

enum class SignalValueType : std::uint8_t {
    boolean = 1,
    signed_integer = 2,
    unsigned_integer = 3,
};

// Numeric values use the precision named by the unit. For example, 87.5 C is
// represented as raw_value=87500 and unit=milli_celsius.
enum class SignalUnit : std::uint8_t {
    none = 0,
    count = 1,
    milli_celsius = 2,
    pascal = 3,
    millivolt = 4,
    milliampere = 5,
    milli_percent = 6,
    milli_revolutions_per_minute = 7,
    millimetres_per_second = 8,
};

enum class SignalQuality : std::uint8_t {
    valid = 1,
    suspect = 2,
    unavailable = 3,
    error = 4,
    out_of_range = 5,
    stale = 6,
    unknown = 7,
};

enum class SignalSourceProtocol : std::uint8_t {
    synthetic = 1,
    obd2 = 2,
    j1939 = 3,
    gps = 4,
};

struct SignalValue {
    SignalValueType type{SignalValueType::signed_integer};
    std::int64_t raw_value{0};
    bool present{false};
};

struct SignalSource {
    SignalSourceProtocol protocol{SignalSourceProtocol::synthetic};
    std::uint8_t bus_index{0};
    std::uint8_t source_address{0};
    std::uint32_t parameter_group_number{0};
    std::uint32_t parameter_number{0};
    bool source_address_present{false};
    bool parameter_group_present{false};
    bool parameter_present{false};
};

struct NormalizedSignal {
    SignalId id{};
    SignalValue value{};
    SignalUnit unit{SignalUnit::none};
    SignalQuality quality{SignalQuality::unknown};
    SignalSource source{};
    std::uint64_t received_at_ms{0};
    std::uint64_t sampled_at_ms{0};
    bool sample_time_present{false};
};

enum class SignalModelError : std::uint8_t {
    none = 0,
    invalid_signal_id,
    unknown_enum,
    inconsistent_value,
    invalid_boolean,
    negative_unsigned_value,
    invalid_unit,
    value_out_of_range,
    invalid_source,
    inconsistent_time,
    clock_regressed,
    invalid_stale_threshold,
};

struct SignalFreshnessResult {
    SignalModelError error{SignalModelError::invalid_signal_id};
    SignalQuality effective_quality{SignalQuality::unknown};
    std::uint64_t age_ms{0};

    [[nodiscard]] constexpr bool evaluated() const {
        return error == SignalModelError::none;
    }
};

[[nodiscard]] SignalModelError make_signal_id(
    std::string_view text,
    SignalId& output);
[[nodiscard]] bool signal_id_equals(
    const SignalId& id,
    std::string_view text);
[[nodiscard]] SignalModelError validate_normalized_signal(
    const NormalizedSignal& signal);
[[nodiscard]] SignalFreshnessResult evaluate_signal_freshness(
    const NormalizedSignal& signal,
    std::uint64_t now_ms,
    std::uint64_t stale_after_ms);

}  // namespace opengauge::telemetry
