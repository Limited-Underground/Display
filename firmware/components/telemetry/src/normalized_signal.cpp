#include "opengauge/normalized_signal.hpp"

#include <algorithm>

namespace opengauge::telemetry {
namespace {

bool is_lower_alpha(char value) {
    return value >= 'a' && value <= 'z';
}

bool is_digit(char value) {
    return value >= '0' && value <= '9';
}

bool known_value_type(SignalValueType type) {
    switch (type) {
        case SignalValueType::boolean:
        case SignalValueType::signed_integer:
        case SignalValueType::unsigned_integer:
            return true;
    }
    return false;
}

bool known_unit(SignalUnit unit) {
    switch (unit) {
        case SignalUnit::none:
        case SignalUnit::count:
        case SignalUnit::milli_celsius:
        case SignalUnit::pascal:
        case SignalUnit::millivolt:
        case SignalUnit::milliampere:
        case SignalUnit::milli_percent:
        case SignalUnit::milli_revolutions_per_minute:
        case SignalUnit::millimetres_per_second:
            return true;
    }
    return false;
}

bool known_quality(SignalQuality quality) {
    switch (quality) {
        case SignalQuality::valid:
        case SignalQuality::suspect:
        case SignalQuality::unavailable:
        case SignalQuality::error:
        case SignalQuality::out_of_range:
        case SignalQuality::stale:
        case SignalQuality::unknown:
            return true;
    }
    return false;
}

bool known_protocol(SignalSourceProtocol protocol) {
    switch (protocol) {
        case SignalSourceProtocol::synthetic:
        case SignalSourceProtocol::obd2:
        case SignalSourceProtocol::j1939:
        case SignalSourceProtocol::gps:
            return true;
    }
    return false;
}

bool quality_allows_value(SignalQuality quality) {
    return quality == SignalQuality::valid ||
           quality == SignalQuality::suspect;
}

bool value_in_unit_range(SignalUnit unit, std::int64_t value) {
    switch (unit) {
        case SignalUnit::none:
        case SignalUnit::count:
            return true;
        case SignalUnit::milli_celsius:
            return value >= -100000 && value <= 250000;
        case SignalUnit::pascal:
            return value >= 0 && value <= 200000000;
        case SignalUnit::millivolt:
            return value >= 0 && value <= 100000;
        case SignalUnit::milliampere:
            return value >= -100000000 && value <= 100000000;
        case SignalUnit::milli_percent:
            return value >= 0 && value <= 100000;
        case SignalUnit::milli_revolutions_per_minute:
            return value >= 0 && value <= 20000000;
        case SignalUnit::millimetres_per_second:
            return value >= 0 && value <= 200000;
    }
    return false;
}

SignalModelError validate_source(const SignalSource& source) {
    if (!known_protocol(source.protocol)) {
        return SignalModelError::unknown_enum;
    }

    switch (source.protocol) {
        case SignalSourceProtocol::j1939:
            if (!source.source_address_present ||
                !source.parameter_group_present ||
                !source.parameter_present ||
                source.parameter_group_number >
                    kMaximumClassicalJ1939Pgn ||
                source.parameter_number > kMaximumJ1939Spn) {
                return SignalModelError::invalid_source;
            }
            return SignalModelError::none;
        case SignalSourceProtocol::obd2:
            if (!source.parameter_present || source.source_address_present ||
                source.parameter_group_present) {
                return SignalModelError::invalid_source;
            }
            return SignalModelError::none;
        case SignalSourceProtocol::gps:
        case SignalSourceProtocol::synthetic:
            if (source.source_address_present ||
                source.parameter_group_present ||
                source.parameter_present) {
                return SignalModelError::invalid_source;
            }
            return SignalModelError::none;
    }
    return SignalModelError::unknown_enum;
}

}  // namespace

SignalModelError make_signal_id(
    std::string_view text,
    SignalId& output) {
    output = {};
    if (text.empty() || text.size() > kMaximumSignalIdBytes ||
        !is_lower_alpha(text.front())) {
        return SignalModelError::invalid_signal_id;
    }

    bool has_namespace_separator = false;
    bool segment_start = false;
    for (const auto value : text) {
        if (value == '.') {
            if (segment_start) {
                return SignalModelError::invalid_signal_id;
            }
            has_namespace_separator = true;
            segment_start = true;
            continue;
        }
        if (segment_start && !is_lower_alpha(value)) {
            return SignalModelError::invalid_signal_id;
        }
        if (!is_lower_alpha(value) && !is_digit(value) && value != '_') {
            return SignalModelError::invalid_signal_id;
        }
        segment_start = false;
    }
    if (!has_namespace_separator || segment_start) {
        return SignalModelError::invalid_signal_id;
    }

    std::copy(text.begin(), text.end(), output.bytes.begin());
    output.length = static_cast<std::uint8_t>(text.size());
    output.bytes[text.size()] = '\0';
    return SignalModelError::none;
}

bool signal_id_equals(const SignalId& id, std::string_view text) {
    return id.length == text.size() &&
           std::equal(text.begin(), text.end(), id.bytes.begin());
}

SignalModelError validate_normalized_signal(
    const NormalizedSignal& signal) {
    if (signal.id.length == 0 ||
        signal.id.length > kMaximumSignalIdBytes ||
        signal.id.bytes[signal.id.length] != '\0') {
        return SignalModelError::invalid_signal_id;
    }
    SignalId canonical_id{};
    const auto id_error = make_signal_id(
        std::string_view(signal.id.bytes.data(), signal.id.length),
        canonical_id);
    if (id_error != SignalModelError::none ||
        !signal_id_equals(canonical_id,
                          std::string_view(signal.id.bytes.data(),
                                           signal.id.length))) {
        return SignalModelError::invalid_signal_id;
    }
    if (!known_value_type(signal.value.type) ||
        !known_unit(signal.unit) || !known_quality(signal.quality)) {
        return SignalModelError::unknown_enum;
    }
    if (quality_allows_value(signal.quality) != signal.value.present) {
        return SignalModelError::inconsistent_value;
    }
    if (signal.value.present) {
        if (signal.value.type == SignalValueType::boolean) {
            if (signal.unit != SignalUnit::none) {
                return SignalModelError::invalid_unit;
            }
            if (signal.value.raw_value != 0 &&
                signal.value.raw_value != 1) {
                return SignalModelError::invalid_boolean;
            }
        } else {
            if (signal.value.type == SignalValueType::unsigned_integer &&
                signal.value.raw_value < 0) {
                return SignalModelError::negative_unsigned_value;
            }
            if (!value_in_unit_range(signal.unit,
                                     signal.value.raw_value)) {
                return SignalModelError::value_out_of_range;
            }
        }
    } else if (signal.value.raw_value != 0) {
        return SignalModelError::inconsistent_value;
    }

    const auto source_error = validate_source(signal.source);
    if (source_error != SignalModelError::none) {
        return source_error;
    }
    if (signal.sample_time_present) {
        if (signal.sampled_at_ms > signal.received_at_ms) {
            return SignalModelError::inconsistent_time;
        }
    } else if (signal.sampled_at_ms != 0) {
        return SignalModelError::inconsistent_time;
    }
    return SignalModelError::none;
}

SignalFreshnessResult evaluate_signal_freshness(
    const NormalizedSignal& signal,
    std::uint64_t now_ms,
    std::uint64_t stale_after_ms) {
    const auto validation = validate_normalized_signal(signal);
    if (validation != SignalModelError::none) {
        return {validation, SignalQuality::unknown, 0};
    }
    if (stale_after_ms == 0) {
        return {
            SignalModelError::invalid_stale_threshold,
            SignalQuality::unknown,
            0};
    }

    const auto reference_time = signal.sample_time_present
                                    ? signal.sampled_at_ms
                                    : signal.received_at_ms;
    if (now_ms < reference_time) {
        return {
            SignalModelError::clock_regressed,
            SignalQuality::unknown,
            0};
    }

    const auto age_ms = now_ms - reference_time;
    auto quality = signal.quality;
    if (quality_allows_value(quality) && age_ms >= stale_after_ms) {
        quality = SignalQuality::stale;
    }
    return {SignalModelError::none, quality, age_ms};
}

}  // namespace opengauge::telemetry
