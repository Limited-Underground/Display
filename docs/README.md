# OpenGauge Documentation

This is the organized entry point for OpenGauge design, evidence, and engineering records. The root [README](../README.md) gives the short project overview; this page routes readers to the right level of detail.

## Start with these documents

| Document | Use it for |
| --- | --- |
| [Architecture](ARCHITECTURE.md) | System roles, component boundaries, protocols, and failure behavior |
| [Product boundaries](PRODUCT_BOUNDARIES_V0.md) | Required and optional devices, display choices, allowed data, and degraded operation |
| [Project status](PROJECT_STATUS.md) | Current assumptions, candidate hardware, unresolved decisions, and the next checkpoint |
| [Progress log](PROGRESS_LOG.md) | Public chronology grouped by date, newest first |
| [Engineering backlog](../tasks/BACKLOG.md) | Work-item status, detailed acceptance evidence, and recommended sequence |
| [Hardware inventory](../hardware/INVENTORY.md) | Candidate equipment, missing pieces, and proof required before support claims |

## Find information by goal

| If you want to... | Start here |
| --- | --- |
| Understand what must be installed in a vehicle | [Product boundaries](PRODUCT_BOUNDARIES_V0.md) and [hardware inventory](../hardware/INVENTORY.md) |
| Follow recent work | [Progress log](PROGRESS_LOG.md) |
| See what is complete or still planned | [Engineering backlog](../tasks/BACKLOG.md) |
| Work on CAN or J1939 | [CAN receiver](can/CAN_RECEIVER_V0.md), [identifier rules](can/J1939_IDENTIFIER_V0.md), and [decoder registry](can/J1939_DECODER_REGISTRY_V0.md) |
| Work on wireless gauges | [ESP-NOW transport](wireless/ESP_NOW_TRANSPORT_V0.md) and the wireless documents below |
| Work on a display | [Gauge dashboard loop](display/GAUGE_DASHBOARD_LOOP_V0.md), [renderer runtime](display/GAUGE_RENDERER_RUNTIME_V0.md), [round render plan](display/GAUGE_ROUND_RENDER_PLAN_V0.md), [view model](display/GAUGE_VIEW_MODEL_V0.md), [trend buffer](display/GAUGE_TREND_BUFFER_V0.md), and the [display bring-up plan](../hardware/WAVESHARE_31262_BRINGUP.md) |
| Understand OpenTrail integration | [Critical-alert format](integration/OPENGAUGE_CRITICAL_ALERT_V0.md) and [acknowledgement format](integration/OPENGAUGE_CRITICAL_ALERT_ACK_V0.md) |
| Review recovery or persistence | The configuration, recovery, and security sections below |

## Vehicle data and alarms

### CAN and J1939

- [Passive CAN receiver contract](can/CAN_RECEIVER_V0.md)
- [Classical J1939 identifier rules](can/J1939_IDENTIFIER_V0.md)
- [J1939 decoder registry](can/J1939_DECODER_REGISTRY_V0.md)

### Normalized telemetry

- [Normalized signal model](telemetry/NORMALIZED_SIGNAL_MODEL_V0.md)
- [Telemetry cache](telemetry/TELEMETRY_CACHE_V0.md)
- [Gateway telemetry loop](gateway/GATEWAY_TELEMETRY_LOOP_V0.md)

### Alarms

- [Alarm engine](alarm/ALARM_ENGINE_V0.md)
- [Cache-to-alarm evaluator](alarm/ALARM_CACHE_EVALUATOR_V0.md)

## Gauge network and display

### Wireless transport

- [ESP-NOW transport boundary](wireless/ESP_NOW_TRANSPORT_V0.md)
- [Gateway-to-gauge telemetry packet](wireless/TELEMETRY_PACKET_V0.md)
- [Telemetry publication scheduler](wireless/TELEMETRY_PUBLISH_SCHEDULER_V0.md)
- [Cache-to-radio publisher](wireless/TELEMETRY_GATEWAY_PUBLISHER_V0.md)
- [Gauge telemetry receiver](wireless/GAUGE_TELEMETRY_RECEIVER_V0.md)

### Display-neutral behavior

- [Gauge view model](display/GAUGE_VIEW_MODEL_V0.md)
- [Gauge dashboard loop](display/GAUGE_DASHBOARD_LOOP_V0.md)
- [Gauge renderer runtime](display/GAUGE_RENDERER_RUNTIME_V0.md)
- [Round gauge render plan](display/GAUGE_ROUND_RENDER_PLAN_V0.md)
- [Gauge trend buffer](display/GAUGE_TREND_BUFFER_V0.md)

## Gauge configuration and storage

- [Gauge layout storage](configuration/GAUGE_LAYOUT_STORAGE_V0.md)
- [Key/value target adapter](configuration/GAUGE_LAYOUT_KV_TARGET_ADAPTER_V0.md)
- [Local change confirmation](configuration/GAUGE_LAYOUT_CHANGE_CONFIRMATION_V0.md)
- [Operator-status projection](configuration/GAUGE_LAYOUT_CHANGE_OPERATOR_STATUS_V0.md)
- [Layout-change workflow](configuration/GAUGE_LAYOUT_CHANGE_WORKFLOW_V0.md)
- [Atomic confirmed running-layout activation](configuration/GAUGE_LAYOUT_ACTIVATION_WORKFLOW_V0.md)
- [Exact-generation layout presentation completion](configuration/GAUGE_LAYOUT_PRESENTATION_COMPLETION_V0.md)
- [Layout import](configuration/GAUGE_LAYOUT_IMPORT_V0.md)
- [Layout export](configuration/GAUGE_LAYOUT_EXPORT_V0.md)
- [Cross-store layout transfer](configuration/GAUGE_LAYOUT_TRANSFER_V0.md)
- [Redacted layout-change diagnostic](diagnostics/GAUGE_LAYOUT_CHANGE_STATUS_DIAGNOSTIC_EVENT_V0.md)

## OpenTrail critical-event integration

### Message and delivery contracts

- [Critical-alert format](integration/OPENGAUGE_CRITICAL_ALERT_V0.md)
- [Critical-alert acknowledgement format](integration/OPENGAUGE_CRITICAL_ALERT_ACK_V0.md)
- [Alarm-to-alert exporter](integration/CRITICAL_ALARM_EXPORTER_V0.md)
- [Application-delivery outbox](integration/CRITICAL_ALERT_OUTBOX_V0.md)
- [Acknowledgement ingress](integration/CRITICAL_ALERT_ACK_INGRESS_V0.md)
- [Negative-acknowledgement policy](integration/CRITICAL_ALERT_ACK_REJECTION_POLICY_V0.md)

### Checkpoints and recovery

- [Acknowledgement checkpoint](integration/CRITICAL_ALERT_ACK_CHECKPOINT_V0.md)
- [Acknowledgement checkpoint store](integration/CRITICAL_ALERT_ACK_CHECKPOINT_STORE_V0.md)
- [Outbox checkpoint](integration/CRITICAL_ALERT_OUTBOX_CHECKPOINT_V0.md)
- [Coordinated recovery checkpoint](integration/CRITICAL_ALERT_RECOVERY_CHECKPOINT_V0.md)
- [Coordinated recovery store](integration/CRITICAL_ALERT_RECOVERY_STORE_V0.md)
- [System recovery envelope](integration/CRITICAL_ALERT_SYSTEM_RECOVERY_V0.md)
- [System recovery store](integration/CRITICAL_ALERT_SYSTEM_RECOVERY_STORE_V0.md)
- [System recovery key/value adapter](integration/CRITICAL_ALERT_SYSTEM_RECOVERY_KV_TARGET_ADAPTER_V0.md)
- [System recovery boot coordinator](integration/CRITICAL_ALERT_SYSTEM_RECOVERY_BOOT_V0.md)
- [System recovery save coordinator](integration/CRITICAL_ALERT_SYSTEM_RECOVERY_SAVE_V0.md)
- [Known-degraded repair coordinator](integration/CRITICAL_ALERT_SYSTEM_RECOVERY_REPAIR_V0.md)
- [Redacted recovery status](integration/CRITICAL_ALERT_SYSTEM_RECOVERY_STATUS_V0.md)
- [Target recovery-adapter plan](integration/TARGET_SYSTEM_RECOVERY_ADAPTER_PLAN.md)
- [Recovery-status diagnostic event](diagnostics/RECOVERY_STATUS_DIAGNOSTIC_EVENT_V0.md)

## Security, diagnostics, updates, and optional modules

### Peer security

- [Peer authorization model](security/PEER_AUTHORIZATION_V0.md)
- [Peer authorization checkpoint](security/PEER_AUTHORIZATION_CHECKPOINT_V0.md)
- [Peer authorization checkpoint store](security/PEER_AUTHORIZATION_CHECKPOINT_STORE_V0.md)

### Platform services

- [Diagnostics foundation](diagnostics/DIAGNOSTICS_FOUNDATION_V0.md)
- [OTA trial and rollback guard](update/UPDATE_BOOT_GUARD_V0.md)
- [GPS fix tracker](gps/GPS_FIX_TRACKER_V0.md)

## Hardware bring-up and physical evidence

Prepared recovery-first procedures:

- [Waveshare ESP32-S3 1.75-inch display](../hardware/WAVESHARE_31262_BRINGUP.md)
- [Wio Tracker L1 Pro](../hardware/WIO_TRACKER_L1_PRO_BRINGUP.md)
- [Veepeak OBDCheck BLE](../hardware/VEEPEAK_OBDCHECK_BLE_BRINGUP.md)

Recorded bench evidence:

- [OG-018H external alert/acknowledgement wire proof](../tests/hardware/OG-018H-2026-08-09.md)
- [OG-018I accepted acknowledgement composition](../tests/hardware/OG-018I-2026-08-09.md)
- [OG-018J terminal stale rejection](../tests/hardware/OG-018J-2026-08-09.md)
- [OG-018K retryable rate-limit rejection](../tests/hardware/OG-018K-2026-08-09.md)
- [OG-018L retry-to-accepted completion](../tests/hardware/OG-018L-2026-08-09.md)
- [OG-018M live host state across the retry lifecycle](../tests/hardware/OG-018M-2026-08-09.md)

Physical evidence proves only the boundary stated in each record. It does not turn candidate equipment into supported hardware or substitute for vehicle, power, target-firmware, or field validation.

## Repository policies

- [Contribution guide](../CONTRIBUTING.md)
- [Security reporting](../SECURITY.md)
- [Apache License 2.0](../LICENSE)
