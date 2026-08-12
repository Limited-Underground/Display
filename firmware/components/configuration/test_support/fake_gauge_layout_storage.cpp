#include "fake_gauge_layout_storage.hpp"

#include <algorithm>

namespace opengauge::configuration::test_support {
namespace {

bool valid_request(std::uint8_t slot, const void* data, std::size_t size) {
    return slot < 2 && data != nullptr && size == kGaugeLayoutRecordBytes;
}

}  // namespace

LayoutStorageError FakeGaugeLayoutStorage::read_slot(
    std::uint8_t slot,
    std::uint8_t* output,
    std::size_t size) {
    if (!valid_request(slot, output, size)) {
        return LayoutStorageError::invalid_argument;
    }
    if (fail_read_[slot]) {
        fail_read_[slot] = false;
        return LayoutStorageError::io_failure;
    }
    if (!present_[slot]) {
        return LayoutStorageError::not_found;
    }
    std::copy(slots_[slot].begin(), slots_[slot].end(), output);
    return LayoutStorageError::none;
}

LayoutStorageError FakeGaugeLayoutStorage::write_slot(
    std::uint8_t slot,
    const std::uint8_t* data,
    std::size_t size) {
    if (!valid_request(slot, data, size)) {
        return LayoutStorageError::invalid_argument;
    }
    ++writes_[slot];
    const auto behavior = next_write_[slot];
    next_write_[slot] = FakeWriteBehavior::normal;
    if (behavior == FakeWriteBehavior::fail_before_write) {
        return LayoutStorageError::io_failure;
    }
    present_[slot] = true;
    if (behavior == FakeWriteBehavior::fail_after_partial_write) {
        std::copy(data, data + size / 2, slots_[slot].begin());
        return LayoutStorageError::io_failure;
    }
    std::copy(data, data + size, slots_[slot].begin());
    if (behavior == FakeWriteBehavior::fail_after_full_write) {
        return LayoutStorageError::commit_uncertain;
    }
    if (behavior == FakeWriteBehavior::corrupt_after_success) {
        slots_[slot][100] ^= 0x5AU;
    }
    return LayoutStorageError::none;
}

LayoutStorageError FakeGaugeLayoutStorage::erase_slot(std::uint8_t slot) {
    if (slot >= 2) {
        return LayoutStorageError::invalid_argument;
    }
    ++erases_[slot];
    const auto behavior = next_erase_[slot];
    next_erase_[slot] = FakeEraseBehavior::normal;
    if (behavior == FakeEraseBehavior::fail_before_erase) {
        return LayoutStorageError::io_failure;
    }
    slots_[slot] = {};
    present_[slot] = false;
    return behavior == FakeEraseBehavior::fail_after_erase
               ? LayoutStorageError::commit_uncertain
               : LayoutStorageError::none;
}

void FakeGaugeLayoutStorage::fail_next_read(std::uint8_t slot) {
    if (slot < 2) {
        fail_read_[slot] = true;
    }
}

void FakeGaugeLayoutStorage::set_next_write_behavior(
    std::uint8_t slot,
    FakeWriteBehavior behavior) {
    if (slot < 2) {
        next_write_[slot] = behavior;
    }
}

void FakeGaugeLayoutStorage::fail_next_erase(std::uint8_t slot) {
    set_next_erase_behavior(slot, FakeEraseBehavior::fail_before_erase);
}

void FakeGaugeLayoutStorage::set_next_erase_behavior(
    std::uint8_t slot,
    FakeEraseBehavior behavior) {
    if (slot < 2) {
        next_erase_[slot] = behavior;
    }
}

void FakeGaugeLayoutStorage::corrupt(
    std::uint8_t slot,
    std::size_t offset,
    std::uint8_t mask) {
    if (slot < 2 && offset < kGaugeLayoutRecordBytes && present_[slot]) {
        slots_[slot][offset] ^= mask;
    }
}

bool FakeGaugeLayoutStorage::present(std::uint8_t slot) const {
    return slot < 2 && present_[slot];
}

std::uint32_t FakeGaugeLayoutStorage::writes(std::uint8_t slot) const {
    return slot < 2 ? writes_[slot] : 0;
}

std::uint32_t FakeGaugeLayoutStorage::erases(std::uint8_t slot) const {
    return slot < 2 ? erases_[slot] : 0;
}

}  // namespace opengauge::configuration::test_support
