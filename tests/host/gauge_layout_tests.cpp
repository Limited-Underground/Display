#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

#include "fake_gauge_layout_storage.hpp"
#include "opengauge/gauge_layout.hpp"

namespace {

using namespace opengauge;
using configuration::test_support::FakeGaugeLayoutStorage;
using configuration::test_support::FakeEraseBehavior;
using configuration::test_support::FakeWriteBehavior;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

display::GaugeWidgetConfiguration widget(
    std::uint16_t id,
    wireless::TelemetrySignalCode code,
    display::GaugeWidgetKind kind,
    std::string_view label) {
    display::GaugeWidgetConfiguration result{};
    result.widget_id = id;
    result.signal_code = code;
    result.kind = kind;
    EXPECT(display::make_gauge_widget_label(label, result.label) ==
           display::GaugeViewModelError::none);
    result.stale_after_ms = 1500;
    if (kind == display::GaugeWidgetKind::needle ||
        kind == display::GaugeWidgetKind::bar) {
        result.scale_min_raw = -100;
        result.scale_max_raw = 5000000;
    }
    return result;
}

configuration::GaugeLayout layout(std::uint64_t generation) {
    configuration::GaugeLayout result{};
    result.generation = generation;
    result.layout_id = 0x10203040U;
    result.brightness_percent = 65;
    result.theme = configuration::GaugeTheme::high_contrast;
    result.widget_count = 2;
    result.widgets[0] = widget(
        7, wireless::TelemetrySignalCode::engine_speed,
        display::GaugeWidgetKind::needle, "Engine RPM");
    result.widgets[1] = widget(
        8, wireless::TelemetrySignalCode::engine_coolant_temperature,
        display::GaugeWidgetKind::numeric, "Coolant");
    return result;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

void repair_crc(
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes>& bytes) {
    constexpr std::size_t offset = configuration::kGaugeLayoutRecordBytes - 4;
    const auto value = crc32(bytes.data(), offset);
    for (std::size_t index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void test_codec_round_trip_and_explicit_wire_fields() {
    const auto expected = layout(0x0102030405060708ULL);
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> bytes{};
    const auto encoded = configuration::encode_gauge_layout(
        expected, bytes.data(), bytes.size());
    EXPECT(encoded.succeeded());
    EXPECT(encoded.bytes == bytes.size());
    EXPECT(bytes[0] == 'O' && bytes[1] == 'G' &&
           bytes[2] == 'L' && bytes[3] == '0');
    EXPECT(bytes[4] == 1 && bytes[5] == 0);
    EXPECT(bytes[6] == 32 && bytes[7] == 0);
    EXPECT(bytes[8] == 0x08 && bytes[15] == 0x01);
    EXPECT(bytes[16] == 0x40 && bytes[19] == 0x10);
    EXPECT(bytes[20] == 2 && bytes[21] == 65 && bytes[22] == 3);
    EXPECT(bytes[32] == 7 && bytes[33] == 0);
    EXPECT(bytes[36] == 10);

    configuration::GaugeLayout decoded{};
    const auto result = configuration::decode_gauge_layout(
        bytes.data(), bytes.size(), decoded);
    EXPECT(result.succeeded());
    EXPECT(decoded.generation == expected.generation);
    EXPECT(decoded.layout_id == expected.layout_id);
    EXPECT(decoded.widget_count == 2);
    EXPECT(decoded.widgets[0].widget_id == 7);
    EXPECT(decoded.widgets[0].scale_min_raw == -100);
    EXPECT(decoded.widgets[0].scale_max_raw == 5000000);
    EXPECT(decoded.widgets[0].label.length == 10);
    EXPECT(decoded.widgets[1].signal_code ==
           wireless::TelemetrySignalCode::engine_coolant_temperature);
}

void test_layout_validation_and_duplicate_rejection() {
    auto candidate = layout(1);
    candidate.generation = 0;
    EXPECT(configuration::validate_gauge_layout(candidate) ==
           configuration::GaugeLayoutCodecError::invalid_layout);
    candidate = layout(1);
    candidate.brightness_percent = 101;
    EXPECT(configuration::validate_gauge_layout(candidate) ==
           configuration::GaugeLayoutCodecError::invalid_layout);
    candidate = layout(1);
    candidate.widgets[1].widget_id = candidate.widgets[0].widget_id;
    EXPECT(configuration::validate_gauge_layout(candidate) ==
           configuration::GaugeLayoutCodecError::duplicate_widget);
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> bytes{};
    EXPECT(configuration::encode_gauge_layout(
               layout(1), nullptr, bytes.size()).error ==
           configuration::GaugeLayoutCodecError::invalid_argument);
}

void test_decoder_rejects_incompatible_corrupt_and_noncanonical_records() {
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> good{};
    EXPECT(configuration::encode_gauge_layout(
               layout(1), good.data(), good.size()).succeeded());
    configuration::GaugeLayout unchanged = layout(99);

    auto bytes = good;
    bytes[0] = 'X';
    EXPECT(configuration::decode_gauge_layout(
               bytes.data(), bytes.size(), unchanged).error ==
           configuration::GaugeLayoutCodecError::bad_magic);
    bytes = good;
    bytes[4] = 2;
    EXPECT(configuration::decode_gauge_layout(
               bytes.data(), bytes.size(), unchanged).error ==
           configuration::GaugeLayoutCodecError::unsupported_version);
    bytes = good;
    bytes[100] ^= 0x11U;
    EXPECT(configuration::decode_gauge_layout(
               bytes.data(), bytes.size(), unchanged).error ==
           configuration::GaugeLayoutCodecError::checksum_mismatch);
    bytes = good;
    bytes[160] = 1;
    repair_crc(bytes);
    EXPECT(configuration::decode_gauge_layout(
               bytes.data(), bytes.size(), unchanged).error ==
           configuration::GaugeLayoutCodecError::noncanonical_record);
    EXPECT(unchanged.generation == 99);
}

void test_empty_store_uses_validated_safe_default() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    auto safe = layout(1);
    configuration::GaugeLayout loaded{};
    const auto result = store.load(safe, loaded);
    EXPECT(result.error == configuration::GaugeLayoutStoreError::none);
    EXPECT(result.source == configuration::GaugeLayoutSource::safe_default);
    EXPECT(result.recovery_required);
    EXPECT(loaded.generation == 1);
    safe.layout_id = 0;
    EXPECT(store.load(safe, loaded).error ==
           configuration::GaugeLayoutStoreError::invalid_layout);
}

void test_two_slot_rotation_generation_and_write_wear() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    EXPECT(store.save(layout(1)).written_slot ==
           configuration::GaugeLayoutSource::slot_a);
    EXPECT(store.save(layout(2)).written_slot ==
           configuration::GaugeLayoutSource::slot_b);
    EXPECT(store.save(layout(3)).written_slot ==
           configuration::GaugeLayoutSource::slot_a);
    EXPECT(store.save(layout(3)).error ==
           configuration::GaugeLayoutStoreError::stale_generation);
    EXPECT(storage.writes(0) == 2);
    EXPECT(storage.writes(1) == 1);
    configuration::GaugeLayout loaded{};
    const auto load = store.load(layout(1), loaded);
    EXPECT(load.source == configuration::GaugeLayoutSource::slot_a);
    EXPECT(!load.recovery_required);
    EXPECT(loaded.generation == 3);
}

void test_equal_generation_conflict_falls_back_visibly() {
    FakeGaugeLayoutStorage storage{};
    auto first = layout(5);
    auto second = layout(5);
    second.brightness_percent = 80;
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> encoded{};
    EXPECT(configuration::encode_gauge_layout(
               first, encoded.data(), encoded.size()).succeeded());
    EXPECT(storage.write_slot(0, encoded.data(), encoded.size()) ==
           configuration::LayoutStorageError::none);
    EXPECT(configuration::encode_gauge_layout(
               second, encoded.data(), encoded.size()).succeeded());
    EXPECT(storage.write_slot(1, encoded.data(), encoded.size()) ==
           configuration::LayoutStorageError::none);

    configuration::GaugeLayoutStore store{storage};
    configuration::GaugeLayout loaded{};
    const auto result = store.load(layout(1), loaded);
    EXPECT(result.error ==
           configuration::GaugeLayoutStoreError::generation_conflict);
    EXPECT(result.source == configuration::GaugeLayoutSource::safe_default);
    EXPECT(result.recovery_required);
    EXPECT(loaded.generation == 1);
}

void test_partial_write_keeps_last_good_slot() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    EXPECT(store.save(layout(1)).saved());
    storage.set_next_write_behavior(
        1, FakeWriteBehavior::fail_after_partial_write);
    EXPECT(store.save(layout(2)).error ==
           configuration::GaugeLayoutStoreError::storage_failure);
    configuration::GaugeLayout loaded{};
    const auto recovered = store.load(layout(1), loaded);
    EXPECT(recovered.source == configuration::GaugeLayoutSource::slot_a);
    EXPECT(recovered.slot_b == configuration::LayoutSlotState::invalid);
    EXPECT(recovered.recovery_required);
    EXPECT(loaded.generation == 1);
    EXPECT(store.save(layout(2)).saved());
}

void test_verify_failure_keeps_other_good_slot() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    EXPECT(store.save(layout(1)).saved());
    EXPECT(store.save(layout(2)).saved());
    storage.set_next_write_behavior(
        0, FakeWriteBehavior::corrupt_after_success);
    EXPECT(store.save(layout(3)).error ==
           configuration::GaugeLayoutStoreError::verification_failure);
    configuration::GaugeLayout loaded{};
    const auto recovered = store.load(layout(1), loaded);
    EXPECT(recovered.source == configuration::GaugeLayoutSource::slot_b);
    EXPECT(recovered.recovery_required);
    EXPECT(loaded.generation == 2);
}

void test_io_failure_and_reset_are_explicit() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    storage.fail_next_read(0);
    configuration::GaugeLayout loaded{};
    const auto fallback = store.load(layout(1), loaded);
    EXPECT(fallback.error ==
           configuration::GaugeLayoutStoreError::storage_failure);
    EXPECT(fallback.source ==
           configuration::GaugeLayoutSource::safe_default);
    EXPECT(fallback.has_usable_layout());
    EXPECT(store.save(layout(1)).saved());
    storage.fail_next_erase(0);
    EXPECT(store.reset() ==
           configuration::GaugeLayoutStoreError::storage_failure);
    EXPECT(storage.erases(0) == 1 && storage.erases(1) == 1);
    EXPECT(!storage.present(1));

    FakeGaugeLayoutStorage uncertain_storage{};
    configuration::GaugeLayoutStore uncertain_store{uncertain_storage};
    EXPECT(uncertain_store.save(layout(1)).saved());
    uncertain_storage.set_next_erase_behavior(
        0, FakeEraseBehavior::fail_after_erase);
    EXPECT(uncertain_store.reset() ==
           configuration::GaugeLayoutStoreError::commit_uncertain);
    EXPECT(!uncertain_storage.present(0) &&
           !uncertain_storage.present(1));
}

void test_store_allocates_generations_and_suppresses_unchanged_writes() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    const auto first = store.save_next_if_changed(layout(0));
    EXPECT(first.succeeded() && first.changed());
    EXPECT(first.generation == 1);
    EXPECT(first.active_slot == configuration::GaugeLayoutSource::slot_a);
    EXPECT(storage.writes(0) == 1 && storage.writes(1) == 0);

    const auto unchanged = store.save_next_if_changed(layout(999));
    EXPECT(unchanged.succeeded() && !unchanged.changed());
    EXPECT(unchanged.generation == 1);
    EXPECT(unchanged.active_slot ==
           configuration::GaugeLayoutSource::slot_a);
    EXPECT(storage.writes(0) == 1 && storage.writes(1) == 0);

    auto changed = layout(0);
    changed.brightness_percent = 66;
    const auto second = store.save_next_if_changed(changed);
    EXPECT(second.succeeded() && second.changed());
    EXPECT(second.generation == 2);
    EXPECT(second.active_slot == configuration::GaugeLayoutSource::slot_b);
    EXPECT(storage.writes(0) == 1 && storage.writes(1) == 1);
}

void test_automatic_update_rejects_invalid_conflict_and_exhaustion() {
    FakeGaugeLayoutStorage invalid_storage{};
    configuration::GaugeLayoutStore invalid_store{invalid_storage};
    auto invalid = layout(0);
    invalid.layout_id = 0;
    EXPECT(invalid_store.save_next_if_changed(invalid).error ==
           configuration::GaugeLayoutStoreError::invalid_layout);
    EXPECT(invalid_storage.writes(0) + invalid_storage.writes(1) == 0);

    FakeGaugeLayoutStorage conflict_storage{};
    auto first = layout(4);
    auto second = layout(4);
    second.brightness_percent = 80;
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> bytes{};
    EXPECT(configuration::encode_gauge_layout(
               first, bytes.data(), bytes.size()).succeeded());
    EXPECT(conflict_storage.write_slot(
               0, bytes.data(), bytes.size()) ==
           configuration::LayoutStorageError::none);
    EXPECT(configuration::encode_gauge_layout(
               second, bytes.data(), bytes.size()).succeeded());
    EXPECT(conflict_storage.write_slot(
               1, bytes.data(), bytes.size()) ==
           configuration::LayoutStorageError::none);
    configuration::GaugeLayoutStore conflict_store{conflict_storage};
    EXPECT(conflict_store.save_next_if_changed(layout(0)).error ==
           configuration::GaugeLayoutStoreError::generation_conflict);
    EXPECT(conflict_storage.writes(0) + conflict_storage.writes(1) == 2);

    FakeGaugeLayoutStorage exhausted_storage{};
    const auto exhausted = layout(
        std::numeric_limits<std::uint64_t>::max());
    EXPECT(configuration::encode_gauge_layout(
               exhausted, bytes.data(), bytes.size()).succeeded());
    EXPECT(exhausted_storage.write_slot(
               0, bytes.data(), bytes.size()) ==
           configuration::LayoutStorageError::none);
    configuration::GaugeLayoutStore exhausted_store{exhausted_storage};
    auto changed = layout(0);
    changed.brightness_percent = 90;
    EXPECT(exhausted_store.save_next_if_changed(changed).error ==
           configuration::GaugeLayoutStoreError::generation_exhausted);
    EXPECT(exhausted_storage.writes(0) + exhausted_storage.writes(1) == 1);
}

void test_automatic_update_keeps_storage_failures_visible() {
    FakeGaugeLayoutStorage read_storage{};
    read_storage.fail_next_read(0);
    configuration::GaugeLayoutStore read_store{read_storage};
    EXPECT(read_store.save_next_if_changed(layout(0)).error ==
           configuration::GaugeLayoutStoreError::storage_failure);
    EXPECT(read_storage.writes(0) + read_storage.writes(1) == 0);

    FakeGaugeLayoutStorage write_storage{};
    write_storage.set_next_write_behavior(
        0, FakeWriteBehavior::fail_before_write);
    configuration::GaugeLayoutStore write_store{write_storage};
    EXPECT(write_store.save_next_if_changed(layout(0)).error ==
           configuration::GaugeLayoutStoreError::storage_failure);
    EXPECT(write_storage.writes(0) == 1 && !write_storage.present(0));
}

void test_canonical_export_is_atomic_and_preserves_recovery_evidence() {
    FakeGaugeLayoutStorage empty_storage{};
    configuration::GaugeLayoutStore empty_store{empty_storage};
    const auto safe_default = layout(1);
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> output{};
    output.fill(0xA5U);

    auto result = empty_store.export_current(
        safe_default, output.data(), output.size() - 1);
    EXPECT(result.error ==
           configuration::GaugeLayoutExportError::invalid_argument);
    EXPECT(!result.exported());
    EXPECT(output.front() == 0xA5U && output.back() == 0xA5U);

    result = empty_store.export_current(
        safe_default, output.data(), output.size());
    EXPECT(result.exported());
    EXPECT(result.load.source == configuration::GaugeLayoutSource::safe_default);
    EXPECT(result.load.recovery_required);
    configuration::GaugeLayout decoded{};
    EXPECT(configuration::decode_gauge_layout(
               output.data(), output.size(), decoded).succeeded());
    EXPECT(decoded.generation == 1);

    FakeGaugeLayoutStorage recovery_storage{};
    configuration::GaugeLayoutStore recovery_store{recovery_storage};
    EXPECT(recovery_store.save(layout(1)).saved());
    auto newer = layout(2);
    newer.brightness_percent = 72;
    EXPECT(recovery_store.save(newer).saved());
    recovery_storage.corrupt(1, 100, 0x20U);
    output.fill(0xA5U);
    result = recovery_store.export_current(
        safe_default, output.data(), output.size());
    EXPECT(result.exported());
    EXPECT(result.load.source == configuration::GaugeLayoutSource::slot_a);
    EXPECT(result.load.recovery_required);
    EXPECT(configuration::decode_gauge_layout(
               output.data(), output.size(), decoded).succeeded());
    EXPECT(decoded.generation == 1);
    EXPECT(decoded.brightness_percent == 65);

    recovery_storage.fail_next_read(1);
    output.fill(0xA5U);
    result = recovery_store.export_current(
        safe_default, output.data(), output.size());
    EXPECT(result.error ==
           configuration::GaugeLayoutExportError::storage_failure);
    EXPECT(!result.exported());
    EXPECT(result.load.source == configuration::GaugeLayoutSource::slot_a);
    EXPECT(result.load.recovery_required);
    EXPECT(output.front() == 0xA5U && output.back() == 0xA5U);

    FakeGaugeLayoutStorage conflict_storage{};
    auto first = layout(5);
    auto second = layout(5);
    second.brightness_percent = 80;
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> first_bytes{};
    std::array<std::uint8_t, configuration::kGaugeLayoutRecordBytes> second_bytes{};
    EXPECT(configuration::encode_gauge_layout(
               first, first_bytes.data(), first_bytes.size()).succeeded());
    EXPECT(configuration::encode_gauge_layout(
               second, second_bytes.data(), second_bytes.size()).succeeded());
    EXPECT(conflict_storage.write_slot(
               0, first_bytes.data(), first_bytes.size()) ==
           configuration::LayoutStorageError::none);
    EXPECT(conflict_storage.write_slot(
               1, second_bytes.data(), second_bytes.size()) ==
           configuration::LayoutStorageError::none);
    configuration::GaugeLayoutStore conflict_store{conflict_storage};
    output.fill(0xA5U);
    result = conflict_store.export_current(
        safe_default, output.data(), output.size());
    EXPECT(result.error ==
           configuration::GaugeLayoutExportError::generation_conflict);
    EXPECT(!result.exported());
    EXPECT(output.front() == 0xA5U && output.back() == 0xA5U);

    auto invalid_default = safe_default;
    invalid_default.layout_id = 0;
    output.fill(0xA5U);
    result = empty_store.export_current(
        invalid_default, output.data(), output.size());
    EXPECT(result.error ==
           configuration::GaugeLayoutExportError::invalid_safe_default);
    EXPECT(!result.exported());
    EXPECT(output.front() == 0xA5U && output.back() == 0xA5U);
}

}  // namespace

int main() {
    test_codec_round_trip_and_explicit_wire_fields();
    test_layout_validation_and_duplicate_rejection();
    test_decoder_rejects_incompatible_corrupt_and_noncanonical_records();
    test_empty_store_uses_validated_safe_default();
    test_two_slot_rotation_generation_and_write_wear();
    test_equal_generation_conflict_falls_back_visibly();
    test_partial_write_keeps_last_good_slot();
    test_verify_failure_keeps_other_good_slot();
    test_io_failure_and_reset_are_explicit();
    test_store_allocates_generations_and_suppresses_unchanged_writes();
    test_automatic_update_rejects_invalid_conflict_and_exhaustion();
    test_automatic_update_keeps_storage_failures_visible();
    test_canonical_export_is_atomic_and_preserves_recovery_evidence();

    if (failures != 0) {
        std::cerr << failures << " gauge layout assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 13 gauge layout scenario groups\n";
    return EXIT_SUCCESS;
}
