#include "opengauge/gauge_layout_change_workflow.hpp"

namespace opengauge::configuration {

GaugeLayoutChangeWorkflow::GaugeLayoutChangeWorkflow(GaugeLayoutStore& store)
    : coordinator_(store) {}

GaugeLayoutChangeWorkflowResult GaugeLayoutChangeWorkflow::start(
    const GaugeLayoutChangePolicy& policy,
    std::uint64_t now_ms) {
    const auto error = coordinator_.start(policy);
    auto result = snapshot(now_ms);
    result.operation_error = error;
    return result;
}

GaugeLayoutChangeWorkflowResult GaugeLayoutChangeWorkflow::stop(
    std::uint64_t now_ms) {
    coordinator_.stop();
    return snapshot(now_ms);
}

GaugeLayoutChangeWorkflowResult GaugeLayoutChangeWorkflow::snapshot(
    std::uint64_t now_ms) const {
    GaugeLayoutChangeObservation observation{};
    observation.operation = GaugeLayoutChangeOperation::snapshot;
    return project(observation, now_ms);
}

GaugeLayoutChangeWorkflowResult GaugeLayoutChangeWorkflow::stage(
    std::uint32_t request_id,
    const GaugeLayout& desired,
    std::uint64_t now_ms) {
    GaugeLayoutChangeObservation observation{};
    observation.operation = GaugeLayoutChangeOperation::stage;
    observation.error = coordinator_.stage(request_id, desired, now_ms);
    return project(observation, now_ms);
}

GaugeLayoutChangeWorkflowResult
GaugeLayoutChangeWorkflow::stage_restore_default(
    std::uint32_t request_id,
    const GaugeLayout& compiled_default,
    std::uint64_t now_ms) {
    return stage(request_id, compiled_default, now_ms);
}

GaugeLayoutImportWorkflowResult
GaugeLayoutChangeWorkflow::stage_import_record(
    std::uint32_t request_id,
    const std::uint8_t* record,
    std::size_t size,
    std::uint64_t now_ms) {
    GaugeLayoutImportWorkflowResult result{};
    GaugeLayout imported{};
    const auto decoded = decode_gauge_layout(record, size, imported);
    result.codec_error = decoded.error;
    if (!decoded.succeeded()) {
        result.workflow = snapshot(now_ms);
        return result;
    }

    result.summary = {
        imported.generation,
        imported.layout_id,
        imported.theme,
        imported.brightness_percent,
        imported.widget_count,
    };
    result.workflow = stage(request_id, imported, now_ms);
    return result;
}

GaugeLayoutChangeWorkflowResult GaugeLayoutChangeWorkflow::confirm(
    std::uint32_t request_id,
    std::uint64_t now_ms) {
    const auto confirmed = coordinator_.confirm(request_id, now_ms);
    GaugeLayoutChangeObservation observation{};
    observation.operation = GaugeLayoutChangeOperation::confirm;
    observation.error = confirmed.error;
    observation.persistence = confirmed.persistence;
    return project(observation, now_ms);
}

GaugeLayoutChangeWorkflowResult GaugeLayoutChangeWorkflow::cancel(
    std::uint32_t request_id,
    std::uint64_t now_ms) {
    GaugeLayoutChangeObservation observation{};
    observation.operation = GaugeLayoutChangeOperation::cancel;
    observation.error = coordinator_.cancel(request_id);
    return project(observation, now_ms);
}

GaugeLayoutChangeWorkflowResult GaugeLayoutChangeWorkflow::service(
    std::uint64_t now_ms) {
    GaugeLayoutChangeObservation observation{};
    observation.operation = GaugeLayoutChangeOperation::service;
    observation.error = coordinator_.service(now_ms);
    return project(observation, now_ms);
}

GaugeLayoutChangeWorkflowResult GaugeLayoutChangeWorkflow::project(
    const GaugeLayoutChangeObservation& observation,
    std::uint64_t now_ms) const {
    const auto projection = project_gauge_layout_change_operator_status(
        coordinator_.status(), observation, now_ms);
    return {
        observation.error,
        observation.persistence,
        projection.error,
        projection.status,
    };
}

}  // namespace opengauge::configuration
