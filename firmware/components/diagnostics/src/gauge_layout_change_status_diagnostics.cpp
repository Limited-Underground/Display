#include "opengauge/gauge_layout_change_status_diagnostics.hpp"

namespace opengauge::diagnostics {
namespace {

using OperatorState = configuration::GaugeLayoutChangeOperatorState;
using OperatorAction = configuration::GaugeLayoutChangeOperatorAction;

constexpr std::uint32_t kStateShift = 0;
constexpr std::uint32_t kActionShift = 4;
constexpr std::uint32_t kAttentionRequiredShift = 7;
constexpr std::uint32_t kConfirmationAllowedShift = 8;
constexpr std::uint32_t kLastOperationRejectedShift = 9;
constexpr std::uint32_t kSensitiveDetailRedactedShift = 10;
constexpr std::uint32_t kVersionShift = 24;
constexpr std::uint32_t kMagicShift = 28;

constexpr std::uint32_t kThreeBitMask = 0x07U;
constexpr std::uint32_t kFourBitMask = 0x0FU;
constexpr std::uint32_t kGaugeLayoutChangeStatusDiagnosticMagic = 0x0BU;

template <typename T>
constexpr std::uint32_t enum_value(T value) {
    return static_cast<std::uint32_t>(value);
}

bool known_state(OperatorState state) {
    return enum_value(state) <= enum_value(OperatorState::clock_fault);
}

bool known_action(OperatorAction action) {
    return enum_value(action) <= enum_value(OperatorAction::service_clock);
}

bool coherent_coarse(const GaugeLayoutChangeStatusDiagnostic& status) {
    if (!known_state(status.state) || !known_action(status.action) ||
        !status.sensitive_detail_redacted) {
        return false;
    }
    switch (status.state) {
        case OperatorState::unavailable:
            return status.action == OperatorAction::none &&
                   !status.attention_required &&
                   !status.confirmation_allowed &&
                   !status.last_operation_rejected;
        case OperatorState::ready:
            return status.action == OperatorAction::stage_new_request &&
                   !status.attention_required &&
                   !status.confirmation_allowed &&
                   !status.last_operation_rejected;
        case OperatorState::confirmation_required:
            return status.action == OperatorAction::confirm_or_cancel &&
                   status.attention_required &&
                   status.confirmation_allowed;
        case OperatorState::applied:
        case OperatorState::unchanged:
            return status.action == OperatorAction::none &&
                   !status.attention_required &&
                   !status.confirmation_allowed &&
                   !status.last_operation_rejected;
        case OperatorState::cancelled:
            return status.action == OperatorAction::stage_new_request &&
                   !status.attention_required &&
                   !status.confirmation_allowed &&
                   !status.last_operation_rejected;
        case OperatorState::expired:
            return (status.action == OperatorAction::service_expiry ||
                    status.action == OperatorAction::stage_new_request) &&
                   status.attention_required &&
                   !status.confirmation_allowed &&
                   !status.last_operation_rejected;
        case OperatorState::rejected:
            return status.action == OperatorAction::stage_new_request &&
                   status.attention_required &&
                   !status.confirmation_allowed &&
                   status.last_operation_rejected;
        case OperatorState::persistence_failed:
            return status.action == OperatorAction::stage_new_request &&
                   status.attention_required &&
                   !status.confirmation_allowed &&
                   !status.last_operation_rejected;
        case OperatorState::restart_required:
            return status.action == OperatorAction::restart_and_reconcile &&
                   status.attention_required &&
                   !status.confirmation_allowed &&
                   !status.last_operation_rejected;
        case OperatorState::clock_fault:
            return status.action == OperatorAction::service_clock &&
                   status.attention_required &&
                   !status.confirmation_allowed &&
                   !status.last_operation_rejected;
    }
    return false;
}

bool coherent_live(
    const configuration::GaugeLayoutChangeOperatorStatus& status) {
    const bool no_request_detail = status.pending_request_id == 0 &&
                                   status.confirmation_remaining_ms == 0;
    const bool no_generation = status.generation == 0;
    switch (status.state) {
        case OperatorState::confirmation_required:
            return status.pending_request_id != 0 &&
                   status.confirmation_remaining_ms != 0 && no_generation;
        case OperatorState::applied:
        case OperatorState::unchanged:
            return no_request_detail && status.generation != 0;
        case OperatorState::unavailable:
        case OperatorState::ready:
        case OperatorState::cancelled:
        case OperatorState::expired:
        case OperatorState::rejected:
        case OperatorState::persistence_failed:
        case OperatorState::restart_required:
        case OperatorState::clock_fault:
            return no_request_detail && no_generation;
    }
    return false;
}

GaugeLayoutChangeStatusDiagnostic coarse_from_live(
    const configuration::GaugeLayoutChangeOperatorStatus& status) {
    return {
        status.state,
        status.action,
        status.attention_required,
        status.confirmation_allowed,
        status.last_operation_rejected,
        true,
    };
}

LogLevel level_for(OperatorState state) {
    switch (state) {
        case OperatorState::unavailable:
        case OperatorState::ready:
        case OperatorState::confirmation_required:
        case OperatorState::applied:
        case OperatorState::unchanged:
        case OperatorState::cancelled:
            return LogLevel::info;
        case OperatorState::expired:
        case OperatorState::rejected:
            return LogLevel::warning;
        case OperatorState::persistence_failed:
        case OperatorState::restart_required:
        case OperatorState::clock_fault:
            return LogLevel::error;
    }
    return LogLevel::error;
}

}  // namespace

GaugeLayoutChangeStatusDiagnosticEncodeResult
encode_gauge_layout_change_status_diagnostic(
    const configuration::GaugeLayoutChangeOperatorStatus& status) {
    const auto diagnostic = coarse_from_live(status);
    if (!coherent_live(status) || !coherent_coarse(diagnostic)) {
        return {};
    }
    const std::uint32_t word =
        (kGaugeLayoutChangeStatusDiagnosticMagic << kMagicShift) |
        (static_cast<std::uint32_t>(
             kGaugeLayoutChangeStatusDiagnosticVersion)
         << kVersionShift) |
        (enum_value(diagnostic.state) << kStateShift) |
        (enum_value(diagnostic.action) << kActionShift) |
        (static_cast<std::uint32_t>(diagnostic.attention_required)
         << kAttentionRequiredShift) |
        (static_cast<std::uint32_t>(diagnostic.confirmation_allowed)
         << kConfirmationAllowedShift) |
        (static_cast<std::uint32_t>(diagnostic.last_operation_rejected)
         << kLastOperationRejectedShift) |
        (static_cast<std::uint32_t>(diagnostic.sensitive_detail_redacted)
         << kSensitiveDetailRedactedShift);
    return {GaugeLayoutChangeStatusDiagnosticError::none, word};
}

GaugeLayoutChangeStatusDiagnosticDecodeResult
decode_gauge_layout_change_status_diagnostic(std::uint32_t word) {
    if (((word >> kMagicShift) & kFourBitMask) !=
        kGaugeLayoutChangeStatusDiagnosticMagic) {
        return {};
    }
    const auto version =
        static_cast<std::uint8_t>((word >> kVersionShift) & kFourBitMask);
    if (version != kGaugeLayoutChangeStatusDiagnosticVersion) {
        return {GaugeLayoutChangeStatusDiagnosticError::unsupported_version};
    }
    if ((word & 0x00FFF800U) != 0) {
        return {};
    }

    GaugeLayoutChangeStatusDiagnostic diagnostic{};
    diagnostic.state = static_cast<OperatorState>(
        (word >> kStateShift) & kFourBitMask);
    diagnostic.action = static_cast<OperatorAction>(
        (word >> kActionShift) & kThreeBitMask);
    diagnostic.attention_required =
        ((word >> kAttentionRequiredShift) & 1U) != 0;
    diagnostic.confirmation_allowed =
        ((word >> kConfirmationAllowedShift) & 1U) != 0;
    diagnostic.last_operation_rejected =
        ((word >> kLastOperationRejectedShift) & 1U) != 0;
    diagnostic.sensitive_detail_redacted =
        ((word >> kSensitiveDetailRedactedShift) & 1U) != 0;
    if (!coherent_coarse(diagnostic)) {
        return {};
    }
    return {GaugeLayoutChangeStatusDiagnosticError::none, diagnostic};
}

GaugeLayoutChangeStatusDiagnosticRecordResult
record_gauge_layout_change_status(
    DiagnosticsService& diagnostics,
    const configuration::GaugeLayoutChangeOperatorStatus& status,
    std::uint64_t now_ms) {
    const auto encoded = encode_gauge_layout_change_status_diagnostic(status);
    if (!encoded.encoded()) {
        return {};
    }
    const auto record = diagnostics.record(
        level_for(status.state), EventCode::configuration_recovery,
        MetricCode::state_code, static_cast<std::int64_t>(encoded.word),
        now_ms);
    return {
        GaugeLayoutChangeStatusDiagnosticError::none,
        record,
        encoded.word};
}

}  // namespace opengauge::diagnostics
