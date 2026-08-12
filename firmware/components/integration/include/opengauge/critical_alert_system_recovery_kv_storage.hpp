#pragma once

#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_system_recovery_store.hpp"

namespace opengauge::integration {

inline constexpr char kCriticalAlertSystemRecoveryPartitionLabel[] = "og_state";
inline constexpr char kCriticalAlertSystemRecoveryNamespace[] = "og_recovery";
inline constexpr char kCriticalAlertSystemRecoverySlotAKey[] = "ors0_a";
inline constexpr char kCriticalAlertSystemRecoverySlotBKey[] = "ors0_b";

static_assert(sizeof(kCriticalAlertSystemRecoveryPartitionLabel) - 1 <= 15);
static_assert(sizeof(kCriticalAlertSystemRecoveryNamespace) - 1 <= 15);
static_assert(sizeof(kCriticalAlertSystemRecoverySlotAKey) - 1 <= 15);
static_assert(sizeof(kCriticalAlertSystemRecoverySlotBKey) - 1 <= 15);

enum class CriticalAlertSystemRecoveryKvBackendError : std::uint8_t {
    none = 0,
    not_found,
    invalid_argument,
    io_failure,
};

// Backend writes and erases stage one mutation. commit() makes that mutation
// durable. One adapter instance must exclusively own its backend transaction.
class CriticalAlertSystemRecoveryKvBackend {
public:
    virtual ~CriticalAlertSystemRecoveryKvBackend() = default;

    [[nodiscard]] virtual CriticalAlertSystemRecoveryKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) = 0;
    [[nodiscard]] virtual CriticalAlertSystemRecoveryKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) = 0;
    [[nodiscard]] virtual CriticalAlertSystemRecoveryKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) = 0;
    [[nodiscard]] virtual CriticalAlertSystemRecoveryKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) = 0;
};

// Maps the two exact 1280-byte ORS0 slots onto isolated key/value blobs. This
// is target-shaped common code, not an ESP-IDF or protected-storage backend.
class CriticalAlertSystemRecoveryKvStorage final
    : public CriticalAlertSystemRecoveryStorage {
public:
    explicit CriticalAlertSystemRecoveryKvStorage(
        CriticalAlertSystemRecoveryKvBackend& backend);

    [[nodiscard]] CriticalAlertSystemRecoveryStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override;
    [[nodiscard]] CriticalAlertSystemRecoveryStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override;
    [[nodiscard]] CriticalAlertSystemRecoveryStorageError erase_slot(
        std::uint8_t slot) override;

private:
    CriticalAlertSystemRecoveryKvBackend& backend_;
};

}  // namespace opengauge::integration
