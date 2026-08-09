# GPS Fix Tracker v0

Status: deterministic transport-neutral host model, 2026-08-09. This is not a
GNSS parser, driver, radio protocol, supported receiver, accuracy result,
surveying system, authenticated location source, or physical hardware test.

## Boundary

`GpsFixTracker` accepts normalized samples from a future local, serial,
ESP-NOW, MeshCore-bridge, or other authenticated adapter. It deliberately does
not know NMEA/vendor messages, radio addresses, keys, antennas, baud rates, or
board pin assignments.

Each sample contains:

- nonzero source boot-session ID and wrap-aware 32-bit sequence;
- source age, combined only with receiver-local monotonic elapsed time;
- explicit no-fix, 2D, 3D, differential, RTK-float, RTK-fixed, or estimated
  quality;
- satellites used;
- separate presence flags for position, altitude, speed, heading, horizontal
  accuracy, and UTC;
- signed latitude/longitude in degrees times 10^7;
- altitude in millimetres, speed in millimetres/second, heading in
  millidegrees, horizontal accuracy in millimetres, and Unix UTC milliseconds.

No-fix samples cannot contain spatial values. Two-dimensional fixes require
position but may omit altitude. All other v0 fix qualities require position and
altitude. Absent fields must be canonical zero. Coordinates, altitude, speed,
heading, accuracy, UTC, satellite count, session, and source age are bounded.

## Freshness and loss

The tracker never compares clocks from different devices. Effective age is the
reported source age plus receiver-local time since acceptance, saturating on
overflow. At exact `age >= stale_after_ms`, all position, motion, accuracy, and
UTC presence/value fields are stripped. A stale record cannot appear as the
last live coordinate.

Within one boot session, duplicate and backward sequence values are rejected;
forward gaps are accepted and counted. Unsigned half-range ordering permits
the normal `uint32_t` wrap from maximum to zero. A new nonzero boot session is
accepted as a restart, resets sequence comparison, and increments a session
change counter. Receiver monotonic time cannot move backward.

No-fix and stale remain different states. A no-fix source may retain a bounded
UTC observation without inventing position; the whole observation becomes
stale at the same freshness boundary.

## Topology

The normalized tracker is identical for these possible topologies:

1. a GNSS receiver attached to the vehicle gateway;
2. a dedicated OpenGauge GPS node publishing authenticated samples;
3. an explicitly authorized bridge from OpenTrail/MeshCore location state;
4. a gauge-local GNSS receiver on a future exact display variant.

No topology is selected merely because hardware includes GNSS. A separate GPS
node preserves the gateway/display failure boundary but adds pairing, radio,
power, antenna, and update-rate dependencies. Reusing OpenTrail/MeshCore avoids
another receiver but must not silently couple core instrumentation to trail
radio availability. Gauge-local GNSS reduces radio hops but duplicates hardware
and may worsen enclosure/antenna constraints.

Bring-up should measure 1 Hz, 5 Hz, and the candidate's supported maximum while
recording delivery gaps, end-to-end age, CPU/radio load, and usefulness; this
document does not select an update rate in advance.

## Host evidence

`tests/host/gps_fix_tracker_tests.cpp` covers eight groups:

1. fix-quality, presence, coordinate, 2D, and 3D rules;
2. speed, heading, accuracy, and UTC boundaries;
3. lifecycle, configuration, missing state, and clear;
4. gap/duplicate/out-of-order accounting and sequence wrap;
5. source restart and receiver-clock regression;
6. exact combined-age stale boundary with value removal;
7. explicit no-fix behavior without fabricated position;
8. source-age limits, invalid counters, and age saturation.

The suite also repeated 100 times with zero failures.

## Remaining gates

- choose topology and authenticated transport, source identity/key lifecycle,
  pairing/revocation, rate limit, and replay policy;
- bind and fuzz the exact receiver/parser while preserving no-fix and partial
  field semantics;
- test cold/warm/hot start, indoor/outdoor reacquisition, antenna orientation,
  loss/recovery, update rates, accuracy, UTC behavior, power, heat, and reset;
- decide privacy, retention, export, and user-consent policy for location;
- map selected fields into the existing normalized telemetry registry only
  after units, rate, and consumer semantics are frozen.
