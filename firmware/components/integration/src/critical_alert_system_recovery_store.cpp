#include "opengauge/critical_alert_system_recovery_store.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace opengauge::integration {
namespace {

struct InspectedSlot {
    CriticalAlertSystemRecoverySlotState state{
        CriticalAlertSystemRecoverySlotState::empty};
    CriticalAlertSystemRecoveryCheckpoint checkpoint{};
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes> bytes{};
};

InspectedSlot inspect_slot(
    CriticalAlertSystemRecoveryStorage& storage, std::uint8_t slot) {
    InspectedSlot result{};
    const auto read =
        storage.read_slot(slot, result.bytes.data(), result.bytes.size());
    if (read == CriticalAlertSystemRecoveryStorageError::not_found)
        return result;
    if (read != CriticalAlertSystemRecoveryStorageError::none) {
        result.state = CriticalAlertSystemRecoverySlotState::io_failure;
        return result;
    }
    result.state = decode_critical_alert_system_recovery_checkpoint(
                       result.bytes.data(), result.bytes.size(),
                       result.checkpoint) ==
                           CriticalAlertSystemRecoveryCheckpointError::none
                       ? CriticalAlertSystemRecoverySlotState::valid
                       : CriticalAlertSystemRecoverySlotState::invalid;
    return result;
}

bool generation_conflict(const InspectedSlot& a, const InspectedSlot& b) {
    return a.state == CriticalAlertSystemRecoverySlotState::valid &&
           b.state == CriticalAlertSystemRecoverySlotState::valid &&
           a.checkpoint.generation == b.checkpoint.generation &&
           a.bytes != b.bytes;
}

std::uint64_t highest_generation(
    const InspectedSlot& a, const InspectedSlot& b) {
    std::uint64_t highest = 0;
    if (a.state == CriticalAlertSystemRecoverySlotState::valid)
        highest = a.checkpoint.generation;
    if (b.state == CriticalAlertSystemRecoverySlotState::valid)
        highest = std::max(highest, b.checkpoint.generation);
    return highest;
}

CriticalAlertSystemRecoverySource source_for(std::uint8_t slot) {
    return slot == 0 ? CriticalAlertSystemRecoverySource::slot_a
                     : CriticalAlertSystemRecoverySource::slot_b;
}

}  // namespace

CriticalAlertSystemRecoveryStore::CriticalAlertSystemRecoveryStore(
    CriticalAlertSystemRecoveryStorage& storage)
    : storage_(storage) {}

CriticalAlertSystemRecoverySaveResult
CriticalAlertSystemRecoveryStore::save(
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    std::uint64_t generation) {
    if (generation == 0)
        return {CriticalAlertSystemRecoveryStoreError::invalid_generation};
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    if (a.state == CriticalAlertSystemRecoverySlotState::io_failure ||
        b.state == CriticalAlertSystemRecoverySlotState::io_failure)
        return {CriticalAlertSystemRecoveryStoreError::storage_failure};
    if (generation_conflict(a, b))
        return {CriticalAlertSystemRecoveryStoreError::generation_conflict};
    if (generation <= highest_generation(a, b))
        return {CriticalAlertSystemRecoveryStoreError::stale_generation};

    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        encoded{};
    const auto recovery = export_critical_alert_system_recovery_checkpoint(
        authorization, ingress, outbox, now_ms, generation, encoded);
    if (!recovery.completed()) {
        CriticalAlertSystemRecoverySaveResult result{
            CriticalAlertSystemRecoveryStoreError::checkpoint_rejected};
        result.recovery = recovery;
        return result;
    }

    std::uint8_t target = 0;
    if (a.state != CriticalAlertSystemRecoverySlotState::valid)
        target = 0;
    else if (b.state != CriticalAlertSystemRecoverySlotState::valid)
        target = 1;
    else
        target = a.checkpoint.generation <= b.checkpoint.generation ? 0 : 1;

    if (storage_.write_slot(target, encoded.data(), encoded.size()) !=
        CriticalAlertSystemRecoveryStorageError::none) {
        CriticalAlertSystemRecoverySaveResult result{
            CriticalAlertSystemRecoveryStoreError::storage_failure};
        result.written_slot = source_for(target);
        result.recovery = recovery;
        result.generation = generation;
        result.commit_uncertain = true;
        return result;
    }
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        verified{};
    if (storage_.read_slot(target, verified.data(), verified.size()) !=
            CriticalAlertSystemRecoveryStorageError::none ||
        verified != encoded) {
        CriticalAlertSystemRecoverySaveResult result{
            CriticalAlertSystemRecoveryStoreError::verification_failure};
        result.written_slot = source_for(target);
        result.recovery = recovery;
        result.generation = generation;
        result.commit_uncertain = true;
        return result;
    }
    CriticalAlertSystemRecoveryCheckpoint decoded{};
    if (decode_critical_alert_system_recovery_checkpoint(
            verified.data(), verified.size(), decoded) !=
            CriticalAlertSystemRecoveryCheckpointError::none ||
        decoded.generation != generation) {
        CriticalAlertSystemRecoverySaveResult result{
            CriticalAlertSystemRecoveryStoreError::verification_failure};
        result.written_slot = source_for(target);
        result.recovery = recovery;
        result.generation = generation;
        result.commit_uncertain = true;
        return result;
    }
    CriticalAlertSystemRecoverySaveResult result{
        CriticalAlertSystemRecoveryStoreError::none};
    result.written_slot = source_for(target);
    result.recovery = recovery;
    result.generation = generation;
    return result;
}

CriticalAlertSystemRecoverySaveResult
CriticalAlertSystemRecoveryStore::save_next(
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms) {
    return save_next_after(authorization, ingress, outbox, now_ms, 0);
}

CriticalAlertSystemRecoverySaveResult
CriticalAlertSystemRecoveryStore::save_next_after(
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    std::uint64_t last_trusted_generation) {
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    if (a.state == CriticalAlertSystemRecoverySlotState::io_failure ||
        b.state == CriticalAlertSystemRecoverySlotState::io_failure)
        return {CriticalAlertSystemRecoveryStoreError::storage_failure};
    if (generation_conflict(a, b))
        return {CriticalAlertSystemRecoveryStoreError::generation_conflict};
    const auto baseline = std::max(
        highest_generation(a, b), last_trusted_generation);
    if (baseline == std::numeric_limits<std::uint64_t>::max())
        return {CriticalAlertSystemRecoveryStoreError::generation_exhausted};
    return save(authorization, ingress, outbox, now_ms, baseline + 1);
}

CriticalAlertSystemRecoveryLoadResult
CriticalAlertSystemRecoveryStore::restore(
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms) {
    return restore_at_or_above(
        authorization, ingress, outbox, now_ms, 0);
}

CriticalAlertSystemRecoveryLoadResult
CriticalAlertSystemRecoveryStore::restore_at_or_above(
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    std::uint64_t minimum_trusted_generation) {
    const auto a = inspect_slot(storage_, 0);
    const auto b = inspect_slot(storage_, 1);
    CriticalAlertSystemRecoveryLoadResult result{};
    result.slot_a = a.state;
    result.slot_b = b.state;
    if (generation_conflict(a, b)) {
        result.error = CriticalAlertSystemRecoveryStoreError::generation_conflict;
        result.recovery_required = true;
        return result;
    }

    const InspectedSlot* selected = nullptr;
    std::uint8_t selected_slot = 0;
    if (a.state == CriticalAlertSystemRecoverySlotState::valid &&
        b.state == CriticalAlertSystemRecoverySlotState::valid) {
        if (b.checkpoint.generation > a.checkpoint.generation) {
            selected = &b;
            selected_slot = 1;
        } else {
            selected = &a;
        }
    } else if (a.state == CriticalAlertSystemRecoverySlotState::valid) {
        selected = &a;
        result.recovery_required = true;
    } else if (b.state == CriticalAlertSystemRecoverySlotState::valid) {
        selected = &b;
        selected_slot = 1;
        result.recovery_required = true;
    }
    if (selected == nullptr) {
        result.error =
            a.state == CriticalAlertSystemRecoverySlotState::io_failure ||
                    b.state == CriticalAlertSystemRecoverySlotState::io_failure
                ? CriticalAlertSystemRecoveryStoreError::storage_failure
                : CriticalAlertSystemRecoveryStoreError::no_checkpoint;
        result.recovery_required = true;
        return result;
    }

    result.source = source_for(selected_slot);
    result.generation = selected->checkpoint.generation;
    if (result.generation < minimum_trusted_generation) {
        result.error = CriticalAlertSystemRecoveryStoreError::rollback_detected;
        result.recovery_required = true;
        return result;
    }
    result.error =
        a.state == CriticalAlertSystemRecoverySlotState::io_failure ||
                b.state == CriticalAlertSystemRecoverySlotState::io_failure
            ? CriticalAlertSystemRecoveryStoreError::storage_failure
            : CriticalAlertSystemRecoveryStoreError::none;
    result.recovery = import_critical_alert_system_recovery_checkpoint(
        selected->bytes.data(), selected->bytes.size(), authorization,
        ingress, outbox, now_ms);
    if (!result.recovery.completed()) {
        result.error = CriticalAlertSystemRecoveryStoreError::checkpoint_rejected;
        result.recovery_required = true;
        return result;
    }
    result.restored = true;
    return result;
}

CriticalAlertSystemRecoveryStoreError
CriticalAlertSystemRecoveryStore::reset() {
    const auto a = storage_.erase_slot(0);
    const auto b = storage_.erase_slot(1);
    return a == CriticalAlertSystemRecoveryStorageError::none &&
                   b == CriticalAlertSystemRecoveryStorageError::none
               ? CriticalAlertSystemRecoveryStoreError::none
               : CriticalAlertSystemRecoveryStoreError::storage_failure;
}

}  // namespace opengauge::integration
