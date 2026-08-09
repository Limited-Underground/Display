#include "fake_esp_now_transport.hpp"

#include <algorithm>

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

namespace test_support {
namespace {

bool valid_channel(std::uint8_t channel) {
    return channel >= 1 && channel <= 14;
}

}  // namespace

FakeEspNowTransport::FakeEspNowTransport(
    std::size_t mtu_bytes,
    std::uint64_t delivery_latency_ms)
    : mtu_bytes_(std::min(mtu_bytes, kMaximumEspNowPayloadBytes)),
      delivery_latency_ms_(delivery_latency_ms) {}

std::size_t FakeEspNowTransport::mtu() const {
    return mtu_bytes_;
}

EspNowStatus FakeEspNowTransport::status() const {
    EspNowState state = EspNowState::offline;
    if (faulted_) {
        state = EspNowState::fault;
    } else if (started_ && available_) {
        if (inbound_count_ != 0) {
            state = EspNowState::receiving;
        } else if (outbound_count_ != 0) {
            state = EspNowState::transmitting;
        } else {
            state = EspNowState::idle;
        }
    }
    return {
        state,
        last_error_,
        mtu_bytes_,
        peer_count_,
        outbound_count_,
        inbound_count_,
        receipt_count_,
        frames_accepted_,
        frames_radio_delivered_,
        frames_delivery_failed_,
        frames_received_};
}

EspNowError FakeEspNowTransport::start(
    const PeerAddress& local_address,
    EspNowPolicy policy) {
    if (started_) {
        return EspNowError::invalid_state;
    }
    if (!is_valid_unicast_address(local_address) ||
        !valid_channel(policy.channel) || mtu_bytes_ == 0) {
        return EspNowError::invalid_argument;
    }
    local_address_ = local_address;
    policy_ = policy;
    started_ = true;
    last_error_ = EspNowError::none;
    return EspNowError::none;
}

void FakeEspNowTransport::stop() {
    started_ = false;
    peers_ = {};
    peer_count_ = 0;
    outbound_ = {};
    outbound_head_ = 0;
    outbound_tail_ = 0;
    outbound_count_ = 0;
    inbound_ = {};
    inbound_head_ = 0;
    inbound_tail_ = 0;
    inbound_count_ = 0;
    receipts_ = {};
    receipt_head_ = 0;
    receipt_tail_ = 0;
    receipt_count_ = 0;
}

std::size_t FakeEspNowTransport::find_peer(
    const PeerAddress& address) const {
    for (std::size_t index = 0; index < peer_count_; ++index) {
        if (peer_address_equals(peers_[index].address, address)) {
            return index;
        }
    }
    return peers_.size();
}

EspNowError FakeEspNowTransport::add_peer(
    const PeerConfiguration& peer) {
    if (!started_) {
        return EspNowError::invalid_state;
    }
    if (!is_valid_unicast_address(peer.address) ||
        peer_address_equals(peer.address, local_address_) ||
        !valid_channel(peer.channel)) {
        return EspNowError::invalid_argument;
    }
    if (peer.channel != policy_.channel) {
        return EspNowError::channel_mismatch;
    }
    if (find_peer(peer.address) != peers_.size()) {
        return EspNowError::peer_already_exists;
    }
    if (peer_count_ == peers_.size()) {
        return EspNowError::peer_table_full;
    }
    peers_[peer_count_] = peer;
    ++peer_count_;
    return EspNowError::none;
}

EspNowError FakeEspNowTransport::remove_peer(
    const PeerAddress& address) {
    if (!started_) {
        return EspNowError::invalid_state;
    }
    const auto index = find_peer(address);
    if (index == peers_.size()) {
        return EspNowError::peer_not_found;
    }
    for (std::size_t move = index + 1; move < peer_count_; ++move) {
        peers_[move - 1] = peers_[move];
    }
    --peer_count_;
    peers_[peer_count_] = {};
    return EspNowError::none;
}

SendResult FakeEspNowTransport::send(
    const PeerAddress& destination,
    ByteView payload,
    std::uint64_t now_ms) {
    if (!started_ || !available_ || faulted_) {
        last_error_ = EspNowError::not_ready;
        return {last_error_, 0, 0};
    }
    if (payload.data == nullptr || payload.size == 0 ||
        !is_valid_unicast_address(destination)) {
        last_error_ = EspNowError::invalid_argument;
        return {last_error_, 0, 0};
    }
    if (payload.size > mtu_bytes_) {
        last_error_ = EspNowError::payload_too_large;
        return {last_error_, 0, 0};
    }
    const auto peer_index = find_peer(destination);
    if (peer_index == peers_.size()) {
        last_error_ = EspNowError::peer_not_found;
        return {last_error_, 0, 0};
    }
    const auto& peer = peers_[peer_index];
    if (policy_.require_encrypted_unicast && !peer.encrypted) {
        last_error_ = EspNowError::encryption_required;
        return {last_error_, 0, 0};
    }
    if (outbound_count_ == outbound_.size()) {
        last_error_ = EspNowError::queue_full;
        return {last_error_, 0, 0};
    }

    auto token = next_token_++;
    if (token == 0) {
        token = next_token_++;
    }
    auto& frame = outbound_[outbound_tail_];
    frame = {};
    frame.destination = destination;
    std::copy(
        payload.data,
        payload.data + payload.size,
        frame.bytes.begin());
    frame.size = payload.size;
    frame.due_at_ms = now_ms + delivery_latency_ms_;
    frame.token = token;
    frame.encrypted = peer.encrypted;
    outbound_tail_ = (outbound_tail_ + 1) % outbound_.size();
    ++outbound_count_;
    ++frames_accepted_;
    last_error_ = EspNowError::none;
    return {EspNowError::none, token, payload.size};
}

ReceiveResult FakeEspNowTransport::receive(
    MutableByteView destination) {
    if (inbound_count_ == 0) {
        return {EspNowError::no_data, 0, {}};
    }
    const auto& frame = inbound_[inbound_head_];
    if (destination.data == nullptr && destination.size != 0) {
        return {EspNowError::invalid_argument, 0, {}};
    }
    if (destination.size < frame.size) {
        return {
            EspNowError::buffer_too_small,
            frame.size,
            frame.metadata};
    }
    std::copy(
        frame.bytes.begin(),
        frame.bytes.begin() + frame.size,
        destination.data);
    const auto result = ReceiveResult{
        EspNowError::none,
        frame.size,
        frame.metadata};
    inbound_[inbound_head_] = {};
    inbound_head_ = (inbound_head_ + 1) % inbound_.size();
    --inbound_count_;
    return result;
}

DeliveryResult FakeEspNowTransport::poll_delivery() {
    if (receipt_count_ == 0) {
        return {EspNowError::no_data, {}};
    }
    const auto receipt = receipts_[receipt_head_];
    receipts_[receipt_head_] = {};
    receipt_head_ = (receipt_head_ + 1) % receipts_.size();
    --receipt_count_;
    return {EspNowError::none, receipt};
}

FakeEspNowTransport* FakeEspNowTransport::find_link(
    const PeerAddress& address) const {
    for (std::size_t index = 0; index < link_count_; ++index) {
        if (links_[index] != nullptr && links_[index]->started_ &&
            peer_address_equals(links_[index]->local_address_, address)) {
            return links_[index];
        }
    }
    return nullptr;
}

bool FakeEspNowTransport::accepts_source(
    const PeerAddress& source,
    bool encrypted) const {
    const auto peer_index = find_peer(source);
    if (peer_index == peers_.size()) {
        return false;
    }
    const auto& peer = peers_[peer_index];
    if (peer.channel != policy_.channel || peer.encrypted != encrypted) {
        return false;
    }
    return !policy_.require_encrypted_unicast || encrypted;
}

bool FakeEspNowTransport::push_inbound(const InboundFrame& frame) {
    if (inbound_count_ == inbound_.size()) {
        return false;
    }
    inbound_[inbound_tail_] = frame;
    inbound_tail_ = (inbound_tail_ + 1) % inbound_.size();
    ++inbound_count_;
    ++frames_received_;
    return true;
}

bool FakeEspNowTransport::push_receipt(
    const DeliveryReceipt& receipt) {
    if (receipt_count_ == receipts_.size()) {
        return false;
    }
    receipts_[receipt_tail_] = receipt;
    receipt_tail_ = (receipt_tail_ + 1) % receipts_.size();
    ++receipt_count_;
    return true;
}

void FakeEspNowTransport::pop_outbound() {
    outbound_[outbound_head_] = {};
    outbound_head_ = (outbound_head_ + 1) % outbound_.size();
    --outbound_count_;
}

void FakeEspNowTransport::service(std::uint64_t now_ms) {
    if (!started_ || !available_ || faulted_) {
        return;
    }
    while (outbound_count_ != 0 && receipt_count_ < receipts_.size()) {
        const auto& frame = outbound_[outbound_head_];
        if (now_ms < frame.due_at_ms) {
            break;
        }

        auto outcome = DeliveryOutcome::no_link;
        auto* peer = find_link(frame.destination);
        if (drop_count_ != 0) {
            --drop_count_;
            outcome = DeliveryOutcome::injected_loss;
        } else if (peer == nullptr) {
            outcome = DeliveryOutcome::no_link;
        } else if (!peer->available_ || peer->faulted_) {
            outcome = DeliveryOutcome::peer_not_ready;
        } else if (peer->policy_.channel != policy_.channel) {
            outcome = DeliveryOutcome::channel_mismatch;
        } else if (!peer->accepts_source(local_address_, frame.encrypted)) {
            outcome = DeliveryOutcome::receiver_rejected;
        } else {
            InboundFrame inbound{};
            inbound.bytes = frame.bytes;
            inbound.size = frame.size;
            inbound.metadata.source = local_address_;
            inbound.metadata.received_at_ms = now_ms;
            inbound.metadata.rssi_dbm = link_rssi_dbm_;
            inbound.metadata.rssi_valid = link_rssi_valid_;
            inbound.metadata.channel = policy_.channel;
            inbound.metadata.encrypted = frame.encrypted;
            outcome = peer->push_inbound(inbound)
                          ? DeliveryOutcome::delivered_to_peer_radio
                          : DeliveryOutcome::receiver_queue_full;
        }

        const DeliveryReceipt receipt{
            frame.token,
            frame.destination,
            outcome,
            now_ms};
        if (!push_receipt(receipt)) {
            break;
        }
        if (outcome == DeliveryOutcome::delivered_to_peer_radio) {
            ++frames_radio_delivered_;
        } else {
            ++frames_delivery_failed_;
        }
        pop_outbound();
    }
}

void FakeEspNowTransport::connect(FakeEspNowTransport& peer) {
    if (&peer == this) {
        return;
    }
    if (find_link(peer.local_address_) == nullptr &&
        link_count_ < links_.size()) {
        links_[link_count_++] = &peer;
    }
    if (peer.find_link(local_address_) == nullptr &&
        peer.link_count_ < peer.links_.size()) {
        peer.links_[peer.link_count_++] = this;
    }
}

void FakeEspNowTransport::set_available(bool available) {
    available_ = available;
}

void FakeEspNowTransport::set_faulted(bool faulted) {
    faulted_ = faulted;
}

void FakeEspNowTransport::set_link_rssi(
    std::int16_t rssi_dbm,
    bool valid) {
    link_rssi_dbm_ = rssi_dbm;
    link_rssi_valid_ = valid;
}

void FakeEspNowTransport::drop_next_transmissions(std::size_t count) {
    drop_count_ = count;
}

}  // namespace test_support
}  // namespace opengauge::wireless
