#include "fake_critical_alert_recovery_storage.hpp"

#include <algorithm>

namespace opengauge::integration::test_support {
namespace {
bool valid(std::uint8_t slot, const void* data, std::size_t size) {
    return slot < 2 && data != nullptr &&
           size == kCriticalAlertRecoveryCheckpointBytes;
}
}  // namespace

CriticalAlertRecoveryStorageError FakeCriticalAlertRecoveryStorage::read_slot(
    std::uint8_t slot, std::uint8_t* output, std::size_t size) {
    if (!valid(slot, output, size)) return CriticalAlertRecoveryStorageError::invalid_argument;
    if (fail_read_[slot]) {
        fail_read_[slot] = false;
        return CriticalAlertRecoveryStorageError::io_failure;
    }
    if (!present_[slot]) return CriticalAlertRecoveryStorageError::not_found;
    std::copy(slots_[slot].begin(), slots_[slot].end(), output);
    return CriticalAlertRecoveryStorageError::none;
}

CriticalAlertRecoveryStorageError FakeCriticalAlertRecoveryStorage::write_slot(
    std::uint8_t slot, const std::uint8_t* data, std::size_t size) {
    if (!valid(slot, data, size)) return CriticalAlertRecoveryStorageError::invalid_argument;
    ++writes_[slot];
    const auto behavior = next_write_[slot];
    next_write_[slot] = FakeRecoveryWriteBehavior::normal;
    if (behavior == FakeRecoveryWriteBehavior::fail_before_write)
        return CriticalAlertRecoveryStorageError::io_failure;
    present_[slot] = true;
    if (behavior == FakeRecoveryWriteBehavior::fail_after_partial_write) {
        std::copy(data, data + size / 2, slots_[slot].begin());
        return CriticalAlertRecoveryStorageError::io_failure;
    }
    if (behavior == FakeRecoveryWriteBehavior::fail_after_configured_prefix) {
        const auto bytes = std::min(size, next_partial_bytes_[slot]);
        next_partial_bytes_[slot] = 0;
        std::copy(data, data + bytes, slots_[slot].begin());
        return CriticalAlertRecoveryStorageError::io_failure;
    }
    std::copy(data, data + size, slots_[slot].begin());
    if (behavior == FakeRecoveryWriteBehavior::fail_after_full_write)
        return CriticalAlertRecoveryStorageError::io_failure;
    if (behavior == FakeRecoveryWriteBehavior::corrupt_after_success)
        slots_[slot][100] ^= 0x5AU;
    return CriticalAlertRecoveryStorageError::none;
}

CriticalAlertRecoveryStorageError FakeCriticalAlertRecoveryStorage::erase_slot(
    std::uint8_t slot) {
    if (slot >= 2) return CriticalAlertRecoveryStorageError::invalid_argument;
    ++erases_[slot];
    if (fail_erase_[slot]) {
        fail_erase_[slot] = false;
        return CriticalAlertRecoveryStorageError::io_failure;
    }
    slots_[slot] = {};
    present_[slot] = false;
    return CriticalAlertRecoveryStorageError::none;
}

void FakeCriticalAlertRecoveryStorage::fail_next_read(std::uint8_t slot) {
    if (slot < 2) fail_read_[slot] = true;
}
void FakeCriticalAlertRecoveryStorage::set_next_write_behavior(
    std::uint8_t slot, FakeRecoveryWriteBehavior behavior) {
    if (slot < 2) next_write_[slot] = behavior;
}
void FakeCriticalAlertRecoveryStorage::set_next_partial_write_bytes(
    std::uint8_t slot, std::size_t bytes) {
    if (slot < 2) {
        next_write_[slot] =
            FakeRecoveryWriteBehavior::fail_after_configured_prefix;
        next_partial_bytes_[slot] = bytes;
    }
}
void FakeCriticalAlertRecoveryStorage::fail_next_erase(std::uint8_t slot) {
    if (slot < 2) fail_erase_[slot] = true;
}
void FakeCriticalAlertRecoveryStorage::corrupt(
    std::uint8_t slot, std::size_t offset, std::uint8_t mask) {
    if (slot < 2 && offset < slots_[slot].size() && present_[slot])
        slots_[slot][offset] ^= mask;
}
bool FakeCriticalAlertRecoveryStorage::present(std::uint8_t slot) const {
    return slot < 2 && present_[slot];
}
std::uint32_t FakeCriticalAlertRecoveryStorage::writes(std::uint8_t slot) const {
    return slot < 2 ? writes_[slot] : 0;
}
std::uint32_t FakeCriticalAlertRecoveryStorage::erases(std::uint8_t slot) const {
    return slot < 2 ? erases_[slot] : 0;
}

}  // namespace opengauge::integration::test_support
