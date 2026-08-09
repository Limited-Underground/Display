#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "opengauge/critical_alert_ack_ingress.hpp"

namespace {

using namespace opengauge::identity;
using namespace opengauge::integration;

std::uint64_t parse_u64(const char* text) {
    std::size_t consumed = 0;
    const auto value = std::stoull(text, &consumed, 0);
    if (text[consumed] != '\0') {
        throw std::invalid_argument("invalid 64-bit integer");
    }
    return value;
}

std::uint32_t parse_u32(const char* text) {
    const auto value = parse_u64(text);
    if (value > 0xFFFFFFFFULL) {
        throw std::invalid_argument("invalid 32-bit integer");
    }
    return static_cast<std::uint32_t>(value);
}

std::uint8_t parse_u8(const char* text) {
    const auto value = parse_u64(text);
    if (value > 0xFFULL) {
        throw std::invalid_argument("invalid 8-bit integer");
    }
    return static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> from_hex(const std::string& text) {
    if (text.size() % 2 != 0) {
        throw std::invalid_argument("hex input must have an even length");
    }
    std::vector<std::uint8_t> bytes(text.size() / 2);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto pair = text.substr(index * 2, 2);
        std::size_t consumed = 0;
        const auto value = std::stoul(pair, &consumed, 16);
        if (consumed != pair.size()) {
            throw std::invalid_argument("invalid hex input");
        }
        bytes[index] = static_cast<std::uint8_t>(value);
    }
    return bytes;
}

int verify(int argc, char** argv, bool accepted) {
    if (argc != 9) {
        throw std::invalid_argument(
            "verification requires alert-hex ack-hex consumer-id "
            "boot-session logical-peer-id key-handle channel");
    }
    const auto alert_bytes = from_hex(argv[2]);
    const auto ack_bytes = from_hex(argv[3]);
    if (alert_bytes.size() != kCriticalAlertFrameBytes ||
        ack_bytes.size() != kCriticalAlertAckFrameBytes) {
        throw std::invalid_argument("alert and ACK must each contain 64 bytes");
    }
    const auto decoded_alert = decode_critical_alert(
        alert_bytes.data(), alert_bytes.size());
    if (!decoded_alert.decoded()) {
        std::cerr << "alert decode error "
                  << static_cast<unsigned>(decoded_alert.error) << '\n';
        return 2;
    }

    const auto consumer_id = parse_u64(argv[4]);
    const auto boot_session = parse_u32(argv[5]);
    const auto logical_peer_id = parse_u32(argv[6]);
    const auto key_handle = parse_u32(argv[7]);
    const auto channel = parse_u8(argv[8]);
    if (consumer_id == 0 || boot_session == 0 || logical_peer_id == 0 ||
        key_handle == 0 || channel == 0) {
        throw std::invalid_argument("bench identity values must be nonzero");
    }

    PeerAuthorizationRegistry registry{};
    if (registry.start({1000}) != PeerAuthorizationError::none) {
        return 3;
    }
    const PairingCandidate candidate{
        1,
        logical_peer_id,
        PeerRole::trail_bridge,
        permission_bit(PeerPermission::receive_critical_alert) |
            permission_bit(PeerPermission::publish_alarm_ack),
        channel,
    };
    if (registry.begin_approval(candidate, 0) !=
            PeerAuthorizationError::none ||
        registry.approve(1, key_handle, 1, 0) !=
            PeerAuthorizationError::none) {
        return 4;
    }

    CriticalAlertOutbox outbox{};
    if (outbox.start({50, 1000, 25, 10000, 3, 1}) !=
        CriticalOutboxError::none) {
        return 5;
    }
    std::array<std::uint8_t, kCriticalAlertFrameBytes> alert_frame{};
    std::copy(alert_bytes.begin(), alert_bytes.end(), alert_frame.begin());
    if (outbox.enqueue(alert_frame, 0) != CriticalOutboxError::none) {
        return 6;
    }
    const auto prepared = outbox.prepare(0);
    if (!prepared.prepared() || prepared.frame != alert_frame ||
        outbox.commit_local_send(prepared.token, true, 0) !=
            CriticalOutboxError::none) {
        return 7;
    }

    CriticalAlertAckIngress ingress{};
    if (ingress.start(
            {decoded_alert.alert.producer_id, 120000}, registry, outbox) !=
            CriticalAlertAckIngressError::none ||
        ingress.bind_consumer_session(
            logical_peer_id, consumer_id, boot_session) !=
            CriticalAlertAckIngressError::none) {
        return 8;
    }
    const CriticalAlertAckTransportContext context{
        true, logical_peer_id, key_handle, channel};
    const auto result = ingress.receive(
        ack_bytes.data(), ack_bytes.size(), context, 1);
    const auto outbox_status = outbox.status();
    const auto ingress_status = ingress.status();
    const bool accepted_success =
        result.outbox_completed &&
        result.disposition == AlertAckDisposition::accepted &&
        result.reason == AlertAckReason::none &&
        outbox_status.acknowledgements == 1 &&
        ingress_status.accepted == 1;
    const bool stale_success =
        !result.outbox_completed &&
        result.disposition == AlertAckDisposition::rejected &&
        result.reason == AlertAckReason::stale &&
        result.remote_rejection_action ==
            CriticalRemoteRejectionAction::terminal &&
        !result.retry_released && result.terminal_failure &&
        outbox_status.acknowledgements == 0 &&
        outbox_status.remote_terminal_failures == 1 &&
        ingress_status.remote_rejections == 1 &&
        ingress_status.remote_terminal_failures == 1;
    const bool success =
        result.processed() && outbox_status.queued_count == 0 &&
        outbox_status.in_flight_count == 0 &&
        (accepted ? accepted_success : stale_success);
    std::cout
        << "{\"processed\":" << (result.processed() ? "true" : "false")
        << ",\"outbox_completed\":"
        << (result.outbox_completed ? "true" : "false")
        << ",\"accepted_none\":"
        << (result.disposition == AlertAckDisposition::accepted &&
                    result.reason == AlertAckReason::none
                ? "true"
                : "false")
        << ",\"rejected_stale\":"
        << (result.disposition == AlertAckDisposition::rejected &&
                    result.reason == AlertAckReason::stale
                ? "true"
                : "false")
        << ",\"retry_released\":"
        << (result.retry_released ? "true" : "false")
        << ",\"terminal_failure\":"
        << (result.terminal_failure ? "true" : "false")
        << ",\"queued_count\":" << outbox_status.queued_count
        << ",\"in_flight_count\":" << outbox_status.in_flight_count
        << ",\"acknowledgements\":" << outbox_status.acknowledgements
        << "}\n";
    return success ? 0 : 9;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            throw std::invalid_argument(
                "expected verify-accepted or verify-stale");
        }
        if (std::string(argv[1]) == "verify-accepted") {
            return verify(argc, argv, true);
        }
        if (std::string(argv[1]) == "verify-stale") {
            return verify(argc, argv, false);
        }
        throw std::invalid_argument("unknown command");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
