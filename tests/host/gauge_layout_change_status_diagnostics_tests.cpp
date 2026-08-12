#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

#include "opengauge/gauge_layout_change_status_diagnostics.hpp"

namespace {

using namespace opengauge;
using namespace opengauge::diagnostics;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

template <typename T, typename = void>
struct has_request_id : std::false_type {};
template <typename T>
struct has_request_id<
    T,
    std::void_t<decltype(std::declval<T>().pending_request_id)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_generation : std::false_type {};
template <typename T>
struct has_generation<
    T,
    std::void_t<decltype(std::declval<T>().generation)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_remaining_time : std::false_type {};
template <typename T>
struct has_remaining_time<
    T,
    std::void_t<decltype(std::declval<T>().confirmation_remaining_ms)>>
    : std::true_type {};

static_assert(!has_request_id<GaugeLayoutChangeStatusDiagnostic>::value);
static_assert(!has_generation<GaugeLayoutChangeStatusDiagnostic>::value);
static_assert(!has_remaining_time<GaugeLayoutChangeStatusDiagnostic>::value);

configuration::GaugeLayoutChangeOperatorStatus applied() {
    configuration::GaugeLayoutChangeOperatorStatus result{};
    result.state = configuration::GaugeLayoutChangeOperatorState::applied;
    result.action = configuration::GaugeLayoutChangeOperatorAction::none;
    result.generation = 8;
    return result;
}

configuration::GaugeLayoutChangeOperatorStatus confirmation(
    bool rejected = false) {
    configuration::GaugeLayoutChangeOperatorStatus result{};
    result.state =
        configuration::GaugeLayoutChangeOperatorState::confirmation_required;
    result.action =
        configuration::GaugeLayoutChangeOperatorAction::confirm_or_cancel;
    result.pending_request_id = 17;
    result.confirmation_remaining_ms = 900;
    result.attention_required = true;
    result.confirmation_allowed = true;
    result.last_operation_rejected = rejected;
    return result;
}

configuration::GaugeLayoutChangeOperatorStatus terminal(
    configuration::GaugeLayoutChangeOperatorState state,
    configuration::GaugeLayoutChangeOperatorAction action) {
    configuration::GaugeLayoutChangeOperatorStatus result{};
    result.state = state;
    result.action = action;
    result.attention_required = true;
    return result;
}

void test_exact_applied_word_and_round_trip() {
    const auto encoded =
        encode_gauge_layout_change_status_diagnostic(applied());
    EXPECT(encoded.encoded());
    EXPECT(encoded.word == 0xB0000403U);
    const auto decoded =
        decode_gauge_layout_change_status_diagnostic(encoded.word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.state ==
           configuration::GaugeLayoutChangeOperatorState::applied);
    EXPECT(decoded.diagnostic.action ==
           configuration::GaugeLayoutChangeOperatorAction::none);
    EXPECT(!decoded.diagnostic.attention_required);
    EXPECT(!decoded.diagnostic.confirmation_allowed);
    EXPECT(decoded.diagnostic.sensitive_detail_redacted);
}

void test_confirmation_word_omits_live_details() {
    const auto normal =
        encode_gauge_layout_change_status_diagnostic(confirmation());
    EXPECT(normal.encoded());
    EXPECT(normal.word == 0xB0000592U);
    const auto decoded =
        decode_gauge_layout_change_status_diagnostic(normal.word);
    EXPECT(decoded.decoded());
    EXPECT(decoded.diagnostic.confirmation_allowed);
    EXPECT(!decoded.diagnostic.last_operation_rejected);

    const auto rejected =
        encode_gauge_layout_change_status_diagnostic(confirmation(true));
    EXPECT(rejected.encoded());
    EXPECT(rejected.word == 0xB0000792U);
    EXPECT(decode_gauge_layout_change_status_diagnostic(rejected.word)
               .diagnostic.last_operation_rejected);
}

void test_all_terminal_categories_round_trip() {
    const std::array<configuration::GaugeLayoutChangeOperatorStatus, 5>
        statuses{{
            terminal(
                configuration::GaugeLayoutChangeOperatorState::expired,
                configuration::GaugeLayoutChangeOperatorAction::service_expiry),
            terminal(
                configuration::GaugeLayoutChangeOperatorState::
                    persistence_failed,
                configuration::GaugeLayoutChangeOperatorAction::
                    stage_new_request),
            terminal(
                configuration::GaugeLayoutChangeOperatorState::restart_required,
                configuration::GaugeLayoutChangeOperatorAction::
                    restart_and_reconcile),
            terminal(
                configuration::GaugeLayoutChangeOperatorState::clock_fault,
                configuration::GaugeLayoutChangeOperatorAction::service_clock),
            [] {
                auto value = terminal(
                    configuration::GaugeLayoutChangeOperatorState::rejected,
                    configuration::GaugeLayoutChangeOperatorAction::
                        stage_new_request);
                value.last_operation_rejected = true;
                return value;
            }(),
        }};
    for (const auto& status : statuses) {
        const auto encoded =
            encode_gauge_layout_change_status_diagnostic(status);
        EXPECT(encoded.encoded());
        const auto decoded =
            decode_gauge_layout_change_status_diagnostic(encoded.word);
        EXPECT(decoded.decoded());
        EXPECT(decoded.diagnostic.state == status.state);
        EXPECT(decoded.diagnostic.action == status.action);
        EXPECT(decoded.diagnostic.attention_required);
    }
}

void test_records_canonical_events_at_bounded_severity() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::trace, ResetReason::power_on}, 0) ==
           DiagnosticsError::none);
    EXPECT(record_gauge_layout_change_status(service, applied(), 1)
               .accepted());

    auto expired = terminal(
        configuration::GaugeLayoutChangeOperatorState::expired,
        configuration::GaugeLayoutChangeOperatorAction::stage_new_request);
    EXPECT(record_gauge_layout_change_status(service, expired, 2)
               .accepted());
    auto restart = terminal(
        configuration::GaugeLayoutChangeOperatorState::restart_required,
        configuration::GaugeLayoutChangeOperatorAction::
            restart_and_reconcile);
    EXPECT(record_gauge_layout_change_status(service, restart, 3)
               .accepted());

    std::array<DiagnosticEvent, kDiagnosticEventCapacity> events{};
    const auto snapshot = service.snapshot_events(events.data(), events.size());
    EXPECT(snapshot.event_count == 4);
    EXPECT(events[1].level == LogLevel::info);
    EXPECT(events[2].level == LogLevel::warning);
    EXPECT(events[3].level == LogLevel::error);
    for (std::size_t index = 1; index < 4; ++index) {
        EXPECT(events[index].code == EventCode::configuration_recovery);
        EXPECT(events[index].metric == MetricCode::state_code);
        EXPECT(decode_gauge_layout_change_status_diagnostic(
                   static_cast<std::uint32_t>(events[index].value))
                   .decoded());
    }
}

void test_threshold_filters_info_but_keeps_error() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::error, ResetReason::software}, 0) ==
           DiagnosticsError::none);
    const auto filtered =
        record_gauge_layout_change_status(service, applied(), 1);
    EXPECT(filtered.accepted());
    EXPECT(!filtered.record.stored);

    const auto clock = terminal(
        configuration::GaugeLayoutChangeOperatorState::clock_fault,
        configuration::GaugeLayoutChangeOperatorAction::service_clock);
    const auto stored =
        record_gauge_layout_change_status(service, clock, 2);
    EXPECT(stored.accepted());
    EXPECT(stored.record.stored);
}

void test_incoherent_live_status_is_rejected_without_record() {
    DiagnosticsService service{};
    EXPECT(service.start({LogLevel::trace, ResetReason::power_on}, 0) ==
           DiagnosticsError::none);
    const auto before = service.status().event_count;

    auto invalid = applied();
    invalid.generation = 0;
    EXPECT(encode_gauge_layout_change_status_diagnostic(invalid).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_status);
    EXPECT(record_gauge_layout_change_status(service, invalid, 1).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_status);

    invalid = confirmation();
    invalid.pending_request_id = 0;
    EXPECT(encode_gauge_layout_change_status_diagnostic(invalid).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_status);

    invalid = confirmation();
    invalid.generation = 2;
    EXPECT(encode_gauge_layout_change_status_diagnostic(invalid).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_status);

    invalid = terminal(
        configuration::GaugeLayoutChangeOperatorState::rejected,
        configuration::GaugeLayoutChangeOperatorAction::stage_new_request);
    EXPECT(encode_gauge_layout_change_status_diagnostic(invalid).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_status);
    EXPECT(service.status().event_count == before);
}

void test_malformed_words_fail_closed() {
    const auto good =
        encode_gauge_layout_change_status_diagnostic(applied()).word;
    EXPECT(decode_gauge_layout_change_status_diagnostic(
               good ^ 0x10000000U).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_word);
    EXPECT(decode_gauge_layout_change_status_diagnostic(
               good | 0x01000000U).error ==
           GaugeLayoutChangeStatusDiagnosticError::unsupported_version);
    EXPECT(decode_gauge_layout_change_status_diagnostic(
               good | (1U << 11U)).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_word);
    EXPECT(decode_gauge_layout_change_status_diagnostic(
               good ^ (1U << 10U)).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_word);
    EXPECT(decode_gauge_layout_change_status_diagnostic(
               good | 0x0FU).error ==
           GaugeLayoutChangeStatusDiagnosticError::invalid_word);
}

void test_diagnostics_lifecycle_and_time_errors_remain_visible() {
    DiagnosticsService stopped{};
    const auto unavailable =
        record_gauge_layout_change_status(stopped, applied(), 1);
    EXPECT(unavailable.error == GaugeLayoutChangeStatusDiagnosticError::none);
    EXPECT(unavailable.record.error == DiagnosticsError::invalid_state);

    EXPECT(stopped.start({LogLevel::trace, ResetReason::power_on}, 10) ==
           DiagnosticsError::none);
    const auto regressed =
        record_gauge_layout_change_status(stopped, applied(), 9);
    EXPECT(regressed.error == GaugeLayoutChangeStatusDiagnosticError::none);
    EXPECT(regressed.record.error == DiagnosticsError::time_regression);
}

}  // namespace

int main() {
    test_exact_applied_word_and_round_trip();
    test_confirmation_word_omits_live_details();
    test_all_terminal_categories_round_trip();
    test_records_canonical_events_at_bounded_severity();
    test_threshold_filters_info_but_keeps_error();
    test_incoherent_live_status_is_rejected_without_record();
    test_malformed_words_fail_closed();
    test_diagnostics_lifecycle_and_time_errors_remain_visible();

    if (failures != 0) {
        std::cerr << failures
                  << " layout-change diagnostic assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 redacted layout-change diagnostic groups\n";
    return EXIT_SUCCESS;
}
