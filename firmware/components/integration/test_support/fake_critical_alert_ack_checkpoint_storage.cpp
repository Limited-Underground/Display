#include "fake_critical_alert_ack_checkpoint_storage.hpp"

#include <algorithm>

namespace opengauge::integration::test_support {
namespace {

bool valid_request(std::uint8_t slot, const void* data, std::size_t size) {
    return slot < 2 && data != nullptr &&
           size == kCriticalAlertAckStoredCheckpointBytes;
}

}  // namespace

CriticalAlertAckCheckpointStorageError
FakeCriticalAlertAckCheckpointStorage::read_slot(
    std::uint8_t slot,
    std::uint8_t* output,
    std::size_t size) {
    if (!valid_request(slot, output, size)) {
        return CriticalAlertAckCheckpointStorageError::invalid_argument;
    }
    if (fail_read_[slot]) {
        fail_read_[slot] = false;
        return CriticalAlertAckCheckpointStorageError::io_failure;
    }
    if (!present_[slot]) {
        return CriticalAlertAckCheckpointStorageError::not_found;
    }
    std::copy(slots_[slot].begin(), slots_[slot].end(), output);
    return CriticalAlertAckCheckpointStorageError::none;
}

CriticalAlertAckCheckpointStorageError
FakeCriticalAlertAckCheckpointStorage::write_slot(
    std::uint8_t slot,
    const std::uint8_t* data,
    std::size_t size) {
    if (!valid_request(slot, data, size)) {
        return CriticalAlertAckCheckpointStorageError::invalid_argument;
    }
    ++writes_[slot];
    const auto behavior = next_write_[slot];
    next_write_[slot] = FakeCheckpointWriteBehavior::normal;
    if (behavior == FakeCheckpointWriteBehavior::fail_before_write) {
        return CriticalAlertAckCheckpointStorageError::io_failure;
    }
    present_[slot] = true;
    if (behavior == FakeCheckpointWriteBehavior::fail_after_partial_write) {
        std::copy(data, data + size / 2, slots_[slot].begin());
        return CriticalAlertAckCheckpointStorageError::io_failure;
    }
    std::copy(data, data + size, slots_[slot].begin());
    if (behavior == FakeCheckpointWriteBehavior::corrupt_after_success) {
        slots_[slot][100] ^= 0x5AU;
    }
    return CriticalAlertAckCheckpointStorageError::none;
}

CriticalAlertAckCheckpointStorageError
FakeCriticalAlertAckCheckpointStorage::erase_slot(std::uint8_t slot) {
    if (slot >= 2) {
        return CriticalAlertAckCheckpointStorageError::invalid_argument;
    }
    ++erases_[slot];
    if (fail_erase_[slot]) {
        fail_erase_[slot] = false;
        return CriticalAlertAckCheckpointStorageError::io_failure;
    }
    slots_[slot] = {};
    present_[slot] = false;
    return CriticalAlertAckCheckpointStorageError::none;
}

void FakeCriticalAlertAckCheckpointStorage::fail_next_read(
    std::uint8_t slot) {
    if (slot < 2) {
        fail_read_[slot] = true;
    }
}

void FakeCriticalAlertAckCheckpointStorage::set_next_write_behavior(
    std::uint8_t slot,
    FakeCheckpointWriteBehavior behavior) {
    if (slot < 2) {
        next_write_[slot] = behavior;
    }
}

void FakeCriticalAlertAckCheckpointStorage::fail_next_erase(
    std::uint8_t slot) {
    if (slot < 2) {
        fail_erase_[slot] = true;
    }
}

void FakeCriticalAlertAckCheckpointStorage::corrupt(
    std::uint8_t slot,
    std::size_t offset,
    std::uint8_t mask) {
    if (slot < 2 && offset < kCriticalAlertAckStoredCheckpointBytes &&
        present_[slot]) {
        slots_[slot][offset] ^= mask;
    }
}

bool FakeCriticalAlertAckCheckpointStorage::present(std::uint8_t slot) const {
    return slot < 2 && present_[slot];
}

std::uint32_t FakeCriticalAlertAckCheckpointStorage::writes(
    std::uint8_t slot) const {
    return slot < 2 ? writes_[slot] : 0;
}

std::uint32_t FakeCriticalAlertAckCheckpointStorage::erases(
    std::uint8_t slot) const {
    return slot < 2 ? erases_[slot] : 0;
}

}  // namespace opengauge::integration::test_support
