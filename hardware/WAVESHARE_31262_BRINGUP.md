# Waveshare SKU 31262 Round Display Arrival and Bring-up

Status: arrival observation and partial read-only Phase-B evidence recorded
2026-08-14; manual recovery, flashing, and visual acceptance remain unexecuted.
The owner reports two units on hand. A package label identifies the public
model `ESP32-S3-Touch-AMOLED-1.75-B` with case, one underside case label names
`ESP32-S3 Touch AMOLED 1.75` and prints a component map, and one photographed
unit renders a powered launcher and public demo information. No supplied
label shows SKU `31262` or a board revision, and the package, physical unit,
and public-safe unit IDs have not yet been associated. Printed component names
and displayed demo metadata are identification evidence only, not working-
peripheral or firmware-version evidence. No OpenGauge compatibility result is
claimed. See the dated
[arrival observation](../tests/hardware/OG-012A-ARRIVAL-2026-08-14.md) and
[Phase-B evidence](../tests/hardware/OG-012A-PHASE-B-2026-08-14.md).

## Exact candidate

The acquired candidate is reported as **ESP32-S3-Touch-AMOLED-1.75-B**, with
procurement mapping Waveshare SKU `31262`: the standard (non-GPS) board supplied
in its protective case. The procurement target is not the `-G` GPS version or
the newer `1.75C` aluminum-case board; the received units still require exact
identification.

Waveshare currently documents the 31262 board with an ESP32-S3R8, 8 MB PSRAM,
16 MB flash, 466x466 QSPI CO5300 AMOLED, CST9217 I2C touch controller, QMI8658
IMU, PCF85063 RTC, AXP2101 power management, USB-C, audio, and a TF slot. These
are vendor specifications until each received unit is identified and tested.

Official references:

- [Waveshare SKU and specification page](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)
- [Product page and version distinction](https://www.waveshare.com/product/esp32-s3-touch-amoled-1.75.htm)
- [Official examples, recovery, and resources](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
- [Official board-support components](https://github.com/waveshareteam/Waveshare-ESP32-components)

## Safety and preservation

- Power the first checks only from a known data-capable USB cable. Never connect
  this development board directly to vehicle 12/24 V.
- Do not add a battery until the exact connector polarity, voltage, protection,
  fit, and enclosure clearance are verified. The protective case is not proof
  of automotive power, environmental, impact, or thermal suitability.
- Do not flash both units at once. Preserve one known-good unit while the other
  is the active experiment.
- Record the currently installed powered demo before erasing it. Any private flash backup stays
  outside Git and is never published without a licensing and secret review.
- Keep display brightness moderate during stationary bench tests and inspect
  for heat, flicker, resets, and image retention.

## Phase A: identify both physical units

Assign public-safe IDs `OG-DISP-001` and `OG-DISP-002`.

For each unit:

1. Photograph the sealed box, public SKU/revision label, case, USB connector,
   buttons, speaker, and accessories. Do not publish a device MAC/serial.
2. Confirm `31262` and `ESP32-S3-Touch-AMOLED-1.75-B`; stop if either unit is
   `-G`, `1.75C`, or another revision because its pin/peripheral assumptions may
   differ.
3. Connect USB, observe Windows enumeration and the currently installed demo, and record
   screen/touch/button/audio behavior before changing anything.
4. Record visible firmware/demo version and boot log if available. Do not infer
   a pass from the screen merely lighting.

## Phase B: read-only silicon and recovery evidence

After the currently installed demo is recorded and its serial port is privately identified,
use installed `python -m esptool` for bounded read-only metadata. Keep the port
value process-local and redact raw output because normal connection setup can
print the device MAC:

```powershell
$ogPort = '<private runtime-only port>'
python -m esptool --chip esp32s3 --port $ogPort --connect-attempts 1 --before usb-reset --after hard-reset --no-stub get-security-info
python -m esptool --chip esp32s3 --port $ogPort --connect-attempts 1 --before usb-reset --after hard-reset --no-stub flash-id
```

Do not use `chip-id` for public evidence on ESP32-S3 because esptool 5.3.1 falls
back to printing the MAC. Record only redacted MCU, flash-size, and security
results. Port names can change after reset.

The cased `-B` exposes `PWR` and `BOOT`; an exposed ESP32 reset control has not
been confirmed. `PWR` is part of the AXP2101 power path and must not be treated
as `RESET`. Prefer native-USB automatic reset, but require visual confirmation
that the application returned; USB re-enumeration alone is insufficient. To
prove a physical fallback, use a battery-free, USB-only unit: disconnect USB and
verify the unit is fully off, hold `BOOT`, reconnect USB, wait for the ROM USB
interface, then release `BOOT`. Exit by disconnecting and reconnecting USB
without holding `BOOT`, then visually verify the previously photographed powered demo.

Stop if full depower is uncertain, the ROM interface does not appear, or the
powered demo does not return. Do not open the case, improvise another
control, or proceed to flashing.

## Phase C: vendor-example baseline

Download the official example archive/repository and record its source URL,
commit/release, toolchain version, filenames, and SHA-256 hashes. Build before
flashing where source is supplied.

Test one unit in this order:

1. HelloWorld/display initialization and full-screen color/geometry check.
2. Touch coordinates at center, four cardinal edges, and multiple rotations.
3. AXP2101 battery/USB voltage, charge-state, temperature, and reset telemetry
   with no battery attached unless explicitly supported by the example.
4. QMI8658 stationary bias, axis directions, and motion response.
5. RTC retention across a short reset/power cycle.
6. LVGL example with measured boot time, heap/PSRAM use, update time, touch
   latency, and 15-minute stability.
7. TF/audio only after their connectors/media and expected behavior are
   understood; they are not first-boot gates.

If the vendor demo does not recover, stop. Use Waveshare's published firmware
only after visual application return through native-USB reset or the
battery-free cold BOOT-strap recovery path is proven. Do not experiment with
OpenGauge first.

## Phase D: OpenGauge display feasibility

Only after both units pass the vendor baseline:

1. Build a target that consumes synthetic normalized signals; do not connect a
   vehicle or OBD adapter.
2. Render representative numeric, needle, bar, warning, unavailable, error,
   and stale views at 466x466.
3. Demonstrate gateway loss: the last number must not remain visually current
   after its stale threshold.
4. Measure cold boot, time to first useful gauge, steady and peak heap/PSRAM,
   binary/partition use, render/update time, touch latency, USB current, and
   enclosure temperature.
5. Run both displays independently and prove a reset/failure on one does not
   stop the other or the synthetic gateway.
6. Repeat USB recovery after OpenGauge firmware and restore a known vendor demo.

## Acceptance record

Create one dated file per physical unit under `tests/hardware/`, then a paired
test record. Include exact hardware/revision, toolchain and source commit,
hashes, wiring/accessories, procedures, raw public-safe measurements, failures,
recovery, and final evidence tier.

Do not mark OG-012A done from a successful build or attractive screenshot.
Rendering, touch, resource, stability, power, heat, independent failure, and
recovery evidence are separate gates.
