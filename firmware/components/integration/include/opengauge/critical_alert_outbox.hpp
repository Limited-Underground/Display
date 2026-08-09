#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_ack.hpp"

namespace opengauge::integration {

inline constexpr std::size_t kCriticalAlertOutboxCapacity = 8;

enum class CriticalOutboxError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    invalid_frame,
    duplicate_event,
    capacity_full,
    no_frame_ready,
    token_mismatch,
    acknowledgement_mismatch,
    clock_regression,
};

enum class CriticalDeliveryFailure : std::uint8_t {
    acknowledgement_timeout = 1,
    maximum_lifetime = 2,
    remote_rejection = 3,
};

enum class CriticalRemoteRejectionAction : std::uint8_t {
    retry = 1,
    terminal = 2,
};

struct CriticalAlertOutboxConfiguration {
    std::uint64_t local_commit_timeout_ms{0};
    std::uint64_t acknowledgement_timeout_ms{0};
    std::uint64_t retry_backoff_ms{0};
    std::uint64_t maximum_lifetime_ms{0};
    std::uint8_t maximum_attempts{0};
    std::uint8_t emergency_reserve{0};
};

struct PreparedCriticalAlert {
    CriticalOutboxError error{CriticalOutboxError::invalid_state};
    std::uint32_t token{0};
    std::uint64_t event_id{0};
    std::array<std::uint8_t, kCriticalAlertFrameBytes> frame{};

    [[nodiscard]] constexpr bool prepared() const {
        return error == CriticalOutboxError::none;
    }
};

struct CriticalAlertAcknowledgement {
    std::uint64_t event_id{0};
    std::uint64_t condition_id{0};
    AlertState state{AlertState::asserted};
};

struct CriticalDeliveryFailureEvent {
    std::uint64_t event_id{0};
    std::uint64_t condition_id{0};
    CriticalDeliveryFailure reason{
        CriticalDeliveryFailure::acknowledgement_timeout};
    std::uint8_t attempts{0};
    AlertAckReason remote_reason{AlertAckReason::none};
};

struct CriticalRemoteRejectionResult {
    CriticalOutboxError error{CriticalOutboxError::invalid_state};
    CriticalRemoteRejectionAction action{
        CriticalRemoteRejectionAction::terminal};
    CriticalDeliveryFailureEvent failure{};
    bool retry_released{false};
    bool terminal_failure{false};
};

struct CriticalOutboxAdvanceResult {
    CriticalOutboxError error{CriticalOutboxError::invalid_state};
    std::array<CriticalDeliveryFailureEvent,
               kCriticalAlertOutboxCapacity> failures{};
    std::size_t failure_count{0};
    std::size_t retries_released{0};
    std::size_t prepared_released{0};
};

struct CriticalAlertOutboxStatus {
    bool running{false};
    std::size_t queued_count{0};
    std::size_t in_flight_count{0};
    bool send_prepared{false};
    std::uint32_t next_token{1};
    std::uint32_t enqueued{0};
    std::uint32_t local_rejections{0};
    std::uint32_t local_acceptances{0};
    std::uint32_t acknowledgements{0};
    std::uint32_t retry_timeouts{0};
    std::uint32_t remote_retries{0};
    std::uint32_t remote_terminal_failures{0};
    std::uint32_t terminal_failures{0};
};

// Application-delivery outbox. Local transport acceptance is never treated as
// an OpenTrail acknowledgement.
class CriticalAlertOutbox {
public:
    [[nodiscard]] CriticalOutboxError start(
        const CriticalAlertOutboxConfiguration& configuration);
    void stop();
    [[nodiscard]] CriticalOutboxError enqueue(
        const std::array<std::uint8_t, kCriticalAlertFrameBytes>& frame,
        std::uint64_t now_ms);
    [[nodiscard]] PreparedCriticalAlert prepare(std::uint64_t now_ms);
    [[nodiscard]] CriticalOutboxError commit_local_send(
        std::uint32_t token,
        bool locally_accepted,
        std::uint64_t now_ms);
    [[nodiscard]] CriticalOutboxError acknowledge(
        const CriticalAlertAcknowledgement& acknowledgement,
        std::uint64_t now_ms);
    [[nodiscard]] CriticalOutboxError validate_acknowledgement(
        const CriticalAlertAcknowledgement& acknowledgement) const;
    [[nodiscard]] CriticalRemoteRejectionResult apply_remote_rejection(
        const CriticalAlertAcknowledgement& acknowledgement,
        AlertAckReason reason,
        std::uint64_t now_ms);
    [[nodiscard]] CriticalOutboxAdvanceResult advance(
        std::uint64_t now_ms);
    [[nodiscard]] CriticalAlertOutboxStatus status() const;

private:
    enum class EntryState : std::uint8_t {
        empty = 0,
        queued,
        prepared,
        in_flight,
    };

    struct Entry {
        EntryState state{EntryState::empty};
        CriticalAlert alert{};
        std::array<std::uint8_t, kCriticalAlertFrameBytes> frame{};
        std::uint64_t enqueued_ms{0};
        std::uint64_t state_changed_ms{0};
        std::uint64_t next_attempt_ms{0};
        std::uint32_t token{0};
        std::uint8_t attempts{0};
    };

    [[nodiscard]] CriticalOutboxError advance_clock(std::uint64_t now_ms);
    [[nodiscard]] std::size_t find_event(std::uint64_t event_id) const;
    [[nodiscard]] std::size_t find_token(std::uint32_t token) const;
    void remove_entry(std::size_t index);
    void refresh_counts();

    CriticalAlertOutboxConfiguration configuration_{};
    std::array<Entry, kCriticalAlertOutboxCapacity> entries_{};
    bool has_clock_{false};
    std::uint64_t last_monotonic_ms_{0};
    CriticalAlertOutboxStatus status_{};
};

}  // namespace opengauge::integration
