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

PairingCandidate gauge_candidate(
    std::uint32_t request,
    std::uint32_t peer) {
    return {
        request,
        peer,
        PeerRole::gauge,
        permission_bit(PeerPermission::receive_telemetry) |
            permission_bit(PeerPermission::publish_alarm_ack),
        6};
}

void approve_gauge(
    PeerAuthorizationRegistry& registry,
    std::uint32_t request,
    std::uint32_t peer,
    std::uint32_t key,
    std::uint64_t now = 0) {
    EXPECT(registry.begin_approval(
               gauge_candidate(request, peer), now) ==
           PeerAuthorizationError::none);
    EXPECT(registry.approve(request, key, 1, now) ==
           PeerAuthorizationError::none);
}

void test_lifecycle_and_configuration() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({0}) ==
           PeerAuthorizationError::invalid_configuration);
    EXPECT(registry.begin_approval(gauge_candidate(1, 1), 0) ==
           PeerAuthorizationError::invalid_state);
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    EXPECT(registry.start({1000}) == PeerAuthorizationError::invalid_state);
    EXPECT(registry.status().running);
    EXPECT(registry.status().peer_count == 0);
    registry.stop();
    EXPECT(registry.service(0) == PeerAuthorizationError::invalid_state);
}

void test_candidate_role_permission_and_channel_validation() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    auto candidate = gauge_candidate(0, 1);
    EXPECT(registry.begin_approval(candidate, 0) ==
           PeerAuthorizationError::invalid_candidate);
    candidate = gauge_candidate(1, 1);
    candidate.requested_permissions =
        permission_bit(PeerPermission::publish_gps);
    EXPECT(registry.begin_approval(candidate, 0) ==
           PeerAuthorizationError::invalid_candidate);
    candidate = gauge_candidate(1, 1);
    candidate.channel = 15;
    EXPECT(registry.begin_approval(candidate, 0) ==
           PeerAuthorizationError::invalid_candidate);
    candidate = gauge_candidate(1, 1);
    candidate.role = PeerRole::gps;
    candidate.requested_permissions =
        permission_bit(PeerPermission::publish_gps);
    EXPECT(registry.begin_approval(candidate, 0) ==
           PeerAuthorizationError::none);
}

void test_single_pending_window_and_exact_expiration() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({100}) == PeerAuthorizationError::none);
    EXPECT(registry.begin_approval(gauge_candidate(1, 10), 1000) ==
           PeerAuthorizationError::none);
    EXPECT(registry.begin_approval(gauge_candidate(2, 11), 1000) ==
           PeerAuthorizationError::approval_pending);
    EXPECT(registry.service(999) ==
           PeerAuthorizationError::clock_regression);
    EXPECT(registry.service(1099) == PeerAuthorizationError::none);
    EXPECT(registry.approve(1, 100, 1, 1100) ==
           PeerAuthorizationError::approval_expired);
    EXPECT(!registry.status().approval_pending);
    EXPECT(registry.status().approvals_expired == 1);
    EXPECT(registry.cancel_approval() ==
           PeerAuthorizationError::approval_not_found);
}

void test_approval_and_role_scoped_authorization() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve_gauge(registry, 1, 10, 100);
    EXPECT(registry.authorize(
               10, 100, 6, PeerPermission::receive_telemetry).authorized);
    EXPECT(registry.authorize(
               10, 100, 6, PeerPermission::publish_gps).error ==
           PeerAuthorizationError::permission_denied);
    EXPECT(registry.authorize(
               10, 101, 6, PeerPermission::receive_telemetry).error ==
           PeerAuthorizationError::key_mismatch);
    EXPECT(registry.authorize(
               10, 100, 7, PeerPermission::receive_telemetry).error ==
           PeerAuthorizationError::channel_mismatch);
    EXPECT(registry.authorize(
               99, 100, 6, PeerPermission::receive_telemetry).error ==
           PeerAuthorizationError::unknown_peer);
    EXPECT(registry.status().authorization_denials == 4);

    const PairingCandidate bridge{
        2,
        20,
        PeerRole::trail_bridge,
        permission_bit(PeerPermission::receive_critical_alert) |
            permission_bit(PeerPermission::publish_alarm_ack),
        6};
    EXPECT(registry.begin_approval(bridge, 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.approve(2, 200, 1, 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.authorize(
               20, 200, 6, PeerPermission::publish_alarm_ack).authorized);
}

void test_capacity_duplicate_peer_and_key_handle() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    for (std::uint32_t index = 1; index <= kMaximumAuthorizedPeers; ++index) {
        approve_gauge(registry, index, index, 100 + index);
    }
    EXPECT(registry.begin_approval(gauge_candidate(20, 1), 0) ==
           PeerAuthorizationError::duplicate_peer);
    EXPECT(registry.begin_approval(gauge_candidate(20, 20), 0) ==
           PeerAuthorizationError::capacity_full);

    PeerAuthorizationRegistry duplicate_key{};
    EXPECT(duplicate_key.start({1000}) == PeerAuthorizationError::none);
    approve_gauge(duplicate_key, 1, 1, 100);
    EXPECT(duplicate_key.begin_approval(gauge_candidate(2, 2), 0) ==
           PeerAuthorizationError::none);
    EXPECT(duplicate_key.approve(2, 100, 1, 0) ==
           PeerAuthorizationError::duplicate_key_handle);
}

void test_revoke_forget_and_replacement() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve_gauge(registry, 1, 10, 100);
    EXPECT(registry.forget_revoked(10) ==
           PeerAuthorizationError::invalid_state);
    EXPECT(registry.revoke(10) == PeerAuthorizationError::none);
    EXPECT(registry.authorize(
               10, 100, 6, PeerPermission::receive_telemetry).error ==
           PeerAuthorizationError::peer_revoked);
    EXPECT(registry.revoke(10) == PeerAuthorizationError::peer_revoked);
    EXPECT(registry.forget_revoked(10) == PeerAuthorizationError::none);
    EXPECT(registry.status().peer_count == 0);
    EXPECT(registry.status().revocations == 1);
    approve_gauge(registry, 2, 10, 200);
    EXPECT(registry.authorize(
               10, 200, 6, PeerPermission::receive_telemetry).authorized);
}

void test_key_rotation_requires_new_epoch_and_unique_handle() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve_gauge(registry, 1, 10, 100);
    approve_gauge(registry, 2, 20, 200);
    EXPECT(registry.rotate_key(10, 300, 1) ==
           PeerAuthorizationError::stale_authorization_epoch);
    EXPECT(registry.rotate_key(10, 200, 2) ==
           PeerAuthorizationError::duplicate_key_handle);
    EXPECT(registry.rotate_key(10, 300, 2) ==
           PeerAuthorizationError::none);
    EXPECT(registry.authorize(
               10, 100, 6, PeerPermission::receive_telemetry).error ==
           PeerAuthorizationError::key_mismatch);
    EXPECT(registry.authorize(
               10, 300, 6, PeerPermission::receive_telemetry).authorized);
    EXPECT(registry.status().key_rotations == 1);
}

void test_atomic_snapshot_and_no_raw_key_storage_shape() {
    PeerAuthorizationRegistry registry{};
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve_gauge(registry, 1, 10, 100);
    approve_gauge(registry, 2, 20, 200);
    std::array<PeerAuthorizationEntry, 2> entries{};
    entries[0].logical_peer_id = 99;
    std::size_t count = 77;
    EXPECT(registry.snapshot(entries.data(), 1, count) ==
           PeerAuthorizationError::insufficient_output_capacity);
    EXPECT(entries[0].logical_peer_id == 99);
    EXPECT(count == 77);
    EXPECT(registry.snapshot(entries.data(), entries.size(), count) ==
           PeerAuthorizationError::none);
    EXPECT(count == 2);
    EXPECT(entries[0].secure_key_handle == 100);
    EXPECT(entries[1].secure_key_handle == 200);
    EXPECT(sizeof(PeerAuthorizationEntry) <= 24);
}

}  // namespace

int main() {
    test_lifecycle_and_configuration();
    test_candidate_role_permission_and_channel_validation();
    test_single_pending_window_and_exact_expiration();
    test_approval_and_role_scoped_authorization();
    test_capacity_duplicate_peer_and_key_handle();
    test_revoke_forget_and_replacement();
    test_key_rotation_requires_new_epoch_and_unique_handle();
    test_atomic_snapshot_and_no_raw_key_storage_shape();

    if (failures != 0) {
        std::cerr << failures << " peer authorization assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 8 peer authorization scenario groups\n";
    return EXIT_SUCCESS;
}
