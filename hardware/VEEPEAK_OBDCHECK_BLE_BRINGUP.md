# Veepeak OBDCheck BLE Read-only Bring-up

Status: prepared procedure only, 2026-08-09. The owner reports the adapter is
on hand, but no OpenGauge, Windows, or vehicle test has been recorded.

## Candidate boundary

The reported Amazon ASIN `B073XKQQQW` corresponds to the Veepeak OBDCheck BLE
family. Veepeak currently documents Bluetooth LE for iOS and Classic Bluetooth
for Android/Windows, ELM327 v1.5 behavior, 9-16 V operation, and these generic
OBD-II protocols: SAE J1850 PWM/VPW, ISO 9141-2, ISO 14230-4 KWP2000, and ISO
15765-4 CAN. Veepeak explicitly says MS-CAN and SW-CAN are not supported.

Official reference: [Veepeak OBDCheck BLE](https://veepeak.com/products/obdcheck-ble)

This adapter is a candidate for generic, request/response OBD-II discovery. It
is not assumed to expose raw CAN, passive listen-only traffic, J1939, MS-CAN,
SW-CAN, proprietary networks, or deterministic high-rate gauge data.

## Vehicle safety boundary

- Record the exact vehicle year/make/model/engine and confirm OBD-II compliance
  before connecting. OpenGauge still needs a separate interface for raw CAN or
  J1939 work.
- Initial work is stationary and read-only. Do not clear codes, reset readiness,
  perform service functions, code modules, send actuator tests, change ECU
  settings, or use an app that does so automatically.
- Do not test while driving. A second person is required for any later moving
  observation, and the display remains supplemental instrumentation.
- Do not probe OBD connector pins with development boards or jumper wires.
- Remove the adapter after the bounded test until sleep/current behavior on the
  target vehicle is known.

## Phase A: unpowered identity

1. Photograph the public product label/case and manual. Do not publish a
   Bluetooth address, vehicle VIN, or device-specific identifier.
2. Confirm it is `OBDCheck BLE`, not BLE+, VP11, VP01, or another variant.
3. Record visible hardware/firmware labeling and the exact manual version.

## Phase B: read-only Windows Bluetooth preflight

Before plugging into a vehicle, run:

```powershell
.\tools\Get-VeepeakBluetoothPreflight.ps1 | Format-List
```

The tool reads current serial-port names and redacted matching PnP/Bluetooth
registry display names. It does not pair, connect, open a COM port, transmit an
ELM command, or change Windows/device state. A saved registry name proves only
that Windows has seen an adapter; it does not prove that the adapter is powered
or in range.

For Windows testing, use the product's Classic Bluetooth path. Record whether
pairing creates incoming/outgoing serial ports and which third-party app/driver
opens the connection. Do not assume the browser's Web Bluetooth path is the
same transport.

## Phase C: powered adapter and connection only

With the vehicle parked, parking brake set, and ignition off:

1. Locate and visually inspect the OBD-II connector; stop for bent, corroded,
   loose, modified, or undocumented wiring.
2. Insert the adapter without forcing it and confirm its power indicator.
3. Turn ignition to the minimum state required by the vehicle/app; do not start
   the engine unless ECU discovery cannot proceed and the owner is present.
4. Pair only this adapter and capture redacted adapter/app/version information.
5. Establish an ELM connection without requesting code clearing or service
   operations. If the app cannot offer a read-only session, stop and select a
   different tool.

## Phase D: bounded generic OBD-II discovery

Record, without publishing the VIN or full raw session:

- detected OBD-II protocol
- ELM identity/version reported by the adapter
- supported Mode 01 PID bitmaps (`0100`, then later pages only when indicated)
- availability and update behavior of a tiny candidate set such as engine RPM,
  vehicle speed, coolant temperature, and control-module voltage
- unsupported, timeout, malformed, stale, and reconnect behavior
- observed update interval and whether one slow PID delays the others

Do not send Mode 04 (clear diagnostic information), actuator/output-control
services, coding/adaptation requests, UDS writes, or proprietary commands. Do
not request or publish Mode 09 VIN in the first pass.

An OBD app may display values after applying its own formulas. Capture the raw
request/response bytes privately where the app permits, then create sanitized
fixtures that contain no VIN, DTC history, serial, or owner-specific data.

## Phase E: OpenGauge decision

Promote the Veepeak path only if the exact required signals are present at a
stable, measured rate and the Windows/ESP32 connection can be implemented
without unsafe commands. Even then, call it a generic OBD-II source—not a CAN
or J1939 gateway.

If required signals are missing, proprietary, or too slow, preserve the result
as a useful negative test and select a properly protected listen-only CAN/J1939
interface. Do not try to bypass the adapter's protocol limits with undocumented
commands.

## Acceptance record

Create `tests/hardware/OG-OBD-YYYY-MM-DD.md` with the exact adapter/manual,
vehicle class (redacted as needed), Windows/app versions, connection method,
read-only command allowlist, sanitized request/response fixtures, PID support,
rates, failures/recovery, final disconnect, and evidence tier.

No write/clear/control command is required to evaluate OpenGauge feasibility.
