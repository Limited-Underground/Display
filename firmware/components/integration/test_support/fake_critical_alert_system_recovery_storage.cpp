#include "fake_critical_alert_system_recovery_storage.hpp"

#include <algorithm>

namespace opengauge::integration::test_support {
namespace {

bool valid(std::uint8_t slot, const void* data, std::size_t size) {
    return slot < 2 && data != nullptr &&
           size == kCriticalAlertSystemRecoveryCheckpointBytes;
}

}  // namespace

CriticalAlertSystemRecoveryStorageError
FakeCriticalAlertSystemRecoveryStorage::read_slot(
    std::uint8_t slot, std::uint8_t* output, std::size_t size) {
    if (!valid(slot, output, size))
        return CriticalAlertSystemRecoveryStorageError::invalid_argument;
    if (fail_read_[slot]) {
        fail_read_[slot] = false;
        return CriticalAlertSystemRecoveryStorageError::io_failure;
    }
    if (!present_[slot])
        return CriticalAlertSystemRecoveryStorageError::not_found;
    std::copy(slots_[slot].begin(), slots_[slot].end(), output);
    return CriticalAlertSystemRecoveryStorageError::none;
}

CriticalAlertSystemRecoveryStorageError
FakeCriticalAlertSystemRecoveryStorage::write_slot(
    std::uint8_t slot, const std::uint8_t* data, std::size_t size) {
    if (!valid(slot, data, size))
        return CriticalAlertSystemRecoveryStorageError::invalid_argument;
    ++writes_[slot];
    const auto behavior = next_write_[slot];
    next_write_[slot] = FakeSystemRecoveryWriteBehavior::normal;
    if (behavior == FakeSystemRecoveryWriteBehavior::fail_before_write)
        return CriticalAlertSystemRecoveryStorageError::io_failure;
    present_[slot] = true;
    if (behavior ==
        FakeSystemRecoveryWriteBehavior::fail_after_configured_prefix) {
        const auto bytes = std::min(size, next_partial_bytes_[slot]);
        next_partial_bytes_[slot] = 0;
        std::copy(data, data + bytes, slots_[slot].begin());
        return CriticalAlertSystemRecoveryStorageError::io_failure;
    }
    std::copy(data, data + size, slots_[slot].begin());
    if (behavior == FakeSystemRecoveryWriteBehavior::fail_after_full_write)
        return CriticalAlertSystemRecoveryStorageError::io_failure;
    if (behavior == FakeSystemRecoveryWriteBehavior::corrupt_after_success)
        slots_[slot][500] ^= 0x5AU;
    return CriticalAlertSystemRecoveryStorageError::none;
}

CriticalAlertSystemRecoveryStorageError
FakeCriticalAlertSystemRecoveryStorage::erase_slot(std::uint8_t slot) {
    if (slot >= 2)
        return CriticalAlertSystemRecoveryStorageError::invalid_argument;
    if (fail_erase_[slot]) {
        fail_erase_[slot] = false;
        return CriticalAlertSystemRecoveryStorageError::io_failure;
    }
    slots_[slot] = {};
    present_[slot] = false;
    return CriticalAlertSystemRecoveryStorageError::none;
}

void FakeCriticalAlertSystemRecoveryStorage::fail_next_read(std::uint8_t slot) {
    if (slot < 2) fail_read_[slot] = true;
}

void FakeCriticalAlertSystemRecoveryStorage::set_next_write_behavior(
    std::uint8_t slot, FakeSystemRecoveryWriteBehavior behavior) {
    if (slot < 2) next_write_[slot] = behavior;
}

void FakeCriticalAlertSystemRecoveryStorage::set_next_partial_write_bytes(
    std::uint8_t slot, std::size_t bytes) {
    if (slot < 2) {
        next_write_[slot] =
            FakeSystemRecoveryWriteBehavior::fail_after_configured_prefix;
        next_partial_bytes_[slot] = bytes;
    }
}

void FakeCriticalAlertSystemRecoveryStorage::fail_next_erase(std::uint8_t slot) {
    if (slot < 2) fail_erase_[slot] = true;
}

void FakeCriticalAlertSystemRecoveryStorage::corrupt(
    std::uint8_t slot, std::size_t offset, std::uint8_t mask) {
    if (slot < 2 && offset < slots_[slot].size() && present_[slot])
        slots_[slot][offset] ^= mask;
}

bool FakeCriticalAlertSystemRecoveryStorage::present(std::uint8_t slot) const {
    return slot < 2 && present_[slot];
}

std::uint32_t FakeCriticalAlertSystemRecoveryStorage::writes(
    std::uint8_t slot) const {
    return slot < 2 ? writes_[slot] : 0;
}

}  // namespace opengauge::integration::test_support
