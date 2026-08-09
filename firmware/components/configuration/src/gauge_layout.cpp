#include "opengauge/gauge_layout.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace opengauge::configuration {
namespace {

constexpr std::size_t kHeaderBytes = 32;
constexpr std::size_t kWidgetSlotBytes = 64;
constexpr std::size_t kWidgetSlotsOffset = kHeaderBytes;
constexpr std::size_t kTailReservedOffset =
    kWidgetSlotsOffset +
    display::kMaximumGaugeWidgets * kWidgetSlotBytes;
constexpr std::size_t kChecksumOffset = kGaugeLayoutRecordBytes - 4;

constexpr std::array<std::uint8_t, 4> kMagic{{'O', 'G', 'L', '0'}};

bool known_theme(GaugeTheme theme) {
    const auto value = static_cast<std::uint8_t>(theme);
    return value >= static_cast<std::uint8_t>(GaugeTheme::dark) &&
           value <= static_cast<std::uint8_t>(GaugeTheme::high_contrast);
}

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index) {
        output[index] = static_cast<std::uint8_t>(
            (value >> (index * 8U)) & 0xFFU);
    }
}

void write_i64(std::uint8_t* output, std::int64_t value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    write_u64(output, bits);
}

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    }
    return value;
}

std::int64_t read_i64(const std::uint8_t* input) {
    const auto bits = read_u64(input);
    std::int64_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
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

bool all_zero(const std::uint8_t* data, std::size_t size) {
    for (std::size_t index = 0; index < size; ++index) {
        if (data[index] != 0) {
            return false;
        }
    }
    return true;
}

struct InspectedSlot {
    LayoutSlotState state{LayoutSlotState::empty};
    GaugeLayout layout{};
    std::array<std::uint8_t, kGaugeLayoutRecordBytes> bytes{};
};

InspectedSlot inspect_slot(GaugeLayoutStorage& storage, std::uint8_t slot) {
    InspectedSlot inspected{};
    const auto read = storage.read_slot(
        slot, inspected.bytes.data(), inspected.bytes.size());
    if (read == LayoutStorageError::not_found) {
        inspected.state = LayoutSlotState::empty;
        return inspected;
    }
    if (read != LayoutStorageError::none) {
        inspected.state = LayoutSlotState::io_failure;
        return inspected;
    }
    const auto decoded = decode_gauge_layout(
        inspected.bytes.data(), inspected.bytes.size(), inspected.layout);
    inspected.state = decoded.succeeded()
                          ? LayoutSlotState::valid
                          : LayoutSlotState::invalid;
    return inspected;
}

GaugeLayoutSource source_for_slot(std::uint8_t slot) {
    return slot == 0 ? GaugeLayoutSource::slot_a : GaugeLayoutSource::slot_b;
}

}  // namespace

GaugeLayoutCodecError validate_gauge_layout(const GaugeLayout& layout) {
    if (layout.generation == 0 || layout.layout_id == 0 ||
        layout.brightness_percent == 0 || layout.brightness_percent > 100 ||
        !known_theme(layout.theme) || layout.widget_count == 0 ||
        layout.widget_count > display::kMaximumGaugeWidgets) {
        return GaugeLayoutCodecError::invalid_layout;
    }
    for (std::size_t index = 0; index < layout.widget_count; ++index) {
        if (display::validate_gauge_widget_configuration(
                layout.widgets[index]) != display::GaugeViewModelError::none) {
            return GaugeLayoutCodecError::invalid_layout;
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (layout.widgets[prior].widget_id ==
                layout.widgets[index].widget_id) {
                return GaugeLayoutCodecError::duplicate_widget;
            }
        }
    }
    return GaugeLayoutCodecError::none;
}

GaugeLayoutCodecResult encode_gauge_layout(
    const GaugeLayout& layout,
    std::uint8_t* output,
    std::size_t output_capacity) {
    if (output == nullptr || output_capacity < kGaugeLayoutRecordBytes) {
        return {GaugeLayoutCodecError::invalid_argument, 0};
    }
    const auto validation = validate_gauge_layout(layout);
    if (validation != GaugeLayoutCodecError::none) {
        return {validation, 0};
    }

    std::array<std::uint8_t, kGaugeLayoutRecordBytes> candidate{};
    std::copy(kMagic.begin(), kMagic.end(), candidate.begin());
    write_u16(candidate.data() + 4, kGaugeLayoutSchemaVersion);
    write_u16(candidate.data() + 6, static_cast<std::uint16_t>(kHeaderBytes));
    write_u64(candidate.data() + 8, layout.generation);
    write_u32(candidate.data() + 16, layout.layout_id);
    candidate[20] = layout.widget_count;
    candidate[21] = layout.brightness_percent;
    candidate[22] = static_cast<std::uint8_t>(layout.theme);

    for (std::size_t index = 0; index < layout.widget_count; ++index) {
        const auto& widget = layout.widgets[index];
        auto* slot = candidate.data() + kWidgetSlotsOffset +
                     index * kWidgetSlotBytes;
        write_u16(slot, widget.widget_id);
        slot[2] = static_cast<std::uint8_t>(widget.signal_code);
        slot[3] = static_cast<std::uint8_t>(widget.kind);
        slot[4] = widget.label.length;
        write_u64(slot + 8, widget.stale_after_ms);
        write_i64(slot + 16, widget.scale_min_raw);
        write_i64(slot + 24, widget.scale_max_raw);
        std::copy(widget.label.bytes.begin(), widget.label.bytes.end(),
                  slot + 32);
    }
    write_u32(candidate.data() + kChecksumOffset,
              crc32(candidate.data(), kChecksumOffset));
    std::copy(candidate.begin(), candidate.end(), output);
    return {GaugeLayoutCodecError::none, candidate.size()};
}

GaugeLayoutCodecResult decode_gauge_layout(
    const std::uint8_t* data,
    std::size_t size,
    GaugeLayout& output) {
    if (data == nullptr || size != kGaugeLayoutRecordBytes) {
        return {GaugeLayoutCodecError::invalid_argument, 0};
    }
    if (!std::equal(kMagic.begin(), kMagic.end(), data)) {
        return {GaugeLayoutCodecError::bad_magic, 0};
    }
    if (read_u16(data + 4) != kGaugeLayoutSchemaVersion) {
        return {GaugeLayoutCodecError::unsupported_version, 0};
    }
    if (read_u16(data + 6) != kHeaderBytes || data[23] != 0 ||
        !all_zero(data + 24, 8) ||
        !all_zero(data + kTailReservedOffset,
                  kChecksumOffset - kTailReservedOffset)) {
        return {GaugeLayoutCodecError::noncanonical_record, 0};
    }
    if (read_u32(data + kChecksumOffset) !=
        crc32(data, kChecksumOffset)) {
        return {GaugeLayoutCodecError::checksum_mismatch, 0};
    }

    GaugeLayout candidate{};
    candidate.generation = read_u64(data + 8);
    candidate.layout_id = read_u32(data + 16);
    candidate.widget_count = data[20];
    candidate.brightness_percent = data[21];
    candidate.theme = static_cast<GaugeTheme>(data[22]);
    if (candidate.widget_count > candidate.widgets.size()) {
        return {GaugeLayoutCodecError::invalid_layout, 0};
    }

    for (std::size_t index = 0; index < candidate.widgets.size(); ++index) {
        const auto* slot = data + kWidgetSlotsOffset +
                           index * kWidgetSlotBytes;
        if (index >= candidate.widget_count) {
            if (!all_zero(slot, kWidgetSlotBytes)) {
                return {GaugeLayoutCodecError::noncanonical_record, 0};
            }
            continue;
        }
        if (!all_zero(slot + 5, 3) || !all_zero(slot + 57, 7)) {
            return {GaugeLayoutCodecError::noncanonical_record, 0};
        }
        auto& widget = candidate.widgets[index];
        widget.widget_id = read_u16(slot);
        widget.signal_code =
            static_cast<wireless::TelemetrySignalCode>(slot[2]);
        widget.kind = static_cast<display::GaugeWidgetKind>(slot[3]);
        widget.label.length = slot[4];
        widget.stale_after_ms = read_u64(slot + 8);
        widget.scale_min_raw = read_i64(slot + 16);
        widget.scale_max_raw = read_i64(slot + 24);
        std::copy(slot + 32, slot + 57, widget.label.bytes.begin());
    }

    const auto validation = validate_gauge_layout(candidate);
    if (validation != GaugeLayoutCodecError::none) {
        return {validation, 0};
    }
    output = candidate;
    return {GaugeLayoutCodecError::none, size};
}

GaugeLayoutStore::GaugeLayoutStore(GaugeLayoutStorage& storage)
    : storage_(storage) {}

GaugeLayoutLoadResult GaugeLayoutStore::load(
    const GaugeLayout& safe_default,
    GaugeLayout& output) {
    if (validate_gauge_layout(safe_default) != GaugeLayoutCodecError::none) {
        return {GaugeLayoutStoreError::invalid_layout};
    }
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    GaugeLayoutLoadResult result{
        GaugeLayoutStoreError::none,
        GaugeLayoutSource::none,
        a.state,
        b.state,
        false};

    if (a.state == LayoutSlotState::valid &&
        b.state == LayoutSlotState::valid) {
        if (a.layout.generation == b.layout.generation &&
            a.bytes != b.bytes) {
            output = safe_default;
            result.error = GaugeLayoutStoreError::generation_conflict;
            result.source = GaugeLayoutSource::safe_default;
            result.recovery_required = true;
            return result;
        }
        if (a.layout.generation >= b.layout.generation) {
            output = a.layout;
            result.source = GaugeLayoutSource::slot_a;
        } else {
            output = b.layout;
            result.source = GaugeLayoutSource::slot_b;
        }
        return result;
    }
    if (a.state == LayoutSlotState::valid ||
        b.state == LayoutSlotState::valid) {
        const bool use_a = a.state == LayoutSlotState::valid;
        output = use_a ? a.layout : b.layout;
        result.source = use_a ? GaugeLayoutSource::slot_a
                              : GaugeLayoutSource::slot_b;
        result.recovery_required = true;
        if (a.state == LayoutSlotState::io_failure ||
            b.state == LayoutSlotState::io_failure) {
            result.error = GaugeLayoutStoreError::storage_failure;
        }
        return result;
    }

    output = safe_default;
    result.source = GaugeLayoutSource::safe_default;
    result.recovery_required = true;
    if (a.state == LayoutSlotState::io_failure ||
        b.state == LayoutSlotState::io_failure) {
        result.error = GaugeLayoutStoreError::storage_failure;
    }
    return result;
}

GaugeLayoutSaveResult GaugeLayoutStore::save(const GaugeLayout& layout) {
    std::array<std::uint8_t, kGaugeLayoutRecordBytes> encoded{};
    if (!encode_gauge_layout(layout, encoded.data(), encoded.size()).succeeded()) {
        return {GaugeLayoutStoreError::invalid_layout};
    }
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    if (a.state == LayoutSlotState::io_failure ||
        b.state == LayoutSlotState::io_failure) {
        return {GaugeLayoutStoreError::storage_failure};
    }

    std::uint64_t highest_generation = 0;
    if (a.state == LayoutSlotState::valid) {
        highest_generation = a.layout.generation;
    }
    if (b.state == LayoutSlotState::valid) {
        highest_generation = std::max(highest_generation, b.layout.generation);
    }
    if (highest_generation != 0 && layout.generation <= highest_generation) {
        return {GaugeLayoutStoreError::stale_generation};
    }

    std::uint8_t target = 0;
    if (a.state != LayoutSlotState::valid) {
        target = 0;
    } else if (b.state != LayoutSlotState::valid) {
        target = 1;
    } else {
        target = a.layout.generation <= b.layout.generation ? 0 : 1;
    }

    if (storage_.write_slot(target, encoded.data(), encoded.size()) !=
        LayoutStorageError::none) {
        return {GaugeLayoutStoreError::storage_failure};
    }
    std::array<std::uint8_t, kGaugeLayoutRecordBytes> verified{};
    if (storage_.read_slot(target, verified.data(), verified.size()) !=
        LayoutStorageError::none || verified != encoded) {
        return {GaugeLayoutStoreError::verification_failure};
    }
    GaugeLayout decoded{};
    if (!decode_gauge_layout(
             verified.data(), verified.size(), decoded).succeeded()) {
        return {GaugeLayoutStoreError::verification_failure};
    }
    return {GaugeLayoutStoreError::none, source_for_slot(target)};
}

GaugeLayoutStoreError GaugeLayoutStore::reset() {
    const auto a = storage_.erase_slot(0);
    const auto b = storage_.erase_slot(1);
    return a == LayoutStorageError::none && b == LayoutStorageError::none
               ? GaugeLayoutStoreError::none
               : GaugeLayoutStoreError::storage_failure;
}

}  // namespace opengauge::configuration
