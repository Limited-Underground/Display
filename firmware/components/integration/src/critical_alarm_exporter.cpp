#include "opengauge/critical_alarm_exporter.hpp"

#include <limits>

namespace opengauge::integration {
namespace {

constexpr std::size_t kNotFound =
    std::numeric_limits<std::size_t>::max();

bool known_type(CriticalAlertType type) {
    switch (type) {
        case CriticalAlertType::engine_over_temperature:
        case CriticalAlertType::oil_pressure_low:
        case CriticalAlertType::charging_failure:
        case CriticalAlertType::rollover_detected:
        case CriticalAlertType::vehicle_immobilized:
        case CriticalAlertType::fuel_critical:
        case CriticalAlertType::generic_critical:
            return true;
    }
    return false;
}

bool valid_severity_for_type(
    CriticalAlertType type,
    AlertSeverity severity) {
    if (type == CriticalAlertType::rollover_detected) {
        return severity == AlertSeverity::emergency;
    }
    return severity == AlertSeverity::critical ||
           severity == AlertSeverity::emergency;
}

CriticalAlarmExportError map_quality(
    telemetry::SignalQuality source,
    AlertQuality& output) {
    switch (source) {
        case telemetry::SignalQuality::valid:
            output = AlertQuality::valid;
            return CriticalAlarmExportError::none;
        case telemetry::SignalQuality::suspect:
            output = AlertQuality::suspect;
            return CriticalAlarmExportError::none;
        case telemetry::SignalQuality::unavailable:
        case telemetry::SignalQuality::stale:
        case telemetry::SignalQuality::unknown:
            output = AlertQuality::unavailable;
            return CriticalAlarmExportError::none;
        case telemetry::SignalQuality::error:
        case telemetry::SignalQuality::out_of_range:
            output = AlertQuality::error;
            return CriticalAlarmExportError::none;
    }
    return CriticalAlarmExportError::invalid_event;
}

CriticalAlarmExportError map_value(
    const alarm::AlarmEvent& event,
    AlertUnit& unit,
    std::int32_t& value,
    bool& present) {
    unit = AlertUnit::none;
    value = 0;
    present = false;
    switch (event.value.type) {
        case telemetry::SignalValueType::boolean:
        case telemetry::SignalValueType::signed_integer:
        case telemetry::SignalValueType::unsigned_integer:
            break;
        default:
            return CriticalAlarmExportError::invalid_event;
    }
    if (!event.value.present) {
        if (event.value.raw_value != 0) {
            return CriticalAlarmExportError::invalid_event;
        }
        return CriticalAlarmExportError::none;
    }
    if (event.signal_quality != telemetry::SignalQuality::valid &&
        event.signal_quality != telemetry::SignalQuality::suspect) {
        return CriticalAlarmExportError::incompatible_quality;
    }

    std::int64_t mapped = event.value.raw_value;
    if (event.value.type == telemetry::SignalValueType::unsigned_integer &&
        mapped < 0) {
        return CriticalAlarmExportError::invalid_event;
    }
    switch (event.unit) {
        case telemetry::SignalUnit::milli_celsius:
            if (event.value.type == telemetry::SignalValueType::boolean) {
                return CriticalAlarmExportError::invalid_event;
            }
            unit = AlertUnit::celsius;
            break;
        case telemetry::SignalUnit::pascal:
            if (event.value.type == telemetry::SignalValueType::boolean) {
                return CriticalAlarmExportError::invalid_event;
            }
            // One pascal is one milli-kilopascal.
            unit = AlertUnit::kilopascal;
            break;
        case telemetry::SignalUnit::millivolt:
            if (event.value.type == telemetry::SignalValueType::boolean) {
                return CriticalAlarmExportError::invalid_event;
            }
            unit = AlertUnit::volt;
            break;
        case telemetry::SignalUnit::milli_percent:
            if (event.value.type == telemetry::SignalValueType::boolean) {
                return CriticalAlarmExportError::invalid_event;
            }
            unit = AlertUnit::percent;
            break;
        case telemetry::SignalUnit::milli_revolutions_per_minute:
            if (event.value.type == telemetry::SignalValueType::boolean) {
                return CriticalAlarmExportError::invalid_event;
            }
            unit = AlertUnit::revolutions_per_minute;
            break;
        case telemetry::SignalUnit::none:
            if (event.value.type != telemetry::SignalValueType::boolean ||
                (mapped != 0 && mapped != 1)) {
                return CriticalAlarmExportError::unsupported_unit;
            }
            unit = AlertUnit::boolean;
            mapped *= 1000;
            break;
        case telemetry::SignalUnit::count:
        case telemetry::SignalUnit::milliampere:
        case telemetry::SignalUnit::millimetres_per_second:
            return CriticalAlarmExportError::unsupported_unit;
    }
    if (mapped < std::numeric_limits<std::int32_t>::min() ||
        mapped > std::numeric_limits<std::int32_t>::max()) {
        return CriticalAlarmExportError::value_out_of_range;
    }
    value = static_cast<std::int32_t>(mapped);
    present = true;
    return CriticalAlarmExportError::none;
}

}  // namespace

CriticalAlarmExportError validate_critical_alarm_mapping(
    const CriticalAlarmMapping& mapping) {
    if (mapping.alarm_rule_id == 0 || !known_type(mapping.alert_type) ||
        !valid_severity_for_type(mapping.alert_type, mapping.severity)) {
        return CriticalAlarmExportError::invalid_mapping;
    }
    return CriticalAlarmExportError::none;
}

CriticalAlarmExportError CriticalAlarmExporter::add_mapping(
    const CriticalAlarmMapping& mapping) {
    if (status_.running) {
        return CriticalAlarmExportError::invalid_state;
    }
    const auto validation = validate_critical_alarm_mapping(mapping);
    if (validation != CriticalAlarmExportError::none) {
        return validation;
    }
    if (find_mapping(mapping.alarm_rule_id) != kNotFound) {
        return CriticalAlarmExportError::duplicate_mapping;
    }
    if (mapping_count_ == mappings_.size()) {
        return CriticalAlarmExportError::mapping_capacity_full;
    }
    for (auto& slot : mappings_) {
        if (!slot.occupied) {
            slot = {mapping, 0, true, false};
            ++mapping_count_;
            status_.mapping_count = mapping_count_;
            return CriticalAlarmExportError::none;
        }
    }
    return CriticalAlarmExportError::mapping_capacity_full;
}

CriticalAlarmExportError CriticalAlarmExporter::clear_mappings() {
    if (status_.running) {
        return CriticalAlarmExportError::invalid_state;
    }
    mappings_ = {};
    mapping_count_ = 0;
    status_ = {};
    return CriticalAlarmExportError::none;
}

CriticalAlarmExportError CriticalAlarmExporter::start(
    CriticalAlarmExporterIdentity identity) {
    if (status_.running) {
        return CriticalAlarmExportError::invalid_state;
    }
    if (mapping_count_ == 0 || identity.producer_id == 0 ||
        identity.vehicle_id == 0 || identity.first_event_id == 0 ||
        identity.first_condition_id == 0) {
        return CriticalAlarmExportError::invalid_configuration;
    }
    for (auto& slot : mappings_) {
        if (slot.occupied) {
            slot.active_condition_id = 0;
            slot.condition_active = false;
        }
    }
    identity_ = identity;
    status_ = {};
    status_.running = true;
    status_.mapping_count = mapping_count_;
    status_.next_event_id = identity.first_event_id;
    status_.next_condition_id = identity.first_condition_id;
    return CriticalAlarmExportError::none;
}

void CriticalAlarmExporter::stop() {
    for (auto& slot : mappings_) {
        if (slot.occupied) {
            slot.active_condition_id = 0;
            slot.condition_active = false;
        }
    }
    identity_ = {};
    status_.running = false;
    status_.active_condition_count = 0;
    status_.next_event_id = 0;
    status_.next_condition_id = 0;
}

CriticalAlarmExportResult CriticalAlarmExporter::export_event(
    const alarm::AlarmEvent& event,
    std::uint64_t now_ms) {
    auto reject = [&](CriticalAlarmExportError error) {
        ++status_.events_rejected;
        CriticalAlarmExportResult result{};
        result.error = error;
        return result;
    };
    if (!status_.running) {
        return reject(CriticalAlarmExportError::invalid_state);
    }
    const auto mapping_index = find_mapping(event.rule_id);
    if (mapping_index == kNotFound) {
        return reject(CriticalAlarmExportError::rule_not_mapped);
    }
    if (event.kind != alarm::AlarmEventKind::asserted &&
        event.kind != alarm::AlarmEventKind::cleared) {
        return reject(CriticalAlarmExportError::event_not_exportable);
    }
    const bool asserting = event.kind == alarm::AlarmEventKind::asserted;
    if ((asserting &&
         (event.lifecycle_after != alarm::AlarmLifecycle::active ||
          !event.condition_present)) ||
        (!asserting &&
         (event.lifecycle_after != alarm::AlarmLifecycle::inactive ||
          event.condition_present))) {
        return reject(CriticalAlarmExportError::invalid_event);
    }
    if (now_ms < event.occurred_at_ms) {
        return reject(CriticalAlarmExportError::clock_regressed);
    }
    const auto age = now_ms - event.occurred_at_ms;
    if (age > kMaximumCriticalAlarmExportAgeMs) {
        return reject(CriticalAlarmExportError::event_too_old);
    }
    if (status_.next_event_id == 0 ||
        (asserting && status_.next_condition_id == 0)) {
        return reject(CriticalAlarmExportError::identity_exhausted);
    }

    auto& slot = mappings_[mapping_index];
    if ((asserting && slot.condition_active) ||
        (!asserting && !slot.condition_active)) {
        return reject(CriticalAlarmExportError::lifecycle_conflict);
    }

    AlertQuality quality{};
    const auto quality_error = map_quality(event.signal_quality, quality);
    if (quality_error != CriticalAlarmExportError::none) {
        return reject(quality_error);
    }
    if (asserting && quality != AlertQuality::valid &&
        quality != AlertQuality::suspect) {
        return reject(CriticalAlarmExportError::incompatible_quality);
    }

    AlertUnit unit{};
    std::int32_t value = 0;
    bool value_present = false;
    const auto value_error = map_value(
        event, unit, value, value_present);
    if (value_error != CriticalAlarmExportError::none) {
        return reject(value_error);
    }

    CriticalAlert alert{};
    alert.type = slot.mapping.alert_type;
    alert.severity = slot.mapping.severity;
    alert.state = asserting ? AlertState::asserted : AlertState::cleared;
    alert.quality = quality;
    alert.unit = unit;
    alert.producer_id = identity_.producer_id;
    alert.vehicle_id = identity_.vehicle_id;
    alert.event_id = status_.next_event_id;
    alert.condition_id = asserting
                             ? status_.next_condition_id
                             : slot.active_condition_id;
    alert.age_ms = static_cast<std::uint32_t>(age);
    alert.value_milli = value;
    alert.diagnostic_code = slot.mapping.diagnostic_code;
    alert.value_present = value_present;

    CriticalAlarmExportResult result{};
    result.alert = alert;
    const auto encoded = encode_critical_alert(alert, result.encoded);
    if (!encoded.encoded()) {
        result.error = CriticalAlarmExportError::codec_failure;
        result.codec_error = encoded.error;
        ++status_.events_rejected;
        return result;
    }

    result.error = CriticalAlarmExportError::none;
    result.encoded_bytes = encoded.encoded_bytes;
    if (asserting) {
        slot.condition_active = true;
        slot.active_condition_id = status_.next_condition_id;
        ++status_.active_condition_count;
        ++status_.assertions_exported;
        status_.next_condition_id =
            status_.next_condition_id ==
                    std::numeric_limits<std::uint64_t>::max()
                ? 0
                : status_.next_condition_id + 1U;
    } else {
        slot.condition_active = false;
        slot.active_condition_id = 0;
        --status_.active_condition_count;
        ++status_.clears_exported;
    }
    status_.next_event_id =
        status_.next_event_id ==
                std::numeric_limits<std::uint64_t>::max()
            ? 0
            : status_.next_event_id + 1U;
    return result;
}

CriticalAlarmExporterStatus CriticalAlarmExporter::status() const {
    return status_;
}

std::size_t CriticalAlarmExporter::find_mapping(
    std::uint16_t rule_id) const {
    for (std::size_t index = 0; index < mappings_.size(); ++index) {
        if (mappings_[index].occupied &&
            mappings_[index].mapping.alarm_rule_id == rule_id) {
            return index;
        }
    }
    return kNotFound;
}

}  // namespace opengauge::integration
