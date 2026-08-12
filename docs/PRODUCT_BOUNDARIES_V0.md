# OpenGauge Product Boundaries v0

Status: release-planning architecture, updated 2026-08-12. Hardware remains
candidate or missing until its own bring-up and physical acceptance evidence
passes.

## Required system roles

### Vehicle gateway

The gateway owns vehicle acquisition and normalization. It requires an exact
MCU/board, CAN controller, transceiver, protection, connector, power design,
and environmental decision that have not yet been selected. Initial physical
CAN/J1939 operation is listen-only.

The gateway publishes selected normalized telemetry and alarms. It does not
broadcast every raw CAN frame, render gauges, or depend on any one display.

### At least one gauge endpoint

A gauge endpoint owns local rendering, input, configuration presentation, and
fail-visible stale/error behavior. It consumes normalized wireless telemetry;
gateway loss must stale/remove numeric values rather than freeze them as current.

The endpoint can use either display shape below. A larger touchscreen is not
required for a valid compact installation.

## Display alternatives

| Shape | Intended experience | Shared contract | Separate acceptance work |
| --- | --- | --- | --- |
| 1.75-inch round touch gauge | Compact dedicated gauge, warning/status view, and bounded local configuration | Normalized telemetry receiver, display-neutral view model/trends, alarms, semantic layout/configuration and stale/error state | Circular layout/readability, glare/view angle, touch targets, boot/frame time, RAM/flash, power/heat, enclosure and USB recovery |
| Larger touchscreen | More simultaneous gauges, trends, status/navigation, and richer configuration | The same normalized telemetry/state and semantic configuration boundaries | Exact panel/controller, layout scaling, touch/accessibility, memory/frame time, power/heat, mounting and recovery |

Semantic layout content can be exported/imported through `OGL0`, but that does
not prove a pixel layout is readable on every display shape. Each renderer owns
geometry and must reject or adapt unsupported presentation without inventing a
measurement.

## Optional roles

| Optional role | Adds | Allowed boundary data | Failure behavior |
| --- | --- | --- | --- |
| GNSS source | Speed/position/altitude/heading/time/fix context | Validated normalized fix fields with quality, presence, source age, and local staleness | Navigation fields become unavailable/stale; vehicle gauges continue |
| Additional gauge endpoint | Another independently selected view | Same authorized normalized telemetry | One display failure does not block gateway or other displays |
| OpenTrail bridge | Group delivery of selected critical vehicle events | Versioned normalized critical alerts and application ACKs only | Local gauges/alarms continue; remote group delivery becomes unavailable |
| Auxiliary/APU module | Future telemetry and separately reviewed functions | Telemetry first; any control uses a separate authenticated/interlocked interface | Core gateway/gauges continue; no unsafe implicit control fallback |
| Diagnostic discovery adapter | Early signal/bus investigation | Explicit read-only discovery data under a separate test procedure | Never becomes a production gateway or safety claim by inference |

## Project boundary

OpenGauge owns vehicle acquisition, decode/normalization, local alarms, gauge
rendering, and its local telemetry network. OpenTrail owns group communication,
group/location context, and its own delivery/security behavior. OpenTrail never
needs raw CAN/J1939 knowledge.

No display, GNSS source, OpenTrail connection, or auxiliary module has production
support status yet. Moving any optional role into the required base requires a
new versioned architecture/support decision and corresponding recovery,
security, hardware, and field evidence.
