# Display remaining project plan

Planning only, 2026-09-21. All children remain pending owner approval. Display is optional to base Trail V1. Historical completed work is reused, not reopened. Vehicle/hardware selections remain explicit decision gates; no physical connection, implementation, publication or progress change is authorized by this plan.

This plan covers the five existing checklist milestones. It is a dependency map, not a claim that unselected vehicles or optional integrations have complete implementation estimates. OG-0023c must allocate approved contingent work if GNSS, OTA transport or the OpenTrail bridge is included.

## OG-0021a - Reconcile Display evidence and remaining scope

Parent: OG-0021. Dependencies: none.

Map existing OG-003A, OG-010D, OG-012A and OG-018-series evidence to the five checklist outcomes; identify reusable code and missing target/vehicle proof. Sources: tasks/BACKLOG.md; docs/PROJECT_STATUS.md; docs/PRODUCT_BOUNDARIES_V0.md.

Acceptance: Each claimed capability has its evidence layer and remaining gate; no completed host task is scheduled again; core vehicle gateway/display and optional integrations are separated.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.

## OG-0021b - Select the first vehicle, signals and protected gateway design

Parent: OG-0021. Dependencies: OG-0021a.

Prepare an owner decision for first vehicle/engine, available legal signal definitions, desired initial gauges/alarms, bus/connector, controller/transceiver, protection/isolation, power and environment. Sources: OG-003A; docs/PROJECT_STATUS.md unresolved decisions.

Acceptance: Owner accepts one bounded vehicle/signals/hardware matrix with electrical passivity and power budgets; unsupported signals and unavailable inputs are explicit.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the prepared choices and select or approve the first supported configuration; no devices need connecting.

Checkpoint: Owner accepts the concrete decision before dependent work begins.

## OG-0021c - Prepare the exact display candidate and recovery test plan

Parent: OG-0021. Dependencies: OG-0021a.

Reconcile OG-DISP-001 vendor demo/recovery evidence; identify exact revision and remaining touch/display/power tests. Preserve OG-DISP-002 until separately selected. Sources: hardware/INVENTORY.md; OG-012A.

Acceptance: One exact candidate, wiring/power state, saved image provenance and restore procedure are documented; physical steps and operator instructions are approved before a connection or write.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.

## OG-0022a - Bind the selected passive acquisition and normalized alarm path

Parent: OG-0022. Dependencies: OG-0021b.

After hardware/signal decisions, bind existing CAN gateway/cache/alarm composition to selected target interfaces using synthetic or legally captured fixtures. Sources: OG-004 through OG-010D; OG-014/014A; docs/ARCHITECTURE.md.

Acceptance: Affected target builds and deterministic malformed/stale/overflow/bus-off tests pass; invalid input never becomes a plausible numeric value; no vehicle transmission API exists. Physical passivity remains a separate gate.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.

## OG-0022b - Compose the selected gauge renderer and input target

Parent: OG-0022. Dependencies: OG-0021b, OG-0021c.

Bind the existing receiver/view-model/layout/trend components to the selected display/touch target for approved initial gauges and alerts. Sources: OG-012A/B/C; docs/PRODUCT_BOUNDARIES_V0.md; docs/ARCHITECTURE.md.

Acceptance: Build and host integration prove valid/stale/error/loss views, local configuration and input behavior; frozen numbers cannot appear current; physical readability and timing remain separately tested.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.

## OG-0022c - Bind target diagnostics and fault presentation

Parent: OG-0022. Dependencies: OG-0022a, OG-0022b.

Bind existing diagnostic rings and redacted recovery status to gateway/display target logging and visible status; define bounded capture and retention. Sources: OG-017; OG-018AE/AF; docs/PROJECT_STATUS.md.

Acceptance: Startup, bus loss, storage failure and radio loss are attributable without private identifiers or keys; recovery-required state is visible; injected tests cover independent failure behavior.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.

## OG-0023a - Bind authenticated ESP-NOW telemetry and peer lifecycle

Parent: OG-0023. Dependencies: OG-0021b, OG-0022a, OG-0022b.

Implement selected target transport over existing explicit normalized codecs, provisioned peers, authorization and freshness rules. Sources: docs/ARCHITECTURE.md ESP-NOW section; OG-010D; OG-016A.

Acceptance: Affected builds and integration tests reject unauthorized/replayed/malformed traffic, recover session changes, and isolate missing peers; CRC is not treated as authentication; physical RF metrics remain a separate gate.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.

## OG-0023b - Bind protected persistence and safe boot recovery

Parent: OG-0023. Dependencies: OG-0021b, OG-0023a.

Resolve concrete protected key storage, trusted generation source and reset authority, then bind existing ORS0/OGL0 target adapters and boot/save coordinators to the accepted target. Sources: docs/integration/TARGET_SYSTEM_RECOVERY_ADAPTER_PLAN.md; OG-018Y through OG-018AH.

Acceptance: Design is accepted before binding; target builds and interruption simulations cover uncertain commits, rollback, degraded media, key loss and safe-mode; transport stays disabled until trust is reconciled.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.

## OG-0023c - Decide update transport and optional integration release scope

Parent: OG-0023. Dependencies: OG-0021a, OG-0023b.

Prepare explicit include/defer decisions for GNSS/time and OpenTrail alert bridge; define required signed update/rollback/recovery policy independently of optional OTA transport. Sources: OG-015/016A/018; docs/PRODUCT_BOUNDARIES_V0.md; docs/PROJECT_STATUS.md.

Acceptance: Owner decision records OTA/GNSS/bridge inclusion or deferral, exact security/recovery requirements and consumer contracts; any included capability gets separately defined dependent tasks before implementation. No optional feature becomes a base Trail V1 dependency.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the prepared choices and select or approve the first supported configuration; no devices need connecting.

Checkpoint: Owner accepts the concrete decision before dependent work begins.

## OG-0024a - Validate selected display and synthetic wireless bench operation

Parent: OG-0024. Dependencies: OG-0021c, OG-0022c, OG-0023a, OG-0023b.

Execute the accepted battery/power and recovery plan on selected hardware; run representative normalized telemetry through actual gateway/display adapters. Sources: OG-012A; hardware/INVENTORY.md; docs/PRODUCT_BOUNDARIES_V0.md.

Acceptance: Exact hardware/artifacts recorded; measure boot/frame time, RAM, touch, readable valid/stale/error views, radio latency/loss/duplicates and reconnect; confirm recovery and fail-independence for every supported configuration.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Device selection is unknown; resolve exact setup before starting. Do not connect or power hardware yet. Review the prepared setup; when explicitly prompted, follow the named device/wiring steps and report the displayed result. Exact actions must be added before this task starts. Selected Display gateway and gauge from OG-0021b/OG-0021c; exact model, revision, wiring and power state must be filled in before physical approval. Use synthetic bench input first; no vehicle connection in this task.

Checkpoint: Before execution approve exact device, wiring, artifact, recovery procedure, operator steps and measured limits; afterward review observed evidence.

## OG-0024b - Prove listen-only acquisition on the selected vehicle

Parent: OG-0024. Dependencies: OG-0024a.

After electrical bench approval, perform explicitly authorized passive capture on the selected vehicle; compare approved signals and warning behavior against independent references. Sources: AGENTS.md vehicle safety; OG-003A/004/006.

Acceptance: Record wiring, termination, voltage/protection, no bus-drive/ACK behavior, traffic quality and normalized signal accuracy; missing/stale/unsupported data is explicit; vehicle operation is unaffected.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Device selection is unknown; resolve exact setup before starting. Do not connect or power hardware yet. Review the prepared setup; when explicitly prompted, follow the named device/wiring steps and report the displayed result. Exact actions must be added before this task starts. Selected Display gateway and gauge from OG-0021b/OG-0021c; exact model, revision, wiring and power state must be filled in before physical approval. Selected vehicle only after electrical/passivity preparation.

Checkpoint: Before execution approve exact device, wiring, artifact, recovery procedure, operator steps and measured limits; afterward review observed evidence.

## OG-0024c - Validate power faults, endurance and supported configuration limits

Parent: OG-0024. Dependencies: OG-0024a, OG-0024b.

Run a bounded approved power interruption, restart, thermal and sustained-use matrix on the selected supported setup. Sources: docs/PROJECT_STATUS.md missing physical gates; OG-018 recovery series.

Acceptance: Measured limits and pass/fail thresholds are agreed before execution; brownout/restart/storage uncertainty restore safely; radio/display failure does not affect vehicle operation; failures prevent a support claim.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Device selection is unknown; resolve exact setup before starting. Do not connect or power hardware yet. Review the prepared setup; when explicitly prompted, follow the named device/wiring steps and report the displayed result. Exact actions must be added before this task starts. Selected Display gateway and gauge from OG-0021b/OG-0021c; exact model, revision, wiring and power state must be filled in before physical approval. Selected vehicle only after electrical/passivity preparation.

Checkpoint: Before execution approve exact device, wiring, artifact, recovery procedure, operator steps and measured limits; afterward review observed evidence.

## OG-0025a - Prepare installation, calibration and service instructions

Parent: OG-0025. Dependencies: OG-0024b, OG-0024c, OG-0023c.

Write instructions from accepted selected-system evidence: safe installation, supported signals/configuration, pairing, calibration checks, stale warnings, diagnostics, update and recovery. Sources: docs/PROJECT_STATUS.md; docs/PRODUCT_BOUNDARIES_V0.md.

Acceptance: An independent walkthrough follows exact instructions; unsupported vehicles/hardware remain explicit; warnings are supplemental instrumentation; private capture data and keys are excluded.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.

## OG-0025b - Assemble the Display release acceptance package

Parent: OG-0025. Dependencies: OG-0025a.

Assemble supported hardware/vehicle matrix, reproducible target artifacts, manifests, licenses, security/recovery evidence and unresolved release gates. Sources: README.md; tasks/BACKLOG.md; docs/PROJECT_STATUS.md.

Acceptance: All included capabilities trace to accepted target/physical evidence; signing/update/recovery gates are satisfied or release is blocked; owner review is requested. Publication is separately authorized; no progress increase comes from planning.

Exclusions: Display remains optional to base Trail V1. No vehicle control, raw CAN/VIN export, unrelated hardware, expanded vehicle support or automatic publication. This proposal does not authorize execution. Once approved, implementation is limited to this task scope; physical operations additionally require exact setup authorization.

Owner/device requirements: Review the reported result when it is ready; no device connection or nearby attendance is needed.

Checkpoint: Review bounded scope and dependency evidence before starting; review result before acceptance.
