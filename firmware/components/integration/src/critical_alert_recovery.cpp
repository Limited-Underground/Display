#include "opengauge/critical_alert_recovery.hpp"

namespace opengauge::integration {

CriticalAlertRecoveryResult export_critical_alert_recovery_checkpoint(
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    std::uint64_t generation,
    std::array<std::uint8_t, kCriticalAlertRecoveryCheckpointBytes>& output) {
    if (generation == 0) {
        return {CriticalAlertRecoveryError::invalid_generation};
    }
    CriticalAlertRecoveryCheckpoint candidate{};
    candidate.generation = generation;
    const auto ack_error = ingress.export_checkpoint(candidate.ack);
    if (ack_error != CriticalAlertAckIngressError::none) {
        CriticalAlertRecoveryResult result{
            CriticalAlertRecoveryError::ack_export_failed};
        result.ack_error = ack_error;
        return result;
    }
    const auto outbox_error =
        outbox.export_checkpoint(now_ms, candidate.outbox);
    if (outbox_error != CriticalOutboxError::none) {
        CriticalAlertRecoveryResult result{
            CriticalAlertRecoveryError::outbox_export_failed};
        result.outbox_error = outbox_error;
        return result;
    }
    const auto checkpoint_error =
        encode_critical_alert_recovery_checkpoint(candidate, output);
    if (checkpoint_error != CriticalAlertRecoveryCheckpointError::none) {
        CriticalAlertRecoveryResult result{
            CriticalAlertRecoveryError::checkpoint_rejected};
        result.checkpoint_error = checkpoint_error;
        return result;
    }
    CriticalAlertRecoveryResult result{CriticalAlertRecoveryError::none};
    result.generation = generation;
    return result;
}

CriticalAlertRecoveryResult import_critical_alert_recovery_checkpoint(
    const std::uint8_t* checkpoint,
    std::size_t checkpoint_size,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms) {
    CriticalAlertRecoveryCheckpoint decoded{};
    const auto checkpoint_error = decode_critical_alert_recovery_checkpoint(
        checkpoint, checkpoint_size, decoded);
    if (checkpoint_error != CriticalAlertRecoveryCheckpointError::none) {
        CriticalAlertRecoveryResult result{
            CriticalAlertRecoveryError::checkpoint_rejected};
        result.checkpoint_error = checkpoint_error;
        return result;
    }
    const auto ack_preflight = ingress.validate_checkpoint_import(
        decoded.ack.data(), decoded.ack.size());
    if (ack_preflight != CriticalAlertAckIngressError::none) {
        CriticalAlertRecoveryResult result{
            CriticalAlertRecoveryError::ack_preflight_failed};
        result.ack_error = ack_preflight;
        result.generation = decoded.generation;
        return result;
    }
    const auto outbox_preflight = outbox.validate_checkpoint_import(
        decoded.outbox.data(), decoded.outbox.size(), now_ms);
    if (outbox_preflight != CriticalOutboxError::none) {
        CriticalAlertRecoveryResult result{
            CriticalAlertRecoveryError::outbox_preflight_failed};
        result.outbox_error = outbox_preflight;
        result.generation = decoded.generation;
        return result;
    }
    const auto outbox_import = outbox.import_checkpoint(
        decoded.outbox.data(), decoded.outbox.size(), now_ms);
    if (outbox_import != CriticalOutboxError::none) {
        CriticalAlertRecoveryResult result{
            CriticalAlertRecoveryError::outbox_import_failed};
        result.outbox_error = outbox_import;
        result.generation = decoded.generation;
        return result;
    }
    const auto ack_import = ingress.import_checkpoint(
        decoded.ack.data(), decoded.ack.size());
    if (ack_import != CriticalAlertAckIngressError::none) {
        CriticalAlertRecoveryResult result{
            CriticalAlertRecoveryError::ack_import_failed};
        result.ack_error = ack_import;
        result.generation = decoded.generation;
        return result;
    }
    CriticalAlertRecoveryResult result{CriticalAlertRecoveryError::none};
    result.generation = decoded.generation;
    return result;
}

}  // namespace opengauge::integration
