#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/j1939_identifier.hpp"

namespace opengauge::can {

inline constexpr std::size_t kClassicalCanPayloadBytes = 8;
inline constexpr std::uint32_t kMaximumStandardCanIdentifier = 0x7FFU;

enum class CanFrameKind : std::uint8_t {
    data = 0,
    remote = 1,
};

struct CanFrame {
    std::uint32_t identifier{0};
    CanFrameFormat format{CanFrameFormat::standard};
    CanFrameKind kind{CanFrameKind::data};
    std::array<std::uint8_t, kClassicalCanPayloadBytes> data{};
    std::uint8_t data_length{0};
    std::uint64_t received_at_ms{0};
};

enum class CanBusState : std::uint8_t {
    error_active = 0,
    error_warning,
    error_passive,
    bus_off,
};

enum class CanReceiverMode : std::uint8_t {
    offline = 0,
    listen_only,
};

struct CanListenPolicy {
    std::uint32_t bitrate{0};
    bool accept_standard_frames{false};
    bool accept_extended_frames{true};
    bool accept_remote_frames{false};
};

enum class CanReceiverError : std::uint8_t {
    none = 0,
    no_frame,
    invalid_argument,
    invalid_state,
    invalid_policy,
    invalid_frame,
    filtered,
    queue_full,
    bus_off,
    clock_regressed,
    hardware_failure,
};

struct CanReceiveMetadata {
    CanBusState bus_state{CanBusState::error_active};
    std::uint32_t overflow_count{0};
    std::size_t queue_depth_after_receive{0};
};

struct CanReceiveResult {
    CanReceiverError error{CanReceiverError::no_frame};
    CanFrame frame{};
    CanReceiveMetadata metadata{};

    [[nodiscard]] constexpr bool has_frame() const {
        return error == CanReceiverError::none;
    }
};

struct CanReceiverStatus {
    CanReceiverMode mode{CanReceiverMode::offline};
    CanBusState bus_state{CanBusState::error_active};
    CanReceiverError last_error{CanReceiverError::none};
    std::uint32_t bitrate{0};
    std::size_t queue_depth{0};
    std::size_t queue_capacity{0};
    std::uint32_t frames_received{0};
    std::uint32_t frames_filtered{0};
    std::uint32_t frames_dropped_overflow{0};
    std::uint32_t bus_state_changes{0};
};

[[nodiscard]] CanReceiverError validate_can_frame(const CanFrame& frame);

// Passive Classical CAN receive boundary. There is deliberately no transmit
// method. A production adapter must configure the controller/transceiver in a
// mode that cannot acknowledge or drive the bus for initial bring-up.
class CanReceiver {
public:
    virtual ~CanReceiver() = default;

    virtual CanReceiverError start_listen_only(CanListenPolicy policy) = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual CanReceiveResult receive() = 0;
    [[nodiscard]] virtual CanReceiverStatus status() const = 0;
};

}  // namespace opengauge::can
