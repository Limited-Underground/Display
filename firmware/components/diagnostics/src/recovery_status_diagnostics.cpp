#include "opengauge/recovery_status_diagnostics.hpp"

namespace opengauge::diagnostics {
namespace {

constexpr std::uint32_t kOperationShift = 0;
constexpr std::uint32_t kStateShift = 2;
constexpr std::uint32_t kReasonShift = 5;
constexpr std::uint32_t kActionShift = 10;
constexpr std::uint32_t kSlotAShift = 13;
constexpr std::uint32_t kSlotBShift = 15;
constexpr std::uint32_t kProtectedKeyErrorShift = 17;
constexpr std::uint32_t kTransportAllowedShift = 20;
constexpr std::uint32_t kAttentionRequiredShift = 21;
constexpr std::uint32_t kRepairRequiredShift = 22;
constexpr std::uint32_t kSensitiveDetailRedactedShift = 23;
constexpr std::uint32_t kVersionShift = 24;
constexpr std::uint32_t kMagicShift = 28;

constexpr std::uint32_t kTwoBitMask = 0x03U;
constexpr std::uint32_t kThreeBitMask = 0x07U;
constexpr std::uint32_t kFiveBitMask = 0x1FU;
constexpr std::uint32_t kFourBitMask = 0x0FU;
constexpr std::uint32_t kRecoveryStatusDiagnosticMagic = 0x0AU;

template <typename T>
constexpr std::uint32_t enum_value(T value) {
    return static_cast<std::uint32_t>(value);
}

bool known_operation(integration::CriticalAlertSystemRecoveryOperation value) {
    return enum_value(value) <= enum_value(
               integration::CriticalAlertSystemRecoveryOperation::repair);
}

bool known_state(integration::CriticalAlertSystemOperatorState value) {
    return enum_value(value) <= enum_value(
               integration::CriticalAlertSystemOperatorState::
                   reboot_reconcile_required);
}

bool known_reason(integration::CriticalAlertSystemOperatorReason value) {
    return enum_value(value) <= enum_value(
               integration::CriticalAlertSystemOperatorReason::invalid_result);
}

bool known_action(integration::CriticalAlertSystemOperatorAction value) {
    return enum_value(value) <= enum_value(
               integration::CriticalAlertSystemOperatorAction::service);
}

bool known_slot(integration::CriticalAlertSystemRecoverySlotState value) {
    return enum_value(value) <= enum_value(
               integration::CriticalAlertSystemRecoverySlotState::io_failure);
}

bool known_key_error(
    integration::CriticalAlertSystemRecoveryKeyValidationError value) {
    return enum_value(value) <= enum_value(
               integration::CriticalAlertSystemRecoveryKeyValidationError::
                   registry_snapshot_failed);
}

bool coherent(const RecoveryStatusDiagnostic& status) {
    if (!known_operation(status.operation) || !known_state(status.state) ||
        !known_reason(status.reason) || !known_action(status.action) ||
        !known_slot(status.slot_a) || !known_slot(status.slot_b) ||
        !known_key_error(status.protected_key_error)) {
        return false;
    }
    if ((status.protected_key_error !=
         integration::CriticalAlertSystemRecoveryKeyValidationError::none) !=
        (status.reason == integration::CriticalAlertSystemOperatorReason::
                              protected_key_unavailable)) {
        return false;
    }

    using Action = integration::CriticalAlertSystemOperatorAction;
    using State = integration::CriticalAlertSystemOperatorState;
    switch (status.state) {
        case State::operational:
            return status.transport_allowed && !status.attention_required &&
                   !status.repair_required && status.action == Action::none;
        case State::operational_degraded:
            return status.transport_allowed && status.attention_required &&
                   status.repair_required &&
                   status.action == Action::repair_redundancy;
        case State::first_boot:
            return !status.transport_allowed && status.attention_required &&
                   !status.repair_required &&
                   status.action == Action::provision;
        case State::reboot_reconcile_required:
            return !status.transport_allowed && status.attention_required &&
                   !status.repair_required &&
                   status.action == Action::reboot_and_reconcile;
        case State::safe_mode:
        case State::service_required:
            return !status.transport_allowed && status.attention_required &&
                   !status.repair_required &&
                   status.action == Action::service;
    }
    return false;
}

RecoveryStatusDiagnostic diagnostic_from_status(
    const integration::CriticalAlertSystemRecoveryStatus& status) {
    return {
        status.operation,
        status.state,
        status.reason,
        status.action,
        status.slot_a,
        status.slot_b,
        status.protected_key_error,
        status.transport_allowed,
        status.attention_required,
        status.repair_required,
        status.sensitive_detail_redacted,
    };
}

LogLevel level_for(integration::CriticalAlertSystemOperatorState state) {
    using State = integration::CriticalAlertSystemOperatorState;
    switch (state) {
        case State::operational:
            return LogLevel::info;
        case State::first_boot:
        case State::operational_degraded:
        case State::reboot_reconcile_required:
            return LogLevel::warning;
        case State::safe_mode:
        case State::service_required:
            return LogLevel::error;
    }
    return LogLevel::error;
}

}  // namespace

RecoveryStatusDiagnosticEncodeResult encode_recovery_status_diagnostic(
    const integration::CriticalAlertSystemRecoveryStatus& status) {
    const auto diagnostic = diagnostic_from_status(status);
    if (!coherent(diagnostic)) {
        return {};
    }

    std::uint32_t word =
        (kRecoveryStatusDiagnosticMagic << kMagicShift) |
        (static_cast<std::uint32_t>(kRecoveryStatusDiagnosticVersion)
         << kVersionShift) |
        (enum_value(diagnostic.operation) << kOperationShift) |
        (enum_value(diagnostic.state) << kStateShift) |
        (enum_value(diagnostic.reason) << kReasonShift) |
        (enum_value(diagnostic.action) << kActionShift) |
        (enum_value(diagnostic.slot_a) << kSlotAShift) |
        (enum_value(diagnostic.slot_b) << kSlotBShift) |
        (enum_value(diagnostic.protected_key_error)
         << kProtectedKeyErrorShift) |
        (static_cast<std::uint32_t>(diagnostic.transport_allowed)
         << kTransportAllowedShift) |
        (static_cast<std::uint32_t>(diagnostic.attention_required)
         << kAttentionRequiredShift) |
        (static_cast<std::uint32_t>(diagnostic.repair_required)
         << kRepairRequiredShift) |
        (static_cast<std::uint32_t>(diagnostic.sensitive_detail_redacted)
         << kSensitiveDetailRedactedShift);
    return {RecoveryStatusDiagnosticError::none, word};
}

RecoveryStatusDiagnosticDecodeResult decode_recovery_status_diagnostic(
    std::uint32_t word) {
    if (((word >> kMagicShift) & kFourBitMask) !=
        kRecoveryStatusDiagnosticMagic) {
        return {};
    }
    const auto version =
        static_cast<std::uint8_t>((word >> kVersionShift) & kFourBitMask);
    if (version != kRecoveryStatusDiagnosticVersion) {
        return {RecoveryStatusDiagnosticError::unsupported_version};
    }

    RecoveryStatusDiagnostic diagnostic{};
    diagnostic.operation =
        static_cast<integration::CriticalAlertSystemRecoveryOperation>(
            (word >> kOperationShift) & kTwoBitMask);
    diagnostic.state =
        static_cast<integration::CriticalAlertSystemOperatorState>(
            (word >> kStateShift) & kThreeBitMask);
    diagnostic.reason =
        static_cast<integration::CriticalAlertSystemOperatorReason>(
            (word >> kReasonShift) & kFiveBitMask);
    diagnostic.action =
        static_cast<integration::CriticalAlertSystemOperatorAction>(
            (word >> kActionShift) & kThreeBitMask);
    diagnostic.slot_a =
        static_cast<integration::CriticalAlertSystemRecoverySlotState>(
            (word >> kSlotAShift) & kTwoBitMask);
    diagnostic.slot_b =
        static_cast<integration::CriticalAlertSystemRecoverySlotState>(
            (word >> kSlotBShift) & kTwoBitMask);
    diagnostic.protected_key_error = static_cast<
        integration::CriticalAlertSystemRecoveryKeyValidationError>(
        (word >> kProtectedKeyErrorShift) & kThreeBitMask);
    diagnostic.transport_allowed =
        ((word >> kTransportAllowedShift) & 1U) != 0;
    diagnostic.attention_required =
        ((word >> kAttentionRequiredShift) & 1U) != 0;
    diagnostic.repair_required =
        ((word >> kRepairRequiredShift) & 1U) != 0;
    diagnostic.sensitive_detail_redacted =
        ((word >> kSensitiveDetailRedactedShift) & 1U) != 0;
    if (!coherent(diagnostic)) {
        return {};
    }
    return {RecoveryStatusDiagnosticError::none, diagnostic};
}

RecoveryStatusDiagnosticRecordResult record_recovery_status(
    DiagnosticsService& diagnostics,
    const integration::CriticalAlertSystemRecoveryStatus& status,
    std::uint64_t now_ms) {
    const auto encoded = encode_recovery_status_diagnostic(status);
    if (!encoded.encoded()) {
        return {};
    }
    const auto record = diagnostics.record(
        level_for(status.state), EventCode::configuration_recovery,
        MetricCode::state_code, static_cast<std::int64_t>(encoded.word),
        now_ms);
    return {RecoveryStatusDiagnosticError::none, record, encoded.word};
}

}  // namespace opengauge::diagnostics
