#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "opengauge/peer_authorization.hpp"

namespace {

using namespace opengauge::identity;
int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

PairingCandidate gauge(std::uint32_t request, std::uint32_t peer) {
    return {request, peer, PeerRole::gauge,
            permission_bit(PeerPermission::receive_telemetry) |
                permission_bit(PeerPermission::publish_alarm_ack), 6};
}

void approve(PeerAuthorizationRegistry& registry, std::uint32_t request,
             std::uint32_t peer, std::uint32_t key, std::uint32_t epoch = 1) {
    EXPECT(registry.begin_approval(gauge(request, peer), 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.approve(request, key, epoch, 0) ==
           PeerAuthorizationError::none);
}

void write_u32(std::uint8_t* output, std::uint32_t value) {
    for (std::size_t index = 0; index < 4; ++index)
        output[index] = static_cast<std::uint8_t>(value >> (index * 8U));
}
void repair_crc(
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes>& bytes) {
    write_u32(bytes.data() + bytes.size() - 4,
              peer_authorization_checkpoint_crc32(
                  bytes.data(), bytes.size() - 4));
}

std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes> populated() {
    PeerAuthorizationRegistry source{};
    EXPECT(source.start({1000}) == PeerAuthorizationError::none);
    approve(source, 1, 10, 100);
    approve(source, 2, 20, 200, 2);
    EXPECT(source.rotate_key(20, 201, 3) == PeerAuthorizationError::none);
    EXPECT(source.revoke(10) == PeerAuthorizationError::none);
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes> output{};
    EXPECT(source.export_checkpoint(output) == PeerAuthorizationError::none);
    return output;
}

void test_round_trip_preserves_active_revoked_and_epoch() {
    const auto encoded = populated();
    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    EXPECT(restored.import_checkpoint(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::none);
    EXPECT(restored.status().peer_count == 2);
    EXPECT(restored.status().active_peer_count == 1);
    EXPECT(restored.authorize(
               10, 100, 6, PeerPermission::receive_telemetry).error ==
           PeerAuthorizationError::peer_revoked);
    EXPECT(restored.authorize(
               20, 201, 6, PeerPermission::receive_telemetry).authorized);
    std::array<PeerAuthorizationEntry, 2> entries{};
    std::size_t count = 0;
    EXPECT(restored.snapshot(entries.data(), entries.size(), count) ==
           PeerAuthorizationError::none);
    EXPECT(!entries[0].active && entries[0].secure_key_handle == 0 &&
           entries[0].authorization_epoch == 1);
    EXPECT(entries[1].active && entries[1].secure_key_handle == 201 &&
           entries[1].authorization_epoch == 3);
}

void test_empty_layout_and_determinism() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({0x0102030405060708ULL}) ==
           PeerAuthorizationError::none);
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes> first{};
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes> second{};
    EXPECT(registry.export_checkpoint(first) == PeerAuthorizationError::none);
    EXPECT(registry.export_checkpoint(second) == PeerAuthorizationError::none);
    EXPECT(first == second);
    EXPECT(first[0] == 'O' && first[1] == 'P' && first[2] == 'A' &&
           first[3] == '0' && first[4] == 0 && first[5] == 0);
    EXPECT(first[8] == 0x08 && first[15] == 0x01);
    for (std::size_t index = 24; index < first.size() - 4; ++index)
        EXPECT(first[index] == 0);
}

void test_pending_and_stopped_export_preserve_output() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes> output{};
    output.fill(0xA5);
    const auto unchanged = output;
    EXPECT(registry.begin_approval(gauge(1, 10), 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.export_checkpoint(output) ==
           PeerAuthorizationError::invalid_state);
    registry.stop();
    EXPECT(registry.export_checkpoint(output) ==
           PeerAuthorizationError::invalid_state);
    EXPECT(output == unchanged);
}

void test_shape_version_configuration_crc_and_atomicity() {
    const auto encoded = populated();
    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    EXPECT(restored.import_checkpoint(nullptr, encoded.size()) ==
           PeerAuthorizationError::checkpoint_malformed);
    EXPECT(restored.status().peer_count == 0);
    auto corrupt = encoded;
    corrupt[100] ^= 1;
    EXPECT(restored.import_checkpoint(corrupt.data(), corrupt.size()) ==
           PeerAuthorizationError::checkpoint_integrity_failure);
    EXPECT(restored.status().peer_count == 0);
    PeerAuthorizationRegistry incompatible{};
    EXPECT(incompatible.start({1001}) == PeerAuthorizationError::none);
    EXPECT(incompatible.import_checkpoint(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::checkpoint_incompatible);
    auto version = encoded;
    version[4] = 1;
    EXPECT(restored.import_checkpoint(version.data(), version.size()) ==
           PeerAuthorizationError::checkpoint_incompatible);
}

void test_noncanonical_padding_and_count_are_rejected() {
    auto encoded = populated();
    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    encoded[16] = 1;
    repair_crc(encoded);
    EXPECT(restored.import_checkpoint(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::checkpoint_malformed);
    encoded = populated();
    encoded[6] = 2;
    repair_crc(encoded);
    EXPECT(restored.import_checkpoint(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::checkpoint_malformed);
    EXPECT(restored.status().peer_count == 0);
}

void test_duplicate_and_entry_invariants_are_rejected() {
    auto encoded = populated();
    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    // Second entry gets the first entry's logical peer ID.
    std::copy(encoded.begin() + 24 + 8, encoded.begin() + 24 + 12,
              encoded.begin() + 48 + 8);
    repair_crc(encoded);
    EXPECT(restored.import_checkpoint(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::checkpoint_malformed);
    encoded = populated();
    encoded[24 + 12] = 1;  // Revoked entry cannot retain a key handle.
    repair_crc(encoded);
    EXPECT(restored.import_checkpoint(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::checkpoint_malformed);
}

void test_import_is_boot_only_and_validation_is_nonmutating() {
    const auto encoded = populated();
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    EXPECT(registry.validate_checkpoint_import(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::none);
    EXPECT(registry.status().peer_count == 0);
    approve(registry, 1, 99, 999);
    EXPECT(registry.import_checkpoint(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::invalid_state);
    EXPECT(registry.status().peer_count == 1);
}

void test_full_capacity_round_trip() {
    PeerAuthorizationRegistry source{};
    EXPECT(source.start({1000}) == PeerAuthorizationError::none);
    for (std::uint32_t index = 1; index <= kMaximumAuthorizedPeers; ++index)
        approve(source, index, index, 100 + index, index);
    std::array<std::uint8_t, kPeerAuthorizationCheckpointBytes> encoded{};
    EXPECT(source.export_checkpoint(encoded) == PeerAuthorizationError::none);
    PeerAuthorizationRegistry restored{};
    EXPECT(restored.start({1000}) == PeerAuthorizationError::none);
    EXPECT(restored.import_checkpoint(encoded.data(), encoded.size()) ==
           PeerAuthorizationError::none);
    EXPECT(restored.status().peer_count == kMaximumAuthorizedPeers);
    EXPECT(restored.status().active_peer_count == kMaximumAuthorizedPeers);
}

}  // namespace

int main() {
    test_round_trip_preserves_active_revoked_and_epoch();
    test_empty_layout_and_determinism();
    test_pending_and_stopped_export_preserve_output();
    test_shape_version_configuration_crc_and_atomicity();
    test_noncanonical_padding_and_count_are_rejected();
    test_duplicate_and_entry_invariants_are_rejected();
    test_import_is_boot_only_and_validation_is_nonmutating();
    test_full_capacity_round_trip();
    if (failures != 0) {
        std::cerr << failures << " peer checkpoint assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 peer authorization checkpoint scenario groups\n";
    return EXIT_SUCCESS;
}
