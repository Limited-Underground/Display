#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>

#include "opengauge/critical_alarm_exporter.hpp"

namespace {

using namespace opengauge;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

integration::CriticalAlarmMapping mapping(
    std::uint16_t rule_id,
    integration::CriticalAlertType type =
        integration::CriticalAlertType::generic_critical,
    integration::AlertSeverity severity =
        integration::AlertSeverity::critical) {
    return {rule_id, type, severity, 0x1234U};
}

alarm::AlarmEvent event(
    std::uint16_t rule_id,
    alarm::AlarmEventKind kind,
    telemetry::SignalQuality quality,
    telemetry::SignalUnit unit,
    std::int64_t value,
    bool value_present,
    std::uint64_t occurred_at_ms) {
    alarm::AlarmEvent result{};
    result.rule_id = rule_id;
    result.kind = kind;
    result.severity = alarm::AlarmSeverity::critical;
    result.lifecycle_after =
        kind == alarm::AlarmEventKind::asserted
            ? alarm::AlarmLifecycle::active
            : alarm::AlarmLifecycle::inactive;
    result.signal_quality = quality;
    result.value = {
        telemetry::SignalValueType::signed_integer,
        value,
        value_present};
    result.unit = unit;
    result.occurred_at_ms = occurred_at_ms;
    result.condition_present =
        kind == alarm::AlarmEventKind::asserted;
    return result;
}

integration::CriticalAlarmExporterIdentity identity(
    std::uint64_t first_event_id = 100,
    std::uint64_t first_condition_id = 200) {
    return {1, 2, first_event_id, first_condition_id};
}

void test_mapping_validation_capacity_and_lifecycle() {
    integration::CriticalAlarmExporter exporter{};
    EXPECT(exporter.start(identity()) ==
           integration::CriticalAlarmExportError::invalid_configuration);
    EXPECT(exporter.add_mapping(mapping(0)) ==
           integration::CriticalAlarmExportError::invalid_mapping);
    EXPECT(exporter.add_mapping(mapping(
               1,
               integration::CriticalAlertType::generic_critical,
               integration::AlertSeverity::warning)) ==
           integration::CriticalAlarmExportError::invalid_mapping);
    EXPECT(exporter.add_mapping(mapping(
               1,
               integration::CriticalAlertType::rollover_detected,
               integration::AlertSeverity::critical)) ==
           integration::CriticalAlarmExportError::invalid_mapping);
    for (std::uint16_t id = 1;
         id <= integration::kMaximumCriticalAlarmMappings;
         ++id) {
        EXPECT(exporter.add_mapping(mapping(id)) ==
               integration::CriticalAlarmExportError::none);
    }
    EXPECT(exporter.add_mapping(mapping(1)) ==
           integration::CriticalAlarmExportError::duplicate_mapping);
    EXPECT(exporter.add_mapping(mapping(17)) ==
           integration::CriticalAlarmExportError::mapping_capacity_full);
    auto invalid_identity = identity();
    invalid_identity.vehicle_id = 0;
    EXPECT(exporter.start(invalid_identity) ==
           integration::CriticalAlarmExportError::invalid_configuration);
    EXPECT(exporter.start(identity()) ==
           integration::CriticalAlarmExportError::none);
    EXPECT(exporter.add_mapping(mapping(17)) ==
           integration::CriticalAlarmExportError::invalid_state);
    EXPECT(exporter.clear_mappings() ==
           integration::CriticalAlarmExportError::invalid_state);
    exporter.stop();
    EXPECT(exporter.clear_mappings() ==
           integration::CriticalAlarmExportError::none);
}

void test_assert_clear_round_trip_uses_stable_condition_and_unique_events() {
    integration::CriticalAlarmExporter exporter{};
    EXPECT(exporter.add_mapping(mapping(
               1,
               integration::CriticalAlertType::engine_over_temperature)) ==
           integration::CriticalAlarmExportError::none);
    EXPECT(exporter.start(identity()) ==
           integration::CriticalAlarmExportError::none);
    const auto asserted = event(
        1,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        105000,
        true,
        10);
    auto exported = exporter.export_event(asserted, 15);
    EXPECT(exported.exported());
    EXPECT(exported.alert.event_id == 100);
    EXPECT(exported.alert.condition_id == 200);
    EXPECT(exported.alert.age_ms == 5);
    EXPECT(exported.alert.value_milli == 105000);
    EXPECT(exported.alert.unit == integration::AlertUnit::celsius);
    const auto decoded = integration::decode_critical_alert(
        exported.encoded.data(), exported.encoded_bytes);
    EXPECT(decoded.decoded());
    EXPECT(decoded.alert.event_id == 100);
    EXPECT(decoded.alert.condition_id == 200);

    const auto cleared = event(
        1,
        alarm::AlarmEventKind::cleared,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        90000,
        true,
        20);
    exported = exporter.export_event(cleared, 20);
    EXPECT(exported.exported());
    EXPECT(exported.alert.state == integration::AlertState::cleared);
    EXPECT(exported.alert.event_id == 101);
    EXPECT(exported.alert.condition_id == 200);
    EXPECT(exporter.status().active_condition_count == 0);
    EXPECT(exporter.status().assertions_exported == 1);
    EXPECT(exporter.status().clears_exported == 1);
}

void test_nontransition_events_do_not_consume_identity() {
    integration::CriticalAlarmExporter exporter{};
    EXPECT(exporter.add_mapping(mapping(1)) ==
           integration::CriticalAlarmExportError::none);
    EXPECT(exporter.start(identity()) ==
           integration::CriticalAlarmExportError::none);
    auto reminder = event(
        1,
        alarm::AlarmEventKind::reminder,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        100000,
        true,
        0);
    reminder.lifecycle_after = alarm::AlarmLifecycle::active;
    reminder.condition_present = true;
    EXPECT(exporter.export_event(reminder, 0).error ==
           integration::CriticalAlarmExportError::event_not_exportable);
    auto acknowledged = reminder;
    acknowledged.kind = alarm::AlarmEventKind::acknowledged;
    EXPECT(exporter.export_event(acknowledged, 0).error ==
           integration::CriticalAlarmExportError::event_not_exportable);
    auto latched = reminder;
    latched.kind = alarm::AlarmEventKind::condition_cleared_latched;
    latched.lifecycle_after = alarm::AlarmLifecycle::latched;
    latched.condition_present = false;
    EXPECT(exporter.export_event(latched, 0).error ==
           integration::CriticalAlarmExportError::event_not_exportable);
    EXPECT(exporter.status().next_event_id == 100);
    EXPECT(exporter.status().next_condition_id == 200);

    const auto asserted = event(
        1,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        100000,
        true,
        0);
    const auto exported = exporter.export_event(asserted, 0);
    EXPECT(exported.exported());
    EXPECT(exported.alert.event_id == 100);
    EXPECT(exported.alert.condition_id == 200);
}

void test_nonvalid_assertion_and_invalid_lifecycle_fail_closed() {
    integration::CriticalAlarmExporter exporter{};
    EXPECT(exporter.add_mapping(mapping(1)) ==
           integration::CriticalAlarmExportError::none);
    EXPECT(exporter.start(identity()) ==
           integration::CriticalAlarmExportError::none);
    const auto stale = event(
        1,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::stale,
        telemetry::SignalUnit::milli_celsius,
        0,
        false,
        0);
    EXPECT(exporter.export_event(stale, 0).error ==
           integration::CriticalAlarmExportError::incompatible_quality);
    auto invalid = event(
        1,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        100000,
        true,
        0);
    invalid.lifecycle_after = alarm::AlarmLifecycle::inactive;
    EXPECT(exporter.export_event(invalid, 0).error ==
           integration::CriticalAlarmExportError::invalid_event);
    invalid = event(
        1,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        1,
        true,
        0);
    invalid.value.type = telemetry::SignalValueType::boolean;
    EXPECT(exporter.export_event(invalid, 0).error ==
           integration::CriticalAlarmExportError::invalid_event);
    invalid.value.present = false;
    EXPECT(exporter.export_event(invalid, 0).error ==
           integration::CriticalAlarmExportError::invalid_event);
    EXPECT(exporter.status().active_condition_count == 0);
    EXPECT(exporter.status().next_event_id == 100);
}

void test_canonical_unit_conversions_and_unsupported_units() {
    struct Case {
        telemetry::SignalUnit source_unit;
        telemetry::SignalValueType value_type;
        std::int64_t source_value;
        integration::AlertUnit expected_unit;
        std::int32_t expected_value;
    };
    const std::array<Case, 6> cases{{
        {telemetry::SignalUnit::milli_celsius,
         telemetry::SignalValueType::signed_integer,
         87500, integration::AlertUnit::celsius, 87500},
        {telemetry::SignalUnit::pascal,
         telemetry::SignalValueType::unsigned_integer,
         500000, integration::AlertUnit::kilopascal, 500000},
        {telemetry::SignalUnit::millivolt,
         telemetry::SignalValueType::unsigned_integer,
         13800, integration::AlertUnit::volt, 13800},
        {telemetry::SignalUnit::milli_percent,
         telemetry::SignalValueType::unsigned_integer,
         50000, integration::AlertUnit::percent, 50000},
        {telemetry::SignalUnit::milli_revolutions_per_minute,
         telemetry::SignalValueType::unsigned_integer,
         1500000, integration::AlertUnit::revolutions_per_minute, 1500000},
        {telemetry::SignalUnit::none,
         telemetry::SignalValueType::boolean,
         1, integration::AlertUnit::boolean, 1000},
    }};

    integration::CriticalAlarmExporter exporter{};
    for (std::uint16_t id = 1; id <= cases.size(); ++id) {
        EXPECT(exporter.add_mapping(mapping(id)) ==
               integration::CriticalAlarmExportError::none);
    }
    EXPECT(exporter.add_mapping(mapping(7)) ==
           integration::CriticalAlarmExportError::none);
    EXPECT(exporter.start(identity()) ==
           integration::CriticalAlarmExportError::none);
    for (std::size_t index = 0; index < cases.size(); ++index) {
        auto asserted = event(
            static_cast<std::uint16_t>(index + 1U),
            alarm::AlarmEventKind::asserted,
            telemetry::SignalQuality::valid,
            cases[index].source_unit,
            cases[index].source_value,
            true,
            index);
        asserted.value.type = cases[index].value_type;
        const auto exported = exporter.export_event(asserted, index);
        EXPECT(exported.exported());
        EXPECT(exported.alert.unit == cases[index].expected_unit);
        EXPECT(exported.alert.value_milli == cases[index].expected_value);
    }

    const auto unsupported = event(
        7,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::count,
        1,
        true,
        10);
    EXPECT(exporter.export_event(unsupported, 10).error ==
           integration::CriticalAlarmExportError::unsupported_unit);
    EXPECT(exporter.status().next_event_id == 106);
}

void test_codec_incompatibility_does_not_commit_lifecycle_or_ids() {
    integration::CriticalAlarmExporter exporter{};
    EXPECT(exporter.add_mapping(mapping(
               1,
               integration::CriticalAlertType::engine_over_temperature)) ==
           integration::CriticalAlarmExportError::none);
    EXPECT(exporter.start(identity()) ==
           integration::CriticalAlarmExportError::none);
    const auto wrong_unit = event(
        1,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::millivolt,
        12000,
        true,
        0);
    const auto failed = exporter.export_event(wrong_unit, 0);
    EXPECT(failed.error == integration::CriticalAlarmExportError::codec_failure);
    EXPECT(failed.codec_error == integration::AlertCodecError::invalid_type_unit);
    EXPECT(exporter.status().active_condition_count == 0);
    EXPECT(exporter.status().next_event_id == 100);
    EXPECT(exporter.status().next_condition_id == 200);

    const auto correct = event(
        1,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        100000,
        true,
        0);
    EXPECT(exporter.export_event(correct, 0).exported());
}

void test_lifecycle_age_and_identity_exhaustion() {
    integration::CriticalAlarmExporter exporter{};
    EXPECT(exporter.add_mapping(mapping(1)) ==
           integration::CriticalAlarmExportError::none);
    EXPECT(exporter.start(identity()) ==
           integration::CriticalAlarmExportError::none);
    const auto cleared = event(
        1,
        alarm::AlarmEventKind::cleared,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        90000,
        true,
        0);
    EXPECT(exporter.export_event(cleared, 0).error ==
           integration::CriticalAlarmExportError::lifecycle_conflict);
    const auto asserted = event(
        1,
        alarm::AlarmEventKind::asserted,
        telemetry::SignalQuality::valid,
        telemetry::SignalUnit::milli_celsius,
        100000,
        true,
        100);
    EXPECT(exporter.export_event(asserted, 99).error ==
           integration::CriticalAlarmExportError::clock_regressed);
    EXPECT(exporter.export_event(asserted, 120101).error ==
           integration::CriticalAlarmExportError::event_too_old);
    EXPECT(exporter.export_event(asserted, 100).exported());
    EXPECT(exporter.export_event(asserted, 100).error ==
           integration::CriticalAlarmExportError::lifecycle_conflict);
    EXPECT(exporter.export_event(cleared, 101).exported());

    exporter.stop();
    EXPECT(exporter.start(identity(
               std::numeric_limits<std::uint64_t>::max(),
               std::numeric_limits<std::uint64_t>::max())) ==
           integration::CriticalAlarmExportError::none);
    EXPECT(exporter.export_event(asserted, 100).exported());
    EXPECT(exporter.status().next_event_id == 0);
    EXPECT(exporter.status().next_condition_id == 0);
    EXPECT(exporter.export_event(cleared, 101).error ==
           integration::CriticalAlarmExportError::identity_exhausted);
}

}  // namespace

int main() {
    test_mapping_validation_capacity_and_lifecycle();
    test_assert_clear_round_trip_uses_stable_condition_and_unique_events();
    test_nontransition_events_do_not_consume_identity();
    test_nonvalid_assertion_and_invalid_lifecycle_fail_closed();
    test_canonical_unit_conversions_and_unsupported_units();
    test_codec_incompatibility_does_not_commit_lifecycle_or_ids();
    test_lifecycle_age_and_identity_exhaustion();

    if (failures != 0) {
        std::cerr << failures << " critical alarm exporter assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 critical alarm exporter scenario groups\n";
    return EXIT_SUCCESS;
}
