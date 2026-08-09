#pragma once

#include <array>

#include "opengauge/critical_alert_recovery_store.hpp"

namespace opengauge::integration::test_support {

enum class FakeRecoveryWriteBehavior : std::uint8_t {
    normal = 0,
    fail_before_write,
    fail_after_partial_write,
    fail_after_configured_prefix,
    fail_after_full_write,
    corrupt_after_success,
};

class FakeCriticalAlertRecoveryStorage final
    : public CriticalAlertRecoveryStorage {
public:
    [[nodiscard]] CriticalAlertRecoveryStorageError read_slot(
        std::uint8_t slot, std::uint8_t* output, std::size_t size) override;
    [[nodiscard]] CriticalAlertRecoveryStorageError write_slot(
        std::uint8_t slot, const std::uint8_t* data, std::size_t size) override;
    [[nodiscard]] CriticalAlertRecoveryStorageError erase_slot(
        std::uint8_t slot) override;
    void fail_next_read(std::uint8_t slot);
    void set_next_write_behavior(std::uint8_t slot, FakeRecoveryWriteBehavior behavior);
    void set_next_partial_write_bytes(std::uint8_t slot, std::size_t bytes);
    void fail_next_erase(std::uint8_t slot);
    void corrupt(std::uint8_t slot, std::size_t offset, std::uint8_t mask);
    [[nodiscard]] bool present(std::uint8_t slot) const;
    [[nodiscard]] std::uint32_t writes(std::uint8_t slot) const;
    [[nodiscard]] std::uint32_t erases(std::uint8_t slot) const;

private:
    std::array<std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes>, 2> slots_{};
    std::array<bool, 2> present_{};
    std::array<bool, 2> fail_read_{};
    std::array<FakeRecoveryWriteBehavior, 2> next_write_{};
    std::array<std::size_t, 2> next_partial_bytes_{};
    std::array<bool, 2> fail_erase_{};
    std::array<std::uint32_t, 2> writes_{};
    std::array<std::uint32_t, 2> erases_{};
};

}  // namespace opengauge::integration::test_support
