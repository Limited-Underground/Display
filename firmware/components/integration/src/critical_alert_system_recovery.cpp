#include "opengauge/critical_alert_system_recovery.hpp"

namespace opengauge::integration {

CriticalAlertSystemRecoveryResult
export_critical_alert_system_recovery_checkpoint(
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms,
    std::uint64_t generation,
    std::array<std::uint8_t, kCriticalAlertSystemRecoveryCheckpointBytes>& output) {
    if (generation == 0)
        return {CriticalAlertSystemRecoveryError::invalid_generation};
    CriticalAlertSystemRecoveryCheckpoint candidate{};
    candidate.generation = generation;
    const auto authorization_error =
        authorization.export_checkpoint(candidate.authorization);
    if (authorization_error != identity::PeerAuthorizationError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::authorization_export_failed};
        result.authorization_error = authorization_error;
        return result;
    }
    const auto critical = export_critical_alert_recovery_checkpoint(
        ingress, outbox, now_ms, generation, candidate.critical);
    if (!critical.completed()) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::critical_export_failed};
        result.critical = critical;
        return result;
    }
    const auto encoded = encode_critical_alert_system_recovery_checkpoint(
        candidate, output);
    if (encoded != CriticalAlertSystemRecoveryCheckpointError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::checkpoint_rejected};
        result.checkpoint_error = encoded;
        return result;
    }
    CriticalAlertSystemRecoveryResult result{
        CriticalAlertSystemRecoveryError::none};
    result.generation = generation;
    return result;
}

CriticalAlertSystemRecoveryResult
import_critical_alert_system_recovery_checkpoint(
    const std::uint8_t* checkpoint,
    std::size_t checkpoint_size,
    identity::PeerAuthorizationRegistry& authorization,
    CriticalAlertAckIngress& ingress,
    CriticalAlertOutbox& outbox,
    std::uint64_t now_ms) {
    CriticalAlertSystemRecoveryCheckpoint decoded{};
    const auto decoded_error = decode_critical_alert_system_recovery_checkpoint(
        checkpoint, checkpoint_size, decoded);
    if (decoded_error != CriticalAlertSystemRecoveryCheckpointError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::checkpoint_rejected};
        result.checkpoint_error = decoded_error;
        return result;
    }
    CriticalAlertRecoveryCheckpoint critical{};
    const auto critical_decode = decode_critical_alert_recovery_checkpoint(
        decoded.critical.data(), decoded.critical.size(), critical);
    if (critical_decode != CriticalAlertRecoveryCheckpointError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::checkpoint_rejected};
        result.checkpoint_error =
            CriticalAlertSystemRecoveryCheckpointError::invalid_critical_checkpoint;
        return result;
    }

    auto authorization_candidate = authorization;
    const auto authorization_preflight =
        authorization_candidate.import_checkpoint(
            decoded.authorization.data(), decoded.authorization.size());
    if (authorization_preflight != identity::PeerAuthorizationError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::authorization_preflight_failed};
        result.authorization_error = authorization_preflight;
        result.generation = decoded.generation;
        return result;
    }
    auto outbox_candidate = outbox;
    const auto outbox_preflight = outbox_candidate.import_checkpoint(
        critical.outbox.data(), critical.outbox.size(), now_ms);
    if (outbox_preflight != CriticalOutboxError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::critical_preflight_failed};
        result.critical.error = CriticalAlertRecoveryError::outbox_preflight_failed;
        result.critical.outbox_error = outbox_preflight;
        result.critical.generation = decoded.generation;
        result.generation = decoded.generation;
        return result;
    }
    const auto ack_preflight =
        ingress.validate_checkpoint_import_with_dependencies(
            critical.ack.data(), critical.ack.size(),
            authorization_candidate, outbox_candidate);
    if (ack_preflight != CriticalAlertAckIngressError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::critical_preflight_failed};
        result.critical.error = CriticalAlertRecoveryError::ack_preflight_failed;
        result.critical.ack_error = ack_preflight;
        result.critical.generation = decoded.generation;
        result.generation = decoded.generation;
        return result;
    }

    const auto authorization_import = authorization.import_checkpoint(
        decoded.authorization.data(), decoded.authorization.size());
    if (authorization_import != identity::PeerAuthorizationError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::authorization_import_failed};
        result.authorization_error = authorization_import;
        result.generation = decoded.generation;
        return result;
    }
    const auto outbox_import = outbox.import_checkpoint(
        critical.outbox.data(), critical.outbox.size(), now_ms);
    if (outbox_import != CriticalOutboxError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::critical_import_failed};
        result.critical.error = CriticalAlertRecoveryError::outbox_import_failed;
        result.critical.outbox_error = outbox_import;
        result.critical.generation = decoded.generation;
        result.generation = decoded.generation;
        return result;
    }
    const auto ack_import = ingress.import_checkpoint(
        critical.ack.data(), critical.ack.size());
    if (ack_import != CriticalAlertAckIngressError::none) {
        CriticalAlertSystemRecoveryResult result{
            CriticalAlertSystemRecoveryError::critical_import_failed};
        result.critical.error = CriticalAlertRecoveryError::ack_import_failed;
        result.critical.ack_error = ack_import;
        result.critical.generation = decoded.generation;
        result.generation = decoded.generation;
        return result;
    }
    CriticalAlertSystemRecoveryResult result{
        CriticalAlertSystemRecoveryError::none};
    result.critical.error = CriticalAlertRecoveryError::none;
    result.critical.generation = decoded.generation;
    result.generation = decoded.generation;
    return result;
}

}  // namespace opengauge::integration
