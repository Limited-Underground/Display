#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "fake_esp_now_transport.hpp"
#include "fake_gauge_layout_storage.hpp"
#include "fake_gauge_renderer.hpp"
#include "opengauge/gauge_renderer_runtime.hpp"

namespace {

using namespace opengauge;
using configuration::test_support::FakeGaugeLayoutStorage;
using display::test_support::FakeGaugeRenderer;
using wireless::test_support::FakeEspNowTransport;

int failures = 0;

void expect(bool condition, const char* expression, int line) {
    if (!condition) {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        ++failures;
    }
}
#define EXPECT(expression) expect((expression), #expression, __LINE__)

wireless::PeerAddress peer(std::uint8_t suffix) {
    return {{0x02U, 0, 0, 0, 0, suffix}};
}

display::GaugeWidgetConfiguration widget(
    std::uint16_t id = 101,
    std::uint64_t stale_after_ms = 100) {
    display::GaugeWidgetConfiguration result{};
    result.widget_id = id;
    result.signal_code = wireless::TelemetrySignalCode::engine_speed;
    result.kind = display::GaugeWidgetKind::needle;
    EXPECT(display::make_gauge_widget_label("RPM", result.label) ==
           display::GaugeViewModelError::none);
    result.stale_after_ms = stale_after_ms;
    result.scale_min_raw = 0;
    result.scale_max_raw = 3000000;
    return result;
}

configuration::GaugeLayout layout(
    std::uint64_t generation = 1,
    std::uint32_t layout_id = 0x120E0001U) {
    configuration::GaugeLayout result{};
    result.generation = generation;
    result.layout_id = layout_id;
    result.brightness_percent = 70;
    result.theme = configuration::GaugeTheme::high_contrast;
    result.widget_count = 1;
    result.widgets[0] = widget();
    return result;
}

display::GaugeDashboardLoopConfiguration configuration() {
    return {layout(), {peer(1), 10, 6, 4}};
}

wireless::WireTelemetrySignal rpm_signal(
    std::int64_t raw_value,
    std::uint32_t source_age_ms = 0) {
    wireless::WireTelemetrySignal result{};
    result.code = wireless::TelemetrySignalCode::engine_speed;
    result.value = {
        telemetry::SignalValueType::unsigned_integer,
        raw_value,
        true};
    result.unit = telemetry::SignalUnit::milli_revolutions_per_minute;
    result.quality = telemetry::SignalQuality::valid;
    result.source_age_ms = source_age_ms;
    return result;
}

wireless::TelemetryBatch batch(
    std::uint32_t sequence,
    std::int64_t raw_value,
    std::uint32_t source_age_ms = 0) {
    wireless::TelemetryBatch result{};
    result.gateway_id = 10;
    result.boot_session_id = 20;
    result.sequence = sequence;
    result.signal_count = 1;
    result.signals[0] = rpm_signal(raw_value, source_age_ms);
    return result;
}

void start_transport_pair(
    FakeEspNowTransport& sender,
    FakeEspNowTransport& gauge) {
    EXPECT(sender.start(peer(1), {6, true}) ==
           wireless::EspNowError::none);
    EXPECT(gauge.start(peer(2), {6, true}) ==
           wireless::EspNowError::none);
    EXPECT(sender.add_peer({peer(2), 6, true}) ==
           wireless::EspNowError::none);
    EXPECT(gauge.add_peer({peer(1), 6, true}) ==
           wireless::EspNowError::none);
    sender.connect(gauge);
}

void deliver(
    FakeEspNowTransport& sender,
    const wireless::TelemetryBatch& telemetry,
    std::uint64_t now_ms) {
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               telemetry, encoded.data(), encoded.size()).encoded());
    EXPECT(sender.send(peer(2), {encoded.data(), encoded.size()}, now_ms)
               .accepted());
    sender.service(now_ms);
}

std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encode_packet(
    const wireless::TelemetryBatch& telemetry) {
    std::array<std::uint8_t, wireless::kTelemetryPacketBytes> encoded{};
    EXPECT(wireless::encode_telemetry_packet(
               telemetry, encoded.data(), encoded.size()).encoded());
    return encoded;
}

display::GaugeDashboardFrame frame(
    std::uint64_t sequence = 1,
    std::uint64_t published_at_ms = 10,
    std::uint64_t age_ms = 0) {
    display::GaugeDashboardFrame result{};
    result.publication_sequence = sequence;
    result.published_at_ms = published_at_ms;
    result.layout_generation = 1;
    result.layout_id = 2;
    result.layout_source = configuration::GaugeLayoutSource::slot_a;
    result.theme = configuration::GaugeTheme::high_contrast;
    result.brightness_percent = 70;
    result.widget_count = 1;
    result.widgets[0].widget_id = 101;
    result.widgets[0].signal_code =
        wireless::TelemetrySignalCode::engine_speed;
    result.widgets[0].kind = display::GaugeWidgetKind::needle;
    EXPECT(display::make_gauge_widget_label(
               "RPM", result.widgets[0].label) ==
           display::GaugeViewModelError::none);
    result.widgets[0].state = display::GaugeValueState::valid;
    result.widgets[0].display_value = {
        telemetry::SignalValueType::unsigned_integer, 1500000, true};
    result.widgets[0].unit =
        telemetry::SignalUnit::milli_revolutions_per_minute;
    result.widgets[0].age_ms = age_ms;
    result.widgets[0].scale_max_raw = 3000000;
    return result;
}

class InjectingTransport final : public wireless::EspNowTransport {
public:
    explicit InjectingTransport(FakeEspNowTransport& delegate)
        : delegate_(delegate) {}

    std::size_t mtu() const override { return delegate_.mtu(); }
    wireless::EspNowStatus status() const override {
        return delegate_.status();
    }
    wireless::EspNowError start(
        const wireless::PeerAddress& local,
        wireless::EspNowPolicy policy) override {
        return delegate_.start(local, policy);
    }
    void stop() override { delegate_.stop(); }
    wireless::EspNowError add_peer(
        const wireless::PeerConfiguration& value) override {
        return delegate_.add_peer(value);
    }
    wireless::EspNowError remove_peer(
        const wireless::PeerAddress& address) override {
        return delegate_.remove_peer(address);
    }
    wireless::SendResult send(
        const wireless::PeerAddress& destination,
        wireless::ByteView payload,
        std::uint64_t now_ms) override {
        return delegate_.send(destination, payload, now_ms);
    }
    wireless::ReceiveResult receive(
        wireless::MutableByteView output) override {
        if (next_receive_error_ != wireless::EspNowError::none) {
            const auto error = next_receive_error_;
            next_receive_error_ = wireless::EspNowError::none;
            return {error};
        }
        if (!has_injected_) {
            return delegate_.receive(output);
        }
        if (output.data == nullptr || output.size < injected_size_) {
            return {wireless::EspNowError::buffer_too_small};
        }
        std::copy(
            injected_.begin(), injected_.begin() + injected_size_,
            output.data);
        has_injected_ = false;
        return {
            wireless::EspNowError::none,
            injected_size_,
            injected_metadata_};
    }
    wireless::DeliveryResult poll_delivery() override {
        return delegate_.poll_delivery();
    }
    void service(std::uint64_t now_ms) override {
        delegate_.service(now_ms);
    }

    void inject(
        const std::uint8_t* bytes,
        std::size_t size,
        wireless::ReceiveMetadata metadata) {
        EXPECT(bytes != nullptr);
        EXPECT(size <= injected_.size());
        if (bytes == nullptr || size > injected_.size()) {
            return;
        }
        std::copy(bytes, bytes + size, injected_.begin());
        injected_size_ = size;
        injected_metadata_ = metadata;
        has_injected_ = true;
    }

    void fail_next_receive() {
        next_receive_error_ = wireless::EspNowError::io_failure;
    }

private:
    FakeEspNowTransport& delegate_;
    std::array<std::uint8_t, wireless::kMaximumEspNowPayloadBytes>
        injected_{};
    std::size_t injected_size_{0};
    wireless::ReceiveMetadata injected_metadata_{};
    wireless::EspNowError next_receive_error_{wireless::EspNowError::none};
    bool has_injected_{false};
};

wireless::ReceiveMetadata metadata(
    wireless::PeerAddress source,
    std::uint64_t received_at_ms,
    bool encrypted = true) {
    wireless::ReceiveMetadata result{};
    result.source = source;
    result.received_at_ms = received_at_ms;
    result.channel = 6;
    result.encrypted = encrypted;
    return result;
}

struct RuntimeFixture {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    InjectingTransport transport{gauge};
    wireless::GaugeTelemetryReceiver receiver{transport};
    display::GaugeViewModel view{receiver};
    display::GaugeDashboardLoop dashboard{store, receiver, view};
    FakeGaugeRenderer renderer{};
    display::GaugeRendererRuntime runtime{dashboard, renderer};

    RuntimeFixture() { start_transport_pair(sender, gauge); }
};

class OrderCheckingRenderer final : public display::GaugeRenderer {
public:
    explicit OrderCheckingRenderer(
        const display::GaugeDashboardLoop& dashboard)
        : dashboard_(dashboard) {}

    bool is_running() const override { return running_; }

    display::GaugeRendererError start() override {
        running_ = true;
        return display::GaugeRendererError::none;
    }

    void stop() override { running_ = false; }

    display::GaugeRendererOfferResult offer(
        const display::GaugeDashboardFrame&) override {
        ++offers_;
        if (!running_ || phase_ != 0 ||
            dashboard_.status().cycles_serviced != offers_) {
            ordering_ok_ = false;
        }
        phase_ = 1;
        return {display::GaugeRendererError::none};
    }

    display::GaugeRendererServiceResult service(
        std::uint64_t) override {
        ++services_;
        if (!running_ || phase_ != 1 || offers_ != services_) {
            ordering_ok_ = false;
        }
        phase_ = 0;
        return {display::GaugeRendererError::none, true};
    }

    [[nodiscard]] bool ordering_ok() const { return ordering_ok_; }
    [[nodiscard]] std::uint32_t offers() const { return offers_; }
    [[nodiscard]] std::uint32_t services() const { return services_; }

private:
    const display::GaugeDashboardLoop& dashboard_;
    std::uint32_t offers_{0};
    std::uint32_t services_{0};
    std::uint8_t phase_{0};
    bool running_{false};
    bool ordering_ok_{true};
};

void test_semantic_frame_equality_is_fieldwise() {
    auto first = frame(1, 10, 5);
    auto second = first;
    second.publication_sequence = 99;
    second.published_at_ms = 1000;
    second.widgets[7].widget_id = 999;
    EXPECT(display::gauge_dashboard_frames_semantically_equal(first, second));

    second.widgets[0].age_ms = 6;
    EXPECT(!display::gauge_dashboard_frames_semantically_equal(first, second));
    second = first;
    second.widgets[0].label.bytes[0] = 'X';
    EXPECT(!display::gauge_dashboard_frames_semantically_equal(first, second));
    second = first;
    second.widgets[0].label.bytes[20] = 'X';
    EXPECT(display::gauge_dashboard_frames_semantically_equal(first, second));
    second = first;
    second.widget_count = 9;
    EXPECT(!display::gauge_dashboard_frames_semantically_equal(first, second));
    first.widgets[0].label.length = 255;
    second = first;
    EXPECT(!display::gauge_dashboard_frames_semantically_equal(first, second));
}

void test_fake_renderer_copy_lifecycle_busy_and_failure_retention() {
    FakeGaugeRenderer renderer{};
    auto input = frame();
    EXPECT(renderer.offer(input).error ==
           display::GaugeRendererError::invalid_state);
    EXPECT(renderer.service(0).error ==
           display::GaugeRendererError::invalid_state);
    EXPECT(renderer.start() == display::GaugeRendererError::none);
    EXPECT(renderer.start() == display::GaugeRendererError::invalid_state);
    EXPECT(renderer.offer(input).accepted());
    input.layout_id = 999;
    display::GaugeDashboardFrame queued{};
    EXPECT(renderer.copy_queued_frame(queued));
    EXPECT(queued.layout_id == 2);
    EXPECT(renderer.offer(frame(2)).error ==
           display::GaugeRendererError::busy);

    renderer.fail_next_service();
    EXPECT(renderer.service(0).error ==
           display::GaugeRendererError::backend_failure);
    EXPECT(renderer.copy_queued_frame(queued));
    EXPECT(renderer.service(0).frame_presented);
    EXPECT(!renderer.copy_queued_frame(queued));
    EXPECT(renderer.copy_presented_frame(queued));
    EXPECT(queued.publication_sequence == 1);

    auto second = frame(2, 20, 10);
    EXPECT(renderer.offer(second).accepted());
    renderer.fail_next_service();
    EXPECT(renderer.service(10).error ==
           display::GaugeRendererError::backend_failure);
    EXPECT(renderer.copy_queued_frame(queued));
    EXPECT(queued.publication_sequence == 2);
    display::GaugeDashboardFrame front{};
    EXPECT(renderer.copy_presented_frame(front));
    EXPECT(front.publication_sequence == 1);
    EXPECT(renderer.service(10).frame_presented);
    EXPECT(renderer.copy_presented_frame(front));
    EXPECT(front.publication_sequence == 2);

    renderer.stop();
    EXPECT(renderer.start() == display::GaugeRendererError::none);
    auto maximum = frame(UINT64_MAX, 30, 0);
    EXPECT(renderer.offer(maximum).accepted());
    EXPECT(renderer.service(30).frame_presented);
    maximum.layout_id = 3;
    EXPECT(renderer.offer(maximum).accepted());
    EXPECT(renderer.service(31).frame_presented);
    EXPECT(renderer.copy_presented_frame(front));
    EXPECT(front.publication_sequence == UINT64_MAX);
    EXPECT(front.layout_id == 3);
    renderer.stop();
    EXPECT(!renderer.status().running);
    EXPECT(!renderer.copy_presented_frame(queued));
}

void test_runtime_start_failure_rolls_back_and_stop_resets() {
    RuntimeFixture fixture{};
    EXPECT(fixture.renderer.start() == display::GaugeRendererError::none);
    const auto prestarted = fixture.runtime.start(configuration());
    EXPECT(prestarted.error ==
           display::GaugeRendererRuntimeError::invalid_state);
    EXPECT(fixture.renderer.is_running());
    EXPECT(!fixture.dashboard.status().running);
    fixture.renderer.stop();

    EXPECT(fixture.dashboard.start(configuration()).started());
    const auto renderer_starts_before = fixture.renderer.status().start_calls;
    EXPECT(fixture.runtime.start(configuration()).error ==
           display::GaugeRendererRuntimeError::invalid_state);
    EXPECT(fixture.dashboard.status().running);
    EXPECT(fixture.renderer.status().start_calls == renderer_starts_before);
    fixture.dashboard.stop();

    fixture.renderer.fail_next_start(
        display::GaugeRendererError::backend_failure, true);
    const auto renderer_stops_before = fixture.renderer.status().stop_calls;
    const auto rejected = fixture.runtime.start(configuration());
    EXPECT(rejected.error ==
           display::GaugeRendererRuntimeError::renderer_start_failure);
    EXPECT(!fixture.dashboard.status().running);
    EXPECT(!fixture.receiver.status().running);
    EXPECT(!fixture.view.status().running);
    EXPECT(!fixture.renderer.status().running);
    EXPECT(fixture.renderer.status().stop_calls == renderer_stops_before + 1);
    EXPECT(!fixture.runtime.status().running);

    EXPECT(fixture.runtime.start(configuration()).started());
    EXPECT(fixture.runtime.start(configuration()).error ==
           display::GaugeRendererRuntimeError::invalid_state);
    EXPECT(fixture.runtime.service(0).succeeded());
    fixture.runtime.stop();
    EXPECT(!fixture.runtime.status().running);
    EXPECT(!fixture.dashboard.status().running);
    EXPECT(!fixture.renderer.status().running);
    EXPECT(fixture.runtime.service(1).error ==
           display::GaugeRendererRuntimeError::invalid_state);
    EXPECT(fixture.runtime.start(configuration()).started());
    const auto restarted = fixture.runtime.status();
    EXPECT(restarted.cycles_serviced == 0);
    EXPECT(!restarted.has_pending_frame);
    EXPECT(!restarted.has_last_accepted_frame);
    EXPECT(fixture.store.save(layout(2, 0x120E0002U)).saved());
    fixture.runtime.stop();
    EXPECT(fixture.runtime.start(configuration()).started());
    EXPECT(fixture.runtime.service(5).succeeded());
    display::GaugeDashboardFrame presented{};
    EXPECT(fixture.renderer.copy_presented_frame(presented));
    EXPECT(presented.layout_generation == 2);
    EXPECT(presented.layout_id == 0x120E0002U);
    EXPECT(presented.publication_sequence == 1);
}

void test_exact_dashboard_offer_renderer_order() {
    FakeGaugeLayoutStorage storage{};
    configuration::GaugeLayoutStore store{storage};
    FakeEspNowTransport sender{};
    FakeEspNowTransport gauge{};
    start_transport_pair(sender, gauge);
    wireless::GaugeTelemetryReceiver receiver{gauge};
    display::GaugeViewModel view{receiver};
    display::GaugeDashboardLoop dashboard{store, receiver, view};
    OrderCheckingRenderer renderer{dashboard};
    display::GaugeRendererRuntime runtime{dashboard, renderer};
    EXPECT(runtime.start(configuration()).started());
    EXPECT(runtime.service(0).succeeded());
    EXPECT(runtime.service(0).succeeded());
    EXPECT(renderer.ordering_ok());
    EXPECT(renderer.offers() == 2);
    EXPECT(renderer.services() == 2);
    EXPECT(dashboard.status().cycles_serviced == 2);
}

void test_every_published_frame_is_offered_even_if_semantically_equal() {
    RuntimeFixture fixture{};
    EXPECT(fixture.runtime.start(configuration()).started());
    auto cycle = fixture.runtime.service(0);
    EXPECT(cycle.succeeded());
    EXPECT(cycle.frame_observed && cycle.offer_attempted);
    EXPECT(cycle.renderer_service.frame_presented);
    cycle = fixture.runtime.service(0);
    EXPECT(cycle.succeeded());
    EXPECT(cycle.frame_semantically_equal);
    EXPECT(cycle.offer_attempted);
    EXPECT(fixture.renderer.status().frames_accepted == 2);
    EXPECT(fixture.runtime.status().equivalent_frames_observed == 1);
}

void test_busy_retains_and_latest_frame_replaces_pending() {
    RuntimeFixture fixture{};
    EXPECT(fixture.runtime.start(configuration()).started());
    fixture.renderer.set_busy(true);
    auto cycle = fixture.runtime.service(0);
    EXPECT(cycle.error == display::GaugeRendererRuntimeError::renderer_busy);
    EXPECT(cycle.pending_after_cycle);
    cycle = fixture.runtime.service(10);
    EXPECT(cycle.pending_replaced);
    EXPECT(cycle.pending_after_cycle);

    deliver(fixture.sender, batch(1, 1500000), 20);
    cycle = fixture.runtime.service(20);
    EXPECT(cycle.pending_replaced);
    EXPECT(cycle.pending_after_cycle);
    EXPECT(fixture.runtime.status().frames_coalesced == 2);

    fixture.renderer.set_busy(false);
    cycle = fixture.runtime.service(21);
    EXPECT(cycle.offer_attempted);
    EXPECT(!cycle.pending_after_cycle);
    display::GaugeDashboardFrame presented{};
    EXPECT(fixture.renderer.copy_presented_frame(presented));
    EXPECT(presented.publication_sequence == 4);
    EXPECT(presented.widgets[0].display_value.present);
    EXPECT(presented.widgets[0].display_value.raw_value == 1500000);
}

void test_hard_offer_failure_retains_and_does_not_skip_service() {
    RuntimeFixture fixture{};
    EXPECT(fixture.runtime.start(configuration()).started());
    fixture.renderer.fail_next_offer();
    auto cycle = fixture.runtime.service(0);
    EXPECT(cycle.error ==
           display::GaugeRendererRuntimeError::renderer_offer_failure);
    EXPECT(cycle.pending_after_cycle);
    EXPECT(fixture.renderer.status().service_calls == 1);
    const auto future = encode_packet(batch(1, 1800000));
    fixture.transport.inject(
        future.data(), future.size(), metadata(peer(1), 100));
    cycle = fixture.runtime.service(1);
    EXPECT(cycle.dashboard.error ==
           display::GaugeDashboardLoopError::view_refresh_failure);
    EXPECT(!cycle.frame_observed);
    EXPECT(cycle.offer_attempted);
    EXPECT(!cycle.pending_after_cycle);
    EXPECT(cycle.renderer_service.frame_presented);
    display::GaugeDashboardFrame presented{};
    EXPECT(fixture.renderer.copy_presented_frame(presented));
    EXPECT(presented.publication_sequence == 1);
    EXPECT(fixture.dashboard.status().cycles_serviced == 2);
    EXPECT(fixture.renderer.status().service_calls == 2);
}

void test_renderer_service_failure_keeps_inflight_and_dashboard_aging() {
    RuntimeFixture fixture{};
    EXPECT(fixture.runtime.start(configuration()).started());
    deliver(fixture.sender, batch(1, 1600000), 0);
    fixture.renderer.fail_next_service();
    auto cycle = fixture.runtime.service(0);
    EXPECT(cycle.error ==
           display::GaugeRendererRuntimeError::renderer_service_failure);
    EXPECT(fixture.renderer.status().has_queued_frame);
    EXPECT(!cycle.pending_after_cycle);

    cycle = fixture.runtime.service(100);
    EXPECT(cycle.succeeded());
    EXPECT(cycle.pending_after_cycle);
    EXPECT(!cycle.offer_attempted);
    EXPECT(cycle.renderer_service.frame_presented);
    EXPECT(cycle.tracked_frame_presented);
    cycle = fixture.runtime.service(100);
    EXPECT(cycle.offer_attempted);
    EXPECT(cycle.renderer_service.frame_presented);
    display::GaugeDashboardFrame presented{};
    EXPECT(fixture.renderer.copy_presented_frame(presented));
    EXPECT(presented.widgets[0].state == display::GaugeValueState::stale);
    EXPECT(!presented.widgets[0].display_value.present);
    EXPECT(fixture.dashboard.status().cycles_serviced == 3);
}

void test_busy_renderer_and_rejected_traffic_still_age_exactly_stale() {
    RuntimeFixture fixture{};
    EXPECT(fixture.runtime.start(configuration()).started());
    fixture.renderer.set_busy(true);
    deliver(fixture.sender, batch(0, 1700000), 10);
    auto cycle = fixture.runtime.service(10);
    EXPECT(cycle.error == display::GaugeRendererRuntimeError::renderer_busy);

    const auto rejected = encode_packet(batch(1, 1800000));
    fixture.transport.inject(
        rejected.data(), rejected.size(), metadata(peer(9), 20));
    cycle = fixture.runtime.service(20);
    EXPECT(cycle.dashboard.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);
    EXPECT(cycle.renderer_offer.error == display::GaugeRendererError::busy);
    EXPECT(cycle.error ==
           display::GaugeRendererRuntimeError::dashboard_service_failure);
    EXPECT(cycle.dashboard.frame_published);
    EXPECT(cycle.frame_observed);
    EXPECT(cycle.offer_attempted);

    fixture.transport.inject(
        rejected.data(), rejected.size(), metadata(peer(1), 30, false));
    cycle = fixture.runtime.service(30);
    EXPECT(cycle.dashboard.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);
    const std::array<std::uint8_t, 3> malformed{{1, 2, 3}};
    fixture.transport.inject(
        malformed.data(), malformed.size(), metadata(peer(1), 40));
    cycle = fixture.runtime.service(40);
    EXPECT(cycle.dashboard.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);
    fixture.transport.fail_next_receive();
    cycle = fixture.runtime.service(50);
    EXPECT(cycle.dashboard.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);
    fixture.transport.inject(
        rejected.data(), rejected.size(), metadata(peer(1), 9));
    cycle = fixture.runtime.service(60);
    EXPECT(cycle.dashboard.error ==
           display::GaugeDashboardLoopError::receiver_service_failure);

    cycle = fixture.runtime.service(110);
    EXPECT(cycle.error == display::GaugeRendererRuntimeError::renderer_busy);
    EXPECT(cycle.pending_after_cycle);
    fixture.renderer.set_busy(false);
    cycle = fixture.runtime.service(110);
    EXPECT(cycle.succeeded());
    EXPECT(cycle.renderer_service.frame_presented);
    display::GaugeDashboardFrame presented{};
    EXPECT(fixture.renderer.copy_presented_frame(presented));
    EXPECT(presented.widgets[0].state == display::GaugeValueState::stale);
    EXPECT(presented.widgets[0].age_ms == 100);
    EXPECT(!presented.widgets[0].display_value.present);
    const auto receiver_status = fixture.receiver.status();
    EXPECT(receiver_status.unauthorized_datagrams == 1);
    EXPECT(receiver_status.unencrypted_datagrams == 1);
    EXPECT(receiver_status.malformed_packets == 1);
    EXPECT(fixture.runtime.status().frames_coalesced == 7);
}

void test_caller_clock_rollback_has_no_side_effect() {
    RuntimeFixture fixture{};
    fixture.renderer.set_hold_presentations(true);
    EXPECT(fixture.runtime.start(configuration()).started());
    EXPECT(fixture.runtime.service(10).succeeded());
    EXPECT(fixture.runtime.service(11).succeeded());
    EXPECT(fixture.runtime.status().has_pending_frame);
    display::GaugeDashboardFrame queued_before{};
    EXPECT(fixture.renderer.copy_queued_frame(queued_before));
    const auto runtime_before = fixture.runtime.status();
    const auto dashboard_before = fixture.dashboard.status();
    const auto renderer_before = fixture.renderer.status();

    const auto rejected = fixture.runtime.service(10);
    EXPECT(rejected.error ==
           display::GaugeRendererRuntimeError::clock_regression);
    const auto runtime_after = fixture.runtime.status();
    const auto dashboard_after = fixture.dashboard.status();
    const auto renderer_after = fixture.renderer.status();
    EXPECT(runtime_after.cycles_serviced == runtime_before.cycles_serviced);
    EXPECT(runtime_after.last_service_time_ms ==
           runtime_before.last_service_time_ms);
    EXPECT(runtime_after.last_error == runtime_before.last_error);
    EXPECT(runtime_after.offers_attempted == runtime_before.offers_attempted);
    EXPECT(runtime_after.frames_coalesced == runtime_before.frames_coalesced);
    EXPECT(runtime_after.has_pending_frame == runtime_before.has_pending_frame);
    EXPECT(dashboard_after.cycles_serviced ==
           dashboard_before.cycles_serviced);
    EXPECT(renderer_after.offer_calls == renderer_before.offer_calls);
    EXPECT(renderer_after.service_calls == renderer_before.service_calls);
    EXPECT(renderer_after.frames_presented ==
           renderer_before.frames_presented);
    display::GaugeDashboardFrame queued_after{};
    EXPECT(fixture.renderer.copy_queued_frame(queued_after));
    EXPECT(queued_after.publication_sequence ==
           queued_before.publication_sequence);
    EXPECT(queued_after.published_at_ms == queued_before.published_at_ms);
}

void test_accepted_offer_clears_only_runtime_pending_copy() {
    RuntimeFixture fixture{};
    fixture.renderer.set_hold_presentations(true);
    EXPECT(fixture.runtime.start(configuration()).started());
    const auto first = fixture.runtime.service(0);
    EXPECT(first.succeeded());
    EXPECT(!first.pending_after_cycle);
    EXPECT(fixture.renderer.status().has_queued_frame);

    const auto second = fixture.runtime.service(1);
    EXPECT(second.succeeded());
    EXPECT(second.pending_after_cycle);
    EXPECT(!second.offer_attempted);
    EXPECT(fixture.renderer.status().has_queued_frame);
    fixture.renderer.set_hold_presentations(false);
    const auto third = fixture.runtime.service(2);
    EXPECT(third.succeeded());
    EXPECT(third.pending_after_cycle);
    EXPECT(!third.offer_attempted);
    EXPECT(third.renderer_service.frame_presented);
    EXPECT(third.tracked_frame_presented);
    const auto fourth = fixture.runtime.service(3);
    EXPECT(fourth.succeeded());
    EXPECT(!fourth.pending_after_cycle);
    EXPECT(fourth.renderer_service.frame_presented);
}

}  // namespace

int main() {
    test_semantic_frame_equality_is_fieldwise();
    test_fake_renderer_copy_lifecycle_busy_and_failure_retention();
    test_runtime_start_failure_rolls_back_and_stop_resets();
    test_exact_dashboard_offer_renderer_order();
    test_every_published_frame_is_offered_even_if_semantically_equal();
    test_busy_retains_and_latest_frame_replaces_pending();
    test_hard_offer_failure_retains_and_does_not_skip_service();
    test_renderer_service_failure_keeps_inflight_and_dashboard_aging();
    test_busy_renderer_and_rejected_traffic_still_age_exactly_stale();
    test_caller_clock_rollback_has_no_side_effect();
    test_accepted_offer_clears_only_runtime_pending_copy();

    if (failures != 0) {
        std::cerr << failures << " gauge renderer runtime checks failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "PASS: 11 gauge renderer runtime scenario groups\n";
    return EXIT_SUCCESS;
}
