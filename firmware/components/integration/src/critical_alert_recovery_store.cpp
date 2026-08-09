#include "opengauge/critical_alert_recovery_store.hpp"

#include <algorithm>
#include <array>

namespace opengauge::integration {
namespace {

struct InspectedSlot {
    CriticalAlertRecoverySlotState state{CriticalAlertRecoverySlotState::empty};
    CriticalAlertRecoveryCheckpoint checkpoint{};
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> bytes{};
};

InspectedSlot inspect_slot(CriticalAlertRecoveryStorage& storage, std::uint8_t slot) {
    InspectedSlot result{};
    const auto read = storage.read_slot(slot, result.bytes.data(), result.bytes.size());
    if (read == CriticalAlertRecoveryStorageError::not_found) {
        return result;
    }
    if (read != CriticalAlertRecoveryStorageError::none) {
        result.state = CriticalAlertRecoverySlotState::io_failure;
        return result;
    }
    result.state = decode_critical_alert_recovery_checkpoint(
                       result.bytes.data(), result.bytes.size(), result.checkpoint) ==
                           CriticalAlertRecoveryCheckpointError::none
                       ? CriticalAlertRecoverySlotState::valid
                       : CriticalAlertRecoverySlotState::invalid;
    return result;
}

bool generation_conflict(const InspectedSlot& a, const InspectedSlot& b) {
    return a.state == CriticalAlertRecoverySlotState::valid &&
           b.state == CriticalAlertRecoverySlotState::valid &&
           a.checkpoint.generation == b.checkpoint.generation &&
           a.bytes != b.bytes;
}

CriticalAlertRecoverySource source_for(std::uint8_t slot) {
    return slot == 0 ? CriticalAlertRecoverySource::slot_a
                     : CriticalAlertRecoverySource::slot_b;
}

}  // namespace

CriticalAlertRecoveryStore::CriticalAlertRecoveryStore(
    CriticalAlertRecoveryStorage& storage) : storage_(storage) {}

CriticalAlertRecoverySaveResult CriticalAlertRecoveryStore::save(
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    std::uint64_t generation) {
    if (generation == 0) {
        return {CriticalAlertRecoveryStoreError::invalid_generation};
    }
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    if (a.state == CriticalAlertRecoverySlotState::io_failure ||
        b.state == CriticalAlertRecoverySlotState::io_failure) {
        return {CriticalAlertRecoveryStoreError::storage_failure};
    }
    if (generation_conflict(a, b)) {
        return {CriticalAlertRecoveryStoreError::generation_conflict};
    }
    std::uint64_t highest = 0;
    if (a.state == CriticalAlertRecoverySlotState::valid) {
        highest = a.checkpoint.generation;
    }
    if (b.state == CriticalAlertRecoverySlotState::valid) {
        highest = std::max(highest, b.checkpoint.generation);
    }
    if (generation <= highest) {
        return {CriticalAlertRecoveryStoreError::stale_generation};
    }
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> encoded{};
    const auto recovery = export_critical_alert_recovery_checkpoint(
        ingress, outbox, now_ms, generation, encoded);
    if (!recovery.completed()) {
        CriticalAlertRecoverySaveResult result{
            CriticalAlertRecoveryStoreError::checkpoint_rejected};
        result.recovery = recovery;
        return result;
    }
    std::uint8_t target = 0;
    if (a.state != CriticalAlertRecoverySlotState::valid) {
        target = 0;
    } else if (b.state != CriticalAlertRecoverySlotState::valid) {
        target = 1;
    } else {
        target = a.checkpoint.generation <= b.checkpoint.generation ? 0 : 1;
    }
    if (storage_.write_slot(target, encoded.data(), encoded.size()) !=
        CriticalAlertRecoveryStorageError::none) {
        return {CriticalAlertRecoveryStoreError::storage_failure};
    }
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes> verified{};
    if (storage_.read_slot(target, verified.data(), verified.size()) !=
            CriticalAlertRecoveryStorageError::none ||
        verified != encoded) {
        return {CriticalAlertRecoveryStoreError::verification_failure};
    }
    CriticalAlertRecoveryCheckpoint decoded{};
    if (decode_critical_alert_recovery_checkpoint(
            verified.data(), verified.size(), decoded) !=
            CriticalAlertRecoveryCheckpointError::none ||
        decoded.generation != generation) {
        return {CriticalAlertRecoveryStoreError::verification_failure};
    }
    CriticalAlertRecoverySaveResult result{CriticalAlertRecoveryStoreError::none};
    result.written_slot = source_for(target);
    result.recovery = recovery;
    result.generation = generation;
    return result;
}

CriticalAlertRecoveryLoadResult CriticalAlertRecoveryStore::restore(
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms) {
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    CriticalAlertRecoveryLoadResult result{};
    result.slot_a = a.state;
    result.slot_b = b.state;
    if (generation_conflict(a, b)) {
        result.error = CriticalAlertRecoveryStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }
    const InspectedSlot* selected = nullptr;
    std::uint8_t selected_slot = 0;
    if (a.state == CriticalAlertRecoverySlotState::valid &&
        b.state == CriticalAlertRecoverySlotState::valid) {
        if (b.checkpoint.generation > a.checkpoint.generation) {
            selected = &b;
            selected_slot = 1;
        } else {
            selected = &a;
        }
    } else if (a.state == CriticalAlertRecoverySlotState::valid) {
        selected = &a;
        result.recovery_required = true;
    } else if (b.state == CriticalAlertRecoverySlotState::valid) {
        selected = &b;
        selected_slot = 1;
        result.recovery_required = true;
    }
    if (selected == nullptr) {
        result.error = a.state == CriticalAlertRecoverySlotState::io_failure ||
                               b.state == CriticalAlertRecoverySlotState::io_failure
                           ? CriticalAlertRecoveryStoreError::storage_failure
                           : CriticalAlertRecoveryStoreError::no_checkpoint;
        result.recovery_required = true;
        return result;
    }
    result.source = source_for(selected_slot);
    result.generation = selected->checkpoint.generation;
    result.error = a.state == CriticalAlertRecoverySlotState::io_failure ||
                           b.state == CriticalAlertRecoverySlotState::io_failure
                       ? CriticalAlertRecoveryStoreError::storage_failure
                       : CriticalAlertRecoveryStoreError::none;
    result.recovery = import_critical_alert_recovery_checkpoint(
        selected->bytes.data(), selected->bytes.size(), ingress, outbox, now_ms);
    if (!result.recovery.completed()) {
        result.error = CriticalAlertRecoveryStoreError::checkpoint_rejected;
        result.recovery_required = true;
        return result;
    }
    result.restored = true;
    return result;
}

CriticalAlertRecoveryStoreError CriticalAlertRecoveryStore::reset() {
    const auto a = storage_.erase_slot(0);
    const auto b = storage_.erase_slot(1);
    return a == CriticalAlertRecoveryStorageError::none &&
                   b == CriticalAlertRecoveryStorageError::none
               ? CriticalAlertRecoveryStoreError::none
               : CriticalAlertRecoveryStoreError::storage_failure;
}

}  // namespace opengauge::integration
