#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "opengauge/can_receiver.hpp"

namespace opengauge::can::test_support {

class FakeCanReceiver final : public CanReceiver {
public:
    static constexpr std::size_t kQueueCapacity = 16;

    CanReceiverError start_listen_only(CanListenPolicy policy) override;
    void stop() override;
    [[nodiscard]] CanReceiveResult receive() override;
    [[nodiscard]] CanReceiverStatus status() const override;

    [[nodiscard]] CanReceiverError inject(const CanFrame& frame);
    [[nodiscard]] CanReceiverError set_bus_state(CanBusState state);
    void fail_hardware(bool fail);

private:
    struct QueuedFrame {
        CanFrame frame{};
        CanBusState captured_bus_state{CanBusState::error_active};
    };

    [[nodiscard]] bool accepts(const CanFrame& frame) const;
    void clear_queue();

    CanListenPolicy policy_{};
    std::array<QueuedFrame, kQueueCapacity> queue_{};
    std::size_t queue_head_{0};
    std::size_t queue_size_{0};
    CanReceiverStatus status_{};
    std::uint64_t last_injected_at_ms_{0};
    bool has_injected_time_{false};
    bool hardware_failed_{false};
};

}  // namespace opengauge::can::test_support
