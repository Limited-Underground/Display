#pragma once

#include <cstdint>
#include <type_traits>

#include "opengauge/diagnostics.hpp"
#include "opengauge/gauge_layout_change_operator_status.hpp"

namespace opengauge::diagnostics {

inline constexpr std::uint8_t kGaugeLayoutChangeStatusDiagnosticVersion = 0;

enum class GaugeLayoutChangeStatusDiagnosticError : std::uint8_t {
    none = 0,
    invalid_status,
    invalid_word,
    unsupported_version,
};

struct GaugeLayoutChangeStatusDiagnostic {
    configuration::GaugeLayoutChangeOperatorState state{
        configuration::GaugeLayoutChangeOperatorState::unavailable};
    configuration::GaugeLayoutChangeOperatorAction action{
        configuration::GaugeLayoutChangeOperatorAction::none};
    bool attention_required{false};
    bool confirmation_allowed{false};
    bool last_operation_rejected{false};
    bool sensitive_detail_redacted{true};
};

struct GaugeLayoutChangeStatusDiagnosticEncodeResult {
    GaugeLayoutChangeStatusDiagnosticError error{
        GaugeLayoutChangeStatusDiagnosticError::invalid_status};
    std::uint32_t word{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == GaugeLayoutChangeStatusDiagnosticError::none;
    }
};

struct GaugeLayoutChangeStatusDiagnosticDecodeResult {
    GaugeLayoutChangeStatusDiagnosticError error{
        GaugeLayoutChangeStatusDiagnosticError::invalid_word};
    GaugeLayoutChangeStatusDiagnostic diagnostic{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == GaugeLayoutChangeStatusDiagnosticError::none;
    }
};

struct GaugeLayoutChangeStatusDiagnosticRecordResult {
    GaugeLayoutChangeStatusDiagnosticError error{
        GaugeLayoutChangeStatusDiagnosticError::invalid_status};
    DiagnosticRecordResult record{};
    std::uint32_t word{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == GaugeLayoutChangeStatusDiagnosticError::none &&
               record.accepted();
    }
};

static_assert(std::is_trivially_copyable_v<
              GaugeLayoutChangeStatusDiagnostic>);
static_assert(sizeof(GaugeLayoutChangeStatusDiagnostic) <= 8);

// The diagnostic intentionally omits request ID, confirmation time, layout
// generation/content, widget labels, and counters. Only coarse operator state,
// action, and flags enter the existing fixed diagnostic ring.
[[nodiscard]] GaugeLayoutChangeStatusDiagnosticEncodeResult
encode_gauge_layout_change_status_diagnostic(
    const configuration::GaugeLayoutChangeOperatorStatus& status);

[[nodiscard]] GaugeLayoutChangeStatusDiagnosticDecodeResult
decode_gauge_layout_change_status_diagnostic(std::uint32_t word);

[[nodiscard]] GaugeLayoutChangeStatusDiagnosticRecordResult
record_gauge_layout_change_status(
    DiagnosticsService& diagnostics,
    const configuration::GaugeLayoutChangeOperatorStatus& status,
    std::uint64_t now_ms);

}  // namespace opengauge::diagnostics
