#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/esp_now_transport.hpp"
#include "opengauge/telemetry_packet.hpp"

namespace opengauge::wireless {

inline constexpr std::size_t kMaximumGaugeStoredSignals = 16;
inline constexpr std::size_t kMaximumGaugePacketsPerCycle = 4;

enum class GaugeReceiverError : std::uint8_t {
    none = 0,
    no_data,
    invalid_state,
    invalid_configuration,
    transport_failure,
    unauthorized_source,
    encryption_required,
    channel_mismatch,
    packet_failure,
    stream_failure,
    signal_capacity_full,
    signal_not_found,
    freshness_failure,
};

struct GaugeReceiverConfiguration {
    PeerAddress expected_gateway_address{};
    std::uint64_t expected_gateway_id{0};
    std::uint8_t expected_channel{0};
    std::uint8_t maximum_packets_per_cycle{0};
};

struct GaugeReceiverCycleResult {
    GaugeReceiverError error{GaugeReceiverError::no_data};
    EspNowError transport_error{EspNowError::none};
    TelemetryPacketError packet_error{TelemetryPacketError::none};
    TelemetryStreamError stream_error{TelemetryStreamError::none};
    std::size_t datagrams_received{0};
    std::size_t packets_accepted{0};
    std::size_t signals_updated{0};
    std::size_t unauthorized_datagrams{0};
    std::size_t unencrypted_datagrams{0};
    std::size_t channel_mismatches{0};
    std::size_t gateway_identity_mismatches{0};
    std::size_t malformed_packets{0};
    std::size_t stream_failures{0};
    std::size_t duplicate_packets{0};
    std::size_t out_of_order_packets{0};
    std::size_t gap_packets{0};
    std::uint32_t missing_packets{0};
    std::size_t session_resets{0};
    bool packet_budget_exhausted{false};

    [[nodiscard]] constexpr bool serviced() const {
        return error == GaugeReceiverError::none ||
               error == GaugeReceiverError::no_data;
    }
};

struct GaugeSignalReadResult {
    GaugeReceiverError error{GaugeReceiverError::signal_not_found};
    TelemetryFreshnessError freshness_error{TelemetryFreshnessError::none};
    TelemetrySignalCode code{TelemetrySignalCode::engine_speed};
    telemetry::SignalQuality effective_quality{
        telemetry::SignalQuality::unknown};
    telemetry::SignalValue display_value{};
    telemetry::SignalUnit unit{telemetry::SignalUnit::none};
    std::uint64_t age_ms{0};
    std::uint32_t boot_session_id{0};
    std::uint32_t packet_sequence{0};
    std::uint64_t packet_received_at_ms{0};

    [[nodiscard]] constexpr bool found() const {
        return error == GaugeReceiverError::none;
    }
};

struct GaugeReceiverStatus {
    bool running{false};
    GaugeReceiverError last_error{GaugeReceiverError::none};
    std::size_t signal_count{0};
    std::uint32_t datagrams_received{0};
    std::uint32_t packets_accepted{0};
    std::uint32_t signals_updated{0};
    std::uint32_t unauthorized_datagrams{0};
    std::uint32_t unencrypted_datagrams{0};
    std::uint32_t channel_mismatches{0};
    std::uint32_t gateway_identity_mismatches{0};
    std::uint32_t malformed_packets{0};
    std::uint32_t stream_failures{0};
    std::uint32_t duplicate_packets{0};
    std::uint32_t out_of_order_packets{0};
    std::uint32_t gap_packets{0};
    std::uint32_t missing_packets{0};
    std::uint32_t session_resets{0};
};

// Single-owner receiver composition. Radio provisioning and key storage remain
// target-owned. One cycle services the transport once and drains no more than
// the configured datagram budget.
class GaugeTelemetryReceiver {
public:
    explicit GaugeTelemetryReceiver(EspNowTransport& transport);

    [[nodiscard]] GaugeReceiverError start(
        GaugeReceiverConfiguration configuration);
    void stop();

    [[nodiscard]] GaugeReceiverCycleResult service(std::uint64_t now_ms);
    [[nodiscard]] GaugeSignalReadResult read(
        TelemetrySignalCode code,
        std::uint64_t now_ms,
        std::uint64_t stale_after_ms) const;
    [[nodiscard]] GaugeReceiverStatus status() const;

private:
    struct StoredSignal {
        WireTelemetrySignal signal{};
        std::uint64_t packet_received_at_ms{0};
        std::uint32_t boot_session_id{0};
        std::uint32_t packet_sequence{0};
        bool occupied{false};
    };

    [[nodiscard]] std::size_t find_signal(TelemetrySignalCode code) const;
    [[nodiscard]] bool can_store_batch(
        const TelemetryBatch& batch,
        bool clear_existing_first) const;
    void store_batch(
        const TelemetryBatch& batch,
        std::uint64_t packet_received_at_ms);
    void clear_store();

    EspNowTransport& transport_;
    GaugeReceiverConfiguration configuration_{};
    TelemetryStreamTracker stream_{};
    std::array<StoredSignal, kMaximumGaugeStoredSignals> signals_{};
    std::size_t signal_count_{0};
    GaugeReceiverStatus status_{};
};

}  // namespace opengauge::wireless
