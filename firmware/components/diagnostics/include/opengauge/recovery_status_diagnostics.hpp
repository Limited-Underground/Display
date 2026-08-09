#pragma once

#include <cstdint>
#include <type_traits>

#include "opengauge/diagnostics.hpp"
#include "opengauge/critical_alert_system_recovery_status.hpp"

namespace opengauge::diagnostics {

inline constexpr std::uint8_t kRecoveryStatusDiagnosticVersion = 0;

enum class RecoveryStatusDiagnosticError : std::uint8_t {
    none = 0,
    invalid_status,
    invalid_word,
    unsupported_version,
};

struct RecoveryStatusDiagnostic {
    integration::CriticalAlertSystemRecoveryOperation operation{
        integration::CriticalAlertSystemRecoveryOperation::boot};
    integration::CriticalAlertSystemOperatorState state{
        integration::CriticalAlertSystemOperatorState::service_required};
    integration::CriticalAlertSystemOperatorReason reason{
        integration::CriticalAlertSystemOperatorReason::invalid_result};
    integration::CriticalAlertSystemOperatorAction action{
        integration::CriticalAlertSystemOperatorAction::service};
    integration::CriticalAlertSystemRecoverySlotState slot_a{
        integration::CriticalAlertSystemRecoverySlotState::empty};
    integration::CriticalAlertSystemRecoverySlotState slot_b{
        integration::CriticalAlertSystemRecoverySlotState::empty};
    integration::CriticalAlertSystemRecoveryKeyValidationError
        protected_key_error{
            integration::CriticalAlertSystemRecoveryKeyValidationError::none};
    bool transport_allowed{false};
    bool attention_required{true};
    bool repair_required{false};
    bool sensitive_detail_redacted{false};
};

struct RecoveryStatusDiagnosticEncodeResult {
    RecoveryStatusDiagnosticError error{
        RecoveryStatusDiagnosticError::invalid_status};
    std::uint32_t word{0};

    [[nodiscard]] constexpr bool encoded() const {
        return error == RecoveryStatusDiagnosticError::none;
    }
};

struct RecoveryStatusDiagnosticDecodeResult {
    RecoveryStatusDiagnosticError error{
        RecoveryStatusDiagnosticError::invalid_word};
    RecoveryStatusDiagnostic diagnostic{};

    [[nodiscard]] constexpr bool decoded() const {
        return error == RecoveryStatusDiagnosticError::none;
    }
};

struct RecoveryStatusDiagnosticRecordResult {
    RecoveryStatusDiagnosticError error{
        RecoveryStatusDiagnosticError::invalid_status};
    DiagnosticRecordResult record{};
    std::uint32_t word{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == RecoveryStatusDiagnosticError::none &&
               record.accepted();
    }
};

static_assert(std::is_trivially_copyable_v<RecoveryStatusDiagnostic>);
static_assert(sizeof(RecoveryStatusDiagnostic) <= 16);

// The versioned 32-bit word contains only coarse operator fields and flags.
// Checkpoint generations remain available in the live status but are omitted
// here to keep one atomic diagnostic event. No identity or key handle is ever
// accepted by this interface.
[[nodiscard]] RecoveryStatusDiagnosticEncodeResult
encode_recovery_status_diagnostic(
    const integration::CriticalAlertSystemRecoveryStatus& status);

[[nodiscard]] RecoveryStatusDiagnosticDecodeResult
decode_recovery_status_diagnostic(std::uint32_t word);

[[nodiscard]] RecoveryStatusDiagnosticRecordResult record_recovery_status(
    DiagnosticsService& diagnostics,
    const integration::CriticalAlertSystemRecoveryStatus& status,
    std::uint64_t now_ms);

}  // namespace opengauge::diagnostics
