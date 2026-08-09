#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/esp_now_transport.hpp"
#include "opengauge/telemetry_packet.hpp"

namespace opengauge::wireless {

inline constexpr std::size_t kMaximumScheduledGaugePeers = 8;
inline constexpr std::size_t kMaximumSubscriptionsPerGauge = 8;
inline constexpr std::size_t kMaximumScheduledSignals = 16;
inline constexpr std::uint32_t kMinimumPeerPacketIntervalMs = 50;
inline constexpr std::uint32_t kMaximumPeerPacketIntervalMs = 60000;

struct GatewayPublishIdentity {
    std::uint64_t gateway_id{0};
    std::uint32_t boot_session_id{0};
    std::uint64_t boot_started_at_ms{0};
};

struct TelemetrySubscription {
    TelemetrySignalCode code{TelemetrySignalCode::engine_speed};
    std::uint32_t minimum_interval_ms{0};
    std::uint32_t maximum_interval_ms{0};
    std::uint32_t stale_after_ms{0};
    std::uint64_t deadband_raw{0};
};

struct PeerPublishPolicy {
    // Default ceiling is 20 packets/s per gauge. This bounds initial-sync and
    // simultaneous quality-change bursts in addition to per-signal intervals.
    std::uint32_t minimum_packet_interval_ms{kMinimumPeerPacketIntervalMs};
};

enum class PublishSchedulerError : std::uint8_t {
    none = 0,
    no_data,
    invalid_argument,
    invalid_state,
    invalid_identity,
    invalid_subscription,
    duplicate_subscription,
    peer_not_found,
    peer_already_exists,
    peer_capacity_full,
    signal_capacity_full,
    clock_regressed,
    signal_rejected,
    plan_pending,
    plan_not_found,
    plan_token_mismatch,
};

struct SchedulerUpdateResult {
    PublishSchedulerError error{PublishSchedulerError::invalid_argument};
    TelemetryPacketError packet_error{TelemetryPacketError::none};
    bool inserted{false};
    bool semantic_change{false};

    [[nodiscard]] constexpr bool accepted() const {
        return error == PublishSchedulerError::none;
    }
};

struct TelemetryPublishPlan {
    PublishSchedulerError error{PublishSchedulerError::no_data};
    std::uint32_t token{0};
    PeerAddress destination{};
    TelemetryBatch batch{};

    [[nodiscard]] constexpr bool prepared() const {
        return error == PublishSchedulerError::none;
    }
};

struct PublishCommitResult {
    PublishSchedulerError error{PublishSchedulerError::plan_not_found};
    bool transport_accepted{false};
    std::uint32_t next_sequence{0};

    [[nodiscard]] constexpr bool committed() const {
        return error == PublishSchedulerError::none;
    }
};

struct PublishSchedulerStatus {
    bool running{false};
    std::size_t peer_count{0};
    std::size_t signal_count{0};
    std::size_t pending_plan_count{0};
    std::uint32_t plans_prepared{0};
    std::uint32_t plans_transport_accepted{0};
    std::uint32_t plans_transport_rejected{0};
};

// Cooperative, single-owner scheduler. It never sleeps, sends, retries, or
// owns a transport. The caller prepares a packet, attempts one nonblocking
// transport send, and commits whether the local queue accepted it. Radio loss
// after local acceptance is handled by receiver sequence/age behavior and the
// configured periodic refresh, not a blocking retransmit loop.
class TelemetryPublishScheduler {
public:
    [[nodiscard]] PublishSchedulerError start(
        GatewayPublishIdentity identity);
    void stop();

    [[nodiscard]] PublishSchedulerError add_peer(
        const PeerAddress& address,
        const TelemetrySubscription* subscriptions,
        std::size_t subscription_count,
        PeerPublishPolicy publish_policy = {});
    [[nodiscard]] PublishSchedulerError remove_peer(
        const PeerAddress& address);

    [[nodiscard]] SchedulerUpdateResult update_signal(
        TelemetrySignalCode code,
        const telemetry::CachedSignalSnapshot& snapshot,
        std::uint64_t observed_at_ms);

    // Drops latest source states and invalidates per-subscription publication
    // baselines without changing peer configuration or packet sequences. New
    // state after a source-cache epoch change is therefore published promptly;
    // absent state is not invented and gauges age their old values stale.
    [[nodiscard]] PublishSchedulerError reset_source_epoch();

    [[nodiscard]] TelemetryPublishPlan prepare(
        const PeerAddress& destination,
        std::uint64_t now_ms);

    // `transport_accepted` means the local nonblocking transport copied the
    // frame into its queue. It is deliberately not MAC delivery or an
    // application acknowledgement.
    [[nodiscard]] PublishCommitResult commit(
        const PeerAddress& destination,
        std::uint32_t plan_token,
        bool transport_accepted,
        std::uint64_t now_ms);

    [[nodiscard]] PublishSchedulerStatus status() const;

private:
    struct SignalState {
        WireTelemetrySignal signal{};
        std::uint64_t observed_at_ms{0};
        std::uint64_t revision{0};
        bool occupied{false};
    };

    struct SubscriptionState {
        TelemetrySubscription policy{};
        WireTelemetrySignal last_published{};
        std::uint64_t last_published_at_ms{0};
        std::uint64_t last_published_revision{0};
        bool has_published{false};
    };

    struct PendingPlan {
        TelemetryBatch batch{};
        std::array<std::uint8_t, kTelemetrySignalsPerPacket>
            subscription_indices{};
        std::array<std::uint64_t, kTelemetrySignalsPerPacket> revisions{};
        std::uint64_t prepared_at_ms{0};
        std::uint32_t token{0};
        bool active{false};
    };

    struct PeerState {
        PeerAddress address{};
        std::array<SubscriptionState, kMaximumSubscriptionsPerGauge>
            subscriptions{};
        std::uint8_t subscription_count{0};
        std::uint32_t next_sequence{0};
        PeerPublishPolicy publish_policy{};
        std::uint64_t last_packet_accepted_at_ms{0};
        bool has_accepted_packet{false};
        PendingPlan pending{};
        bool occupied{false};
    };

    [[nodiscard]] std::size_t find_peer(
        const PeerAddress& address) const;
    [[nodiscard]] std::size_t find_signal(
        TelemetrySignalCode code) const;

    GatewayPublishIdentity identity_{};
    std::array<PeerState, kMaximumScheduledGaugePeers> peers_{};
    std::array<SignalState, kMaximumScheduledSignals> signals_{};
    std::size_t peer_count_{0};
    std::size_t signal_count_{0};
    std::uint32_t next_plan_token_{1};
    std::uint32_t plans_prepared_{0};
    std::uint32_t plans_transport_accepted_{0};
    std::uint32_t plans_transport_rejected_{0};
    bool running_{false};
};

}  // namespace opengauge::wireless
