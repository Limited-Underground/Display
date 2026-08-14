# OpenGauge Hardware Inventory and Evidence

Status date: 2026-08-14

This inventory separates acquisition state from compatibility evidence.
`Ordered` and `on hand` do not mean supported. Hardware becomes validated only
after a repeatable test record identifies the exact unit, firmware/software,
wiring, procedure, and result.

## Current candidates

| Role | Exact item | Acquisition state | Evidence state | Intended first check |
| --- | --- | --- | --- | --- |
| Round gauge display | 2 x Waveshare ESP32-S3-Touch-AMOLED-1.75-B candidates, procurement mapping SKU 31262, in protective case | Owner reports two on hand. `OG-DISP-001` is the active battery-free USB candidate; labeled `OG-DISP-002` remained disconnected and untouched | Candidate only; public labels and powered-demo evidence are recorded. `OG-DISP-001` has bounded ESP32-S3/16 MB/security metadata, a private exact-size backup outside Git, passed cold BOOT recovery, visibly ran the hash-pinned official HelloWorld image, and returned to a visually good demo after a verified write of the hash-pinned official factory image. The privacy-safe USB interface returned. Exact board revision, touch and other peripherals, resource/timing/stability, power/heat, paired independence, OpenGauge firmware, and compatibility remain untested | Continue `hardware/WAVESHARE_31262_BRINGUP.md`: preserve `OG-DISP-002`, complete the vendor touch/PMIC/IMU/RTC/LVGL baseline on `OG-DISP-001`, then test measured synthetic OpenGauge states and repeat recovery |
| ESP32-S3 bench mule | Espressif ESP32-S3-DevKitC-1-N8R8 | Owner reports ordered; not yet received | Candidate only | Record revision; run USB/serial recovery and deterministic GPIO/synthetic-telemetry smoke test |
| Generic OBD-II discovery adapter | Veepeak OBDCheck BLE, Amazon ASIN B073XKQQQW | Owner reports on hand | Candidate only; no OpenGauge test | Follow `hardware/VEEPEAK_OBDCHECK_BLE_BRINGUP.md`: identify the exact variant, enumerate the Windows Classic-Bluetooth path, and perform allowlisted read-only capability/rate discovery with the vehicle stationary |
| CAN/J1939 gateway | No protected CAN/J1939 interface selected | Not acquired | Missing | Select controller/transceiver, protection, connector, power, and isolation appropriate to the target vehicle and bus |
| GPS source candidate | Seeed Studio Wio Tracker L1 Pro MeshCore companion | Owner reports ordered; not yet received | Candidate only; shipping MeshCore/GNSS and OpenGauge adapter untested | Follow `hardware/WIO_TRACKER_L1_PRO_BRINGUP.md`: preserve the shipping image, prove USB recovery, baseline MeshCore/GNSS, then test a bounded authenticated `GpsFixSample` adapter without publishing location/identity secrets |

## Important boundaries

- The Veepeak adapter is an optional generic OBD-II discovery path; it is not
  evidence of raw CAN, MS-CAN, SW-CAN, or J1939 access.
- The ESP32 development boards and round displays are not automotive power or
  CAN interfaces.
- Initial vehicle access is read-only/listen-only. No command, coding,
  clearing, actuation, or permanent installation is part of bring-up.
- A vehicle test waits for the exact vehicle, connector, protocol, and power
  constraints to be recorded and reviewed.
- The Waveshare SKU 31262 `-B` is the standard non-GPS board in a protective
  case. Do not treat it as the `-G` GNSS version or connect it directly to
  vehicle power.

## Arrival evidence checklist

For every incoming item:

1. Photograph the package label and both sides of the board/enclosure.
2. Record exact SKU, revision, serial/MAC where appropriate, and included
   cable/antenna/accessories without publishing secrets.
3. Confirm the host enumerates the device and capture recovery/bootloader steps.
4. Preserve the shipping firmware when practical or record its exact image and
   version before reflashing.
5. Run the smallest official vendor example before OpenGauge firmware.
6. Create a dated test record with pass/fail/blocked evidence; do not promote
   the inventory row to validated from observation alone.
