#include "fake_can_receiver.hpp"

namespace opengauge::can::test_support {
namespace {

bool supported_bitrate(std::uint32_t bitrate) {
    return bitrate == 125000U || bitrate == 250000U ||
           bitrate == 500000U || bitrate == 1000000U;
}

bool known_bus_state(CanBusState state) {
    switch (state) {
        case CanBusState::error_active:
        case CanBusState::error_warning:
        case CanBusState::error_passive:
        case CanBusState::bus_off:
            return true;
    }
    return false;
}

}  // namespace

CanReceiverError FakeCanReceiver::start_listen_only(CanListenPolicy policy) {
    if (status_.mode != CanReceiverMode::offline) {
        status_.last_error = CanReceiverError::invalid_state;
        return status_.last_error;
    }
    if (!supported_bitrate(policy.bitrate) ||
        (!policy.accept_standard_frames &&
         !policy.accept_extended_frames)) {
        status_.last_error = CanReceiverError::invalid_policy;
        return status_.last_error;
    }
    policy_ = policy;
    clear_queue();
    status_ = {};
    status_.mode = CanReceiverMode::listen_only;
    status_.bus_state = CanBusState::error_active;
    status_.bitrate = policy.bitrate;
    status_.queue_capacity = kQueueCapacity;
    has_injected_time_ = false;
    hardware_failed_ = false;
    return CanReceiverError::none;
}

void FakeCanReceiver::stop() {
    clear_queue();
    status_ = {};
    status_.mode = CanReceiverMode::offline;
    status_.queue_capacity = kQueueCapacity;
    policy_ = {};
    last_injected_at_ms_ = 0;
    has_injected_time_ = false;
    hardware_failed_ = false;
}

CanReceiveResult FakeCanReceiver::receive() {
    if (status_.mode != CanReceiverMode::listen_only) {
        status_.last_error = CanReceiverError::invalid_state;
        return {status_.last_error, {}, {}};
    }
    if (hardware_failed_) {
        status_.last_error = CanReceiverError::hardware_failure;
        return {status_.last_error, {}, {}};
    }
    if (queue_size_ == 0) {
        status_.last_error = CanReceiverError::no_frame;
        return {
            status_.last_error,
            {},
            {status_.bus_state,
             status_.frames_dropped_overflow,
             0}};
    }
    const auto queued = queue_[queue_head_];
    queue_head_ = (queue_head_ + 1U) % queue_.size();
    --queue_size_;
    status_.queue_depth = queue_size_;
    status_.last_error = CanReceiverError::none;
    return {
        CanReceiverError::none,
        queued.frame,
        {queued.captured_bus_state,
         status_.frames_dropped_overflow,
         queue_size_}};
}

CanReceiverStatus FakeCanReceiver::status() const {
    return status_;
}

CanReceiverError FakeCanReceiver::inject(const CanFrame& frame) {
    if (status_.mode != CanReceiverMode::listen_only) {
        status_.last_error = CanReceiverError::invalid_state;
        return status_.last_error;
    }
    if (hardware_failed_) {
        status_.last_error = CanReceiverError::hardware_failure;
        return status_.last_error;
    }
    const auto validation = validate_can_frame(frame);
    if (validation != CanReceiverError::none) {
        status_.last_error = validation;
        return validation;
    }
    if (has_injected_time_ && frame.received_at_ms < last_injected_at_ms_) {
        status_.last_error = CanReceiverError::clock_regressed;
        return status_.last_error;
    }
    last_injected_at_ms_ = frame.received_at_ms;
    has_injected_time_ = true;
    if (status_.bus_state == CanBusState::bus_off) {
        status_.last_error = CanReceiverError::bus_off;
        return status_.last_error;
    }
    if (!accepts(frame)) {
        ++status_.frames_filtered;
        status_.last_error = CanReceiverError::filtered;
        return status_.last_error;
    }
    if (queue_size_ == queue_.size()) {
        ++status_.frames_dropped_overflow;
        status_.last_error = CanReceiverError::queue_full;
        return status_.last_error;
    }
    const auto tail = (queue_head_ + queue_size_) % queue_.size();
    queue_[tail] = {frame, status_.bus_state};
    ++queue_size_;
    ++status_.frames_received;
    status_.queue_depth = queue_size_;
    status_.last_error = CanReceiverError::none;
    return CanReceiverError::none;
}

CanReceiverError FakeCanReceiver::set_bus_state(CanBusState state) {
    if (status_.mode != CanReceiverMode::listen_only) {
        status_.last_error = CanReceiverError::invalid_state;
        return status_.last_error;
    }
    if (!known_bus_state(state)) {
        status_.last_error = CanReceiverError::invalid_argument;
        return status_.last_error;
    }
    if (state != status_.bus_state) {
        status_.bus_state = state;
        ++status_.bus_state_changes;
    }
    status_.last_error = CanReceiverError::none;
    return CanReceiverError::none;
}

void FakeCanReceiver::fail_hardware(bool fail) {
    hardware_failed_ = fail;
    if (fail) {
        status_.last_error = CanReceiverError::hardware_failure;
    } else if (status_.last_error == CanReceiverError::hardware_failure) {
        status_.last_error = CanReceiverError::none;
    }
}

bool FakeCanReceiver::accepts(const CanFrame& frame) const {
    const auto format_accepted =
        (frame.format == CanFrameFormat::standard &&
         policy_.accept_standard_frames) ||
        (frame.format == CanFrameFormat::extended &&
         policy_.accept_extended_frames);
    return format_accepted &&
           (frame.kind != CanFrameKind::remote ||
            policy_.accept_remote_frames);
}

void FakeCanReceiver::clear_queue() {
    queue_ = {};
    queue_head_ = 0;
    queue_size_ = 0;
}

}  // namespace opengauge::can::test_support
