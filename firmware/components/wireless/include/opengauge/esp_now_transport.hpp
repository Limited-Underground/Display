#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opengauge::wireless {

inline constexpr std::size_t kMaximumEspNowPayloadBytes = 250;

struct ByteView {
    const std::uint8_t* data{nullptr};
    std::size_t size{0};
};

struct MutableByteView {
    std::uint8_t* data{nullptr};
    std::size_t size{0};
};

struct PeerAddress {
    std::array<std::uint8_t, 6> bytes{};
};

[[nodiscard]] bool peer_address_equals(
    const PeerAddress& left,
    const PeerAddress& right);
[[nodiscard]] bool is_valid_unicast_address(const PeerAddress& address);

enum class EspNowError : std::uint8_t {
    none = 0,
    no_data,
    invalid_argument,
    invalid_state,
    payload_too_large,
    buffer_too_small,
    not_ready,
    busy,
    queue_full,
    peer_not_found,
    peer_already_exists,
    peer_table_full,
    channel_mismatch,
    encryption_required,
    io_failure,
    internal_failure,
};

enum class EspNowState : std::uint8_t {
    offline = 0,
    idle,
    transmitting,
    receiving,
    fault,
};

struct EspNowPolicy {
    std::uint8_t channel{0};
    bool require_encrypted_unicast{true};
};

struct PeerConfiguration {
    PeerAddress address{};
    std::uint8_t channel{0};
    bool encrypted{false};
};

struct SendResult {
    EspNowError error{EspNowError::invalid_state};
    std::uint32_t token{0};
    std::size_t accepted_bytes{0};

    [[nodiscard]] constexpr bool accepted() const {
        return error == EspNowError::none;
    }
};

struct ReceiveMetadata {
    PeerAddress source{};
    std::uint64_t received_at_ms{0};
    std::int16_t rssi_dbm{0};
    std::uint8_t channel{0};
    bool rssi_valid{false};
    bool encrypted{false};
};

struct ReceiveResult {
    EspNowError error{EspNowError::no_data};
    std::size_t received_bytes{0};
    ReceiveMetadata metadata{};

    [[nodiscard]] constexpr bool has_frame() const {
        return error == EspNowError::none;
    }
};

enum class DeliveryOutcome : std::uint8_t {
    delivered_to_peer_radio = 1,
    no_link,
    peer_not_ready,
    channel_mismatch,
    receiver_rejected,
    receiver_queue_full,
    injected_loss,
};

struct DeliveryReceipt {
    std::uint32_t token{0};
    PeerAddress destination{};
    DeliveryOutcome outcome{DeliveryOutcome::no_link};
    std::uint64_t completed_at_ms{0};

    [[nodiscard]] constexpr bool radio_delivered() const {
        return outcome == DeliveryOutcome::delivered_to_peer_radio;
    }
};

struct DeliveryResult {
    EspNowError error{EspNowError::no_data};
    DeliveryReceipt receipt{};

    [[nodiscard]] constexpr bool has_receipt() const {
        return error == EspNowError::none;
    }
};

struct EspNowStatus {
    EspNowState state{EspNowState::offline};
    EspNowError last_error{EspNowError::none};
    std::size_t mtu_bytes{0};
    std::size_t peer_count{0};
    std::size_t transmit_queue_depth{0};
    std::size_t receive_queue_depth{0};
    std::size_t completion_queue_depth{0};
    std::uint32_t frames_accepted{0};
    std::uint32_t frames_radio_delivered{0};
    std::uint32_t frames_delivery_failed{0};
    std::uint32_t frames_received{0};
};

// This boundary moves opaque, unicast ESP-NOW datagrams. A successful send
// means copied into the local queue. A radio-delivered receipt models the
// ESP-NOW MAC callback only; it is not an authenticated application ACK and
// does not prove that a gauge decoded, stored, or rendered the payload.
class EspNowTransport {
public:
    virtual ~EspNowTransport() = default;

    [[nodiscard]] virtual std::size_t mtu() const = 0;
    [[nodiscard]] virtual EspNowStatus status() const = 0;

    virtual EspNowError start(
        const PeerAddress& local_address,
        EspNowPolicy policy) = 0;
    virtual void stop() = 0;

    virtual EspNowError add_peer(
        const PeerConfiguration& peer) = 0;
    virtual EspNowError remove_peer(
        const PeerAddress& address) = 0;

    virtual SendResult send(
        const PeerAddress& destination,
        ByteView payload,
        std::uint64_t now_ms) = 0;
    virtual ReceiveResult receive(MutableByteView destination) = 0;
    virtual DeliveryResult poll_delivery() = 0;

    // Advances a cooperative adapter without blocking a decoder or UI task.
    virtual void service(std::uint64_t now_ms) = 0;
};

}  // namespace opengauge::wireless
