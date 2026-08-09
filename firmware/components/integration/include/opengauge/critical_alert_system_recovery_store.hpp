#pragma once

#include "opengauge/critical_alert_system_recovery.hpp"

namespace opengauge::integration {

enum class CriticalAlertSystemRecoveryStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

class CriticalAlertSystemRecoveryStorage {
public:
    virtual ~CriticalAlertSystemRecoveryStorage() = default;
    [[nodiscard]] virtual CriticalAlertSystemRecoveryStorageError read_slot(
        std::uint8_t slot, std::uint8_t* output, std::size_t size) = 0;
    [[nodiscard]] virtual CriticalAlertSystemRecoveryStorageError write_slot(
        std::uint8_t slot, const std::uint8_t* data, std::size_t size) = 0;
    [[nodiscard]] virtual CriticalAlertSystemRecoveryStorageError erase_slot(
        std::uint8_t slot) = 0;
};

enum class CriticalAlertSystemRecoverySlotState : std::uint8_t {
    empty = 0,
    valid,
    invalid,
    io_failure,
};

enum class CriticalAlertSystemRecoveryStoreError : std::uint8_t {
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

enum class CriticalAlertSystemRecoverySource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
};

struct CriticalAlertSystemRecoveryLoadResult {
    CriticalAlertSystemRecoveryStoreError error{
        CriticalAlertSystemRecoveryStoreError::no_checkpoint};
    CriticalAlertSystemRecoverySource source{
        CriticalAlertSystemRecoverySource::none};
    CriticalAlertSystemRecoverySlotState slot_a{
        CriticalAlertSystemRecoverySlotState::empty};
    CriticalAlertSystemRecoverySlotState slot_b{
        CriticalAlertSystemRecoverySlotState::empty};
    CriticalAlertSystemRecoveryResult recovery{};
    std::uint64_t generation{0};
    bool recovery_required{false};
    bool restored{false};
};

struct CriticalAlertSystemRecoverySaveResult {
    CriticalAlertSystemRecoveryStoreError error{
        CriticalAlertSystemRecoveryStoreError::storage_failure};
    CriticalAlertSystemRecoverySource written_slot{
        CriticalAlertSystemRecoverySource::none};
    CriticalAlertSystemRecoveryResult recovery{};
    std::uint64_t generation{0};
    bool commit_uncertain{false};

    [[nodiscard]] constexpr bool saved() const {
        return error == CriticalAlertSystemRecoveryStoreError::none;
    }
};

class CriticalAlertSystemRecoveryStore {
public:
    explicit CriticalAlertSystemRecoveryStore(
        CriticalAlertSystemRecoveryStorage& storage);

    [[nodiscard]] CriticalAlertSystemRecoverySaveResult save(
        identity::PeerAuthorizationRegistry& authorization,
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms,
        std::uint64_t generation);
    [[nodiscard]] CriticalAlertSystemRecoverySaveResult save_next(
        identity::PeerAuthorizationRegistry& authorization,
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms);
    [[nodiscard]] CriticalAlertSystemRecoveryLoadResult restore(
        identity::PeerAuthorizationRegistry& authorization,
        CriticalAlertAckIngress& ingress,
        CriticalAlertOutbox& outbox,
        std::uint64_t now_ms);
    [[nodiscard]] CriticalAlertSystemRecoveryStoreError reset();

private:
    CriticalAlertSystemRecoveryStorage& storage_;
};

}  // namespace opengauge::integration
