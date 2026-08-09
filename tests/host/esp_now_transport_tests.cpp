#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_esp_now_transport.hpp"

namespace {

using namespace opengauge::wireless;
using opengauge::wireless::test_support::FakeEspNowTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

PeerAddress address(std::uint8_t suffix) {
    return {{0x02U, 0x00U, 0x00U, 0x00U, 0x00U, suffix}};
}

void start_pair(
    FakeEspNowTransport& left,
    FakeEspNowTransport& right,
    bool encrypted = true,
    std::uint8_t channel = 6) {
    EXPECT(left.start(address(1), {channel, encrypted}) ==
           EspNowError::none);
    EXPECT(right.start(address(2), {channel, encrypted}) ==
           EspNowError::none);
    EXPECT(left.add_peer({address(2), channel, encrypted}) ==
           EspNowError::none);
    EXPECT(right.add_peer({address(1), channel, encrypted}) ==
           EspNowError::none);
    left.connect(right);
}

void test_identity_channel_start_and_status() {
    FakeEspNowTransport transport(500);
    EXPECT(transport.mtu() == kMaximumEspNowPayloadBytes);
    EXPECT(transport.status().state == EspNowState::offline);

    EXPECT(transport.start({}, {6, true}) ==
           EspNowError::invalid_argument);
    EXPECT(transport.start(
               {{0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU, 0xFFU}},
               {6, true}) == EspNowError::invalid_argument);
    EXPECT(transport.start(
               {{0x03U, 0, 0, 0, 0, 1}},
               {6, true}) == EspNowError::invalid_argument);
    EXPECT(transport.start(address(1), {0, true}) ==
           EspNowError::invalid_argument);
    EXPECT(transport.start(address(1), {15, true}) ==
           EspNowError::invalid_argument);

    EXPECT(transport.start(address(1), {6, true}) == EspNowError::none);
    EXPECT(transport.start(address(1), {6, true}) ==
           EspNowError::invalid_state);
    const auto status = transport.status();
    EXPECT(status.state == EspNowState::idle);
    EXPECT(status.mtu_bytes == kMaximumEspNowPayloadBytes);
    EXPECT(status.peer_count == 0);
}

void test_peer_table_rules() {
    FakeEspNowTransport transport{};
    EXPECT(transport.add_peer({address(2), 6, true}) ==
           EspNowError::invalid_state);
    EXPECT(transport.start(address(1), {6, true}) == EspNowError::none);
    EXPECT(transport.add_peer({address(1), 6, true}) ==
           EspNowError::invalid_argument);
    EXPECT(transport.add_peer({address(2), 7, true}) ==
           EspNowError::channel_mismatch);
    EXPECT(transport.add_peer({address(2), 6, true}) == EspNowError::none);
    EXPECT(transport.add_peer({address(2), 6, true}) ==
           EspNowError::peer_already_exists);

    for (std::uint8_t suffix = 3;
         suffix <= FakeEspNowTransport::kPeerCapacity + 1;
         ++suffix) {
        EXPECT(transport.add_peer({address(suffix), 6, true}) ==
               EspNowError::none);
    }
    EXPECT(transport.status().peer_count ==
           FakeEspNowTransport::kPeerCapacity);
    EXPECT(transport.add_peer({address(20), 6, true}) ==
           EspNowError::peer_table_full);
    EXPECT(transport.remove_peer(address(2)) == EspNowError::none);
    EXPECT(transport.remove_peer(address(2)) ==
           EspNowError::peer_not_found);
}

void test_encrypted_delivery_metadata_and_receipt() {
    FakeEspNowTransport sender(32, 25);
    FakeEspNowTransport receiver(32, 25);
    start_pair(sender, receiver);
    sender.set_link_rssi(-67, true);
    const std::array<std::uint8_t, 6> frame{
        0x00U, 0xFFU, 0x10U, 0x00U, 0x7EU, 0x42U};

    const auto sent = sender.send(
        address(2),
        {frame.data(), frame.size()},
        100);
    EXPECT(sent.accepted());
    EXPECT(sent.token != 0);
    EXPECT(sent.accepted_bytes == frame.size());
    EXPECT(sender.status().state == EspNowState::transmitting);

    sender.service(124);
    EXPECT(sender.poll_delivery().error == EspNowError::no_data);
    sender.service(125);
    const auto delivery = sender.poll_delivery();
    EXPECT(delivery.has_receipt());
    EXPECT(delivery.receipt.token == sent.token);
    EXPECT(delivery.receipt.radio_delivered());
    EXPECT(delivery.receipt.completed_at_ms == 125);

    std::array<std::uint8_t, 6> output{};
    const auto received = receiver.receive({output.data(), output.size()});
    EXPECT(received.has_frame());
    EXPECT(received.received_bytes == frame.size());
    EXPECT(output == frame);
    EXPECT(peer_address_equals(received.metadata.source, address(1)));
    EXPECT(received.metadata.channel == 6);
    EXPECT(received.metadata.encrypted);
    EXPECT(received.metadata.rssi_valid);
    EXPECT(received.metadata.rssi_dbm == -67);
    EXPECT(receiver.status().frames_received == 1);
    EXPECT(sender.status().frames_radio_delivered == 1);
}

void test_security_and_channel_rejection() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport receiver{};
    EXPECT(sender.start(address(1), {6, true}) == EspNowError::none);
    EXPECT(sender.add_peer({address(2), 6, false}) == EspNowError::none);
    const std::array<std::uint8_t, 1> frame{1};
    EXPECT(sender.send(address(2), {frame.data(), frame.size()}, 0).error ==
           EspNowError::encryption_required);

    FakeEspNowTransport open_sender{};
    FakeEspNowTransport encrypted_receiver{};
    EXPECT(open_sender.start(address(1), {6, false}) == EspNowError::none);
    EXPECT(encrypted_receiver.start(address(2), {6, false}) ==
           EspNowError::none);
    EXPECT(open_sender.add_peer({address(2), 6, false}) == EspNowError::none);
    EXPECT(encrypted_receiver.add_peer({address(1), 6, true}) ==
           EspNowError::none);
    open_sender.connect(encrypted_receiver);
    EXPECT(open_sender.send(
               address(2), {frame.data(), frame.size()}, 0).accepted());
    open_sender.service(0);
    auto delivery = open_sender.poll_delivery();
    EXPECT(delivery.has_receipt());
    EXPECT(delivery.receipt.outcome == DeliveryOutcome::receiver_rejected);

    FakeEspNowTransport channel_sender{};
    FakeEspNowTransport channel_receiver{};
    EXPECT(channel_sender.start(address(1), {6, false}) == EspNowError::none);
    EXPECT(channel_receiver.start(address(2), {7, false}) == EspNowError::none);
    EXPECT(channel_sender.add_peer({address(2), 6, false}) ==
           EspNowError::none);
    EXPECT(channel_receiver.add_peer({address(1), 7, false}) ==
           EspNowError::none);
    channel_sender.connect(channel_receiver);
    EXPECT(channel_sender.send(
               address(2), {frame.data(), frame.size()}, 0).accepted());
    channel_sender.service(0);
    delivery = channel_sender.poll_delivery();
    EXPECT(delivery.receipt.outcome == DeliveryOutcome::channel_mismatch);
}

void test_buffer_and_queue_backpressure() {
    FakeEspNowTransport sender(16);
    FakeEspNowTransport receiver(16);
    start_pair(sender, receiver);
    const std::array<std::uint8_t, 4> frame{1, 2, 3, 4};
    EXPECT(sender.send(address(2), {frame.data(), frame.size()}, 0).accepted());
    sender.service(0);

    std::array<std::uint8_t, 2> small{};
    auto received = receiver.receive({small.data(), small.size()});
    EXPECT(received.error == EspNowError::buffer_too_small);
    EXPECT(received.received_bytes == frame.size());
    EXPECT(receiver.status().receive_queue_depth == 1);
    std::array<std::uint8_t, 4> adequate{};
    EXPECT(receiver.receive({adequate.data(), adequate.size()}).has_frame());
    EXPECT(adequate == frame);

    sender.poll_delivery();
    for (std::size_t index = 0;
         index < FakeEspNowTransport::kQueueCapacity;
         ++index) {
        EXPECT(sender.send(
                   address(2), {frame.data(), frame.size()}, 1).accepted());
    }
    EXPECT(sender.send(
               address(2), {frame.data(), frame.size()}, 1).error ==
           EspNowError::queue_full);
}

void test_delivery_failure_outcomes() {
    const std::array<std::uint8_t, 1> frame{0xA5U};

    FakeEspNowTransport no_link{};
    EXPECT(no_link.start(address(1), {6, true}) == EspNowError::none);
    EXPECT(no_link.add_peer({address(2), 6, true}) == EspNowError::none);
    EXPECT(no_link.send(address(2), {frame.data(), frame.size()}, 0).accepted());
    no_link.service(0);
    EXPECT(no_link.poll_delivery().receipt.outcome ==
           DeliveryOutcome::no_link);

    FakeEspNowTransport sender{};
    FakeEspNowTransport receiver{};
    start_pair(sender, receiver);
    receiver.set_available(false);
    EXPECT(sender.send(address(2), {frame.data(), frame.size()}, 0).accepted());
    sender.service(0);
    EXPECT(sender.poll_delivery().receipt.outcome ==
           DeliveryOutcome::peer_not_ready);

    receiver.set_available(true);
    sender.drop_next_transmissions(1);
    EXPECT(sender.send(address(2), {frame.data(), frame.size()}, 1).accepted());
    sender.service(1);
    EXPECT(sender.poll_delivery().receipt.outcome ==
           DeliveryOutcome::injected_loss);
    EXPECT(sender.status().frames_delivery_failed == 2);
}

void test_receiver_and_completion_queues_are_bounded() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport receiver{};
    start_pair(sender, receiver);
    const std::array<std::uint8_t, 1> frame{0x11U};

    for (std::size_t index = 0;
         index < FakeEspNowTransport::kQueueCapacity;
         ++index) {
        EXPECT(sender.send(
                   address(2), {frame.data(), frame.size()}, index).accepted());
    }
    sender.service(FakeEspNowTransport::kQueueCapacity);
    EXPECT(receiver.status().receive_queue_depth ==
           FakeEspNowTransport::kQueueCapacity);
    EXPECT(sender.status().completion_queue_depth ==
           FakeEspNowTransport::kQueueCapacity);

    EXPECT(sender.send(address(2), {frame.data(), frame.size()}, 10).accepted());
    sender.service(10);
    EXPECT(sender.status().transmit_queue_depth == 1);
    EXPECT(sender.poll_delivery().has_receipt());
    sender.service(10);
    EXPECT(sender.status().transmit_queue_depth == 0);
    for (std::size_t index = 1;
         index < FakeEspNowTransport::kQueueCapacity;
         ++index) {
        const auto earlier = sender.poll_delivery();
        EXPECT(earlier.has_receipt());
        EXPECT(earlier.receipt.radio_delivered());
    }
    const auto final = sender.poll_delivery();
    EXPECT(final.has_receipt());
    EXPECT(final.receipt.outcome == DeliveryOutcome::receiver_queue_full);
}

void test_stop_clears_queues_without_claiming_delivery() {
    FakeEspNowTransport sender{};
    FakeEspNowTransport receiver{};
    start_pair(sender, receiver);
    const std::array<std::uint8_t, 1> frame{0x55U};
    EXPECT(sender.send(address(2), {frame.data(), frame.size()}, 0).accepted());
    sender.stop();
    EXPECT(sender.status().state == EspNowState::offline);
    EXPECT(sender.status().transmit_queue_depth == 0);
    EXPECT(sender.status().completion_queue_depth == 0);
    EXPECT(sender.status().peer_count == 0);
    EXPECT(sender.send(address(2), {frame.data(), frame.size()}, 0).error ==
           EspNowError::not_ready);
    EXPECT(receiver.status().receive_queue_depth == 0);
}

}  // namespace

int main() {
    test_identity_channel_start_and_status();
    test_peer_table_rules();
    test_encrypted_delivery_metadata_and_receipt();
    test_security_and_channel_rejection();
    test_buffer_and_queue_backpressure();
    test_delivery_failure_outcomes();
    test_receiver_and_completion_queues_are_bounded();
    test_stop_clears_queues_without_claiming_delivery();

    if (failures != 0) {
        std::cerr << failures << " ESP-NOW transport assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 ESP-NOW transport scenario groups\n";
    return EXIT_SUCCESS;
}
