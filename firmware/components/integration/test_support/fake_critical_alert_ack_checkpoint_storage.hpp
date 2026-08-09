#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/critical_alert_ack_checkpoint_store.hpp"

namespace opengauge::integration::test_support {

enum class FakeCheckpointWriteBehavior : std::uint8_t {
    normal = 0,
    fail_before_write,
    fail_after_partial_write,
    corrupt_after_success,
};

class FakeCriticalAlertAckCheckpointStorage final
    : public CriticalAlertAckCheckpointStorage {
public:
    [[nodiscard]] CriticalAlertAckCheckpointStorageError read_slot(
        std::uint8_t slot,
        std::uint8_t* output,
        std::size_t size) override;
    [[nodiscard]] CriticalAlertAckCheckpointStorageError write_slot(
        std::uint8_t slot,
        const std::uint8_t* data,
        std::size_t size) override;
    [[nodiscard]] CriticalAlertAckCheckpointStorageError erase_slot(
        std::uint8_t slot) override;

    void fail_next_read(std::uint8_t slot);
    void set_next_write_behavior(
        std::uint8_t slot,
        FakeCheckpointWriteBehavior behavior);
    void fail_next_erase(std::uint8_t slot);
    void corrupt(std::uint8_t slot, std::size_t offset, std::uint8_t mask);

    [[nodiscard]] bool present(std::uint8_t slot) const;
    [[nodiscard]] std::uint32_t writes(std::uint8_t slot) const;
    [[nodiscard]] std::uint32_t erases(std::uint8_t slot) const;

private:
    std::array<
        std::array<std::uint8_t, kCriticalAlertAckStoredCheckpointBytes>,
        2> slots_{};
    std::array<bool, 2> present_{};
    std::array<bool, 2> fail_read_{};
    std::array<FakeCheckpointWriteBehavior, 2> next_write_{};
    std::array<bool, 2> fail_erase_{};
    std::array<std::uint32_t, 2> writes_{};
    std::array<std::uint32_t, 2> erases_{};
};

}  // namespace opengauge::integration::test_support
