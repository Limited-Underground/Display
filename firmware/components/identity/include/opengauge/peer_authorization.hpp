#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace opengauge::identity {

inline constexpr std::size_t kMaximumAuthorizedPeers = 8;

enum class PeerRole : std::uint8_t {
    gateway = 1,
    gauge = 2,
    gps = 3,
    trail_bridge = 4,
};

enum class PeerPermission : std::uint16_t {
    publish_telemetry = 1U << 0U,
    receive_telemetry = 1U << 1U,
    publish_gps = 1U << 2U,
    receive_critical_alert = 1U << 3U,
    publish_alarm_ack = 1U << 4U,
    receive_configuration = 1U << 5U,
};

[[nodiscard]] constexpr std::uint16_t permission_bit(
    PeerPermission permission) {
    return static_cast<std::uint16_t>(permission);
}

inline constexpr std::uint16_t kAllPeerPermissionBits =
    permission_bit(PeerPermission::publish_telemetry) |
    permission_bit(PeerPermission::receive_telemetry) |
    permission_bit(PeerPermission::publish_gps) |
    permission_bit(PeerPermission::receive_critical_alert) |
    permission_bit(PeerPermission::publish_alarm_ack) |
    permission_bit(PeerPermission::receive_configuration);

enum class PeerAuthorizationError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    invalid_candidate,
    approval_pending,
    approval_not_found,
    approval_expired,
    duplicate_peer,
    duplicate_key_handle,
    capacity_full,
    unknown_peer,
    peer_revoked,
    key_mismatch,
    channel_mismatch,
    permission_denied,
    stale_authorization_epoch,
    insufficient_output_capacity,
    clock_regression,
};

struct PeerAuthorizationConfiguration {
    std::uint64_t approval_window_ms{0};
};

struct PairingCandidate {
    std::uint32_t request_id{0};
    std::uint32_t logical_peer_id{0};
    PeerRole role{PeerRole::gauge};
    std::uint16_t requested_permissions{0};
    std::uint8_t channel{0};
};

struct PeerAuthorizationEntry {
    std::uint32_t logical_peer_id{0};
    PeerRole role{PeerRole::gauge};
    std::uint16_t permissions{0};
    std::uint8_t channel{0};
    std::uint32_t secure_key_handle{0};
    std::uint32_t authorization_epoch{0};
    bool active{false};
};

struct AuthorizationDecision {
    PeerAuthorizationError error{PeerAuthorizationError::invalid_state};
    bool authorized{false};
};

struct PeerAuthorizationStatus {
    bool running{false};
    bool approval_pending{false};
    PairingCandidate pending_candidate{};
    std::uint64_t approval_opened_ms{0};
    std::size_t peer_count{0};
    std::size_t active_peer_count{0};
    std::uint32_t approvals_completed{0};
    std::uint32_t approvals_expired{0};
    std::uint32_t authorization_denials{0};
    std::uint32_t revocations{0};
    std::uint32_t key_rotations{0};
};

// Opaque logical IDs and secure-storage key handles only. Raw keys, PINs,
// addresses, discovery payloads, and user-facing codes remain adapter-owned.
class PeerAuthorizationRegistry {
public:
    [[nodiscard]] PeerAuthorizationError start(
        const PeerAuthorizationConfiguration& configuration);
    void stop();

    [[nodiscard]] PeerAuthorizationError begin_approval(
        const PairingCandidate& candidate,
        std::uint64_t now_ms);
    [[nodiscard]] PeerAuthorizationError approve(
        std::uint32_t request_id,
        std::uint32_t secure_key_handle,
        std::uint32_t authorization_epoch,
        std::uint64_t now_ms);
    [[nodiscard]] PeerAuthorizationError cancel_approval();
    [[nodiscard]] PeerAuthorizationError service(std::uint64_t now_ms);

    [[nodiscard]] AuthorizationDecision authorize(
        std::uint32_t logical_peer_id,
        std::uint32_t secure_key_handle,
        std::uint8_t channel,
        PeerPermission required_permission);
    [[nodiscard]] PeerAuthorizationError rotate_key(
        std::uint32_t logical_peer_id,
        std::uint32_t new_secure_key_handle,
        std::uint32_t new_authorization_epoch);
    [[nodiscard]] PeerAuthorizationError revoke(
        std::uint32_t logical_peer_id);
    [[nodiscard]] PeerAuthorizationError forget_revoked(
        std::uint32_t logical_peer_id);

    [[nodiscard]] PeerAuthorizationError snapshot(
        PeerAuthorizationEntry* output,
        std::size_t output_capacity,
        std::size_t& output_count) const;
    [[nodiscard]] PeerAuthorizationStatus status() const;

private:
    [[nodiscard]] std::size_t find_peer(std::uint32_t logical_peer_id) const;
    [[nodiscard]] bool key_handle_in_use(
        std::uint32_t secure_key_handle,
        std::uint32_t except_peer_id = 0) const;
    void clear_pending();

    PeerAuthorizationConfiguration configuration_{};
    std::array<PeerAuthorizationEntry, kMaximumAuthorizedPeers> peers_{};
    PeerAuthorizationStatus status_{};
};

}  // namespace opengauge::identity
