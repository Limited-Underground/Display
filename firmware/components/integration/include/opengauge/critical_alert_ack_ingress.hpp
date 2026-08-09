#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_ack.hpp"
#include "opengauge/critical_alert_outbox.hpp"
#include "opengauge/peer_authorization.hpp"

namespace opengauge::integration {

inline constexpr std::size_t kCriticalAlertAckConsumerCapacity = 8;
inline constexpr std::uint32_t kCriticalAlertAckReplayWindow = 32;
inline constexpr std::uint8_t kCriticalAlertAckCheckpointVersion = 0;
inline constexpr std::size_t kCriticalAlertAckCheckpointBytes = 280;

enum class CriticalAlertAckIngressError : std::uint8_t {
    none = 0,
    invalid_state,
    invalid_configuration,
    invalid_binding,
    duplicate_consumer,
    capacity_full,
    transport_not_authenticated,
    authorization_denied,
    unknown_consumer,
    consumer_mismatch,
    producer_mismatch,
    session_mismatch,
    observed_age_exceeded,
    codec_rejected,
    replay_duplicate,
    replay_too_old,
    replay_ambiguous,
    outbox_mismatch,
    clock_regression,
    authorization_epoch_mismatch,
    checkpoint_malformed,
    checkpoint_incompatible,
    checkpoint_integrity_failure,
    checkpoint_authorization_mismatch,
};

struct CriticalAlertAckIngressConfiguration {
    std::uint64_t local_producer_id{0};
    std::uint32_t maximum_observed_alert_age_ms{0};
};

// All fields are adapter-supplied metadata. secure_key_handle is an opaque
// reference to separately protected key material, never the key itself.
struct CriticalAlertAckTransportContext {
    bool authenticated{false};
    std::uint32_t logical_peer_id{0};
    std::uint32_t secure_key_handle{0};
    std::uint8_t channel{0};
};

struct CriticalAlertAckIngressResult {
    CriticalAlertAckIngressError error{
        CriticalAlertAckIngressError::invalid_state};
    AlertAckCodecError codec_error{AlertAckCodecError::none};
    identity::PeerAuthorizationError authorization_error{
        identity::PeerAuthorizationError::none};
    CriticalOutboxError outbox_error{CriticalOutboxError::none};
    AlertAckDisposition disposition{AlertAckDisposition::accepted};
    AlertAckReason reason{AlertAckReason::none};
    bool outbox_completed{false};
    CriticalRemoteRejectionAction remote_rejection_action{
        CriticalRemoteRejectionAction::terminal};
    bool retry_released{false};
    bool terminal_failure{false};
    CriticalDeliveryFailureEvent failure{};

    [[nodiscard]] constexpr bool processed() const {
        return error == CriticalAlertAckIngressError::none;
    }
};

struct CriticalAlertAckIngressStatus {
    bool running{false};
    std::size_t binding_count{0};
    std::uint32_t processed{0};
    std::uint32_t accepted{0};
    std::uint32_t remote_rejections{0};
    std::uint32_t remote_retries{0};
    std::uint32_t remote_terminal_failures{0};
    std::uint32_t transport_denials{0};
    std::uint32_t codec_rejections{0};
    std::uint32_t identity_rejections{0};
    std::uint32_t replay_rejections{0};
    std::uint32_t outbox_rejections{0};
    std::uint32_t clock_regressions{0};
    std::uint32_t checkpoint_exports{0};
    std::uint32_t checkpoint_imports{0};
    std::uint32_t checkpoint_rejections{0};
};

// Admission/correlation boundary for already framed ACK bytes. Binding a
// consumer boot session is an explicit local approval action; a received frame
// can never create or silently replace a session binding.
class CriticalAlertAckIngress {
public:
    [[nodiscard]] CriticalAlertAckIngressError start(
        const CriticalAlertAckIngressConfiguration& configuration,
        identity::PeerAuthorizationRegistry& authorization,
        CriticalAlertOutbox& outbox);
    void stop();

    [[nodiscard]] CriticalAlertAckIngressError bind_consumer_session(
        std::uint32_t logical_peer_id,
        std::uint64_t consumer_id,
        std::uint32_t consumer_boot_session_id);
    [[nodiscard]] CriticalAlertAckIngressError unbind_consumer(
        std::uint32_t logical_peer_id);
    [[nodiscard]] CriticalAlertAckIngressError export_checkpoint(
        std::array<std::uint8_t, kCriticalAlertAckCheckpointBytes>& output);
    [[nodiscard]] CriticalAlertAckIngressError import_checkpoint(
        const std::uint8_t* checkpoint,
        std::size_t checkpoint_size);
    [[nodiscard]] CriticalAlertAckIngressError validate_checkpoint_import(
        const std::uint8_t* checkpoint,
        std::size_t checkpoint_size) const;

    [[nodiscard]] CriticalAlertAckIngressResult receive(
        const std::uint8_t* frame,
        std::size_t frame_size,
        const CriticalAlertAckTransportContext& transport,
        std::uint64_t now_ms);
    [[nodiscard]] CriticalAlertAckIngressStatus status() const;

private:
    struct ConsumerBinding {
        bool active{false};
        std::uint32_t logical_peer_id{0};
        std::uint64_t consumer_id{0};
        std::uint32_t consumer_boot_session_id{0};
        std::uint32_t authorization_epoch{0};
        bool has_sequence{false};
        std::uint32_t highest_sequence{0};
        std::uint32_t replay_bitmap{0};
    };

    struct ReplayCandidate {
        CriticalAlertAckIngressError error{
            CriticalAlertAckIngressError::none};
        bool has_sequence{false};
        std::uint32_t highest_sequence{0};
        std::uint32_t replay_bitmap{0};
    };

    [[nodiscard]] std::size_t find_binding(
        std::uint32_t logical_peer_id) const;
    [[nodiscard]] ReplayCandidate preview_sequence(
        const ConsumerBinding& binding,
        std::uint32_t sequence) const;
    [[nodiscard]] bool current_authorization_epoch(
        std::uint32_t logical_peer_id,
        std::uint32_t& authorization_epoch) const;

    CriticalAlertAckIngressConfiguration configuration_{};
    identity::PeerAuthorizationRegistry* authorization_{nullptr};
    CriticalAlertOutbox* outbox_{nullptr};
    std::array<ConsumerBinding, kCriticalAlertAckConsumerCapacity> bindings_{};
    bool has_clock_{false};
    std::uint64_t last_monotonic_ms_{0};
    CriticalAlertAckIngressStatus status_{};
};

}  // namespace opengauge::integration
