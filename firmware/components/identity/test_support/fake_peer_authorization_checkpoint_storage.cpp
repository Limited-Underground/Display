#include "fake_peer_authorization_checkpoint_storage.hpp"

#include <algorithm>

namespace opengauge::identity::test_support {
namespace {

bool valid(std::uint8_t slot, const void* data, std::size_t size) {
    return slot < 2 && data != nullptr &&
           size == kPeerAuthorizationStoredCheckpointBytes;
}

}  // namespace

PeerAuthorizationCheckpointStorageError
FakePeerAuthorizationCheckpointStorage::read_slot(
    std::uint8_t slot, std::uint8_t* output, std::size_t size) {
    if (!valid(slot, output, size))
        return PeerAuthorizationCheckpointStorageError::invalid_argument;
    if (fail_read_[slot]) {
        fail_read_[slot] = false;
        return PeerAuthorizationCheckpointStorageError::io_failure;
    }
    if (!present_[slot])
        return PeerAuthorizationCheckpointStorageError::not_found;
    std::copy(slots_[slot].begin(), slots_[slot].end(), output);
    return PeerAuthorizationCheckpointStorageError::none;
}

PeerAuthorizationCheckpointStorageError
FakePeerAuthorizationCheckpointStorage::write_slot(
    std::uint8_t slot, const std::uint8_t* data, std::size_t size) {
    if (!valid(slot, data, size))
        return PeerAuthorizationCheckpointStorageError::invalid_argument;
    ++writes_[slot];
    const auto behavior = next_write_[slot];
    next_write_[slot] = FakePeerCheckpointWriteBehavior::normal;
    if (behavior == FakePeerCheckpointWriteBehavior::fail_before_write)
        return PeerAuthorizationCheckpointStorageError::io_failure;
    present_[slot] = true;
    if (behavior == FakePeerCheckpointWriteBehavior::fail_after_configured_prefix) {
        const auto bytes = std::min(size, next_partial_bytes_[slot]);
        next_partial_bytes_[slot] = 0;
        std::copy(data, data + bytes, slots_[slot].begin());
        return PeerAuthorizationCheckpointStorageError::io_failure;
    }
    std::copy(data, data + size, slots_[slot].begin());
    if (behavior == FakePeerCheckpointWriteBehavior::fail_after_full_write)
        return PeerAuthorizationCheckpointStorageError::io_failure;
    if (behavior == FakePeerCheckpointWriteBehavior::corrupt_after_success)
        slots_[slot][100] ^= 0x5AU;
    return PeerAuthorizationCheckpointStorageError::none;
}

PeerAuthorizationCheckpointStorageError
FakePeerAuthorizationCheckpointStorage::erase_slot(std::uint8_t slot) {
    if (slot >= 2)
        return PeerAuthorizationCheckpointStorageError::invalid_argument;
    ++erases_[slot];
    if (fail_erase_[slot]) {
        fail_erase_[slot] = false;
        return PeerAuthorizationCheckpointStorageError::io_failure;
    }
    slots_[slot] = {};
    present_[slot] = false;
    return PeerAuthorizationCheckpointStorageError::none;
}

void FakePeerAuthorizationCheckpointStorage::fail_next_read(std::uint8_t slot) {
    if (slot < 2) fail_read_[slot] = true;
}

void FakePeerAuthorizationCheckpointStorage::set_next_write_behavior(
    std::uint8_t slot, FakePeerCheckpointWriteBehavior behavior) {
    if (slot < 2) next_write_[slot] = behavior;
}

void FakePeerAuthorizationCheckpointStorage::set_next_partial_write_bytes(
    std::uint8_t slot, std::size_t bytes) {
    if (slot < 2) {
        next_write_[slot] =
            FakePeerCheckpointWriteBehavior::fail_after_configured_prefix;
        next_partial_bytes_[slot] = bytes;
    }
}

void FakePeerAuthorizationCheckpointStorage::fail_next_erase(std::uint8_t slot) {
    if (slot < 2) fail_erase_[slot] = true;
}

void FakePeerAuthorizationCheckpointStorage::corrupt(
    std::uint8_t slot, std::size_t offset, std::uint8_t mask) {
    if (slot < 2 && offset < slots_[slot].size() && present_[slot])
        slots_[slot][offset] ^= mask;
}

bool FakePeerAuthorizationCheckpointStorage::present(std::uint8_t slot) const {
    return slot < 2 && present_[slot];
}

std::uint32_t FakePeerAuthorizationCheckpointStorage::writes(
    std::uint8_t slot) const {
    return slot < 2 ? writes_[slot] : 0;
}

std::uint32_t FakePeerAuthorizationCheckpointStorage::erases(
    std::uint8_t slot) const {
    return slot < 2 ? erases_[slot] : 0;
}

}  // namespace opengauge::identity::test_support
