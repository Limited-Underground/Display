#include "opengauge/telemetry_gateway_publisher.hpp"

#include <array>

namespace opengauge::wireless {
namespace {

constexpr std::array<TelemetrySignalCode, 4> kRegisteredCodes{{
    TelemetrySignalCode::engine_speed,
    TelemetrySignalCode::engine_coolant_temperature,
    TelemetrySignalCode::vehicle_speed,
    TelemetrySignalCode::electrical_voltage,
}};

bool find_registered_code(
    const telemetry::SignalId& id,
    TelemetrySignalCode& output) {
    for (const auto code : kRegisteredCodes) {
        const auto* descriptor = telemetry_signal_descriptor(code);
        if (descriptor != nullptr &&
            telemetry::signal_id_equals(id, descriptor->normalized_id)) {
            output = code;
            return true;
        }
    }
    return false;
}

}  // namespace

PublishSchedulerError TelemetryGatewayPublisher::start(
    GatewayPublishIdentity identity) {
    if (running_) {
        return PublishSchedulerError::invalid_state;
    }
    const auto error = scheduler_.start(identity);
    if (error == PublishSchedulerError::none) {
        cache_cursor_ = {};
        cache_cursor_initialized_ = false;
        running_ = true;
    }
    return error;
}

void TelemetryGatewayPublisher::stop() {
    scheduler_.stop();
    cache_cursor_ = {};
    cache_cursor_initialized_ = false;
    running_ = false;
}

PublishSchedulerError TelemetryGatewayPublisher::add_peer(
    const PeerAddress& address,
    const TelemetrySubscription* subscriptions,
    std::size_t subscription_count,
    PeerPublishPolicy publish_policy) {
    return scheduler_.add_peer(
        address, subscriptions, subscription_count, publish_policy);
}

PublishSchedulerError TelemetryGatewayPublisher::remove_peer(
    const PeerAddress& address) {
    return scheduler_.remove_peer(address);
}

CachePollResult TelemetryGatewayPublisher::poll_cache(
    telemetry::TelemetryCache& cache,
    std::uint64_t now_ms) {
    if (!running_) {
        return {GatewayPublisherError::invalid_state};
    }
    if (!cache_cursor_initialized_) {
        const auto current = cache.current_cursor();
        cache_cursor_ = {current.epoch, 0};
        cache_cursor_initialized_ = true;
    }

    std::array<telemetry::CachedSignalSnapshot,
               telemetry::kTelemetryCacheCapacity>
        snapshots{};
    auto changes = cache.collect_changes(
        cache_cursor_, now_ms, snapshots.data(), snapshots.size());
    bool epoch_changed = false;
    if (changes.error == telemetry::CacheError::cursor_epoch_mismatch) {
        const auto scheduler_error = scheduler_.reset_source_epoch();
        if (scheduler_error != PublishSchedulerError::none) {
            return {
                GatewayPublisherError::scheduler_failure,
                changes.error,
                scheduler_error};
        }
        const auto current = cache.current_cursor();
        cache_cursor_ = {current.epoch, 0};
        changes = cache.collect_changes(
            cache_cursor_, now_ms, snapshots.data(), snapshots.size());
        epoch_changed = true;
    }
    if (!changes.collected()) {
        return {
            GatewayPublisherError::cache_failure,
            changes.error,
            PublishSchedulerError::none,
            0,
            0,
            0,
            epoch_changed};
    }

    std::size_t updated = 0;
    std::size_t skipped = 0;
    for (std::size_t index = 0; index < changes.snapshot_count; ++index) {
        TelemetrySignalCode code{};
        if (!find_registered_code(snapshots[index].signal.id, code)) {
            ++skipped;
            continue;
        }
        const auto update = scheduler_.update_signal(
            code, snapshots[index], now_ms);
        if (!update.accepted()) {
            return {
                GatewayPublisherError::scheduler_failure,
                telemetry::CacheError::none,
                update.error,
                changes.snapshot_count,
                updated,
                skipped,
                epoch_changed};
        }
        ++updated;
    }
    cache_cursor_ = changes.next_cursor;
    return {
        GatewayPublisherError::none,
        telemetry::CacheError::none,
        PublishSchedulerError::none,
        changes.snapshot_count,
        updated,
        skipped,
        epoch_changed};
}

PeerServiceResult TelemetryGatewayPublisher::service_peer(
    EspNowTransport& transport,
    const PeerAddress& destination,
    std::uint64_t now_ms) {
    if (!running_) {
        return {GatewayPublisherError::invalid_state};
    }
    const auto plan = scheduler_.prepare(destination, now_ms);
    if (!plan.prepared()) {
        return {
            plan.error == PublishSchedulerError::no_data
                ? GatewayPublisherError::no_data
                : GatewayPublisherError::scheduler_failure,
            plan.error};
    }

    std::array<std::uint8_t, kTelemetryPacketBytes> encoded{};
    const auto encoding = encode_telemetry_packet(
        plan.batch, encoded.data(), encoded.size());
    if (!encoding.encoded()) {
        const auto ignored = scheduler_.commit(
            destination, plan.token, false, now_ms);
        static_cast<void>(ignored);
        return {
            GatewayPublisherError::codec_failure,
            PublishSchedulerError::none,
            encoding.error};
    }

    const auto sent = transport.send(
        destination,
        {encoded.data(), encoding.encoded_bytes},
        now_ms);
    const auto committed = scheduler_.commit(
        destination, plan.token, sent.accepted(), now_ms);
    if (!committed.committed()) {
        return {
            GatewayPublisherError::scheduler_failure,
            committed.error,
            TelemetryPacketError::none,
            sent.error,
            plan.batch.sequence,
            sent.token,
            encoding.encoded_bytes,
            sent.accepted()};
    }
    if (!sent.accepted()) {
        return {
            GatewayPublisherError::transport_failure,
            PublishSchedulerError::none,
            TelemetryPacketError::none,
            sent.error,
            plan.batch.sequence,
            0,
            encoding.encoded_bytes,
            false};
    }
    return {
        GatewayPublisherError::none,
        PublishSchedulerError::none,
        TelemetryPacketError::none,
        EspNowError::none,
        plan.batch.sequence,
        sent.token,
        encoding.encoded_bytes,
        true};
}

PublishSchedulerStatus TelemetryGatewayPublisher::scheduler_status() const {
    return scheduler_.status();
}

}  // namespace opengauge::wireless
