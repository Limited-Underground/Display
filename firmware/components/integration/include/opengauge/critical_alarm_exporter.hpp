#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/alarm_engine.hpp"
#include "opengauge/critical_alert.hpp"

namespace opengauge::integration {

inline constexpr std::size_t kMaximumCriticalAlarmMappings = 16;
inline constexpr std::uint32_t kMaximumCriticalAlarmExportAgeMs = 120000U;

enum class CriticalAlarmExportError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    invalid_mapping,
    duplicate_mapping,
    mapping_capacity_full,
    rule_not_mapped,
    event_not_exportable,
    invalid_event,
    incompatible_quality,
    unsupported_unit,
    value_out_of_range,
    clock_regressed,
    event_too_old,
    lifecycle_conflict,
    identity_exhausted,
    codec_failure,
};

struct CriticalAlarmMapping {
    std::uint16_t alarm_rule_id{0};
    CriticalAlertType alert_type{CriticalAlertType::generic_critical};
    AlertSeverity severity{AlertSeverity::critical};
    std::uint32_t diagnostic_code{0};
};

struct CriticalAlarmExporterIdentity {
    std::uint64_t producer_id{0};
    std::uint64_t vehicle_id{0};
    std::uint64_t first_event_id{0};
    std::uint64_t first_condition_id{0};
};

struct CriticalAlarmExportResult {
    CriticalAlarmExportError error{
        CriticalAlarmExportError::invalid_state};
    AlertCodecError codec_error{AlertCodecError::none};
    CriticalAlert alert{};
    std::array<std::uint8_t, kCriticalAlertFrameBytes> encoded{};
    std::size_t encoded_bytes{0};

    [[nodiscard]] constexpr bool exported() const {
        return error == CriticalAlarmExportError::none &&
               encoded_bytes == kCriticalAlertFrameBytes;
    }
};

struct CriticalAlarmExporterStatus {
    bool running{false};
    std::size_t mapping_count{0};
    std::size_t active_condition_count{0};
    std::uint64_t next_event_id{0};
    std::uint64_t next_condition_id{0};
    std::uint32_t assertions_exported{0};
    std::uint32_t clears_exported{0};
    std::uint32_t events_rejected{0};
};

[[nodiscard]] CriticalAlarmExportError validate_critical_alarm_mapping(
    const CriticalAlarmMapping& mapping);

// Maps selected normalized alarm lifecycle transitions to the independent
// transport-neutral OpenGauge-to-OpenTrail frame. It does not provision trust,
// transport keys, delivery, retries, or radio airtime.
class CriticalAlarmExporter {
public:
    [[nodiscard]] CriticalAlarmExportError add_mapping(
        const CriticalAlarmMapping& mapping);
    [[nodiscard]] CriticalAlarmExportError clear_mappings();

    [[nodiscard]] CriticalAlarmExportError start(
        CriticalAlarmExporterIdentity identity);
    void stop();

    [[nodiscard]] CriticalAlarmExportResult export_event(
        const alarm::AlarmEvent& event,
        std::uint64_t now_ms);

    [[nodiscard]] CriticalAlarmExporterStatus status() const;

private:
    struct MappingSlot {
        CriticalAlarmMapping mapping{};
        std::uint64_t active_condition_id{0};
        bool occupied{false};
        bool condition_active{false};
    };

    [[nodiscard]] std::size_t find_mapping(std::uint16_t rule_id) const;

    std::array<MappingSlot, kMaximumCriticalAlarmMappings> mappings_{};
    std::size_t mapping_count_{0};
    CriticalAlarmExporterIdentity identity_{};
    CriticalAlarmExporterStatus status_{};
};

}  // namespace opengauge::integration
