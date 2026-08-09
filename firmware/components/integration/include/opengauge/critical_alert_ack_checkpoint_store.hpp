#pragma once

#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_ack_ingress.hpp"

namespace opengauge::integration {

inline constexpr std::uint8_t kCriticalAlertAckStoredCheckpointVersion = 0;
inline constexpr std::size_t kCriticalAlertAckStoredCheckpointBytes = 320;

enum class CriticalAlertAckCheckpointStorageError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

class CriticalAlertAckCheckpointStorage {
public:
    virtual ~CriticalAlertAckCheckpointStorage() = default;
    [[nodiscard]] virtual CriticalAlertAckCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) = 0;
    [[nodiscard]] virtual CriticalAlertAckCheckpointStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual CriticalAlertAckCheckpointStorageError erase_slot(
        std::uint8_t slot) = 0;
};

enum class CriticalAlertAckCheckpointSlotState : std::uint8_t {
    empty = 0,
    valid,
    invalid,
    io_failure,
};

enum class CriticalAlertAckCheckpointStoreError : std::uint8_t {
    none = 0,
    no_checkpoint,
    invalid_generation,
    stale_generation,
    generation_conflict,
    storage_failure,
    verification_failure,
    checkpoint_rejected,
};

enum class CriticalAlertAckCheckpointSource : std::uint8_t {
    none = 0,
    slot_a,
    slot_b,
};

struct CriticalAlertAckCheckpointLoadResult {
    CriticalAlertAckCheckpointStoreError error{
        CriticalAlertAckCheckpointStoreError::no_checkpoint};
    CriticalAlertAckCheckpointSource source{
        CriticalAlertAckCheckpointSource::none};
    CriticalAlertAckCheckpointSlotState slot_a{
        CriticalAlertAckCheckpointSlotState::empty};
    CriticalAlertAckCheckpointSlotState slot_b{
        CriticalAlertAckCheckpointSlotState::empty};
    CriticalAlertAckIngressError ingress_error{
        CriticalAlertAckIngressError::none};
    std::uint64_t generation{0};
    bool recovery_required{false};
    bool restored{false};
};

struct CriticalAlertAckCheckpointSaveResult {
    CriticalAlertAckCheckpointStoreError error{
        CriticalAlertAckCheckpointStoreError::storage_failure};
    CriticalAlertAckCheckpointSource written_slot{
        CriticalAlertAckCheckpointSource::none};
    CriticalAlertAckIngressError ingress_error{
        CriticalAlertAckIngressError::none};
    std::uint64_t generation{0};

    [[nodiscard]] constexpr bool saved() const {
        return error == CriticalAlertAckCheckpointStoreError::none;
    }
};

class CriticalAlertAckCheckpointStore {
public:
    explicit CriticalAlertAckCheckpointStore(
        CriticalAlertAckCheckpointStorage& storage);

    [[nodiscard]] CriticalAlertAckCheckpointLoadResult restore(
        CriticalAlertAckIngress& ingress);
    [[nodiscard]] CriticalAlertAckCheckpointSaveResult save(
        CriticalAlertAckIngress& ingress,
        std::uint64_t generation);
    [[nodiscard]] CriticalAlertAckCheckpointStoreError reset();

private:
    CriticalAlertAckCheckpointStorage& storage_;
};

}  // namespace opengauge::integration
