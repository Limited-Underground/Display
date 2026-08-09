#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/gauge_view_model.hpp"

namespace opengauge::configuration {

inline constexpr std::uint16_t kGaugeLayoutSchemaVersion = 1;
inline constexpr std::size_t kGaugeLayoutRecordBytes = 576;

enum class GaugeTheme : std::uint8_t {
    dark = 1,
    light = 2,
    high_contrast = 3,
};

enum class GaugeLayoutCodecError : std::uint8_t {
    none = 0,
    invalid_argument,
    invalid_layout,
    duplicate_widget,
    bad_magic,
    unsupported_version,
    noncanonical_record,
    checksum_mismatch,
};

struct GaugeLayout {
    std::uint64_t generation{0};
    std::uint32_t layout_id{0};
    std::uint8_t brightness_percent{0};
    GaugeTheme theme{GaugeTheme::dark};
    std::uint8_t widget_count{0};
    std::array<display::GaugeWidgetConfiguration,
               display::kMaximumGaugeWidgets> widgets{};
};

struct GaugeLayoutCodecResult {
    GaugeLayoutCodecError error{GaugeLayoutCodecError::invalid_argument};
    std::size_t bytes{0};

    [[nodiscard]] constexpr bool succeeded() const {
        return error == GaugeLayoutCodecError::none;
    }
};

[[nodiscard]] GaugeLayoutCodecError validate_gauge_layout(
    const GaugeLayout& layout);
[[nodiscard]] GaugeLayoutCodecResult encode_gauge_layout(
    const GaugeLayout& layout,
    std::uint8_t* output,
    std::size_t output_capacity);
[[nodiscard]] GaugeLayoutCodecResult decode_gauge_layout(
    const std::uint8_t* data,
    std::size_t size,
    GaugeLayout& output);

enum class LayoutStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

class GaugeLayoutStorage {
public:
    virtual ~GaugeLayoutStorage() = default;
    [[nodiscard]] virtual LayoutStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) = 0;
    [[nodiscard]] virtual LayoutStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual LayoutStorageError erase_slot(
        std::uint8_t slot) = 0;
};

enum class LayoutSlotState : std::uint8_t {
    empty = 0,
    valid,
    invalid,
    io_failure,
};

enum class GaugeLayoutStoreError : std::uint8_t {
    none = 0,
    invalid_layout,
    stale_generation,
    generation_conflict,
    storage_failure,
    verification_failure,
};

enum class GaugeLayoutSource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
    safe_default,
};

struct GaugeLayoutLoadResult {
    GaugeLayoutStoreError error{GaugeLayoutStoreError::storage_failure};
    GaugeLayoutSource source{GaugeLayoutSource::none};
    LayoutSlotState slot_a{LayoutSlotState::empty};
    LayoutSlotState slot_b{LayoutSlotState::empty};
    bool recovery_required{false};

    [[nodiscard]] constexpr bool has_usable_layout() const {
        return source != GaugeLayoutSource::none;
    }
};

struct GaugeLayoutSaveResult {
    GaugeLayoutStoreError error{GaugeLayoutStoreError::storage_failure};
    GaugeLayoutSource written_slot{GaugeLayoutSource::none};

    [[nodiscard]] constexpr bool saved() const {
        return error == GaugeLayoutStoreError::none;
    }
};

class GaugeLayoutStore {
public:
    explicit GaugeLayoutStore(GaugeLayoutStorage& storage);

    [[nodiscard]] GaugeLayoutLoadResult load(
        const GaugeLayout& safe_default,
        GaugeLayout& output);
    [[nodiscard]] GaugeLayoutSaveResult save(const GaugeLayout& layout);
    [[nodiscard]] GaugeLayoutStoreError reset();

private:
    GaugeLayoutStorage& storage_;
};

}  // namespace opengauge::configuration
