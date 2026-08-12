#include "opengauge/critical_alert_system_recovery_kv_storage.hpp"

namespace opengauge::integration {
namespace {

const char* key_for_slot(std::uint8_t slot) {
    switch (slot) {
        case 0:
            return kCriticalAlertSystemRecoverySlotAKey;
        case 1:
            return kCriticalAlertSystemRecoverySlotBKey;
        default:
            return nullptr;
    }
}

CriticalAlertSystemRecoveryStorageError map_error(
    CriticalAlertSystemRecoveryKvBackendError error) {
    switch (error) {
        case CriticalAlertSystemRecoveryKvBackendError::none:
            return CriticalAlertSystemRecoveryStorageError::none;
        case CriticalAlertSystemRecoveryKvBackendError::not_found:
            return CriticalAlertSystemRecoveryStorageError::not_found;
        case CriticalAlertSystemRecoveryKvBackendError::invalid_argument:
            return CriticalAlertSystemRecoveryStorageError::invalid_argument;
        case CriticalAlertSystemRecoveryKvBackendError::io_failure:
            return CriticalAlertSystemRecoveryStorageError::io_failure;
    }
    return CriticalAlertSystemRecoveryStorageError::io_failure;
}

CriticalAlertSystemRecoveryStorageError commit_pending(
    CriticalAlertSystemRecoveryKvBackend& backend) {
    return map_error(backend.commit(
        kCriticalAlertSystemRecoveryPartitionLabel,
        kCriticalAlertSystemRecoveryNamespace));
}

}  // namespace

CriticalAlertSystemRecoveryKvStorage::CriticalAlertSystemRecoveryKvStorage(
    CriticalAlertSystemRecoveryKvBackend& backend)
    : backend_(backend) {}

CriticalAlertSystemRecoveryStorageError
CriticalAlertSystemRecoveryKvStorage::read_slot(
    std::uint8_t slot,
    std::uint8_t* output,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || output == nullptr ||
        size != kCriticalAlertSystemRecoveryCheckpointBytes) {
        return CriticalAlertSystemRecoveryStorageError::invalid_argument;
    }

    std::size_t actual_size = 0;
    const auto read = backend_.read_blob(
        kCriticalAlertSystemRecoveryPartitionLabel,
        kCriticalAlertSystemRecoveryNamespace,
        key,
        output,
        size,
        actual_size);
    if (read != CriticalAlertSystemRecoveryKvBackendError::none) {
        return map_error(read);
    }
    return actual_size == kCriticalAlertSystemRecoveryCheckpointBytes
               ? CriticalAlertSystemRecoveryStorageError::none
               : CriticalAlertSystemRecoveryStorageError::io_failure;
}

CriticalAlertSystemRecoveryStorageError
CriticalAlertSystemRecoveryKvStorage::write_slot(
    std::uint8_t slot,
    const std::uint8_t* data,
    std::size_t size) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr || data == nullptr ||
        size != kCriticalAlertSystemRecoveryCheckpointBytes) {
        return CriticalAlertSystemRecoveryStorageError::invalid_argument;
    }

    const auto written = backend_.write_blob(
        kCriticalAlertSystemRecoveryPartitionLabel,
        kCriticalAlertSystemRecoveryNamespace,
        key,
        data,
        size);
    if (written != CriticalAlertSystemRecoveryKvBackendError::none) {
        return map_error(written);
    }
    return commit_pending(backend_);
}

CriticalAlertSystemRecoveryStorageError
CriticalAlertSystemRecoveryKvStorage::erase_slot(std::uint8_t slot) {
    const auto* key = key_for_slot(slot);
    if (key == nullptr) {
        return CriticalAlertSystemRecoveryStorageError::invalid_argument;
    }

    const auto erased = backend_.erase_key(
        kCriticalAlertSystemRecoveryPartitionLabel,
        kCriticalAlertSystemRecoveryNamespace,
        key);
    if (erased == CriticalAlertSystemRecoveryKvBackendError::not_found) {
        return CriticalAlertSystemRecoveryStorageError::none;
    }
    if (erased != CriticalAlertSystemRecoveryKvBackendError::none) {
        return map_error(erased);
    }
    return commit_pending(backend_);
}

}  // namespace opengauge::integration
