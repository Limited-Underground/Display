#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "opengauge/normalized_signal.hpp"

namespace {

using namespace opengauge::telemetry;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

NormalizedSignal engine_speed_signal() {
    NormalizedSignal signal{};
    EXPECT(make_signal_id("engine.speed", signal.id) ==
           SignalModelError::none);
    signal.value = {
        SignalValueType::unsigned_integer,
        1250000,
        true,
    };
    signal.unit = SignalUnit::milli_revolutions_per_minute;
    signal.quality = SignalQuality::valid;
    signal.source.protocol = SignalSourceProtocol::j1939;
    signal.source.source_address = 0x00;
    signal.source.parameter_group_number = 0xF004;
    signal.source.parameter_number = 190;
    signal.source.source_address_present = true;
    signal.source.parameter_group_present = true;
    signal.source.parameter_present = true;
    signal.received_at_ms = 1000;
    signal.sampled_at_ms = 990;
    signal.sample_time_present = true;
    return signal;
}

void test_namespaced_signal_ids() {
    SignalId id{};
    EXPECT(make_signal_id("engine.coolant_temperature", id) ==
           SignalModelError::none);
    EXPECT(signal_id_equals(id, "engine.coolant_temperature"));
    EXPECT(make_signal_id("engine", id) ==
           SignalModelError::invalid_signal_id);
    EXPECT(make_signal_id("Engine.speed", id) ==
           SignalModelError::invalid_signal_id);
    EXPECT(make_signal_id("engine..speed", id) ==
           SignalModelError::invalid_signal_id);
    EXPECT(make_signal_id("engine.1speed", id) ==
           SignalModelError::invalid_signal_id);
    EXPECT(make_signal_id("engine.speed-kph", id) ==
           SignalModelError::invalid_signal_id);

    const std::string too_long(kMaximumSignalIdBytes + 1, 'a');
    EXPECT(make_signal_id(too_long, id) ==
           SignalModelError::invalid_signal_id);
}

void test_valid_typed_reference_signals() {
    const auto engine_speed = engine_speed_signal();
    EXPECT(validate_normalized_signal(engine_speed) ==
           SignalModelError::none);

    auto switch_signal = engine_speed;
    EXPECT(make_signal_id("vehicle.ignition_on", switch_signal.id) ==
           SignalModelError::none);
    switch_signal.value = {SignalValueType::boolean, 1, true};
    switch_signal.unit = SignalUnit::none;
    switch_signal.quality = SignalQuality::suspect;
    EXPECT(validate_normalized_signal(switch_signal) ==
           SignalModelError::none);
}

void test_quality_and_value_consistency() {
    auto signal = engine_speed_signal();
    signal.quality = SignalQuality::unavailable;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::inconsistent_value);

    signal = engine_speed_signal();
    signal.value.present = false;
    signal.value.raw_value = 0;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::inconsistent_value);

    signal = engine_speed_signal();
    signal.value = {SignalValueType::boolean, 2, true};
    signal.unit = SignalUnit::none;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::invalid_boolean);

    signal = engine_speed_signal();
    signal.value = {SignalValueType::boolean, 1, true};
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::invalid_unit);

    signal = engine_speed_signal();
    signal.value.raw_value = -1;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::negative_unsigned_value);

    signal = engine_speed_signal();
    signal.value.raw_value = 20000001;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::value_out_of_range);

    signal = engine_speed_signal();
    signal.quality = SignalQuality::stale;
    signal.value.present = false;
    signal.value.raw_value = 42;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::inconsistent_value);
}

void test_source_metadata_is_protocol_specific() {
    auto signal = engine_speed_signal();
    signal.source.parameter_group_present = false;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::invalid_source);

    signal = engine_speed_signal();
    signal.source.parameter_group_number =
        kMaximumClassicalJ1939Pgn + 1U;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::invalid_source);

    signal = engine_speed_signal();
    signal.source.protocol = SignalSourceProtocol::obd2;
    signal.source.source_address_present = false;
    signal.source.parameter_group_present = false;
    signal.source.parameter_group_number = 0;
    signal.source.parameter_number = 0x010CU;
    signal.source.parameter_present = true;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::none);

    signal.source.source_address_present = true;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::invalid_source);
}

void test_sample_and_receive_time_rules() {
    auto signal = engine_speed_signal();
    signal.sampled_at_ms = signal.received_at_ms + 1U;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::inconsistent_time);

    signal = engine_speed_signal();
    signal.sample_time_present = false;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::inconsistent_time);

    signal.sampled_at_ms = 0;
    EXPECT(validate_normalized_signal(signal) ==
           SignalModelError::none);
}

void test_freshness_boundaries_and_clock_regression() {
    auto signal = engine_speed_signal();
    auto result = evaluate_signal_freshness(signal, 1989, 1000);
    EXPECT(result.evaluated());
    EXPECT(result.age_ms == 999U);
    EXPECT(result.effective_quality == SignalQuality::valid);

    result = evaluate_signal_freshness(signal, 1990, 1000);
    EXPECT(result.evaluated());
    EXPECT(result.age_ms == 1000U);
    EXPECT(result.effective_quality == SignalQuality::stale);

    result = evaluate_signal_freshness(signal, 989, 1000);
    EXPECT(result.error == SignalModelError::clock_regressed);

    result = evaluate_signal_freshness(signal, 1990, 0);
    EXPECT(result.error == SignalModelError::invalid_stale_threshold);

    signal.quality = SignalQuality::unavailable;
    signal.value.present = false;
    signal.value.raw_value = 0;
    result = evaluate_signal_freshness(signal, 9990, 1000);
    EXPECT(result.evaluated());
    EXPECT(result.effective_quality == SignalQuality::unavailable);
}

}  // namespace

int main() {
    test_namespaced_signal_ids();
    test_valid_typed_reference_signals();
    test_quality_and_value_consistency();
    test_source_metadata_is_protocol_specific();
    test_sample_and_receive_time_rules();
    test_freshness_boundaries_and_clock_regression();

    if (failures != 0) {
        std::cerr << failures << " normalized signal assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 6 normalized signal scenario groups\n";
    return EXIT_SUCCESS;
}
