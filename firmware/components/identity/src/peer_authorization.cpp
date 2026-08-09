#include "opengauge/peer_authorization.hpp"

#include <algorithm>
#include <limits>

namespace opengauge::identity {
namespace {

constexpr std::size_t kNotFound = std::numeric_limits<std::size_t>::max();
constexpr std::array<std::uint8_t, 4> kCheckpointMagic{{'O', 'P', 'A', '0'}};
constexpr std::size_t kCheckpointHeaderBytes = 24;
constexpr std::size_t kCheckpointEntryBytes = 24;
constexpr std::size_t kCheckpointEntriesOffset = kCheckpointHeaderBytes;
constexpr std::size_t kCheckpointTailOffset =
    kCheckpointEntriesOffset + kMaximumAuthorizedPeers * kCheckpointEntryBytes;
constexpr std::size_t kCheckpointCrcOffset =
    kPeerAuthorizationCheckpointBytes - 4;

void write_u16(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value);
    output[1] = static_cast<std::uint8_t>(value >> 8U);
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

void write_u64(std::uint8_t* output, std::uint64_t value) {
    for (std::size_t index = 0; index < 8; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}

std::uint16_t read_u16(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(input[0]) |
           static_cast<std::uint16_t>(input[1] << 8U);
}

std::uint32_t read_u32(const std::uint8_t* input) {
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < 4; ++index)
        value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
    return value;
}

std::uint64_t read_u64(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index)
        value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
    return value;
}

bool all_zero(const std::uint8_t* data, std::size_t size) {
    return std::all_of(data, data + size, [](std::uint8_t value) {
        return value == 0;
    });
}

bool known_role(PeerRole role) {
    const auto value = static_cast<std::uint8_t>(role);
    return value >= static_cast<std::uint8_t>(PeerRole::gateway) &&
           value <= static_cast<std::uint8_t>(PeerRole::trail_bridge);
}

std::uint16_t allowed_permissions(PeerRole role) {
    switch (role) {
        case PeerRole::gateway:
            return permission_bit(PeerPermission::publish_telemetry) |
                   permission_bit(PeerPermission::receive_configuration);
        case PeerRole::gauge:
            return permission_bit(PeerPermission::receive_telemetry) |
                   permission_bit(PeerPermission::publish_alarm_ack);
        case PeerRole::gps:
            return permission_bit(PeerPermission::publish_gps);
        case PeerRole::trail_bridge:
            return permission_bit(PeerPermission::receive_critical_alert) |
                   permission_bit(PeerPermission::publish_alarm_ack);
    }
    return 0;
}

bool valid_permissions(PeerRole role, std::uint16_t permissions) {
    const auto allowed = allowed_permissions(role);
    return permissions != 0 && (permissions & ~kAllPeerPermissionBits) == 0 &&
           (permissions & ~allowed) == 0;
}

bool known_permission(PeerPermission permission) {
    const auto bit = permission_bit(permission);
    return bit != 0 && (bit & (bit - 1U)) == 0 &&
           (bit & ~kAllPeerPermissionBits) == 0;
}

void saturating_increment(std::uint32_t& value) {
    if (value != std::numeric_limits<std::uint32_t>::max()) {
        ++value;
    }
}

}  // namespace

std::uint32_t peer_authorization_checkpoint_crc32(
    const std::uint8_t* data,
    std::size_t size) {
    if (data == nullptr && size != 0) return 0;
    std::uint32_t crc = 0xFFFFFFFFU;
    for (std::size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for (std::uint8_t bit = 0; bit < 8; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                -(static_cast<std::int32_t>(crc & 1U)));
            crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return ~crc;
}

PeerAuthorizationError PeerAuthorizationRegistry::start(
    const PeerAuthorizationConfiguration& configuration) {
    if (status_.running) {
        return PeerAuthorizationError::invalid_state;
    }
    if (configuration.approval_window_ms == 0) {
        return PeerAuthorizationError::invalid_configuration;
    }
    configuration_ = configuration;
    peers_ = {};
    status_ = {};
    status_.running = true;
    return PeerAuthorizationError::none;
}

void PeerAuthorizationRegistry::stop() {
    status_.running = false;
    clear_pending();
}

PeerAuthorizationError PeerAuthorizationRegistry::begin_approval(
    const PairingCandidate& candidate,
    std::uint64_t now_ms) {
    if (!status_.running) {
        return PeerAuthorizationError::invalid_state;
    }
    if (status_.approval_pending) {
        return PeerAuthorizationError::approval_pending;
    }
    if (candidate.request_id == 0 || candidate.logical_peer_id == 0 ||
        !known_role(candidate.role) ||
        !valid_permissions(candidate.role, candidate.requested_permissions) ||
        candidate.channel == 0 || candidate.channel > 14) {
        return PeerAuthorizationError::invalid_candidate;
    }
    if (find_peer(candidate.logical_peer_id) != kNotFound) {
        return PeerAuthorizationError::duplicate_peer;
    }
    if (status_.peer_count == peers_.size()) {
        return PeerAuthorizationError::capacity_full;
    }
    status_.approval_pending = true;
    status_.pending_candidate = candidate;
    status_.approval_opened_ms = now_ms;
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::approve(
    std::uint32_t request_id,
    std::uint32_t secure_key_handle,
    std::uint32_t authorization_epoch,
    std::uint64_t now_ms) {
    if (!status_.running) {
        return PeerAuthorizationError::invalid_state;
    }
    if (!status_.approval_pending ||
        request_id != status_.pending_candidate.request_id) {
        return PeerAuthorizationError::approval_not_found;
    }
    if (now_ms < status_.approval_opened_ms) {
        return PeerAuthorizationError::clock_regression;
    }
    if (now_ms - status_.approval_opened_ms >=
        configuration_.approval_window_ms) {
        clear_pending();
        saturating_increment(status_.approvals_expired);
        return PeerAuthorizationError::approval_expired;
    }
    if (secure_key_handle == 0 || authorization_epoch == 0) {
        return PeerAuthorizationError::invalid_candidate;
    }
    if (key_handle_in_use(secure_key_handle)) {
        return PeerAuthorizationError::duplicate_key_handle;
    }

    auto& entry = peers_[status_.peer_count];
    entry.logical_peer_id = status_.pending_candidate.logical_peer_id;
    entry.role = status_.pending_candidate.role;
    entry.permissions = status_.pending_candidate.requested_permissions;
    entry.channel = status_.pending_candidate.channel;
    entry.secure_key_handle = secure_key_handle;
    entry.authorization_epoch = authorization_epoch;
    entry.active = true;
    ++status_.peer_count;
    ++status_.active_peer_count;
    saturating_increment(status_.approvals_completed);
    clear_pending();
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::cancel_approval() {
    if (!status_.running || !status_.approval_pending) {
        return PeerAuthorizationError::approval_not_found;
    }
    clear_pending();
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::service(
    std::uint64_t now_ms) {
    if (!status_.running) {
        return PeerAuthorizationError::invalid_state;
    }
    if (!status_.approval_pending) {
        return PeerAuthorizationError::none;
    }
    if (now_ms < status_.approval_opened_ms) {
        return PeerAuthorizationError::clock_regression;
    }
    if (now_ms - status_.approval_opened_ms >=
        configuration_.approval_window_ms) {
        clear_pending();
        saturating_increment(status_.approvals_expired);
        return PeerAuthorizationError::approval_expired;
    }
    return PeerAuthorizationError::none;
}

AuthorizationDecision PeerAuthorizationRegistry::authorize(
    std::uint32_t logical_peer_id,
    std::uint32_t secure_key_handle,
    std::uint8_t channel,
    PeerPermission required_permission) {
    if (!status_.running) {
        return {PeerAuthorizationError::invalid_state, false};
    }
    const auto index = find_peer(logical_peer_id);
    PeerAuthorizationError error = PeerAuthorizationError::none;
    if (index == kNotFound) {
        error = PeerAuthorizationError::unknown_peer;
    } else if (!peers_[index].active) {
        error = PeerAuthorizationError::peer_revoked;
    } else if (secure_key_handle != peers_[index].secure_key_handle) {
        error = PeerAuthorizationError::key_mismatch;
    } else if (channel != peers_[index].channel) {
        error = PeerAuthorizationError::channel_mismatch;
    } else if (!known_permission(required_permission) ||
               (peers_[index].permissions &
                permission_bit(required_permission)) == 0) {
        error = PeerAuthorizationError::permission_denied;
    }
    if (error != PeerAuthorizationError::none) {
        saturating_increment(status_.authorization_denials);
        return {error, false};
    }
    return {PeerAuthorizationError::none, true};
}

PeerAuthorizationError PeerAuthorizationRegistry::rotate_key(
    std::uint32_t logical_peer_id,
    std::uint32_t new_secure_key_handle,
    std::uint32_t new_authorization_epoch) {
    if (!status_.running) {
        return PeerAuthorizationError::invalid_state;
    }
    const auto index = find_peer(logical_peer_id);
    if (index == kNotFound) {
        return PeerAuthorizationError::unknown_peer;
    }
    auto& entry = peers_[index];
    if (!entry.active) {
        return PeerAuthorizationError::peer_revoked;
    }
    if (new_secure_key_handle == 0 ||
        new_authorization_epoch <= entry.authorization_epoch) {
        return PeerAuthorizationError::stale_authorization_epoch;
    }
    if (key_handle_in_use(new_secure_key_handle, logical_peer_id)) {
        return PeerAuthorizationError::duplicate_key_handle;
    }
    entry.secure_key_handle = new_secure_key_handle;
    entry.authorization_epoch = new_authorization_epoch;
    saturating_increment(status_.key_rotations);
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::revoke(
    std::uint32_t logical_peer_id) {
    if (!status_.running) {
        return PeerAuthorizationError::invalid_state;
    }
    const auto index = find_peer(logical_peer_id);
    if (index == kNotFound) {
        return PeerAuthorizationError::unknown_peer;
    }
    auto& entry = peers_[index];
    if (!entry.active) {
        return PeerAuthorizationError::peer_revoked;
    }
    entry.active = false;
    entry.secure_key_handle = 0;
    --status_.active_peer_count;
    saturating_increment(status_.revocations);
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::forget_revoked(
    std::uint32_t logical_peer_id) {
    if (!status_.running) {
        return PeerAuthorizationError::invalid_state;
    }
    const auto index = find_peer(logical_peer_id);
    if (index == kNotFound) {
        return PeerAuthorizationError::unknown_peer;
    }
    if (peers_[index].active) {
        return PeerAuthorizationError::invalid_state;
    }
    for (std::size_t move = index + 1; move < status_.peer_count; ++move) {
        peers_[move - 1] = peers_[move];
    }
    peers_[status_.peer_count - 1] = {};
    --status_.peer_count;
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::snapshot(
    PeerAuthorizationEntry* output,
    std::size_t output_capacity,
    std::size_t& output_count) const {
    if (!status_.running) {
        return PeerAuthorizationError::invalid_state;
    }
    if ((output == nullptr && output_capacity != 0) ||
        output_capacity < status_.peer_count) {
        return output_capacity < status_.peer_count
                   ? PeerAuthorizationError::insufficient_output_capacity
                   : PeerAuthorizationError::invalid_candidate;
    }
    for (std::size_t index = 0; index < status_.peer_count; ++index) {
        output[index] = peers_[index];
    }
    output_count = status_.peer_count;
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::export_checkpoint(
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes>& output) {
    if (!status_.running || status_.approval_pending) {
        return PeerAuthorizationError::invalid_state;
    }
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes> candidate{};
    std::copy(kCheckpointMagic.begin(), kCheckpointMagic.end(), candidate.begin());
    candidate[4] = kPeerAuthorizationCheckpointVersion;
    candidate[5] = static_cast<std::uint8_t>(status_.peer_count);
    candidate[6] = static_cast<std::uint8_t>(status_.active_peer_count);
    write_u64(candidate.data() + 8, configuration_.approval_window_ms);
    for (std::size_t index = 0; index < status_.peer_count; ++index) {
        const auto& peer = peers_[index];
        auto* entry = candidate.data() + kCheckpointEntriesOffset +
                      index * kCheckpointEntryBytes;
        entry[0] = peer.active ? 1 : 2;
        entry[1] = static_cast<std::uint8_t>(peer.role);
        entry[2] = peer.channel;
        write_u16(entry + 4, peer.permissions);
        write_u32(entry + 8, peer.logical_peer_id);
        write_u32(entry + 12, peer.secure_key_handle);
        write_u32(entry + 16, peer.authorization_epoch);
    }
    write_u32(candidate.data() + kCheckpointCrcOffset,
              peer_authorization_checkpoint_crc32(
                  candidate.data(), kCheckpointCrcOffset));
    output = candidate;
    saturating_increment(status_.checkpoint_exports);
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::import_checkpoint(
    const std::uint8_t* checkpoint,
    std::size_t checkpoint_size) {
    const auto reject = [this](PeerAuthorizationError error) {
        saturating_increment(status_.checkpoint_rejections);
        return error;
    };
    if (!status_.running || status_.approval_pending || status_.peer_count != 0 ||
        status_.approvals_completed != 0 || status_.approvals_expired != 0 ||
        status_.authorization_denials != 0 || status_.revocations != 0 ||
        status_.key_rotations != 0 || status_.checkpoint_exports != 0 ||
        status_.checkpoint_imports != 0) {
        return PeerAuthorizationError::invalid_state;
    }
    if (checkpoint == nullptr ||
        checkpoint_size != kPeerAuthorizationCheckpointBytes ||
        !std::equal(kCheckpointMagic.begin(), kCheckpointMagic.end(), checkpoint)) {
        return reject(PeerAuthorizationError::checkpoint_malformed);
    }
    if (checkpoint[4] != kPeerAuthorizationCheckpointVersion ||
        read_u64(checkpoint + 8) != configuration_.approval_window_ms) {
        return reject(PeerAuthorizationError::checkpoint_incompatible);
    }
    if (checkpoint[5] > kMaximumAuthorizedPeers || checkpoint[6] > checkpoint[5] ||
        checkpoint[7] != 0 || !all_zero(checkpoint + 16, 8) ||
        !all_zero(checkpoint + kCheckpointTailOffset,
                  kCheckpointCrcOffset - kCheckpointTailOffset)) {
        return reject(PeerAuthorizationError::checkpoint_malformed);
    }
    if (read_u32(checkpoint + kCheckpointCrcOffset) !=
        peer_authorization_checkpoint_crc32(checkpoint, kCheckpointCrcOffset)) {
        return reject(PeerAuthorizationError::checkpoint_integrity_failure);
    }
    std::array<PeerAuthorizationEntry, kMaximumAuthorizedPeers> candidate{};
    std::size_t active_count = 0;
    for (std::size_t index = 0; index < candidate.size(); ++index) {
        const auto* entry = checkpoint + kCheckpointEntriesOffset +
                            index * kCheckpointEntryBytes;
        if (index >= checkpoint[5]) {
            if (!all_zero(entry, kCheckpointEntryBytes))
                return reject(PeerAuthorizationError::checkpoint_malformed);
            continue;
        }
        const auto active = entry[0] == 1;
        const auto revoked = entry[0] == 2;
        PeerAuthorizationEntry peer{};
        peer.active = active;
        peer.role = static_cast<PeerRole>(entry[1]);
        peer.channel = entry[2];
        peer.permissions = read_u16(entry + 4);
        peer.logical_peer_id = read_u32(entry + 8);
        peer.secure_key_handle = read_u32(entry + 12);
        peer.authorization_epoch = read_u32(entry + 16);
        if ((!active && !revoked) || entry[3] != 0 || entry[6] != 0 ||
            entry[7] != 0 || !all_zero(entry + 20, 4) ||
            !known_role(peer.role) ||
            !valid_permissions(peer.role, peer.permissions) ||
            peer.channel == 0 || peer.channel > 14 ||
            peer.logical_peer_id == 0 || peer.authorization_epoch == 0 ||
            (active && peer.secure_key_handle == 0) ||
            (revoked && peer.secure_key_handle != 0)) {
            return reject(PeerAuthorizationError::checkpoint_malformed);
        }
        for (std::size_t prior = 0; prior < index; ++prior) {
            if (candidate[prior].logical_peer_id == peer.logical_peer_id ||
                (active && candidate[prior].active &&
                 candidate[prior].secure_key_handle == peer.secure_key_handle)) {
                return reject(PeerAuthorizationError::checkpoint_malformed);
            }
        }
        candidate[index] = peer;
        if (active) ++active_count;
    }
    if (active_count != checkpoint[6]) {
        return reject(PeerAuthorizationError::checkpoint_malformed);
    }
    peers_ = candidate;
    status_.peer_count = checkpoint[5];
    status_.active_peer_count = active_count;
    saturating_increment(status_.checkpoint_imports);
    return PeerAuthorizationError::none;
}

PeerAuthorizationError PeerAuthorizationRegistry::validate_checkpoint_import(
    const std::uint8_t* checkpoint,
    std::size_t checkpoint_size) const {
    auto candidate = *this;
    return candidate.import_checkpoint(checkpoint, checkpoint_size);
}

PeerAuthorizationStatus PeerAuthorizationRegistry::status() const {
    return status_;
}

std::size_t PeerAuthorizationRegistry::find_peer(
    std::uint32_t logical_peer_id) const {
    for (std::size_t index = 0; index < status_.peer_count; ++index) {
        if (peers_[index].logical_peer_id == logical_peer_id) {
            return index;
        }
    }
    return kNotFound;
}

bool PeerAuthorizationRegistry::key_handle_in_use(
    std::uint32_t secure_key_handle,
    std::uint32_t except_peer_id) const {
    for (std::size_t index = 0; index < status_.peer_count; ++index) {
        if (peers_[index].active &&
            peers_[index].logical_peer_id != except_peer_id &&
            peers_[index].secure_key_handle == secure_key_handle) {
            return true;
        }
    }
    return false;
}

void PeerAuthorizationRegistry::clear_pending() {
    status_.approval_pending = false;
    status_.pending_candidate = {};
    status_.approval_opened_ms = 0;
}

}  // namespace opengauge::identity
