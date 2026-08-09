#pragma once

#include <cstddef>
#include <cstdint>

#include "opengauge/telemetry_cache.hpp"
#include "opengauge/telemetry_publish_scheduler.hpp"

namespace opengauge::wireless {

enum class GatewayPublisherError : std::uint8_t {
    none = 0,
    no_data,
    invalid_state,
    cache_failure,
    scheduler_failure,
    codec_failure,
    transport_failure,
};

struct CachePollResult {
    GatewayPublisherError error{GatewayPublisherError::invalid_state};
    telemetry::CacheError cache_error{telemetry::CacheError::none};
    PublishSchedulerError scheduler_error{PublishSchedulerError::none};
    std::size_t snapshots_collected{0};
    std::size_t registered_signals_updated{0};
    std::size_t unregistered_signals_skipped{0};
    bool cache_epoch_changed{false};

    [[nodiscard]] constexpr bool polled() const {
        return error == GatewayPublisherError::none;
    }
};

struct PeerServiceResult {
    GatewayPublisherError error{GatewayPublisherError::no_data};
    PublishSchedulerError scheduler_error{PublishSchedulerError::none};
    TelemetryPacketError packet_error{TelemetryPacketError::none};
    EspNowError transport_error{EspNowError::none};
    std::uint32_t packet_sequence{0};
    std::uint32_t transport_token{0};
    std::size_t encoded_bytes{0};
    bool local_queue_accepted{false};

    [[nodiscard]] constexpr bool queued() const {
        return error == GatewayPublisherError::none &&
               local_queue_accepted;
    }
};

// Composes cache cursor polling, the publication scheduler, explicit packet
// encoding, and one nonblocking transport enqueue attempt. Transport service,
// delivery callbacks, cache acquisition, and peer/key provisioning remain
// target-owned operations.
class TelemetryGatewayPublisher {
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

    [[nodiscard]] CachePollResult poll_cache(
        telemetry::TelemetryCache& cache,
        std::uint64_t now_ms);

    [[nodiscard]] PeerServiceResult service_peer(
        EspNowTransport& transport,
        const PeerAddress& destination,
        std::uint64_t now_ms);

    [[nodiscard]] PublishSchedulerStatus scheduler_status() const;

private:
    TelemetryPublishScheduler scheduler_{};
    telemetry::CacheCursor cache_cursor_{};
    bool cache_cursor_initialized_{false};
    bool running_{false};
};

}  // namespace opengauge::wireless
