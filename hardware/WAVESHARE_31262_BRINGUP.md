# Waveshare SKU 31262 Round Display Arrival and Bring-up

Status: prepared procedure only, 2026-08-09. The two ordered units have not
arrived and no OpenGauge compatibility result is claimed.

## Exact candidate

The ordered item is reported as **ESP32-S3-Touch-AMOLED-1.75-B**, Waveshare SKU
`31262`: the standard (non-GPS) board supplied in its protective case. This is
not the `-G` GPS version and not the newer `1.75C` aluminum-case board.

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
- Record the shipping demo before erasing it. Any private flash backup stays
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
3. Connect USB, observe Windows enumeration and the shipping demo, and record
   screen/touch/button/audio behavior before changing anything.
4. Record visible firmware/demo version and boot log if available. Do not infer
   a pass from the screen merely lighting.

## Phase B: read-only silicon and recovery evidence

After the shipping demo is recorded and its serial port is identified, use the
installed `python -m esptool` for read-only metadata:

```powershell
python -m esptool --port COMx chip-id
python -m esptool --port COMx flash-id
python -m esptool --port COMx get-security-info
```

Record MCU revision, flash size, PSRAM report where available, crystal, and
security state. COM numbers can change after reset.

Prove download-mode recovery without flashing: hold `BOOT`, press and release
`RESET`, then release `BOOT`. Waveshare documents this as the recovery path when
USB flashing fails. Confirm the ROM port appears, then reset back to the
shipping application.

If any read-only query leaves the unit in download mode, manually reset it and
verify the original demo returns before proceeding.

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

If the vendor demo does not recover, use Waveshare's published firmware and
BOOT/RESET path before experimenting with OpenGauge.

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
