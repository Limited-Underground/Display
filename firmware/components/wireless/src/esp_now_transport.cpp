#include "opengauge/esp_now_transport.hpp"

namespace opengauge::wireless {

bool peer_address_equals(
    const PeerAddress& left,
    const PeerAddress& right) {
    return left.bytes == right.bytes;
}

bool is_valid_unicast_address(const PeerAddress& address) {
    bool any_nonzero = false;
    bool all_broadcast = true;
    for (const auto byte : address.bytes) {
        any_nonzero = any_nonzero || byte != 0;
        all_broadcast = all_broadcast && byte == 0xFFU;
    }
    return any_nonzero && !all_broadcast &&
           (address.bytes[0] & 0x01U) == 0;
}

}  // namespace opengauge::wireless
