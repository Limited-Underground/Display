#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

#include "opengauge/gauge_layout_kv_storage.hpp"

namespace {

using namespace opengauge;
using namespace opengauge::configuration;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

class FakeKvBackend final : public GaugeLayoutKvBackend {
public:
    GaugeLayoutKvBackendError read_blob(
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
            return GaugeLayoutKvBackendError::invalid_argument;
        }
        if (fail_read_slot == slot) {
            fail_read_slot = -1;
            return GaugeLayoutKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return GaugeLayoutKvBackendError::not_found;
        }
        actual_size = sizes[slot];
        if (capacity < actual_size) {
            return GaugeLayoutKvBackendError::invalid_argument;
        }
        std::copy(
            durable[slot].begin(),
            durable[slot].begin() +
                static_cast<std::ptrdiff_t>(actual_size),
            output);
        return GaugeLayoutKvBackendError::none;
    }

    GaugeLayoutKvBackendError write_blob(
        const char* partition_label,
        const char* namespace_name,
        const char* key,
        const std::uint8_t* data,
        std::size_t size) override {
        ++write_calls;
        const int slot = slot_for(key);
        if (!binding_matches(partition_label, namespace_name) || slot < 0 ||
            data == nullptr || size != kGaugeLayoutRecordBytes) {
            return GaugeLayoutKvBackendError::invalid_argument;
        }
        if (fail_write_slot == slot) {
            fail_write_slot = -1;
            return GaugeLayoutKvBackendError::io_failure;
        }
        pending = Pending::write;
        pending_slot = slot;
        std::copy(data, data + size, pending_bytes.begin());
        return GaugeLayoutKvBackendError::none;
    }

    GaugeLayoutKvBackendError erase_key(
        const char* partition_label,
        const char* namespace_name,
        const char* key) override {
        ++erase_calls;
        const int slot = slot_for(key);
        if (!binding_matches(partition_label, namespace_name) || slot < 0) {
            return GaugeLayoutKvBackendError::invalid_argument;
        }
        if (fail_erase_slot == slot) {
            fail_erase_slot = -1;
            return GaugeLayoutKvBackendError::io_failure;
        }
        if (!present[slot]) {
            return GaugeLayoutKvBackendError::not_found;
        }
        pending = Pending::erase;
        pending_slot = slot;
        return GaugeLayoutKvBackendError::none;
    }

    GaugeLayoutKvBackendError commit(
        const char* partition_label,
        const char* namespace_name) override {
        ++commit_calls;
        if (!binding_matches(partition_label, namespace_name)) {
            return GaugeLayoutKvBackendError::invalid_argument;
        }
        if (fail_commit_call != 0 && commit_calls == fail_commit_call) {
            if (apply_then_fail) {
                apply_pending();
            } else {
                pending = Pending::none;
            }
            return GaugeLayoutKvBackendError::io_failure;
        }
        apply_pending();
        return GaugeLayoutKvBackendError::none;
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
        const std::array<std::uint8_t, kGaugeLayoutRecordBytes>& bytes,
        std::size_t size = kGaugeLayoutRecordBytes) {
        durable[slot] = bytes;
        sizes[slot] = size;
        present[slot] = true;
    }

    bool binding_matches(
        const char* partition_label,
        const char* namespace_name) {
        const bool exact =
            partition_label != nullptr && namespace_name != nullptr &&
            std::strcmp(partition_label, kGaugeLayoutPartitionLabel) == 0 &&
            std::strcmp(namespace_name, kGaugeLayoutNamespace) == 0;
        exact_binding = exact_binding && exact;
        return exact;
    }

    int slot_for(const char* key) const {
        if (key == nullptr) {
            return -1;
        }
        if (std::strcmp(key, kGaugeLayoutSlotAKey) == 0) {
            return 0;
        }
        if (std::strcmp(key, kGaugeLayoutSlotBKey) == 0) {
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
            sizes[pending_slot] = kGaugeLayoutRecordBytes;
            present[pending_slot] = true;
        } else if (pending == Pending::erase) {
            durable[pending_slot].fill(0);
            sizes[pending_slot] = 0;
            present[pending_slot] = false;
        }
        pending = Pending::none;
    }

    std::array<std::array<std::uint8_t, kGaugeLayoutRecordBytes>, 2> durable{};
    std::array<std::uint8_t, kGaugeLayoutRecordBytes> pending_bytes{};
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

display::GaugeWidgetConfiguration widget(
    std::uint16_t id,
    wireless::TelemetrySignalCode code,
    display::GaugeWidgetKind kind,
    std::string_view label) {
    display::GaugeWidgetConfiguration result{};
    result.widget_id = id;
    result.signal_code = code;
    result.kind = kind;
    EXPECT(display::make_gauge_widget_label(label, result.label) ==
           display::GaugeViewModelError::none);
    result.stale_after_ms = 1500;
    if (kind == display::GaugeWidgetKind::needle ||
        kind == display::GaugeWidgetKind::bar) {
        result.scale_min_raw = -100;
        result.scale_max_raw = 5000000;
    }
    return result;
}

GaugeLayout layout(std::uint64_t generation) {
    GaugeLayout result{};
    result.generation = generation;
    result.layout_id = 0x10203040U;
    result.brightness_percent = 65;
    result.theme = GaugeTheme::high_contrast;
    result.widget_count = 2;
    result.widgets[0] = widget(
        7, wireless::TelemetrySignalCode::engine_speed,
        display::GaugeWidgetKind::needle, "Engine RPM");
    result.widgets[1] = widget(
        8, wireless::TelemetrySignalCode::engine_coolant_temperature,
        display::GaugeWidgetKind::numeric, "Coolant");
    return result;
}

std::array<std::uint8_t, kGaugeLayoutRecordBytes> encoded_layout(
    std::uint64_t generation) {
    std::array<std::uint8_t, kGaugeLayoutRecordBytes> bytes{};
    EXPECT(encode_gauge_layout(
               layout(generation), bytes.data(), bytes.size()).succeeded());
    return bytes;
}

void test_fixed_binding_and_invalid_arguments() {
    EXPECT(std::strcmp(kGaugeLayoutPartitionLabel, "og_config") == 0);
    EXPECT(std::strcmp(kGaugeLayoutNamespace, "gauge_layout") == 0);
    EXPECT(std::strcmp(kGaugeLayoutSlotAKey, "ogl0_a") == 0);
    EXPECT(std::strcmp(kGaugeLayoutSlotBKey, "ogl0_b") == 0);

    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);
    std::array<std::uint8_t, kGaugeLayoutRecordBytes> bytes{};
    EXPECT(storage.read_slot(2, bytes.data(), bytes.size()) ==
           LayoutStorageError::invalid_argument);
    EXPECT(storage.read_slot(0, nullptr, bytes.size()) ==
           LayoutStorageError::invalid_argument);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size() - 1) ==
           LayoutStorageError::invalid_argument);
    EXPECT(storage.erase_slot(2) == LayoutStorageError::invalid_argument);
    EXPECT(backend.read_calls == 0 && backend.write_calls == 0 &&
           backend.erase_calls == 0 && backend.commit_calls == 0);
}

void test_exact_missing_wrong_size_and_failed_reads() {
    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);
    const auto expected = encoded_layout(1);
    std::array<std::uint8_t, kGaugeLayoutRecordBytes> output{};

    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           LayoutStorageError::not_found);
    backend.seed(0, expected);
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           LayoutStorageError::none);
    EXPECT(output == expected);
    backend.seed(1, expected, expected.size() - 1);
    EXPECT(storage.read_slot(1, output.data(), output.size()) ==
           LayoutStorageError::io_failure);
    backend.fail_read_slot = 0;
    EXPECT(storage.read_slot(0, output.data(), output.size()) ==
           LayoutStorageError::io_failure);
    EXPECT(backend.exact_binding);
}

void test_write_and_erase_require_commit() {
    const auto bytes = encoded_layout(1);
    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);

    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           LayoutStorageError::none);
    EXPECT(backend.present[0] && backend.durable[0] == bytes);
    EXPECT(backend.write_calls == 1 && backend.commit_calls == 1);
    EXPECT(storage.erase_slot(0) == LayoutStorageError::none);
    EXPECT(!backend.present[0] && backend.commit_calls == 2);
    EXPECT(storage.erase_slot(0) == LayoutStorageError::none);
    EXPECT(backend.commit_calls == 2);
}

void test_backend_failures_are_mapped() {
    const auto bytes = encoded_layout(1);
    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);

    backend.fail_write_slot = 0;
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           LayoutStorageError::io_failure);
    backend.fail_commit(1, false);
    EXPECT(storage.write_slot(0, bytes.data(), bytes.size()) ==
           LayoutStorageError::commit_uncertain);
    EXPECT(!backend.present[0]);
    backend.clear_failure();
    backend.fail_erase_slot = 0;
    EXPECT(storage.erase_slot(0) == LayoutStorageError::io_failure);
}

void test_real_store_rotates_and_restarts() {
    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);
    GaugeLayoutStore store(storage);
    EXPECT(store.save(layout(1)).written_slot == GaugeLayoutSource::slot_a);
    EXPECT(store.save(layout(2)).written_slot == GaugeLayoutSource::slot_b);
    EXPECT(backend.present[0] && backend.present[1]);

    GaugeLayoutKvStorage restarted_storage(backend);
    GaugeLayoutStore restarted_store(restarted_storage);
    GaugeLayout loaded{};
    const auto result = restarted_store.load(layout(1), loaded);
    EXPECT(result.error == GaugeLayoutStoreError::none);
    EXPECT(result.source == GaugeLayoutSource::slot_b);
    EXPECT(!result.recovery_required && loaded.generation == 2);
}

void test_applied_failed_commit_is_selected_after_restart() {
    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);
    GaugeLayoutStore store(storage);
    EXPECT(store.save(layout(1)).saved());
    backend.fail_commit(1, true);
    EXPECT(store.save(layout(2)).uncertain());
    EXPECT(backend.present[1]);

    backend.clear_failure();
    GaugeLayoutKvStorage restarted_storage(backend);
    GaugeLayoutStore restarted_store(restarted_storage);
    GaugeLayout loaded{};
    const auto result = restarted_store.load(layout(1), loaded);
    EXPECT(result.error == GaugeLayoutStoreError::none);
    EXPECT(result.source == GaugeLayoutSource::slot_b);
    EXPECT(loaded.generation == 2);
}

void test_unapplied_failed_commit_preserves_prior_layout() {
    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);
    GaugeLayoutStore store(storage);
    EXPECT(store.save(layout(1)).saved());
    backend.fail_commit(1, false);
    EXPECT(store.save(layout(2)).uncertain());
    EXPECT(!backend.present[1]);

    backend.clear_failure();
    GaugeLayoutKvStorage restarted_storage(backend);
    GaugeLayoutStore restarted_store(restarted_storage);
    GaugeLayout loaded{};
    const auto result = restarted_store.load(layout(9), loaded);
    EXPECT(result.error == GaugeLayoutStoreError::none);
    EXPECT(result.source == GaugeLayoutSource::slot_a);
    EXPECT(result.recovery_required && loaded.generation == 1);
}

void test_real_store_reset_erases_both_keys() {
    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);
    GaugeLayoutStore store(storage);
    EXPECT(store.save(layout(1)).saved());
    EXPECT(store.save(layout(2)).saved());
    EXPECT(store.reset() == GaugeLayoutStoreError::none);
    EXPECT(!backend.present[0] && !backend.present[1]);

    GaugeLayout loaded{};
    const auto result = store.load(layout(9), loaded);
    EXPECT(result.source == GaugeLayoutSource::safe_default);
    EXPECT(result.recovery_required && loaded.generation == 9);
}

void test_uncertain_reset_is_selected_only_after_restart() {
    for (const bool apply_first : {false, true}) {
        for (const std::uint32_t failed_commit : {1U, 2U}) {
            FakeKvBackend backend;
            GaugeLayoutKvStorage storage(backend);
            GaugeLayoutStore store(storage);
            EXPECT(store.save(layout(1)).saved());
            EXPECT(store.save(layout(2)).saved());

            backend.fail_commit(failed_commit, apply_first);
            EXPECT(store.reset() == GaugeLayoutStoreError::commit_uncertain);

            backend.clear_failure();
            GaugeLayoutKvStorage restarted_storage(backend);
            GaugeLayoutStore restarted_store(restarted_storage);
            GaugeLayout loaded{};
            const auto result = restarted_store.load(layout(9), loaded);
            if (apply_first) {
                EXPECT(result.source == GaugeLayoutSource::safe_default);
                EXPECT(result.recovery_required && loaded.generation == 9);
                EXPECT(!backend.present[0] && !backend.present[1]);
            } else {
                const auto surviving_slot = failed_commit == 1U ? 0U : 1U;
                EXPECT(result.source ==
                       (surviving_slot == 0U
                            ? GaugeLayoutSource::slot_a
                            : GaugeLayoutSource::slot_b));
                EXPECT(result.recovery_required);
                EXPECT(loaded.generation == surviving_slot + 1U);
                EXPECT(backend.present[surviving_slot]);
                EXPECT(!backend.present[1U - surviving_slot]);
            }
        }
    }
}

void test_store_owned_updates_suppress_unchanged_kv_writes() {
    FakeKvBackend backend;
    GaugeLayoutKvStorage storage(backend);
    GaugeLayoutStore store(storage);

    const auto first = store.save_next_if_changed(layout(0));
    EXPECT(first.changed() && first.generation == 1);
    EXPECT(backend.write_calls == 1 && backend.commit_calls == 1);
    const auto unchanged = store.save_next_if_changed(layout(500));
    EXPECT(unchanged.succeeded() && !unchanged.changed());
    EXPECT(unchanged.generation == 1);
    EXPECT(backend.write_calls == 1 && backend.commit_calls == 1);

    auto changed = layout(0);
    changed.theme = GaugeTheme::light;
    const auto second = store.save_next_if_changed(changed);
    EXPECT(second.changed() && second.generation == 2);
    EXPECT(backend.write_calls == 2 && backend.commit_calls == 2);

    GaugeLayoutKvStorage restarted_storage(backend);
    GaugeLayoutStore restarted_store(restarted_storage);
    GaugeLayout loaded{};
    const auto result = restarted_store.load(layout(9), loaded);
    EXPECT(result.source == GaugeLayoutSource::slot_b);
    EXPECT(!result.recovery_required && loaded.generation == 2);
    EXPECT(loaded.theme == GaugeTheme::light);
}

void test_uncertain_updates_reconcile_before_retry() {
    FakeKvBackend applied_backend;
    GaugeLayoutKvStorage applied_storage(applied_backend);
    GaugeLayoutStore applied_store(applied_storage);
    EXPECT(applied_store.save_next_if_changed(layout(0)).changed());
    auto desired = layout(0);
    desired.brightness_percent = 70;
    applied_backend.fail_commit(1, true);
    const auto applied = applied_store.save_next_if_changed(desired);
    EXPECT(applied.error == GaugeLayoutStoreError::commit_uncertain);
    EXPECT(!applied.succeeded() && !applied.changed());
    EXPECT(applied_backend.present[1]);

    applied_backend.clear_failure();
    GaugeLayoutKvStorage applied_restarted_storage(applied_backend);
    GaugeLayoutStore applied_restarted_store(applied_restarted_storage);
    const auto reconciled =
        applied_restarted_store.save_next_if_changed(desired);
    EXPECT(reconciled.succeeded() && !reconciled.changed());
    EXPECT(reconciled.generation == 2);
    EXPECT(applied_backend.write_calls == 2);

    FakeKvBackend unapplied_backend;
    GaugeLayoutKvStorage unapplied_storage(unapplied_backend);
    GaugeLayoutStore unapplied_store(unapplied_storage);
    EXPECT(unapplied_store.save_next_if_changed(layout(0)).changed());
    unapplied_backend.fail_commit(1, false);
    const auto unapplied = unapplied_store.save_next_if_changed(desired);
    EXPECT(unapplied.error == GaugeLayoutStoreError::commit_uncertain);
    EXPECT(!unapplied_backend.present[1]);

    unapplied_backend.clear_failure();
    GaugeLayoutKvStorage unapplied_restarted_storage(unapplied_backend);
    GaugeLayoutStore unapplied_restarted_store(unapplied_restarted_storage);
    const auto retried =
        unapplied_restarted_store.save_next_if_changed(desired);
    EXPECT(retried.changed() && retried.generation == 2);
    EXPECT(unapplied_backend.write_calls == 3);
    EXPECT(unapplied_backend.present[1]);
}

}  // namespace

int main() {
    test_fixed_binding_and_invalid_arguments();
    test_exact_missing_wrong_size_and_failed_reads();
    test_write_and_erase_require_commit();
    test_backend_failures_are_mapped();
    test_real_store_rotates_and_restarts();
    test_applied_failed_commit_is_selected_after_restart();
    test_unapplied_failed_commit_preserves_prior_layout();
    test_real_store_reset_erases_both_keys();
    test_uncertain_reset_is_selected_only_after_restart();
    test_store_owned_updates_suppress_unchanged_kv_writes();
    test_uncertain_updates_reconcile_before_retry();

    if (failures != 0) {
        std::cerr << failures << " layout key/value assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 11 gauge layout key/value storage groups\n";
    return EXIT_SUCCESS;
}
