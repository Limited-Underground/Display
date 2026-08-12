#include "opengauge/gauge_layout_kv_storage.hpp"

namespace opengauge::configuration {
namespace {

const char* key_for_slot(std::uint8_t slot) {
    switch (slot) {
        case 0:
            return kGaugeLayoutSlotAKey;
        case 1:
            return kGaugeLayoutSlotBKey;
        default:
            return nullptr;
    }
}

LayoutStorageError map_error(GaugeLayoutKvBackendError error) {
    switch (error) {
        case GaugeLayoutKvBackendError::none:
            return LayoutStorageError::none;
        case GaugeLayoutKvBackendError::not_found:
            return LayoutStorageError::not_found;
        case GaugeLayoutKvBackendError::invalid_argument:
            return LayoutStorageError::invalid_argument;
        case GaugeLayoutKvBackendError::io_failure:
            return LayoutStorageError::io_failure;
    }
    return LayoutStorageError::io_failure;
}

LayoutStorageError commit_pending(GaugeLayoutKvBackend& backend) {
    return map_error(backend.commit(
        kGaugeLayoutPartitionLabel, kGaugeLayoutNamespace));
}

}  // namespace

GaugeLayoutKvStorage::GaugeLayoutKvStorage(GaugeLayoutKvBackend& backend)
    : backend_(backend) {}

LayoutStorageError GaugeLayoutKvStorage::read_slot(
    std::uint8_t slot,
    std::uint8_t* output,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || output == nullptr ||
        size != kGaugeLayoutRecordBytes) {
        return LayoutStorageError::invalid_argument;
    }

    std::size_t actual_size = 0;
    const auto read = backend_.read_blob(
        kGaugeLayoutPartitionLabel,
        kGaugeLayoutNamespace,
        key,
        output,
        size,
        actual_size);
    if (read != GaugeLayoutKvBackendError::none) {
        return map_error(read);
    }
    return actual_size == kGaugeLayoutRecordBytes
               ? LayoutStorageError::none
               : LayoutStorageError::io_failure;
}

LayoutStorageError GaugeLayoutKvStorage::write_slot(
    std::uint8_t slot,
    const std::uint8_t* data,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || data == nullptr ||
        size != kGaugeLayoutRecordBytes) {
        return LayoutStorageError::invalid_argument;
    }

    const auto written = backend_.write_blob(
        kGaugeLayoutPartitionLabel,
        kGaugeLayoutNamespace,
        key,
        data,
        size);
    if (written != GaugeLayoutKvBackendError::none) {
        return map_error(written);
    }
    return commit_pending(backend_);
}

LayoutStorageError GaugeLayoutKvStorage::erase_slot(std::uint8_t slot) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr) {
        return LayoutStorageError::invalid_argument;
    }

    const auto erased = backend_.erase_key(
        kGaugeLayoutPartitionLabel,
        kGaugeLayoutNamespace,
        key);
    if (erased == GaugeLayoutKvBackendError::not_found) {
        return LayoutStorageError::none;
    }
    if (erased != GaugeLayoutKvBackendError::none) {
        return map_error(erased);
    }
    return commit_pending(backend_);
}

}  // namespace opengauge::configuration
