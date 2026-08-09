#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/critical_alert_outbox.hpp"

namespace {

using namespace opengauge::integration;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

CriticalAlert alert(
    std::uint64_t event_id,
    AlertSeverity severity = AlertSeverity::critical) {
    CriticalAlert value{};
    value.type = CriticalAlertType::engine_over_temperature;
    value.severity = severity;
    value.state = AlertState::asserted;
    value.quality = AlertQuality::valid;
    value.unit = AlertUnit::celsius;
    value.producer_id = 1;
    value.vehicle_id = 2;
    value.event_id = event_id;
    value.condition_id = 1000 + event_id;
    value.age_ms = 10;
    value.value_milli = 110000;
    value.value_present = true;
    return value;
}

std::array<std::uint8_t, kCriticalAlertFrameBytes> frame(
    std::uint64_t event_id,
    AlertSeverity severity = AlertSeverity::critical) {
    std::array<std::uint8_t, kCriticalAlertFrameBytes> output{};
    EXPECT(encode_critical_alert(alert(event_id, severity), output).encoded());
    return output;
}

CriticalAlertOutboxConfiguration configuration() {
    return {50, 100, 25, 500, 2, 1};
}

void test_lifecycle_configuration_and_frame_validation() {
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.enqueue(frame(1), 0) == CriticalOutboxError::invalid_state);
    auto invalid = configuration();
    invalid.maximum_attempts = 0;
    EXPECT(outbox.start(invalid) ==
           CriticalOutboxError::invalid_configuration);
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::invalid_state);
    auto malformed = frame(1);
    malformed[0] = 'X';
    EXPECT(outbox.enqueue(malformed, 0) ==
           CriticalOutboxError::invalid_frame);
    outbox.stop();
    EXPECT(outbox.prepare(0).error == CriticalOutboxError::invalid_state);
}

void test_capacity_reserve_priority_duplicate_and_oldest_order() {
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(1), 0) == CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(2, AlertSeverity::emergency), 1) ==
           CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(1), 2) ==
           CriticalOutboxError::duplicate_event);
    auto prepared = outbox.prepare(2);
    EXPECT(prepared.event_id == 2);
    EXPECT(outbox.commit_local_send(prepared.token, true, 2) ==
           CriticalOutboxError::none);
    EXPECT(outbox.acknowledge({2, 1002, AlertState::asserted}, 3) ==
           CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(3), 4) == CriticalOutboxError::none);
    prepared = outbox.prepare(4);
    EXPECT(prepared.event_id == 1);

    CriticalAlertOutbox full{};
    EXPECT(full.start(configuration()) == CriticalOutboxError::none);
    for (std::uint64_t id = 1; id < kCriticalAlertOutboxCapacity; ++id) {
        EXPECT(full.enqueue(frame(id), id) == CriticalOutboxError::none);
    }
    EXPECT(full.enqueue(frame(8), 8) == CriticalOutboxError::capacity_full);
    EXPECT(full.enqueue(frame(99, AlertSeverity::emergency), 9) ==
           CriticalOutboxError::none);
    EXPECT(full.enqueue(frame(100, AlertSeverity::emergency), 10) ==
           CriticalOutboxError::capacity_full);
}

void test_local_rejection_does_not_consume_attempt() {
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(1), 0) == CriticalOutboxError::none);
    auto prepared = outbox.prepare(0);
    EXPECT(prepared.prepared());
    EXPECT(outbox.commit_local_send(prepared.token, false, 0) ==
           CriticalOutboxError::none);
    EXPECT(outbox.prepare(24).error == CriticalOutboxError::no_frame_ready);
    prepared = outbox.prepare(25);
    EXPECT(prepared.event_id == 1);
    EXPECT(outbox.status().local_rejections == 1);
    EXPECT(outbox.status().local_acceptances == 0);
}

void test_ack_timeout_retry_and_terminal_failure() {
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(1), 0) == CriticalOutboxError::none);
    auto prepared = outbox.prepare(0);
    EXPECT(outbox.commit_local_send(prepared.token, true, 0) ==
           CriticalOutboxError::none);
    auto advanced = outbox.advance(99);
    EXPECT(advanced.retries_released == 0);
    advanced = outbox.advance(100);
    EXPECT(advanced.retries_released == 1);
    EXPECT(outbox.prepare(124).error == CriticalOutboxError::no_frame_ready);
    prepared = outbox.prepare(125);
    EXPECT(outbox.commit_local_send(prepared.token, true, 125) ==
           CriticalOutboxError::none);
    advanced = outbox.advance(225);
    EXPECT(advanced.failure_count == 1);
    EXPECT(advanced.failures[0].reason ==
           CriticalDeliveryFailure::acknowledgement_timeout);
    EXPECT(advanced.failures[0].attempts == 2);
    EXPECT(outbox.status().terminal_failures == 1);
}

void test_late_ack_after_retry_release_is_accepted() {
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(1), 0) == CriticalOutboxError::none);
    const auto prepared = outbox.prepare(0);
    EXPECT(outbox.commit_local_send(prepared.token, true, 0) ==
           CriticalOutboxError::none);
    EXPECT(outbox.advance(100).retries_released == 1);
    EXPECT(outbox.acknowledge({1, 1001, AlertState::asserted}, 101) ==
           CriticalOutboxError::none);
    EXPECT(outbox.status().acknowledgements == 1);
    EXPECT(outbox.status().queued_count == 0);
}

void test_ack_and_token_mismatch_preserve_entry() {
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(1), 0) == CriticalOutboxError::none);
    const auto prepared = outbox.prepare(0);
    EXPECT(outbox.commit_local_send(prepared.token + 1, true, 0) ==
           CriticalOutboxError::token_mismatch);
    EXPECT(outbox.commit_local_send(prepared.token, true, 0) ==
           CriticalOutboxError::none);
    EXPECT(outbox.acknowledge({1, 999, AlertState::asserted}, 1) ==
           CriticalOutboxError::acknowledgement_mismatch);
    EXPECT(outbox.acknowledge({1, 1001, AlertState::cleared}, 1) ==
           CriticalOutboxError::acknowledgement_mismatch);
    EXPECT(outbox.status().in_flight_count == 1);
}

void test_prepared_commit_timeout_releases_without_attempt() {
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(1), 0) == CriticalOutboxError::none);
    const auto prepared = outbox.prepare(0);
    EXPECT(prepared.prepared());
    EXPECT(outbox.advance(49).prepared_released == 0);
    EXPECT(outbox.advance(50).prepared_released == 1);
    EXPECT(!outbox.status().send_prepared);
    EXPECT(outbox.prepare(74).error == CriticalOutboxError::no_frame_ready);
    EXPECT(outbox.prepare(75).event_id == 1);
    EXPECT(outbox.status().local_acceptances == 0);
}

void test_lifetime_failure_and_clock_regression() {
    CriticalAlertOutbox outbox{};
    EXPECT(outbox.start(configuration()) == CriticalOutboxError::none);
    EXPECT(outbox.enqueue(frame(1), 10) == CriticalOutboxError::none);
    EXPECT(outbox.prepare(9).error == CriticalOutboxError::clock_regression);
    EXPECT(outbox.advance(509).failure_count == 0);
    const auto advanced = outbox.advance(510);
    EXPECT(advanced.failure_count == 1);
    EXPECT(advanced.failures[0].reason ==
           CriticalDeliveryFailure::maximum_lifetime);
    EXPECT(advanced.failures[0].attempts == 0);
    EXPECT(outbox.status().queued_count == 0);
}

}  // namespace

int main() {
    test_lifecycle_configuration_and_frame_validation();
    test_capacity_reserve_priority_duplicate_and_oldest_order();
    test_local_rejection_does_not_consume_attempt();
    test_ack_timeout_retry_and_terminal_failure();
    test_late_ack_after_retry_release_is_accepted();
    test_ack_and_token_mismatch_preserve_entry();
    test_prepared_commit_timeout_releases_without_attempt();
    test_lifetime_failure_and_clock_regression();

    if (failures != 0) {
        std::cerr << failures << " critical alert outbox assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 critical alert outbox scenario groups\n";
    return EXIT_SUCCESS;
}
