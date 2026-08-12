#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "opengauge/critical_alert_system_recovery_kv_storage.hpp"

namespace {

using namespace opengauge::identity;
using namespace opengauge::integration;

constexpr std::uint64_t kProducerId = 1;
constexpr std::uint64_t kConsumerId = 2;
constexpr std::uint32_t kPeerId = 10;
constexpr std::uint32_t kKeyHandle = 100;
constexpr std::uint32_t kBootSession = 500;
constexpr std::uint8_t kChannel = 6;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeKvBackend final : public CriticalAlertSystemRecoveryKvBackend {
public:
    CriticalAlertSystemRecoveryKvBackendError read_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        std::uint8_t* output,
        std::size_t capacity,
        std::size_t& actual_size) override {
        ++read_calls;
        const int slot = slot_for(key);
        if (!binding_matches(partition_label, namespace_name) || slot < 0 ||
            output == nullptr) {
            return CriticalAlertSystemRecoveryKvBackendError::invalid_argument;
        }
        if (fail_read_slot == slot) {
            fail_read_slot = -1;
            return CriticalAlertSystemRecoveryKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return CriticalAlertSystemRecoveryKvBackendError::not_found;
        }
        actual_size = sizes[slot];
        if (capacity < actual_size) {
            return CriticalAlertSystemRecoveryKvBackendError::invalid_argument;
        }
        std::copy(
            durable[slot].begin(),
            durable[slot].begin() + static_cast<std::ptrdiff_t>(actual_size),
            output);
        return CriticalAlertSystemRecoveryKvBackendError::none;
    }

    CriticalAlertSystemRecoveryKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        const int slot = slot_for(key);
        if (!binding_matches(partition_label, namespace_name) || slot < 0 ||
            data == nullptr ||
            size != kCriticalAlertSystemRecoveryCheckpointBytes) {
            return CriticalAlertSystemRecoveryKvBackendError::invalid_argument;
        }
        if (fail_write_slot == slot) {
            fail_write_slot = -1;
            return CriticalAlertSystemRecoveryKvBackendError::io_failure;
        }
        pending = Pending::write;
        pending_slot = slot;
        std::copy(data, data + size, pending_bytes.begin());
        return CriticalAlertSystemRecoveryKvBackendError::none;
    }

    CriticalAlertSystemRecoveryKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) override {
        ++erase_calls;
        const int slot = slot_for(key);
        if (!binding_matches(partition_label, namespace_name) || slot < 0) {
            return CriticalAlertSystemRecoveryKvBackendError::invalid_argument;
        }
        if (fail_erase_slot == slot) {
            fail_erase_slot = -1;
            return CriticalAlertSystemRecoveryKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return CriticalAlertSystemRecoveryKvBackendError::not_found;
        }
        pending = Pending::erase;
        pending_slot = slot;
        return CriticalAlertSystemRecoveryKvBackendError::none;
    }

    CriticalAlertSystemRecoveryKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) override {
        ++commit_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return CriticalAlertSystemRecoveryKvBackendError::invalid_argument;
        }
        if (fail_commit_call != 0 && commit_calls == fail_commit_call) {
            if (apply_then_fail) {
                apply_pending();
            } else {
                pending = Pending::none;
            }
            return CriticalAlertSystemRecoveryKvBackendError::io_failure;
        }
        apply_pending();
        return CriticalAlertSystemRecoveryKvBackendError::none;
    }

    void fail_commit(std::uint32_t call, bool apply_first) {
        commit_calls = 0;
        fail_commit_call = call;
        apply_then_fail = apply_first;
    }

    void clear_failure() {
        fail_read_slot = -1;
        fail_write_slot = -1;
        fail_erase_slot = -1;
        fail_commit_call = 0;
        apply_then_fail = false;
        commit_calls = 0;
        pending = Pending::none;
    }

    void seed(
        std::size_t slot,
        const std::array<
            std::uint8_t,
            kCriticalAlertSystemRecoveryCheckpointBytes>& bytes,
        std::size_t size = kCriticalAlertSystemRecoveryCheckpointBytes) {
        durable[slot] = bytes;
        sizes[slot] = size;
        present[slot] = true;
    }

    bool binding_matches(
        const char* partition_label,
        const char* namespace_name) {
        const bool exact =
            partition_label != nullptr && namespace_name != nullptr &&
            std::strcmp(
                partition_label,
                kCriticalAlertSystemRecoveryPartitionLabel) == 0 &&
            std::strcmp(
                namespace_name,
                kCriticalAlertSystemRecoveryNamespace) == 0;
        exact_binding = exact_binding && exact;
        return exact;
    }

    int slot_for(const char* key) const {
        if (key == nullptr) {
            return -1;
        }
        if (std::strcmp(key, kCriticalAlertSystemRecoverySlotAKey) == 0) {
            return 0;
        }
        if (std::strcmp(key, kCriticalAlertSystemRecoverySlotBKey) == 0) {
            return 1;
        }
        return -1;
    }

    enum class Pending : std::uint8_t {
        none = 0,
        write,
        erase,
    };

    void apply_pending() {
        if (pending == Pending::write) {
            durable[pending_slot] = pending_bytes;
            sizes[pending_slot] = kCriticalAlertSystemRecoveryCheckpointBytes;
            present[pending_slot] = true;
        } else if (pending == Pending::erase) {
            durable[pending_slot].fill(0);
            sizes[pending_slot] = 0;
            present[pending_slot] = false;
        }
        pending = Pending::none;
    }

    std::array<
        std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>,
        2> durable{};
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        pending_bytes{};
    std::array<bool, 2> present{};
    std::array<std::size_t, 2> sizes{};
    Pending pending{Pending::none};
    int pending_slot{-1};
    int fail_read_slot{-1};
    int fail_write_slot{-1};
    int fail_erase_slot{-1};
    std::uint32_t fail_commit_call{0};
    bool apply_then_fail{false};
    bool exact_binding{true};
    std::uint32_t read_calls{0};
    std::uint32_t write_calls{0};
    std::uint32_t erase_calls{0};
    std::uint32_t commit_calls{0};
};

PairingCandidate bridge(std::uint32_t request, std::uint32_t peer) {
    return {
        request,
        peer,
        PeerRole::trail_bridge,
        permission_bit(PeerPermission::receive_critical_alert) |
            permission_bit(PeerPermission::publish_alarm_ack),
        kChannel};
}

void approve(
    PeerAuthorizationRegistry& registry,
    std::uint32_t request,
    std::uint32_t peer,
    std::uint32_t key) {
    EXPECT(registry.begin_approval(bridge(request, peer), 0) ==
           PeerAuthorizationError::none);
    EXPECT(registry.approve(request, key, 1, 0) ==
           PeerAuthorizationError::none);
}

CriticalAlert alert() {
    CriticalAlert value{};
    value.type = CriticalAlertType::engine_over_temperature;
    value.severity = AlertSeverity::critical;
    value.state = AlertState::asserted;
    value.quality = AlertQuality::valid;
    value.unit = AlertUnit::celsius;
    value.producer_id = kProducerId;
    value.vehicle_id = 3;
    value.event_id = 7;
    value.condition_id = 70;
    value.age_ms = 10;
    value.value_milli = 110000;
    value.value_present = true;
    return value;
}

std::array<std::uint8_t, kCriticalAlertFrameBytes> alert_frame() {
    std::array<std::uint8_t, kCriticalAlertFrameBytes> output{};
    EXPECT(encode_critical_alert(alert(), output).encoded());
    return output;
}

std::array<std::uint8_t, kCriticalAlertAckFrameBytes> rejection_frame() {
    CriticalAlertAck value{};
    value.disposition = AlertAckDisposition::rejected;
    value.reason = AlertAckReason::rate_limited;
    value.state = AlertState::asserted;
    value.consumer_id = kConsumerId;
    value.producer_id = kProducerId;
    value.event_id = 7;
    value.condition_id = 70;
    value.consumer_boot_session_id = kBootSession;
    value.ack_sequence = 10;
    value.observed_alert_age_ms = 10;
    std::array<std::uint8_t, kCriticalAlertAckFrameBytes> output{};
    EXPECT(encode_critical_alert_ack(value, output).encoded());
    return output;
}

void start_source(
    PeerAuthorizationRegistry& registry,
    CriticalAlertOutbox& outbox,
    CriticalAlertAckIngress& ingress) {
    EXPECT(registry.start({1000}) == PeerAuthorizationError::none);
    approve(registry, 1, kPeerId, kKeyHandle);
    EXPECT(outbox.start({50, 100, 25, 10000, 3, 1}) ==
           CriticalOutboxError::none);
    EXPECT(ingress.start({kProducerId, 1000}, registry, outbox) ==
           CriticalAlertAckIngressError::none);
    EXPECT(ingress.bind_consumer_session(
               kPeerId, kConsumerId, kBootSession) ==
           CriticalAlertAckIngressError::none);
    EXPECT(outbox.enqueue(alert_frame(), 0) == CriticalOutboxError::none);
    const auto prepared = outbox.prepare(0);
    EXPECT(prepared.prepared());
    EXPECT(outbox.commit_local_send(prepared.token, true, 0) ==
           CriticalOutboxError::none);
    const auto ack = rejection_frame();
    const CriticalAlertAckTransportContext transport{
        true, kPeerId, kKeyHandle, kChannel};
    EXPECT(ingress.receive(
               ack.data(), ack.size(), transport, 1).retry_released);
}

void test_fixed_binding_and_public_arguments() {
    EXPECT(std::strcmp(kCriticalAlertSystemRecoveryPartitionLabel,
                       "og_state") == 0);
    EXPECT(std::strcmp(kCriticalAlertSystemRecoveryNamespace,
                       "og_recovery") == 0);
    EXPECT(std::strcmp(kCriticalAlertSystemRecoverySlotAKey, "ors0_a") == 0);
    EXPECT(std::strcmp(kCriticalAlertSystemRecoverySlotBKey, "ors0_b") == 0);

    FakeKvBackend backend;
    CriticalAlertSystemRecoveryKvStorage storage(backend);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        bytes{};
    EXPECT(storage.read_slot(2, bytes.data(), bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::invalid_argument);
    EXPECT(storage.read_slot(0, nullptr, bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::invalid_argument);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size() - 1) ==
           CriticalAlertSystemRecoveryStorageError::invalid_argument);
    EXPECT(storage.erase_slot(2) ==
           CriticalAlertSystemRecoveryStorageError::invalid_argument);
    EXPECT(backend.read_calls == 0 && backend.write_calls == 0 &&
           backend.erase_calls == 0 && backend.commit_calls == 0);
}

void test_missing_present_wrong_size_and_failed_reads() {
    FakeKvBackend backend;
    CriticalAlertSystemRecoveryKvStorage storage(backend);
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        expected{};
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        output{};
    expected.fill(0x5AU);

    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           CriticalAlertSystemRecoveryStorageError::not_found);
    backend.seed(0, expected);
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           CriticalAlertSystemRecoveryStorageError::none);
    EXPECT(output == expected);
    backend.seed(1, expected, expected.size() - 1);
    EXPECT(storage.read_slot(1, output.data(), output.size()) ==
           CriticalAlertSystemRecoveryStorageError::io_failure);
    backend.fail_read_slot = 0;
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           CriticalAlertSystemRecoveryStorageError::io_failure);
    EXPECT(backend.exact_binding);
}

void test_writes_require_backend_commit() {
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        bytes{};
    bytes.fill(0x33U);

    FakeKvBackend backend;
    CriticalAlertSystemRecoveryKvStorage storage(backend);
    EXPECT(storage.write_slot(1, bytes.data(), bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::none);
    EXPECT(backend.present[1]);
    EXPECT(backend.durable[1] == bytes);
    EXPECT(backend.write_calls == 1 && backend.commit_calls == 1);

    backend.fail_write_slot = 0;
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::io_failure);
    backend.fail_commit(1, false);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           CriticalAlertSystemRecoveryStorageError::io_failure);
    EXPECT(!backend.present[0]);
}

void test_erase_is_committed_and_missing_is_idempotent() {
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        bytes{};
    bytes.fill(0xA5U);
    FakeKvBackend backend;
    backend.seed(0, bytes);
    CriticalAlertSystemRecoveryKvStorage storage(backend);

    EXPECT(storage.erase_slot(0) ==
           CriticalAlertSystemRecoveryStorageError::none);
    EXPECT(!backend.present[0]);
    const auto committed = backend.commit_calls;
    EXPECT(storage.erase_slot(0) ==
           CriticalAlertSystemRecoveryStorageError::none);
    EXPECT(backend.commit_calls == committed);
    backend.fail_erase_slot = 1;
    EXPECT(storage.erase_slot(1) ==
           CriticalAlertSystemRecoveryStorageError::io_failure);
}

void test_real_store_saves_and_rotates_exact_records() {
    FakeKvBackend backend;
    CriticalAlertSystemRecoveryKvStorage storage(backend);
    CriticalAlertSystemRecoveryStore store(storage);
    PeerAuthorizationRegistry authorization{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_source(authorization, outbox, ingress);

    const auto first = store.save_next(authorization, ingress, outbox, 1);
    const auto second = store.save_next(authorization, ingress, outbox, 2);

    EXPECT(first.saved());
    EXPECT(first.generation == 1);
    EXPECT(first.written_slot == CriticalAlertSystemRecoverySource::slot_a);
    EXPECT(second.saved());
    EXPECT(second.generation == 2);
    EXPECT(second.written_slot == CriticalAlertSystemRecoverySource::slot_b);
    EXPECT(backend.present[0] && backend.present[1]);
    EXPECT(backend.exact_binding);
}

void test_applied_then_failed_commit_is_discovered_after_restart() {
    FakeKvBackend backend;
    backend.fail_commit(1, true);
    CriticalAlertSystemRecoveryKvStorage storage(backend);
    CriticalAlertSystemRecoveryStore store(storage);
    PeerAuthorizationRegistry authorization{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_source(authorization, outbox, ingress);

    const auto uncertain =
        store.save_next(authorization, ingress, outbox, 1);
    EXPECT(uncertain.error ==
           CriticalAlertSystemRecoveryStoreError::storage_failure);
    EXPECT(uncertain.commit_uncertain);
    EXPECT(backend.present[0]);

    backend.clear_failure();
    CriticalAlertSystemRecoveryKvStorage restarted_storage(backend);
    CriticalAlertSystemRecoveryStore restarted_store(restarted_storage);
    const auto inspected = restarted_store.inspect();
    EXPECT(inspected.checkpoint_available);
    EXPECT(inspected.generation == 1);
    EXPECT(inspected.source == CriticalAlertSystemRecoverySource::slot_a);
}

void test_unapplied_failed_commit_remains_empty_after_restart() {
    FakeKvBackend backend;
    backend.fail_commit(1, false);
    CriticalAlertSystemRecoveryKvStorage storage(backend);
    CriticalAlertSystemRecoveryStore store(storage);
    PeerAuthorizationRegistry authorization{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_source(authorization, outbox, ingress);

    const auto uncertain =
        store.save_next(authorization, ingress, outbox, 1);
    EXPECT(uncertain.error ==
           CriticalAlertSystemRecoveryStoreError::storage_failure);
    EXPECT(uncertain.commit_uncertain);
    EXPECT(!backend.present[0]);

    backend.clear_failure();
    CriticalAlertSystemRecoveryKvStorage restarted_storage(backend);
    CriticalAlertSystemRecoveryStore restarted_store(restarted_storage);
    const auto inspected = restarted_store.inspect();
    EXPECT(inspected.error ==
           CriticalAlertSystemRecoveryStoreError::no_checkpoint);
    EXPECT(!inspected.checkpoint_available);
}

void test_real_store_reset_erases_both_keys() {
    FakeKvBackend backend;
    CriticalAlertSystemRecoveryKvStorage storage(backend);
    CriticalAlertSystemRecoveryStore store(storage);
    PeerAuthorizationRegistry authorization{};
    CriticalAlertOutbox outbox{};
    CriticalAlertAckIngress ingress{};
    start_source(authorization, outbox, ingress);
    EXPECT(store.save_next(authorization, ingress, outbox, 1).saved());
    EXPECT(store.save_next(authorization, ingress, outbox, 2).saved());

    EXPECT(store.reset() == CriticalAlertSystemRecoveryStoreError::none);
    EXPECT(!backend.present[0]);
    EXPECT(!backend.present[1]);
}

void test_commit_failure_after_erase_remains_visible() {
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        bytes{};
    bytes.fill(0xC3U);
    FakeKvBackend backend;
    backend.seed(0, bytes);
    backend.fail_commit(1, true);
    CriticalAlertSystemRecoveryKvStorage storage(backend);

    EXPECT(storage.erase_slot(0) ==
           CriticalAlertSystemRecoveryStorageError::io_failure);
    EXPECT(!backend.present[0]);
    backend.clear_failure();
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>
        output{};
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           CriticalAlertSystemRecoveryStorageError::not_found);
}

}  // namespace

int main() {
    test_fixed_binding_and_public_arguments();
    test_missing_present_wrong_size_and_failed_reads();
    test_writes_require_backend_commit();
    test_erase_is_committed_and_missing_is_idempotent();
    test_real_store_saves_and_rotates_exact_records();
    test_applied_then_failed_commit_is_discovered_after_restart();
    test_unapplied_failed_commit_remains_empty_after_restart();
    test_real_store_reset_erases_both_keys();
    test_commit_failure_after_erase_remains_visible();

    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 9 system recovery key/value storage groups\n";
    return EXIT_SUCCESS;
}
