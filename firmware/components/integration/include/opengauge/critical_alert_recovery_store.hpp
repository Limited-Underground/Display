#pragma once

#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_recovery.hpp"

namespace opengauge::integration {

enum class CriticalAlertRecoveryStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

class CriticalAlertRecoveryStorage {
public:
    virtual ~CriticalAlertRecoveryStorage() = default;
    [[nodiscard]] virtual CriticalAlertRecoveryStorageError read_slot(
        std::uint8_t slot, std::uint8_t* output, std::size_t size) = 0;
    [[nodiscard]] virtual CriticalAlertRecoveryStorageError write_slot(
        std::uint8_t slot, const std::uint8_t* data, std::size_t size) = 0;
    [[nodiscard]] virtual CriticalAlertRecoveryStorageError erase_slot(
        std::uint8_t slot) = 0;
};

enum class CriticalAlertRecoverySlotState : std::uint8_t {
    empty = 0,
    valid,
    invalid,
    io_failure,
};

enum class CriticalAlertRecoveryStoreError : std::uint8_t {
    none = 0,
    no_checkpoint,
    invalid_generation,
    generation_exhausted,
    stale_generation,
    generation_conflict,
    storage_failure,
    verification_failure,
    checkpoint_rejected,
};

enum class CriticalAlertRecoverySource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
};

struct CriticalAlertRecoveryLoadResult {
    CriticalAlertRecoveryStoreError error{
        CriticalAlertRecoveryStoreError::no_checkpoint};
    CriticalAlertRecoverySource source{CriticalAlertRecoverySource::none};
    CriticalAlertRecoverySlotState slot_a{CriticalAlertRecoverySlotState::empty};
    CriticalAlertRecoverySlotState slot_b{CriticalAlertRecoverySlotState::empty};
    CriticalAlertRecoveryResult recovery{};
    std::uint64_t generation{0};
    bool recovery_required{false};
    bool restored{false};
};

struct CriticalAlertRecoverySaveResult {
    CriticalAlertRecoveryStoreError error{
        CriticalAlertRecoveryStoreError::storage_failure};
    CriticalAlertRecoverySource written_slot{CriticalAlertRecoverySource::none};
    CriticalAlertRecoveryResult recovery{};
    std::uint64_t generation{0};
    bool commit_uncertain{false};
    [[nodiscard]] constexpr bool saved() const {
        return error == CriticalAlertRecoveryStoreError::none;
    }
};

class CriticalAlertRecoveryStore {
public:
    explicit CriticalAlertRecoveryStore(CriticalAlertRecoveryStorage& storage);
    [[nodiscard]] CriticalAlertRecoverySaveResult save(
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms,
        std::uint64_t generation);
    [[nodiscard]] CriticalAlertRecoverySaveResult save_next(
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms);
    [[nodiscard]] CriticalAlertRecoveryLoadResult restore(
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms);
    [[nodiscard]] CriticalAlertRecoveryStoreError reset();

private:
    CriticalAlertRecoveryStorage& storage_;
};

}  // namespace opengauge::integration
