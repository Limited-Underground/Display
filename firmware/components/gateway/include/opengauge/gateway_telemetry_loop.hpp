#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/can_receiver.hpp"
#include "opengauge/j1939_decoder.hpp"
#include "opengauge/telemetry_cache.hpp"
#include "opengauge/telemetry_gateway_publisher.hpp"

namespace opengauge::gateway {

inline constexpr std::size_t kMaximumCanFramesPerGatewayCycle = 16;
inline constexpr std::size_t kMaximumDecodedSignalsPerCanFrame = 8;

enum class GatewayLoopError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    can_start_failure,
    publisher_start_failure,
    can_receive_failure,
    decode_failure,
    cache_write_failure,
    cache_poll_failure,
    peer_service_failure,
};

struct GatewayLoopConfiguration {
    can::CanListenPolicy can_policy{};
    wireless::GatewayPublishIdentity publish_identity{};
    std::uint64_t cache_stale_after_ms{0};
    std::uint8_t maximum_can_frames_per_cycle{0};
};

struct GatewayLoopStartResult {
    GatewayLoopError error{GatewayLoopError::invalid_configuration};
    can::CanReceiverError can_error{can::CanReceiverError::none};
    wireless::PublishSchedulerError publisher_error{
        wireless::PublishSchedulerError::none};

    [[nodiscard]] constexpr bool started() const {
        return error == GatewayLoopError::none;
    }
};

struct GatewayLoopCycleResult {
    GatewayLoopError error{GatewayLoopError::none};
    can::CanReceiverError can_error{can::CanReceiverError::none};
    can::J1939DecodeError decode_error{can::J1939DecodeError::none};
    telemetry::CacheError cache_error{telemetry::CacheError::none};
    wireless::GatewayPublisherError publisher_error{
        wireless::GatewayPublisherError::none};
    can::CanBusState bus_state{can::CanBusState::error_active};
    std::uint32_t receiver_overflow_count{0};
    std::size_t can_frames_received{0};
    std::size_t can_frames_decoded{0};
    std::size_t unsupported_can_frames{0};
    std::size_t decode_failures{0};
    std::size_t signals_decoded{0};
    std::size_t cache_writes_accepted{0};
    std::size_t cache_writes_rejected{0};
    std::size_t cache_snapshots_collected{0};
    std::size_t peers_serviced{0};
    std::size_t packets_queued{0};
    std::size_t peers_with_no_data{0};
    std::size_t peer_service_failures{0};
    bool can_frame_budget_exhausted{false};

    [[nodiscard]] constexpr bool healthy() const {
        return error == GatewayLoopError::none;
    }
};

struct GatewayLoopStatus {
    bool running{false};
    std::size_t peer_count{0};
    std::uint32_t cycles_serviced{0};
    std::uint32_t can_frames_received{0};
    std::uint32_t can_frames_decoded{0};
    std::uint32_t unsupported_can_frames{0};
    std::uint32_t decode_failures{0};
    std::uint32_t signals_decoded{0};
    std::uint32_t cache_writes_accepted{0};
    std::uint32_t cache_writes_rejected{0};
    std::uint32_t can_receive_failures{0};
    std::uint32_t packets_queued{0};
    std::uint32_t peer_service_failures{0};
    std::uint32_t last_receiver_overflow_count{0};
    can::CanBusState last_bus_state{can::CanBusState::error_active};
};

// Single-owner cooperative composition. It performs no sleeps, allocation,
// pairing, key provisioning, or hardware initialization beyond the abstract
// listen-only receiver start. Each cycle drains a bounded number of CAN frames,
// polls cache changes once, attempts at most one enqueue per gauge peer, and
// services the transport once.
class GatewayTelemetryLoop {
public:
    GatewayTelemetryLoop(
        can::CanReceiver& receiver,
        const can::J1939DecoderRegistry& decoders,
        telemetry::TelemetryCache& cache,
        wireless::TelemetryGatewayPublisher& publisher,
        wireless::EspNowTransport& transport);

    [[nodiscard]] GatewayLoopStartResult start(
        GatewayLoopConfiguration configuration);
    void stop();

    [[nodiscard]] wireless::PublishSchedulerError add_peer(
        const wireless::PeerAddress& address,
        const wireless::TelemetrySubscription* subscriptions,
        std::size_t subscription_count,
        wireless::PeerPublishPolicy publish_policy = {});
    [[nodiscard]] wireless::PublishSchedulerError remove_peer(
        const wireless::PeerAddress& address);

    [[nodiscard]] GatewayLoopCycleResult service(std::uint64_t now_ms);
    [[nodiscard]] GatewayLoopStatus status() const;

private:
    struct PeerSlot {
        wireless::PeerAddress address{};
        bool occupied{false};
    };

    [[nodiscard]] std::size_t find_peer(
        const wireless::PeerAddress& address) const;
    [[nodiscard]] std::size_t find_empty_peer() const;

    can::CanReceiver& receiver_;
    const can::J1939DecoderRegistry& decoders_;
    telemetry::TelemetryCache& cache_;
    wireless::TelemetryGatewayPublisher& publisher_;
    wireless::EspNowTransport& transport_;
    GatewayLoopConfiguration configuration_{};
    std::array<PeerSlot, wireless::kMaximumScheduledGaugePeers> peers_{};
    GatewayLoopStatus status_{};
};

}  // namespace opengauge::gateway
