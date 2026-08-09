#include "opengauge/can_receiver.hpp"

namespace opengauge::can {

CanReceiverError validate_can_frame(const CanFrame& frame) {
    switch (frame.format) {
        case CanFrameFormat::standard:
            if (frame.identifier > kMaximumStandardCanIdentifier) {
                return CanReceiverError::invalid_frame;
            }
            break;
        case CanFrameFormat::extended:
            if (frame.identifier > kMaximumExtendedCanIdentifier) {
                return CanReceiverError::invalid_frame;
            }
            break;
        default:
            return CanReceiverError::invalid_frame;
    }
    switch (frame.kind) {
        case CanFrameKind::data:
        case CanFrameKind::remote:
            break;
        default:
            return CanReceiverError::invalid_frame;
    }
    if (frame.data_length > kClassicalCanPayloadBytes) {
        return CanReceiverError::invalid_frame;
    }
    for (std::size_t index = frame.data_length;
         index < frame.data.size();
         ++index) {
        if (frame.data[index] != 0) {
            return CanReceiverError::invalid_frame;
        }
    }
    if (frame.kind == CanFrameKind::remote) {
        for (const auto byte : frame.data) {
            if (byte != 0) {
                return CanReceiverError::invalid_frame;
            }
        }
    }
    return CanReceiverError::none;
}

}  // namespace opengauge::can
