# Wio Tracker L1 Pro Arrival and GNSS Bring-up

Status: prepared procedure only, 2026-08-09. The owner reports a Seeed Studio
Wio Tracker L1 Pro MeshCore companion ordered; it has not arrived or been tested
for OpenGauge.

## Preserve before changing firmware

1. Photograph package labels, enclosure, ports, antenna, display, accessories,
   and regulatory markings. Record exact SKU/revision privately.
2. Charge it with the supplied/recommended USB arrangement before sustained
   radio/GNSS testing.
3. Record the shipping firmware name/version and visible boot/display behavior.
4. Prove the normal USB enumeration and documented bootloader/recovery path.
5. Preserve or locate the exact restorable shipping image before reflashing.
6. Do not publish serial numbers, MACs, pairing codes, keys, precise home
   coordinates, or raw location logs.

The reported MeshCore firmware is useful baseline evidence, not proof of an
OpenGauge-compatible GNSS API. Do not erase a working baseline merely to force
an assumed integration.

## Baseline matrix

Record pass/fail/blocked plus versions and timestamps for:

- cold boot on charged battery and USB;
- charge indication, battery reporting, sleep/wake, and clean shutdown;
- display/buttons and any documented vendor diagnostics;
- USB console/firmware update and intentional recovery;
- current MeshCore companion connection and LoRa send/receive baseline;
- GNSS no-fix indication indoors, then first outdoor fix;
- cold-start time to first fix, warm restart, and reacquisition after antenna
  obstruction;
- reported fix quality, satellites, coordinates, altitude, speed/heading at
  rest, horizontal accuracy, UTC, and update interval;
- antenna attachment/orientation and failure behavior without assuming the
  included antenna covers every radio/GNSS path;
- power/heat over a representative GNSS plus radio session.

## OpenGauge adapter proof

Only after the baseline is recoverable:

1. identify a documented serial/API/firmware path that can emit normalized
   fields without exposing pairing secrets or precise test locations;
2. map it into `GpsFixSample` behind a transport/parser adapter;
3. replay recorded synthetic/sanitized fixtures for no-fix, 2D, 3D, partial
   fields, bad ranges, UTC absent/present, loss, duplicate, reordering, source
   restart, and exact staleness;
4. measure 1 Hz, 5 Hz, and supported-maximum delivery/age/power behavior;
5. test loss of GNSS, MeshCore/radio, USB, and power independently from core
   vehicle instrumentation;
6. restore the shipping image and repeat recovery before any compatibility
   claim.

No test requires opening the enclosure unless documented recovery proves
impossible and the owner separately authorizes it.
