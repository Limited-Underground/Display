#include <array>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

#include "opengauge/recovery_status_diagnostics.hpp"

namespace {

using namespace opengauge::diagnostics;
using namespace opengauge::integration;

int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

template <typename T, typename = void>
struct has_peer_id : std::false_type {};
template <typename T>
struct has_peer_id<
    T,
    std::void_t<decltype(std::declval<T>().key_validation_peer_id)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_key_handle : std::false_type {};
template <typename T>
struct has_key_handle<
    T,
    std::void_t<decltype(std::declval<T>().secure_key_handle)>>
    : std::true_type {};

static_assert(!has_peer_id<RecoveryStatusDiagnostic>::value);
static_assert(!has_key_handle<RecoveryStatusDiagnostic>::value);

CriticalAlertSystemRecoveryStatus operational() {
    CriticalAlertSystemRecoveryStatus status{};
    status.operation = CriticalAlertSystemRecoveryOperation::boot;
    status.state = CriticalAlertSystemOperatorState::operational;
    status.reason = CriticalAlertSystemOperatorReason::none;
    status.action = CriticalAlertSystemOperatorAction::none;
    status.slot_a = CriticalAlertSystemRecoverySlotState::valid;
    status.slot_b = CriticalAlertSystemRecoverySlotState::valid;
    status.observed_generation = 8;
    status.trusted_generation = 8;
    status.transport_allowed = true;
    status.attention_required = false;
    return status;
}

void test_exact_operational_word_and_round_trip() {
    const auto encoded = encode_recovery_status_diagnostic(operational());
    EXPECT(encoded.encoded());
    EXPECT(encoded.word == 0xA010A008U);
    const auto decoded = decode_recovery_status_diagnostic(encoded.word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.operation ==
           CriticalAlertSystemRecoveryOperation::boot);
    EXPECT(decoded.diagnostic.state ==
           CriticalAlertSystemOperatorState::operational);
    EXPECT(decoded.diagnostic.slot_a ==
           CriticalAlertSystemRecoverySlotState::valid);
    EXPECT(decoded.diagnostic.slot_b ==
           CriticalAlertSystemRecoverySlotState::valid);
    EXPECT(decoded.diagnostic.transport_allowed);
    EXPECT(!decoded.diagnostic.attention_required);
}

void test_degraded_and_redacted_key_words_round_trip() {
    auto degraded = operational();
    degraded.state =
        CriticalAlertSystemOperatorState::operational_degraded;
    degraded.action = CriticalAlertSystemOperatorAction::repair_redundancy;
    degraded.slot_a = CriticalAlertSystemRecoverySlotState::invalid;
    degraded.attention_required = true;
    degraded.repair_required = true;
    const auto degraded_decoded = decode_recovery_status_diagnostic(
        encode_recovery_status_diagnostic(degraded).word);
    EXPECT(degraded_decoded.decoded());
    EXPECT(degraded_decoded.diagnostic.repair_required);
    EXPECT(degraded_decoded.diagnostic.action ==
           CriticalAlertSystemOperatorAction::repair_redundancy);

    CriticalAlertSystemRecoveryStatus key_failure{};
    key_failure.state = CriticalAlertSystemOperatorState::service_required;
    key_failure.reason =
        CriticalAlertSystemOperatorReason::protected_key_unavailable;
    key_failure.action = CriticalAlertSystemOperatorAction::service;
    key_failure.protected_key_error =
        CriticalAlertSystemRecoveryKeyValidationError::key_unavailable;
    key_failure.sensitive_detail_redacted = true;
    const auto key_decoded = decode_recovery_status_diagnostic(
        encode_recovery_status_diagnostic(key_failure).word);
    EXPECT(key_decoded.decoded());
    EXPECT(key_decoded.diagnostic.protected_key_error ==
           CriticalAlertSystemRecoveryKeyValidationError::key_unavailable);
    EXPECT(key_decoded.diagnostic.sensitive_detail_redacted);
}

void test_records_one_canonical_event_at_state_severity() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::trace, ResetReason::power_on}, 0) ==
           DiagnosticsError::none);
    const auto first = record_recovery_status(service, operational(), 1);
    EXPECT(first.accepted() && first.record.stored);

    auto degraded = operational();
    degraded.state =
        CriticalAlertSystemOperatorState::operational_degraded;
    degraded.action = CriticalAlertSystemOperatorAction::repair_redundancy;
    degraded.attention_required = true;
    degraded.repair_required = true;
    const auto second = record_recovery_status(service, degraded, 2);
    EXPECT(second.accepted() && second.record.stored);

    CriticalAlertSystemRecoveryStatus service_required{};
    service_required.state =
        CriticalAlertSystemOperatorState::service_required;
    service_required.reason =
        CriticalAlertSystemOperatorReason::storage_unavailable;
    service_required.action = CriticalAlertSystemOperatorAction::service;
    const auto third = record_recovery_status(service, service_required, 3);
    EXPECT(third.accepted() && third.record.stored);

    std::array<DiagnosticEvent, kDiagnosticEventCapacity> events{};
    const auto snapshot = service.snapshot_events(events.data(), events.size());
    EXPECT(snapshot.event_count == 4);
    EXPECT(events[1].level == LogLevel::info);
    EXPECT(events[2].level == LogLevel::warning);
    EXPECT(events[3].level == LogLevel::error);
    for (std::size_t index = 1; index < 4; ++index) {
        EXPECT(events[index].code == EventCode::configuration_recovery);
        EXPECT(events[index].metric == MetricCode::state_code);
        EXPECT(decode_recovery_status_diagnostic(
                   static_cast<std::uint32_t>(events[index].value))
                   .decoded());
    }
}

void test_diagnostics_threshold_can_filter_non_error_status() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::error, ResetReason::software}, 0) ==
           DiagnosticsError::none);
    const auto filtered = record_recovery_status(service, operational(), 1);
    EXPECT(filtered.accepted());
    EXPECT(!filtered.record.stored);
    EXPECT(service.status().event_count == 0);

    auto service_required = operational();
    service_required.state =
        CriticalAlertSystemOperatorState::service_required;
    service_required.reason =
        CriticalAlertSystemOperatorReason::storage_unavailable;
    service_required.action = CriticalAlertSystemOperatorAction::service;
    service_required.transport_allowed = false;
    service_required.attention_required = true;
    const auto stored =
        record_recovery_status(service, service_required, 2);
    EXPECT(stored.accepted() && stored.record.stored);
}

void test_incoherent_or_unknown_status_is_rejected_without_record() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::trace, ResetReason::power_on}, 0) ==
           DiagnosticsError::none);
    const auto before = service.status().event_count;

    auto invalid = operational();
    invalid.transport_allowed = false;
    EXPECT(encode_recovery_status_diagnostic(invalid).error ==
           RecoveryStatusDiagnosticError::invalid_status);
    EXPECT(record_recovery_status(service, invalid, 1).error ==
           RecoveryStatusDiagnosticError::invalid_status);

    invalid = operational();
    invalid.reason = static_cast<CriticalAlertSystemOperatorReason>(99);
    EXPECT(record_recovery_status(service, invalid, 1).error ==
           RecoveryStatusDiagnosticError::invalid_status);

    invalid = operational();
    invalid.protected_key_error =
        CriticalAlertSystemRecoveryKeyValidationError::backend_failure;
    EXPECT(record_recovery_status(service, invalid, 1).error ==
           RecoveryStatusDiagnosticError::invalid_status);
    EXPECT(service.status().event_count == before);
}

void test_malformed_words_fail_closed() {
    const auto good = encode_recovery_status_diagnostic(operational()).word;
    EXPECT(decode_recovery_status_diagnostic(good ^ 0x10000000U).error ==
           RecoveryStatusDiagnosticError::invalid_word);
    EXPECT(decode_recovery_status_diagnostic(good | 0x01000000U).error ==
           RecoveryStatusDiagnosticError::unsupported_version);
    EXPECT(decode_recovery_status_diagnostic(good | (7U << 10U)).error ==
           RecoveryStatusDiagnosticError::invalid_word);
    EXPECT(decode_recovery_status_diagnostic(good ^ (1U << 20U)).error ==
           RecoveryStatusDiagnosticError::invalid_word);
}

void test_service_lifecycle_and_time_errors_remain_visible() {
    DiagnosticsService stopped{};
    const auto unavailable =
        record_recovery_status(stopped, operational(), 1);
    EXPECT(unavailable.error == RecoveryStatusDiagnosticError::none);
    EXPECT(unavailable.record.error == DiagnosticsError::invalid_state);

    EXPECT(stopped.start({LogLevel::trace, ResetReason::power_on}, 10) ==
           DiagnosticsError::none);
    const auto regressed =
        record_recovery_status(stopped, operational(), 9);
    EXPECT(regressed.error == RecoveryStatusDiagnosticError::none);
    EXPECT(regressed.record.error == DiagnosticsError::time_regression);
}

void test_payload_is_fixed_and_identifier_free() {
    static_assert(std::is_trivially_copyable_v<RecoveryStatusDiagnostic>);
    EXPECT(sizeof(RecoveryStatusDiagnostic) <= 16);
    EXPECT(sizeof(RecoveryStatusDiagnosticEncodeResult) <= 8);
}

}  // namespace

int main() {
    test_exact_operational_word_and_round_trip();
    test_degraded_and_redacted_key_words_round_trip();
    test_records_one_canonical_event_at_state_severity();
    test_diagnostics_threshold_can_filter_non_error_status();
    test_incoherent_or_unknown_status_is_rejected_without_record();
    test_malformed_words_fail_closed();
    test_service_lifecycle_and_time_errors_remain_visible();
    test_payload_is_fixed_and_identifier_free();
    if (failures != 0) {
        std::cerr << failures
                  << " recovery diagnostics assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 redacted recovery diagnostics groups\n";
    return EXIT_SUCCESS;
}
