#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/esp_now_transport.hpp"

namespace opengauge::wireless::test_support {

class FakeEspNowTransport final : public EspNowTransport {
public:
    inline static constexpr std::size_t kPeerCapacity = 8;
    inline static constexpr std::size_t kLinkCapacity = 8;
    inline static constexpr std::size_t kQueueCapacity = 4;

    explicit FakeEspNowTransport(
        std::size_t mtu_bytes = kMaximumEspNowPayloadBytes,
        std::uint64_t delivery_latency_ms = 0);

    [[nodiscard]] std::size_t mtu() const override;
    [[nodiscard]] EspNowStatus status() const override;

    EspNowError start(
        const PeerAddress& local_address,
        EspNowPolicy policy) override;
    void stop() override;

    EspNowError add_peer(const PeerConfiguration& peer) override;
    EspNowError remove_peer(const PeerAddress& address) override;

    SendResult send(
        const PeerAddress& destination,
        ByteView payload,
        std::uint64_t now_ms) override;
    ReceiveResult receive(MutableByteView destination) override;
    DeliveryResult poll_delivery() override;
    void service(std::uint64_t now_ms) override;

    void connect(FakeEspNowTransport& peer);
    void set_available(bool available);
    void set_faulted(bool faulted);
    void set_link_rssi(std::int16_t rssi_dbm, bool valid = true);
    void drop_next_transmissions(std::size_t count);

private:
    struct OutboundFrame {
        PeerAddress destination{};
        std::array<std::uint8_t, kMaximumEspNowPayloadBytes> bytes{};
        std::size_t size{0};
        std::uint64_t due_at_ms{0};
        std::uint32_t token{0};
        bool encrypted{false};
    };

    struct InboundFrame {
        std::array<std::uint8_t, kMaximumEspNowPayloadBytes> bytes{};
        std::size_t size{0};
        ReceiveMetadata metadata{};
    };

    [[nodiscard]] std::size_t find_peer(
        const PeerAddress& address) const;
    [[nodiscard]] FakeEspNowTransport* find_link(
        const PeerAddress& address) const;
    [[nodiscard]] bool accepts_source(
        const PeerAddress& source,
        bool encrypted) const;
    [[nodiscard]] bool push_inbound(
        const InboundFrame& frame);
    [[nodiscard]] bool push_receipt(
        const DeliveryReceipt& receipt);
    void pop_outbound();

    std::size_t mtu_bytes_{0};
    std::uint64_t delivery_latency_ms_{0};
    PeerAddress local_address_{};
    EspNowPolicy policy_{};
    bool started_{false};
    bool available_{true};
    bool faulted_{false};
    std::int16_t link_rssi_dbm_{0};
    bool link_rssi_valid_{false};
    std::size_t drop_count_{0};
    std::uint32_t next_token_{1};

    std::array<PeerConfiguration, kPeerCapacity> peers_{};
    std::size_t peer_count_{0};
    std::array<FakeEspNowTransport*, kLinkCapacity> links_{};
    std::size_t link_count_{0};

    std::array<OutboundFrame, kQueueCapacity> outbound_{};
    std::size_t outbound_head_{0};
    std::size_t outbound_tail_{0};
    std::size_t outbound_count_{0};
    std::array<InboundFrame, kQueueCapacity> inbound_{};
    std::size_t inbound_head_{0};
    std::size_t inbound_tail_{0};
    std::size_t inbound_count_{0};
    std::array<DeliveryReceipt, kQueueCapacity> receipts_{};
    std::size_t receipt_head_{0};
    std::size_t receipt_tail_{0};
    std::size_t receipt_count_{0};

    EspNowError last_error_{EspNowError::none};
    std::uint32_t frames_accepted_{0};
    std::uint32_t frames_radio_delivered_{0};
    std::uint32_t frames_delivery_failed_{0};
    std::uint32_t frames_received_{0};
};

}  // namespace opengauge::wireless::test_support
