#include "opengauge/gauge_layout_activation_workflow.hpp"

namespace opengauge::display {

GaugeLayoutActivationWorkflow::GaugeLayoutActivationWorkflow(
    configuration::GaugeLayoutStore& store,
    GaugeRendererRuntime& renderer_runtime)
    : store_(store),
      change_workflow_(store),
      renderer_runtime_(renderer_runtime) {}

configuration::GaugeLayoutChangeWorkflowResult
GaugeLayoutActivationWorkflow::start(
    const configuration::GaugeLayoutChangePolicy& policy,
    std::uint64_t now_ms) {
    if (status_.activation_pending || status_.restart_required) {
        return blocked_change(now_ms);
    }
    return change_workflow_.start(policy, now_ms);
}

configuration::GaugeLayoutChangeWorkflowResult
GaugeLayoutActivationWorkflow::stop(std::uint64_t now_ms) {
    if (status_.activation_pending || status_.restart_required) {
        return blocked_change(now_ms);
    }
    return change_workflow_.stop(now_ms);
}

configuration::GaugeLayoutChangeWorkflowResult
GaugeLayoutActivationWorkflow::snapshot(std::uint64_t now_ms) const {
    return change_workflow_.snapshot(now_ms);
}

configuration::GaugeLayoutChangeWorkflowResult
GaugeLayoutActivationWorkflow::stage(
    std::uint32_t request_id,
    const configuration::GaugeLayout& desired,
    std::uint64_t now_ms) {
    if (status_.activation_pending || status_.restart_required) {
        return blocked_change(now_ms);
    }
    return change_workflow_.stage(request_id, desired, now_ms);
}

configuration::GaugeLayoutChangeWorkflowResult
GaugeLayoutActivationWorkflow::stage_restore_default(
    std::uint32_t request_id,
    const configuration::GaugeLayout& compiled_default,
    std::uint64_t now_ms) {
    if (status_.activation_pending || status_.restart_required) {
        return blocked_change(now_ms);
    }
    return change_workflow_.stage_restore_default(
        request_id, compiled_default, now_ms);
}

configuration::GaugeLayoutImportWorkflowResult
GaugeLayoutActivationWorkflow::stage_import_record(
    std::uint32_t request_id,
    const std::uint8_t* record,
    std::size_t size,
    std::uint64_t now_ms) {
    if (!status_.activation_pending && !status_.restart_required) {
        return change_workflow_.stage_import_record(
            request_id, record, size, now_ms);
    }
    configuration::GaugeLayoutImportWorkflowResult result{};
    result.workflow = blocked_change(now_ms);
    return result;
}

configuration::GaugeLayoutChangeWorkflowResult
GaugeLayoutActivationWorkflow::cancel(
    std::uint32_t request_id,
    std::uint64_t now_ms) {
    if (status_.activation_pending || status_.restart_required) {
        return blocked_change(now_ms);
    }
    return change_workflow_.cancel(request_id, now_ms);
}

configuration::GaugeLayoutChangeWorkflowResult
GaugeLayoutActivationWorkflow::service(std::uint64_t now_ms) {
    if (status_.activation_pending || status_.restart_required) {
        return blocked_change(now_ms);
    }
    return change_workflow_.service(now_ms);
}

GaugeLayoutActivationWorkflowResult
GaugeLayoutActivationWorkflow::confirm_and_activate(
    std::uint32_t request_id,
    std::uint64_t now_ms) {
    GaugeLayoutActivationWorkflowResult result{};
    if (status_.restart_required && !status_.activation_pending) {
        result.error = GaugeLayoutActivationWorkflowError::restart_required;
        result.restart_required = true;
        return result;
    }
    if (status_.activation_pending) {
        result.error = GaugeLayoutActivationWorkflowError::activation_pending;
        result.expected_generation = status_.pending_generation;
        result.retry_required = true;
        result.restart_required = true;
        return result;
    }
    result.error = preflight();
    if (result.error != GaugeLayoutActivationWorkflowError::none) {
        return result;
    }
    result.persistence_attempted = true;
    result.persistence = change_workflow_.confirm(request_id, now_ms);
    if (result.persistence.operation_error ==
        configuration::GaugeLayoutChangeError::persistence_uncertain) {
        status_.restart_required = true;
        result.error = GaugeLayoutActivationWorkflowError::persistence_failure;
        result.restart_required = true;
        return result;
    }
    if (result.persistence.operation_error !=
            configuration::GaugeLayoutChangeError::none ||
        !result.persistence.persistence.succeeded()) {
        result.error = GaugeLayoutActivationWorkflowError::persistence_failure;
        return result;
    }

    result.expected_generation = result.persistence.persistence.generation;
    result.activation_attempted = true;
    result.activation = renderer_runtime_.activate_persisted_layout(
        result.expected_generation);
    if (!result.activation.activated()) {
        status_.activation_pending = true;
        status_.restart_required = true;
        status_.pending_generation = result.expected_generation;
        result.error = GaugeLayoutActivationWorkflowError::activation_failure;
        result.retry_required = true;
        result.restart_required = true;
        return result;
    }

    result.error = GaugeLayoutActivationWorkflowError::none;
    result.presentation_pending = result.activation.presentation_pending;
    return result;
}

GaugeLayoutActivationWorkflowResult
GaugeLayoutActivationWorkflow::retry_activation() {
    GaugeLayoutActivationWorkflowResult result{};
    result.expected_generation = status_.pending_generation;
    if (status_.restart_required && !status_.activation_pending) {
        result.error = GaugeLayoutActivationWorkflowError::restart_required;
        result.restart_required = true;
        return result;
    }
    if (!status_.activation_pending || status_.pending_generation == 0) {
        return result;
    }
    result.retry_required = true;
    result.restart_required = true;
    result.error = preflight();
    if (result.error != GaugeLayoutActivationWorkflowError::none) {
        return result;
    }

    result.activation_attempted = true;
    result.activation = renderer_runtime_.activate_persisted_layout(
        status_.pending_generation);
    if (!result.activation.activated()) {
        result.error = GaugeLayoutActivationWorkflowError::activation_failure;
        return result;
    }

    result.error = GaugeLayoutActivationWorkflowError::none;
    result.retry_required = false;
    result.restart_required = false;
    result.presentation_pending = result.activation.presentation_pending;
    clear_activation_pending();
    return result;
}

GaugeLayoutActivationWorkflowStatus
GaugeLayoutActivationWorkflow::status() const {
    return status_;
}

configuration::GaugeLayoutChangeWorkflowResult
GaugeLayoutActivationWorkflow::blocked_change(
    std::uint64_t now_ms) const {
    auto result = change_workflow_.snapshot(now_ms);
    result.operation_error = configuration::GaugeLayoutChangeError::invalid_state;
    return result;
}

GaugeLayoutActivationWorkflowError
GaugeLayoutActivationWorkflow::preflight() const {
    if (!change_workflow_.bound_to(store_) ||
        !renderer_runtime_.bound_to(store_)) {
        return GaugeLayoutActivationWorkflowError::store_mismatch;
    }
    if (!renderer_runtime_.layout_activation_ready()) {
        return GaugeLayoutActivationWorkflowError::invalid_state;
    }
    return GaugeLayoutActivationWorkflowError::none;
}

void GaugeLayoutActivationWorkflow::clear_activation_pending() {
    status_ = {};
}

}  // namespace opengauge::display
