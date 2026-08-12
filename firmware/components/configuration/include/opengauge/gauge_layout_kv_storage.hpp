#pragma once

#include "opengauge/gauge_layout.hpp"
#include "opengauge/key_value_blob_backend.hpp"

namespace opengauge::configuration {

inline constexpr char kGaugeLayoutPartitionLabel[] = "og_config";
inline constexpr char kGaugeLayoutNamespace[] = "gauge_layout";
inline constexpr char kGaugeLayoutSlotAKey[] = "ogl0_a";
inline constexpr char kGaugeLayoutSlotBKey[] = "ogl0_b";

static_assert(sizeof(kGaugeLayoutPartitionLabel) - 1 <= 15);
static_assert(sizeof(kGaugeLayoutNamespace) - 1 <= 15);
static_assert(sizeof(kGaugeLayoutSlotAKey) - 1 <= 15);
static_assert(sizeof(kGaugeLayoutSlotBKey) - 1 <= 15);

using GaugeLayoutKvBackendError = storage::KeyValueBlobBackendError;
using GaugeLayoutKvBackend = storage::KeyValueBlobBackend;

// Maps the two exact 576-byte OGL0 slots onto isolated key/value blobs. This
// is target-shaped common code, not an ESP-IDF or physical-durability result.
class GaugeLayoutKvStorage final : public GaugeLayoutStorage {
public:
    explicit GaugeLayoutKvStorage(GaugeLayoutKvBackend& backend);

    [[nodiscard]] LayoutStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override;
    [[nodiscard]] LayoutStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override;
    [[nodiscard]] LayoutStorageError erase_slot(
        std::uint8_t slot) override;

private:
    GaugeLayoutKvBackend& backend_;
};

}  // namespace opengauge::configuration
