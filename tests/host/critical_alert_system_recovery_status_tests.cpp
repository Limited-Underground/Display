#include <cstdlib>
#include <iostream>
#include <type_traits>
#include <utility>

#include "opengauge/critical_alert_system_recovery_status.hpp"

namespace {

using namespace opengauge::integration;

int failures = 0;
void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

template <typename T, typename = void>
struct has_peer_id : std::false_type {};
template <typename T>
struct has_peer_id<
    T,
    std::void_t<decltype(std::declval<T>().key_validation_peer_id)>>
    : std::true_type {};

template <typename T, typename = void>
struct has_key_handle : std::false_type {};
template <typename T>
struct has_key_handle<
    T,
    std::void_t<decltype(std::declval<T>().secure_key_handle)>>
    : std::true_type {};

static_assert(!has_peer_id<CriticalAlertSystemRecoveryStatus>::value);
static_assert(!has_key_handle<CriticalAlertSystemRecoveryStatus>::value);

CriticalAlertSystemRecoveryInspectionResult healthy_inspection(
    std::uint64_t generation) {
    return {
        CriticalAlertSystemRecoveryStoreError::none,
        CriticalAlertSystemRecoverySource::slot_b,
        CriticalAlertSystemRecoverySlotState::valid,
        CriticalAlertSystemRecoverySlotState::valid,
        generation,
        true,
        false,
    };
}

void test_boot_operational_and_degraded_status() {
    CriticalAlertSystemBootResult restored{};
    restored.state = CriticalAlertSystemBootState::restored;
    restored.reason = CriticalAlertSystemBootReason::none;
    restored.inspection = healthy_inspection(7);
    restored.trusted_generation = 7;
    restored.active_generation = 7;
    restored.transport_allowed = true;
    const auto operational = make_recovery_status(restored);
    EXPECT(operational.operation ==
           CriticalAlertSystemRecoveryOperation::boot);
    EXPECT(operational.state ==
           CriticalAlertSystemOperatorState::operational);
    EXPECT(operational.action == CriticalAlertSystemOperatorAction::none);
    EXPECT(!operational.attention_required && operational.transport_allowed);
    EXPECT(operational.observed_generation == 7 &&
           operational.trusted_generation == 7);

    restored.state = CriticalAlertSystemBootState::restored_degraded;
    restored.inspection.slot_a =
        CriticalAlertSystemRecoverySlotState::invalid;
    restored.inspection.recovery_required = true;
    restored.repair_required = true;
    const auto degraded = make_recovery_status(restored);
    EXPECT(degraded.state ==
           CriticalAlertSystemOperatorState::operational_degraded);
    EXPECT(degraded.action ==
           CriticalAlertSystemOperatorAction::repair_redundancy);
    EXPECT(degraded.attention_required && degraded.repair_required);
    EXPECT(degraded.transport_allowed);
}

void test_first_boot_requests_provisioning() {
    CriticalAlertSystemBootResult boot{};
    boot.state = CriticalAlertSystemBootState::first_boot;
    boot.reason = CriticalAlertSystemBootReason::clean_first_boot;
    boot.inspection.slot_a = CriticalAlertSystemRecoverySlotState::empty;
    boot.inspection.slot_b = CriticalAlertSystemRecoverySlotState::empty;
    const auto status = make_recovery_status(boot);
    EXPECT(status.state == CriticalAlertSystemOperatorState::first_boot);
    EXPECT(status.reason ==
           CriticalAlertSystemOperatorReason::clean_first_boot);
    EXPECT(status.action == CriticalAlertSystemOperatorAction::provision);
    EXPECT(status.attention_required && !status.transport_allowed);
}

void test_protected_key_failure_is_redacted() {
    CriticalAlertSystemBootResult boot{};
    boot.state = CriticalAlertSystemBootState::service_required;
    boot.reason = CriticalAlertSystemBootReason::protected_key_unavailable;
    boot.inspection = healthy_inspection(9);
    boot.active_generation = 9;
    boot.trusted_generation = 9;
    boot.load.recovery.key_validation_error =
        CriticalAlertSystemRecoveryKeyValidationError::purpose_mismatch;
    boot.load.recovery.key_validation_peer_id = 0xDEADBEEFU;
    const auto status = make_recovery_status(boot);
    EXPECT(status.reason ==
           CriticalAlertSystemOperatorReason::protected_key_unavailable);
    EXPECT(status.protected_key_error ==
           CriticalAlertSystemRecoveryKeyValidationError::purpose_mismatch);
    EXPECT(status.sensitive_detail_redacted);
    EXPECT(status.action == CriticalAlertSystemOperatorAction::service);
}

void test_safe_mode_and_incoherent_boot_fail_closed() {
    CriticalAlertSystemBootResult boot{};
    boot.state = CriticalAlertSystemBootState::safe_mode;
    boot.reason = CriticalAlertSystemBootReason::rollback_detected;
    const auto safe = make_recovery_status(boot);
    EXPECT(safe.state == CriticalAlertSystemOperatorState::safe_mode);
    EXPECT(safe.reason ==
           CriticalAlertSystemOperatorReason::rollback_detected);
    EXPECT(safe.action == CriticalAlertSystemOperatorAction::service);

    boot.transport_allowed = true;
    const auto invalid = make_recovery_status(boot);
    EXPECT(invalid.state ==
           CriticalAlertSystemOperatorState::service_required);
    EXPECT(invalid.reason ==
           CriticalAlertSystemOperatorReason::invalid_result);
    EXPECT(!invalid.transport_allowed && invalid.attention_required);
}

void test_save_statuses_preserve_ordering_outcome() {
    CriticalAlertSystemPersistenceResult saved{};
    saved.state = CriticalAlertSystemPersistenceState::committed;
    saved.reason = CriticalAlertSystemPersistenceReason::none;
    saved.inspection = healthy_inspection(10);
    saved.save.error = CriticalAlertSystemRecoveryStoreError::none;
    saved.save.generation = 11;
    saved.prior_trusted_generation = 10;
    saved.observed_trusted_readback = 11;
    saved.committed_generation = 11;
    saved.transport_allowed = true;
    const auto committed = make_recovery_status(saved);
    EXPECT(committed.state ==
           CriticalAlertSystemOperatorState::operational);
    EXPECT(committed.observed_generation == 11 &&
           committed.trusted_generation == 11);
    EXPECT(!committed.attention_required);

    saved.state =
        CriticalAlertSystemPersistenceState::reboot_reconcile_required;
    saved.reason = CriticalAlertSystemPersistenceReason::commit_uncertain;
    saved.transport_allowed = false;
    saved.committed_generation = 0;
    saved.observed_trusted_readback = 0;
    saved.save.error = CriticalAlertSystemRecoveryStoreError::storage_failure;
    const auto reconcile = make_recovery_status(saved);
    EXPECT(reconcile.state == CriticalAlertSystemOperatorState::
                                  reboot_reconcile_required);
    EXPECT(reconcile.reason ==
           CriticalAlertSystemOperatorReason::commit_uncertain);
    EXPECT(reconcile.action ==
           CriticalAlertSystemOperatorAction::reboot_and_reconcile);
    EXPECT(reconcile.observed_generation == 10 &&
           reconcile.trusted_generation == 10);
}

void test_repair_statuses_select_safe_evidence() {
    CriticalAlertSystemRepairResult repaired{};
    repaired.state = CriticalAlertSystemRepairState::repaired;
    repaired.reason = CriticalAlertSystemRepairReason::none;
    repaired.before = healthy_inspection(4);
    repaired.before.slot_a = CriticalAlertSystemRecoverySlotState::invalid;
    repaired.before.recovery_required = true;
    repaired.persistence.committed_generation = 5;
    repaired.persistence.observed_trusted_readback = 5;
    repaired.after = healthy_inspection(5);
    repaired.repaired_generation = 5;
    repaired.transport_allowed = true;
    const auto complete = make_recovery_status(repaired);
    EXPECT(complete.state ==
           CriticalAlertSystemOperatorState::operational);
    EXPECT(complete.slot_a == CriticalAlertSystemRecoverySlotState::valid &&
           complete.slot_b == CriticalAlertSystemRecoverySlotState::valid);
    EXPECT(complete.observed_generation == 5 &&
           complete.trusted_generation == 5);

    CriticalAlertSystemRepairResult stale{};
    stale.reason = CriticalAlertSystemRepairReason::boot_evidence_stale;
    stale.before = healthy_inspection(4);
    const auto refused = make_recovery_status(stale);
    EXPECT(refused.state ==
           CriticalAlertSystemOperatorState::service_required);
    EXPECT(refused.reason ==
           CriticalAlertSystemOperatorReason::stale_repair_evidence);

    CriticalAlertSystemRepairResult uncertain{};
    uncertain.state =
        CriticalAlertSystemRepairState::reboot_reconcile_required;
    uncertain.reason = CriticalAlertSystemRepairReason::persistence_failed;
    uncertain.before = healthy_inspection(4);
    uncertain.persistence.reason =
        CriticalAlertSystemPersistenceReason::trusted_advance_failed;
    uncertain.persistence.committed_generation = 5;
    uncertain.persistence.prior_trusted_generation = 4;
    const auto reboot = make_recovery_status(uncertain);
    EXPECT(reboot.reason ==
           CriticalAlertSystemOperatorReason::trust_update_failed);
    EXPECT(reboot.action ==
           CriticalAlertSystemOperatorAction::reboot_and_reconcile);
    EXPECT(reboot.observed_generation == 5 &&
           reboot.trusted_generation == 4);
}

void test_unknown_or_incoherent_results_fail_closed() {
    CriticalAlertSystemPersistenceResult save{};
    save.state = static_cast<CriticalAlertSystemPersistenceState>(99);
    const auto invalid_save = make_recovery_status(save);
    EXPECT(invalid_save.reason ==
           CriticalAlertSystemOperatorReason::invalid_result);
    EXPECT(invalid_save.action == CriticalAlertSystemOperatorAction::service);

    save.state = CriticalAlertSystemPersistenceState::committed;
    save.reason = static_cast<CriticalAlertSystemPersistenceReason>(99);
    save.save.error = CriticalAlertSystemRecoveryStoreError::none;
    save.committed_generation = 2;
    save.observed_trusted_readback = 2;
    save.transport_allowed = true;
    const auto invalid_save_reason = make_recovery_status(save);
    EXPECT(invalid_save_reason.state ==
           CriticalAlertSystemOperatorState::service_required);
    EXPECT(invalid_save_reason.reason ==
           CriticalAlertSystemOperatorReason::invalid_result);

    CriticalAlertSystemRepairResult repair{};
    repair.state = CriticalAlertSystemRepairState::repaired;
    repair.reason = CriticalAlertSystemRepairReason::none;
    repair.repaired_generation = 1;
    repair.transport_allowed = false;
    const auto invalid_repair = make_recovery_status(repair);
    EXPECT(invalid_repair.reason ==
           CriticalAlertSystemOperatorReason::invalid_result);
    EXPECT(!invalid_repair.transport_allowed);

    repair.state =
        CriticalAlertSystemRepairState::reboot_reconcile_required;
    repair.reason = CriticalAlertSystemRepairReason::persistence_failed;
    repair.persistence.reason =
        static_cast<CriticalAlertSystemPersistenceReason>(99);
    const auto invalid_nested_reason = make_recovery_status(repair);
    EXPECT(invalid_nested_reason.state ==
           CriticalAlertSystemOperatorState::service_required);
    EXPECT(invalid_nested_reason.action ==
           CriticalAlertSystemOperatorAction::service);
}

}  // namespace

int main() {
    test_boot_operational_and_degraded_status();
    test_first_boot_requests_provisioning();
    test_protected_key_failure_is_redacted();
    test_safe_mode_and_incoherent_boot_fail_closed();
    test_save_statuses_preserve_ordering_outcome();
    test_repair_statuses_select_safe_evidence();
    test_unknown_or_incoherent_results_fail_closed();
    if (failures != 0) {
        std::cerr << failures << " recovery status assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 7 redacted recovery status groups\n";
    return EXIT_SUCCESS;
}
